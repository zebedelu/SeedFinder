/* core/crack_java.c — SeedCracker Java Edition.
 * Placement-only (NÃO usa isViableStructurePos/applySeed: biomas Java dependem
 * da seed 64-bit completa, fora do escopo 48-bit). Mesmo precedente do
 * crack_auto.c (varredura placement-only). */
#include "crack_java.h"
#include "platform_threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define JV_A  341873128712ULL
#define JV_B  132897987541ULL
#define JV_K  0x5DEECE66DULL
#define JV_ADD 0xBULL
#define JV_M19 0x7FFFFULL   /* 2^19 - 1 */

static inline long long jvChunkOf(double block)
{
    long long b = (long long)block;
    return (b >= 0) ? b / 16 : (b - 15) / 16;
}

int javaTargetsBuild(const int *types, int numTypes, const double *xb, const double *zb,
                     int tolerance, JavaTarget out[], int *nOut, char **err)
{
    *err = NULL; *nOut = 0;
    if (numTypes < 1 || numTypes > JAVA_MAX_STRUCTURES) {
        *err = strdup("{\"error\":\"1-24 structures required\"}"); return 1;
    }
    if (tolerance < 0 || tolerance > 8) {
        *err = strdup("{\"error\":\"tolerance must be between 0 and 8 chunks\"}"); return 1;
    }
    int maxD2 = tolerance * tolerance;
    for (int i = 0; i < numTypes; i++) {
        int t = types[i];
        if (t == Mineshaft) {
            *err = strdup("{\"error\":\"Mineshaft is not supported by SeedCracker\"}"); return 1;
        }
        StructureConfig sc;
        if (!getStructureConfig(t, MC_NEWEST, &sc)) {
            *err = strdup("{\"error\":\"unknown structure type\"}"); return 1;
        }
        long long cx = jvChunkOf(xb[i]);
        long long cz = jvChunkOf(zb[i]);
        if (cx < -100000000LL || cx > 100000000LL || cz < -100000000LL || cz > 100000000LL) {
            *err = strdup("{\"error\":\"coordinates out of range\"}"); return 1;
        }
        JavaTarget *o = &out[i];
        o->type = t; o->cfg = sc; o->chunkX = cx; o->chunkZ = cz;
        o->maxD2 = maxD2; o->tolerance = tolerance;
        int rs = sc.regionSize;
        long long lox = (cx - tolerance - (rs - 1)) / rs;
        long long hix = (cx + tolerance >= 0) ? (cx + tolerance) / rs
                                              : (cx + tolerance - (rs - 1)) / rs;
        long long loz = (cz - tolerance - (rs - 1)) / rs;
        long long hiz = (cz + tolerance >= 0) ? (cz + tolerance) / rs
                                              : (cz + tolerance - (rs - 1)) / rs;
        int k = 0;
        for (long long x = lox; x <= hix && k < JAVA_MAX_REGIONS; x++)
            for (long long z = loz; z <= hiz && k < JAVA_MAX_REGIONS; z++) {
                o->regX[k] = (int)x; o->regZ[k] = (int)z; k++;
            }
        if (k == 0) { *err = strdup("{\"error\":\"invalid coordinates\"}"); return 1; }
        o->numRegions = k;
    }
    *nOut = numTypes;
    return 0;
}

/* Ordem de varredura: indices com menos regioes primeiro. */
static void jvOrder(const JavaTarget *targets, int n, int *order)
{
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 0; i < n; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++)
            if (targets[order[j]].numRegions < targets[order[best]].numRegions) best = j;
        int t = order[i]; order[i] = order[best]; order[best] = t;
    }
}

int javaCheckSeed(const JavaTarget *targets, int n, uint64_t s48,
                  int *matchesOut, int64_t *scoreOut)
{
    int order[JAVA_MAX_STRUCTURES];
    jvOrder(targets, n, order);
    typedef struct { int d2, x, z; } JVC;
    int64_t score = 0;
    for (int k = 0; k < n; k++) {
        const JavaTarget *t = &targets[order[k]];
        JVC small[64];
        JVC *cands = small;
        int cap = 64, cnt = 0;
        if (t->numRegions > cap) { cap = t->numRegions; cands = malloc((size_t)cap * sizeof(JVC)); }
        if (!cands) return 0;
        for (int r = 0; r < t->numRegions; r++) {
            Pos pos;
            if (!getStructurePos(t->type, MC_NEWEST, s48, t->regX[r], t->regZ[r], &pos))
                continue;
            long long dx = (long long)(pos.x >> 4) - t->chunkX;
            long long dz = (long long)(pos.z >> 4) - t->chunkZ;
            long long d2 = dx * dx + dz * dz;
            if (d2 > t->maxD2) continue;
            cands[cnt].d2 = (int)d2; cands[cnt].x = pos.x; cands[cnt].z = pos.z; cnt++;
        }
        for (int i = 1; i < cnt; i++) {           /* insertion sort por d2 asc */
            JVC key = cands[i]; int j = i - 1;
            while (j >= 0 && cands[j].d2 > key.d2) { cands[j + 1] = cands[j]; j--; }
            cands[j + 1] = key;
        }
        int ok = cnt > 0;
        int mx = ok ? cands[0].x : 0, mz = ok ? cands[0].z : 0, md = ok ? cands[0].d2 : 0;
        if (cands != small) free(cands);
        if (!ok) return 0;
        score += md;
        matchesOut[order[k] * 2]     = mx >> 4;
        matchesOut[order[k] * 2 + 1] = mz >> 4;
    }
    if (scoreOut) *scoreOut = score;
    return 1;
}

typedef struct { uint64_t seed; int64_t score; } JavaHit;

typedef struct {
    const JavaTarget *targets; int n;
    uint64_t start, end;
    int maxResults;
    double deadline; volatile int *stop;
    uint64_t checked;
    JavaHit *hits; int hitCount; int64_t worst; int worstIdx;
    int *matches;              /* maxResults * n * 2 ints */
} JvWorker;

static void jvInsert(JvWorker *w, uint64_t seed, int64_t score, const int *mm)
{
    if (w->hitCount < w->maxResults) {
        w->hits[w->hitCount].seed = seed;
        w->hits[w->hitCount].score = score;
        memcpy(w->matches + (size_t)w->hitCount * w->n * 2, mm,
               (size_t)w->n * 2 * sizeof(int));
        w->hitCount++;
        if (score < w->worst) { w->worst = score; w->worstIdx = w->hitCount - 1; }
        return;
    }
    if (score >= w->worst) return;
    int slot = w->worstIdx;
    w->hits[slot].seed = seed;
    w->hits[slot].score = score;
    memcpy(w->matches + (size_t)slot * w->n * 2, mm, (size_t)w->n * 2 * sizeof(int));
    w->worst = INT64_MAX; w->worstIdx = -1;
    for (int i = 0; i < w->hitCount; i++)
        if (w->hits[i].score < w->worst) { w->worst = w->hits[i].score; w->worstIdx = i; }
}

static void *jvSweepWorker(void *arg)
{
    JvWorker *w = (JvWorker *)arg;
    int mm[2 * JAVA_MAX_STRUCTURES];
    for (uint64_t s = w->start; s < w->end && !*w->stop; s++) {
        int64_t sc = 0;
        if (javaCheckSeed(w->targets, w->n, s, mm, &sc)) jvInsert(w, s, sc, mm);
        w->checked++;
        if ((s & 0xFFFFFULL) == 0 && w->deadline > 0.0 && nowms_s() > w->deadline)
            { *w->stop = 1; break; }
    }
    return NULL;
}

typedef struct { JavaHit *hits; int *matches; int c; uint64_t checked; int timedOut; } JvRun;

/* Varre [start,end) particionado. 0 ok; OOM -> -1. */
static int jvRunSweep(const JavaTarget *targets, int n,
                      uint64_t start, uint64_t end, int maxResults,
                      double deadline, int numThreads, JvRun *out)
{
    JvWorker *ws = calloc((size_t)numThreads, sizeof(JvWorker));
    CrackThread *th = calloc((size_t)numThreads, sizeof(CrackThread));
    volatile int stop = 0;
    uint64_t part = (end - start) / (uint64_t)numThreads;
    for (int i = 0; i < numThreads; i++) {
        ws[i].targets = targets; ws[i].n = n;
        ws[i].start = start + (uint64_t)i * part;
        ws[i].end = (i == numThreads - 1) ? end : ws[i].start + part;
        ws[i].maxResults = maxResults;
        ws[i].deadline = deadline; ws[i].stop = &stop;
        ws[i].worst = INT64_MAX; ws[i].worstIdx = -1;
        ws[i].hits = malloc((size_t)maxResults * sizeof(JavaHit));
        ws[i].matches = malloc((size_t)maxResults * n * 2 * sizeof(int));
        if (!ws[i].hits || !ws[i].matches) {
            for (int q = 0; q < numThreads; q++) { free(ws[q].hits); free(ws[q].matches); }
            free(ws); free(th); return -1;
        }
    }
    if (numThreads == 1) jvSweepWorker(&ws[0]);
    else {
        for (int i = 0; i < numThreads; i++) th[i] = crackThreadCreate(jvSweepWorker, &ws[i]);
        for (int i = 0; i < numThreads; i++) crackThreadJoin(th[i]);
    }
    out->timedOut = stop;
    out->checked = 0;
    out->c = 0;
    out->hits = malloc((size_t)maxResults * sizeof(JavaHit));
    out->matches = malloc((size_t)maxResults * n * 2 * sizeof(int));
    if (!out->hits || !out->matches) {
        for (int i = 0; i < numThreads; i++) { free(ws[i].hits); free(ws[i].matches); }
        free(ws); free(th); return -1;
    }
    for (int i = 0; i < numThreads; i++) {
        out->checked += ws[i].checked;
        for (int q = 0; q < ws[i].hitCount; q++) {
            if (out->c < maxResults) {
                out->hits[out->c] = ws[i].hits[q];
                memcpy(out->matches + (size_t)out->c * n * 2,
                       ws[i].matches + (size_t)q * n * 2, (size_t)n * 2 * sizeof(int));
                out->c++;
            } else {
                int64_t w = INT64_MAX; int wIdx = -1;
                for (int u = 0; u < out->c; u++)
                    if (out->hits[u].score < w) { w = out->hits[u].score; wIdx = u; }
                if (ws[i].hits[q].score >= w) continue;
                out->hits[wIdx] = ws[i].hits[q];
                memcpy(out->matches + (size_t)wIdx * n * 2,
                       ws[i].matches + (size_t)q * n * 2, (size_t)n * 2 * sizeof(int));
            }
        }
        free(ws[i].hits); free(ws[i].matches);
    }
    free(ws); free(th);
    return 0;
}

/* insertion sort asc por score + serializacao (envelope identico ao seedfinder_crack). */
static char *jvSerialize(JvRun *run, int n, int numThreads)
{
    int c = run->c;
    for (int i = 1; i < c; i++) {
        JavaHit key = run->hits[i];
        int krow[2 * JAVA_MAX_STRUCTURES];
        memcpy(krow, run->matches + (size_t)i * n * 2, (size_t)n * 2 * sizeof(int));
        int j = i - 1;
        while (j >= 0 && run->hits[j].score > key.score) {
            run->hits[j + 1] = run->hits[j];
            memcpy(run->matches + (size_t)(j + 1) * n * 2,
                   run->matches + (size_t)j * n * 2, (size_t)n * 2 * sizeof(int));
            j--;
        }
        run->hits[j + 1] = key;
        memcpy(run->matches + (size_t)(j + 1) * n * 2, krow, (size_t)n * 2 * sizeof(int));
    }
    size_t cap = 128 + (size_t)c * ((size_t)n * 26 + 80);
    char *buf = malloc(cap);
    int off = sprintf(buf, "{\"results\":[");
    for (int i = 0; i < c; i++) {
        off += sprintf(buf + off, "%s{\"seed\":%llu,\"score\":%lld,\"matches\":[",
                       i ? "," : "", (unsigned long long)run->hits[i].seed,
                       (long long)run->hits[i].score);
        const int *mm = run->matches + (size_t)i * n * 2;
        for (int k = 0; k < n; k++)
            off += sprintf(buf + off, "%s[%d,%d]", k ? "," : "", mm[k * 2], mm[k * 2 + 1]);
        off += sprintf(buf + off, "]}");
    }
    sprintf(buf + off, "],\"checked\":%llu,\"timed_out\":%s,\"threads\":%d}",
            (unsigned long long)run->checked, run->timedOut ? "true" : "false", numThreads);
    return buf;
}

/* Teste %4 dos 19 bits baixos: os bits 17-18 do estado avancado sao
 * determinados por lo19 (LCG mod 2^19 e' bijectivo). tolerance == 0 aqui. */
static int jvLiftTest(const JavaTarget *targets, int n, uint64_t lo19)
{
    int order[JAVA_MAX_STRUCTURES];
    jvOrder(targets, n, order);
    for (int k = 0; k < n; k++) {
        const JavaTarget *t = &targets[order[k]];
        int ok = 0;
        for (int r = 0; r < t->numRegions && !ok; r++) {
            uint64_t rx = (uint64_t)(int64_t)t->regX[r];
            uint64_t rz = (uint64_t)(int64_t)t->regZ[r];
            uint64_t rl = (lo19 + rx * JV_A + rz * JV_B
                           + (uint64_t)(int32_t)t->cfg.salt) & JV_M19;
            uint64_t al = ((rl ^ (JV_K & JV_M19)) * JV_K + JV_ADD) & JV_M19;
            int ox = (int)((al >> 17) & 3);
            al = (al * JV_K + JV_ADD) & JV_M19;
            int oz = (int)((al >> 17) & 3);
            long long ox0 = t->chunkX - (long long)t->regX[r] * (long long)t->cfg.regionSize;
            long long oz0 = t->chunkZ - (long long)t->regZ[r] * (long long)t->cfg.regionSize;
            if (ox == (int)(ox0 & 3) && oz == (int)(oz0 & 3)) { ok = 1; break; }
        }
        if (!ok) return 0;
    }
    return 1;
}

typedef struct {
    const JavaTarget *targets; int n;
    uint64_t start, end;          /* filtro de range na completacao */
    int maxResults; double deadline; volatile int *stop;
    uint64_t checked;
    JavaHit *hits; int hitCount; int64_t worst; int worstIdx;
    int *matches;
    /* jvInsert: JvLiftWorker e' layout-compativel com JvWorker — os campos que
     * jvInsert le (n, maxResults, deadline, stop, hits, hitCount, worst,
     * worstIdx, matches) ocupam os MESMOS offsets do JvWorker; surv/nSurv vem
     * DEPOIS de matches para nao deslocar o prefixo. */
    uint64_t *surv; int nSurv;   /* fatia deste worker (distribuida em jvRunLift) */
} JvLiftWorker;

static void *jvCompleteWorker(void *arg)
{
    JvLiftWorker *w = (JvLiftWorker *)arg;
    uint64_t hiLo = w->start >> 19;
    uint64_t hiHi = (w->end - 1) >> 19;
    int mm[2 * JAVA_MAX_STRUCTURES];
    for (int s = 0; s < w->nSurv && !*w->stop; s++) {
        uint64_t lo19 = w->surv[s];
        for (uint64_t hi = hiLo; hi <= hiHi; hi++) {
            uint64_t s48 = (hi << 19) | lo19;
            if (s48 < w->start || s48 >= w->end) continue;
            int64_t sc = 0;
            if (javaCheckSeed(w->targets, w->n, s48, mm, &sc)) {
                jvInsert((JvWorker *)w, s48, sc, mm);
            }
            w->checked++;
            if ((w->checked & 0xFFFFFULL) == 0 && w->deadline > 0.0 &&
                nowms_s() > w->deadline) { *w->stop = 1; break; }
        }
    }
    return NULL;
}

/* Estagio 1 (sweep 2^19) + estagio 2 (completacao). 0 ok, -1 OOM. */
static int jvRunLift(const JavaTarget *targets, int n,
                     uint64_t start, uint64_t end, int maxResults,
                     double deadline, int numThreads, JvRun *out)
{
    /* Estagio 1: 2^19 low-bit sweep com o teste %4. */
    int survCount = 0;
    uint64_t *surv = malloc(((size_t)1 << 19) * sizeof(uint64_t));
    if (!surv) return -1;
    for (uint64_t lo19 = 0; lo19 < (1ULL << 19); lo19++)
        if (jvLiftTest(targets, n, lo19)) surv[survCount++] = lo19;
    out->checked = 1ULL << 19;

    /* Estagio 2: completacao sobre os sobreviventes, particionada. */
    int t = numThreads < survCount ? numThreads : (survCount > 0 ? survCount : 1);
    JvLiftWorker *ws = calloc((size_t)t, sizeof(JvLiftWorker));
    CrackThread *th = calloc((size_t)t, sizeof(CrackThread));
    volatile int stop = 0;
    for (int i = 0; i < t; i++) {
        ws[i].targets = targets; ws[i].n = n;
        ws[i].start = start; ws[i].end = end;
        ws[i].maxResults = maxResults;
        ws[i].deadline = deadline; ws[i].stop = &stop;
        ws[i].worst = INT64_MAX; ws[i].worstIdx = -1;
        ws[i].hits = malloc((size_t)maxResults * sizeof(JavaHit));
        ws[i].matches = malloc((size_t)maxResults * n * 2 * sizeof(int));
        if (!ws[i].hits || !ws[i].matches) { free(surv); return -1; }
        /* distribui sobreviventes round-robin: sobrevivente i -> worker i % t */
        ws[i].surv = surv; ws[i].nSurv = 0;
    }
    for (int i = 0; i < survCount; i++) ws[i % t].nSurv++;
    /* Distribui para um buffer SEPARADO: escrever in-place em surv[] corrompe a
     * lista, pois o destino base[wi]+cursor pode exceder a origem i ainda nao
     * lida (ex.: item 3 -> pos 138, sobrescrevendo o survivor 138). Depois cada
     * worker aponta para a propria fatia. */
    uint64_t *slice = malloc((size_t)survCount * sizeof(uint64_t));
    if (!slice) { free(surv); return -1; }
    int *cursor = calloc((size_t)t, sizeof(int));
    int *base = calloc((size_t)t, sizeof(int));
    int acc = 0;
    for (int i = 0; i < t; i++) { base[i] = acc; acc += ws[i].nSurv; }
    for (int i = 0; i < survCount; i++) { int wi = i % t; slice[base[wi] + cursor[wi]++] = surv[i]; }
    for (int i = 0; i < t; i++) ws[i].surv = slice + base[i];
    free(cursor); free(base);

    if (t == 1) jvCompleteWorker(&ws[0]);
    else {
        for (int i = 0; i < t; i++) th[i] = crackThreadCreate(jvCompleteWorker, &ws[i]);
        for (int i = 0; i < t; i++) crackThreadJoin(th[i]);
    }
    out->timedOut = stop;
    out->c = 0;
    out->hits = malloc((size_t)maxResults * sizeof(JavaHit));
    out->matches = malloc((size_t)maxResults * n * 2 * sizeof(int));
    if (!out->hits || !out->matches) { free(surv); free(slice); return -1; }
    for (int i = 0; i < t; i++) {
        out->checked += ws[i].checked;
        for (int q = 0; q < ws[i].hitCount; q++) {
            if (out->c < maxResults) {
                out->hits[out->c] = ws[i].hits[q];
                memcpy(out->matches + (size_t)out->c * n * 2,
                       ws[i].matches + (size_t)q * n * 2, (size_t)n * 2 * sizeof(int));
                out->c++;
            } else {
                int64_t w = INT64_MAX; int wIdx = -1;
                for (int u = 0; u < out->c; u++)
                    if (out->hits[u].score < w) { w = out->hits[u].score; wIdx = u; }
                if (ws[i].hits[q].score >= w) continue;
                out->hits[wIdx] = ws[i].hits[q];
                memcpy(out->matches + (size_t)wIdx * n * 2,
                       ws[i].matches + (size_t)q * n * 2, (size_t)n * 2 * sizeof(int));
            }
        }
        free(ws[i].hits); free(ws[i].matches);
    }
    free(ws); free(th); free(surv); free(slice);
    return 0;
}

/* =========================================================================
 * LIFTING tolerante (espelho do auto-mode/crack64). Usado quando o SWEEP e'
 * inviavel (span > 2^33) e o caminho lo19 (%4) nao se aplica (tolerance > 0
 * ou tipo nao-liftavel). Stage A = sweep lo32 com placement Java + tolerance
 * na semente representativa; Stage B = lift dos hi16 por sobrevivente.
 * ========================================================================= */

#define JV_TOL_SAMPLE   (1ULL << 22)   /* amostra do pre-check (~1 s) */
#define JV_TOL_MAXSURV  (1 << 20)      /* teto de sobreviventes preditos */
#define JV_TOL_MAXB     (1 << 28)      /* teto de verificacoes do Stage B */

/* Stage A: 1 se a semente representativa sRep = (hiBase<<32)|lo32 coloca TODAS
 * as estruturas dentro de maxD2 (cada uma em alguma regiao candidata).
 * hiBase = primeiro hi16 da janela; 0 no full-range => exato p/ seeds < 2^32.
 * order = poucas-regioes-primeiro (jvOrder), computado UMA vez por run. */
static int jvHitLo32(const JavaTarget *targets, const int *order, int n,
                     uint64_t lo32, uint64_t hiBase)
{
    uint64_t sRep = (hiBase << 32) | lo32;
    for (int k = 0; k < n; k++) {
        const JavaTarget *t = &targets[order[k]];
        int ok = 0;
        for (int r = 0; r < t->numRegions && !ok; r++) {
            Pos pos;
            if (!getStructurePos(t->type, MC_NEWEST, sRep, t->regX[r], t->regZ[r], &pos))
                continue;
            long long dx = (long long)(pos.x >> 4) - t->chunkX;
            long long dz = (long long)(pos.z >> 4) - t->chunkZ;
            if (dx * dx + dz * dz <= (long long)t->maxD2) ok = 1;
        }
        if (!ok) return 0;
    }
    return 1;
}

/* Worker intercalado: o prefixo replica EXATAMENTE o JvWorker (jvInsert e'
 * layout-dependente — mesmo padrao do JvLiftWorker); os extras (order, loStart,
 * loEnd, hiLo, hiHi, hiBase) ficam DEPOIS de matches para nao deslocar o
 * prefixo. A definicao completa vem ANTES de jvRunTolLift (que usa sizeof). */
typedef struct JvTolWorker {
    const JavaTarget *targets; int n;
    uint64_t start, end;
    int maxResults;
    double deadline; volatile int *stop;
    uint64_t checked;
    JavaHit *hits; int hitCount; int64_t worst; int worstIdx;
    int *matches;
    const int *order;
    uint64_t loStart, loEnd;
    uint64_t hiLo, hiHi, hiBase;
} JvTolWorker;

static void *jvTolWorkerRun(void *arg)
{
    JvTolWorker *w = (JvTolWorker *)arg;
    int mm[2 * JAVA_MAX_STRUCTURES];
    for (uint64_t lo = w->loStart; lo < w->loEnd && !*w->stop; lo++) {
        if (jvHitLo32(w->targets, w->order, w->n, lo, w->hiBase)) {
            for (uint64_t hi = w->hiLo; hi <= w->hiHi && !*w->stop; hi++) {
                uint64_t s48 = (hi << 32) | lo;
                if (s48 < w->start || s48 >= w->end) continue;
                int64_t sc = 0;
                if (javaCheckSeed(w->targets, w->n, s48, mm, &sc))
                    jvInsert((JvWorker *)w, s48, sc, mm);
                w->checked++;
                if ((w->checked & 0xFFFFFULL) == 0 && w->deadline > 0.0 &&
                    nowms_s() > w->deadline) { *w->stop = 1; break; }
            }
        }
        w->checked++;
        if ((lo & 0xFFFFFULL) == 0 && w->deadline > 0.0 && nowms_s() > w->deadline)
            { *w->stop = 1; break; }
    }
    return NULL;
}

/* Stage A + B INTERCALADOS por worker (sem materializar sobreviventes —
 * ponytail): ao achar um lo32 que passa jvHitLo32, completa imediatamente os
 * hi16 da janela com javaCheckSeed. 0 ok / 1 erro acionavel (*err) / -1 OOM. */
static int jvRunTolLift(const JavaTarget *targets, int n,
                        uint64_t start, uint64_t end, int maxResults,
                        double deadline, int numThreads, char **err)
{
    uint64_t hiLo = start >> 32;
    uint64_t hiHi = (end - 1) >> 32;
    uint64_t hiBase = hiLo;
    uint64_t hiCount = hiHi - hiLo + 1;

    int order[JAVA_MAX_STRUCTURES];
    jvOrder(targets, n, order);

    /* Pre-check amostral (mesmo padrao do crack64 full-range): varre 2^22
     * lo32 e extrapola os sobreviventes x1024. Rejeita antes do sweep 2^32. */
    {
        uint64_t s = 0;
        for (uint64_t lo = 0; lo < JV_TOL_SAMPLE; lo++)
            if (jvHitLo32(targets, order, n, lo, hiBase)) s++;
        uint64_t pred = s * (0x100000000ULL / JV_TOL_SAMPLE);
        if (pred > (uint64_t)JV_TOL_MAXSURV || pred * hiCount > (uint64_t)JV_TOL_MAXB) {
            char msg[288];
            snprintf(msg, sizeof msg,
                     "{\"error\":\"tolerant Java lift infeasible: ~%llu lo32 "
                     "survivors (%llu in 2^22 sample) x %llu high-bit lifts "
                     "(est. %llu verifications > %d) - add more structures, "
                     "lower tolerance, or narrow start/end\"}",
                     (unsigned long long)pred, (unsigned long long)s,
                     (unsigned long long)hiCount,
                     (unsigned long long)(pred * hiCount), JV_TOL_MAXB);
            *err = strdup(msg);
            return 1;
        }
    }

    int t = numThreads;
    JvTolWorker *ws = calloc((size_t)t, sizeof(JvTolWorker));
    CrackThread *th = calloc((size_t)t, sizeof(CrackThread));
    volatile int stop = 0;
    uint64_t part = (1ULL << 32) / (uint64_t)t;
    for (int i = 0; i < t; i++) {
        ws[i].targets = targets; ws[i].n = n;
        ws[i].start = start; ws[i].end = end;
        ws[i].maxResults = maxResults;
        ws[i].deadline = deadline; ws[i].stop = &stop;
        ws[i].worst = INT64_MAX; ws[i].worstIdx = -1;
        ws[i].hits = malloc((size_t)maxResults * sizeof(JavaHit));
        ws[i].matches = malloc((size_t)maxResults * n * 2 * sizeof(int));
        ws[i].order = order;
        ws[i].loStart = (uint64_t)i * part;
        ws[i].loEnd = (i == t - 1) ? (1ULL << 32) : (uint64_t)(i + 1) * part;
        ws[i].hiLo = hiLo; ws[i].hiHi = hiHi; ws[i].hiBase = hiBase;
        if (!ws[i].hits || !ws[i].matches) {
            for (int q = 0; q < t; q++) { free(ws[q].hits); free(ws[q].matches); }
            free(ws); free(th);
            return -1;
        }
    }
    if (t == 1) jvTolWorkerRun(&ws[0]);
    else {
        for (int i = 0; i < t; i++) th[i] = crackThreadCreate(jvTolWorkerRun, &ws[i]);
        for (int i = 0; i < t; i++) crackThreadJoin(th[i]);
    }
    *err = NULL;

    /* merge top-N (mesmo padrao de jvRunSweep) em um JvRun local. */
    JvRun out;
    out.timedOut = stop;
    out.c = 0;
    out.hits = malloc((size_t)maxResults * sizeof(JavaHit));
    out.matches = malloc((size_t)maxResults * n * 2 * sizeof(int));
    if (!out.hits || !out.matches) {
        for (int i = 0; i < t; i++) { free(ws[i].hits); free(ws[i].matches); }
        free(ws); free(th);
        return -1;
    }
    out.checked = 0;
    for (int i = 0; i < t; i++) {
        out.checked += ws[i].checked;
        for (int q = 0; q < ws[i].hitCount; q++) {
            if (out.c < maxResults) {
                out.hits[out.c] = ws[i].hits[q];
                memcpy(out.matches + (size_t)out.c * n * 2,
                       ws[i].matches + (size_t)q * n * 2, (size_t)n * 2 * sizeof(int));
                out.c++;
            } else {
                int64_t w = INT64_MAX; int wIdx = -1;
                for (int u = 0; u < out.c; u++)
                    if (out.hits[u].score < w) { w = out.hits[u].score; wIdx = u; }
                if (ws[i].hits[q].score >= w) continue;
                out.hits[wIdx] = ws[i].hits[q];
                memcpy(out.matches + (size_t)wIdx * n * 2,
                       ws[i].matches + (size_t)q * n * 2, (size_t)n * 2 * sizeof(int));
            }
        }
        free(ws[i].hits); free(ws[i].matches);
    }
    free(ws); free(th);

    char *buf = jvSerialize(&out, n, t);
    free(out.hits); free(out.matches);
    *err = buf;                    /* chamador devolve *err como resultado */
    return 0;
}

SEEDFINDER_API char *seedfinder_crack_java(
    const int *types, int numTypes,
    const double *xBlocks, const double *zBlocks,
    int tolerance,
    uint64_t startSeed, uint64_t endSeed,
    int maxResults, double timeBudgetSec, int numThreads)
{
    JavaTarget targets[JAVA_MAX_STRUCTURES];
    int n = 0; char *err = NULL;
    if (javaTargetsBuild(types, numTypes, xBlocks, zBlocks, tolerance, targets, &n, &err))
        return err;
    if (maxResults <= 0 || maxResults > 2000)
        return strdup("{\"error\":\"maxResults must be between 1 and 2000\"}");
    if (numThreads < 1 || numThreads > 64) numThreads = 8;
    startSeed &= JAVA_MASK48;
    if (endSeed > (1ULL << 48)) endSeed = 1ULL << 48;
    if (startSeed >= endSeed) return strdup("{\"error\":\"empty seed range\"}");

#if defined(__EMSCRIPTEN__)
    numThreads = 1;
#endif
    if ((endSeed - startSeed) < (uint64_t)numThreads) numThreads = 1;

    double deadline = (timeBudgetSec > 0.0) ? nowms_s() + timeBudgetSec * 1000.0 : 0.0;

    int liftable = 1;
    for (int i = 0; i < n && liftable; i++)
        if (targets[i].cfg.chunkRange % 4 != 0) liftable = 0;

    if (tolerance == 0 && liftable) {
        /* LIFTING lo19 (inalterado). */
        JvRun run;
        if (jvRunLift(targets, n, startSeed, endSeed, maxResults, deadline,
                      numThreads, &run) != 0)
            return strdup("{\"error\":\"out of memory\"}");
        char *buf = jvSerialize(&run, n, numThreads);
        free(run.hits); free(run.matches);
        return buf;
    }
    if ((endSeed - startSeed) <= (1ULL << 33)) {
        /* SWEEP (inalterado). */
        JvRun run;
        if (jvRunSweep(targets, n, startSeed, endSeed, maxResults, deadline,
                       numThreads, &run) != 0)
            return strdup("{\"error\":\"out of memory\"}");
        char *buf = jvSerialize(&run, n, numThreads);
        free(run.hits); free(run.matches);
        return buf;
    }
    /* LIFTING tolerante (novo). */
    int rc = jvRunTolLift(targets, n, startSeed, endSeed, maxResults,
                          deadline, numThreads, &err);
    if (rc != 0) {
        if (err) return err;
        return strdup("{\"error\":\"out of memory\"}");
    }
    return err;                    /* jvRunTolLift devolveu o JSON pronto */
}