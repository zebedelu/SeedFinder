// core/crack64.c (Task 3 fast-path + Task 4 sweep48 + Task 6 crack64 pipeline)
#include "crack64.h"
#include "ChunkBiomesGUI/Bfinders.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include "platform_threads.h" // (Task 2) — usado na Task 5; ja incluido aqui sem custo

int javaCfgFor(int structureType, JavaCfg *out) {
    StructureConfig sconf;
    if (structureType != Trail_Ruins && structureType != Trial_Chambers) return 0;
    // Config JAVA (placement idêntico ao Java Edition — ver Bfinders.c):
    if (!getStructureConfig(structureType, MC_NEWEST, &sconf)) return 0;
    out->regionSize = sconf.regionSize;
    out->chunkRange = sconf.chunkRange;
    out->salt       = sconf.salt;
    return 1;
}

void javaChunk(const JavaCfg *c, uint64_t s48, int regX, int regZ,
               long long *chunkX, long long *chunkZ) {
    // setLargeFeatureWithSalt: seed = (regX*C1 + regZ*C2 + worldSeed + salt) ^ MUL, & M48
    uint64_t st = ((uint64_t)regX * 341873128712ULL
                 + (uint64_t)regZ * 132897987541ULL
                 + s48 + c->salt) ^ C64_MUL;
    st &= C64_M48;
    // nextInt(chunkRange): bits = state >>> 17 (31 bits); bound pequeno => sem retry.
    st = (st * C64_MUL + C64_ADD) & C64_M48;
    int ox = (int)((st >> 17) % (uint32_t)c->chunkRange);
    st = (st * C64_MUL + C64_ADD) & C64_M48;
    int oz = (int)((st >> 17) % (uint32_t)c->chunkRange);
    // getStructurePos devolve o bloco NO CANTO do chunk: (reg*RS + o) << 4
    // (finders.h:790, sem o +8 de centroide). O crack engine extrai o chunk
    // anchor como (blockX - 8) >> 4 (seedfinder_wrapper.c:235). Como o bloco
    // e' multiplo exato de 16, ((a*16) - 8) >> 4 == a - 1 para todo inteiro
    // ( ate' negativos), logo o chunk anchor sai direto sem o round-trip
    // de shift (evita shift assinado e acelera o hot loop do sweep).
    *chunkX = (long long)regX * c->regionSize + ox - 1;
    *chunkZ = (long long)regZ * c->regionSize + oz - 1;
}

int anchor48Build(Anchor48 *a, const int *types, const double *xb, const double *zb,
                  int n, int tolerance) {
    if (n < 1 || n > C64_MAX) return -1;
    a->nJava = 0;
    a->maxD2 = tolerance * tolerance;
    for (int i = 0; i < n; i++) {
        JavaCfg c;
        if (!javaCfgFor(types[i], &c)) return -1; // chamador valida antes; cinto de seguranca
        long long cx = (xb[i] >= 0) ? (long long)xb[i] / 16 : ((long long)xb[i] - 15) / 16;
        long long cz = (zb[i] >= 0) ? (long long)zb[i] / 16 : ((long long)zb[i] - 15) / 16;
        int j = a->nJava++;
        a->cfg[j] = c; a->chunkX[j] = cx; a->chunkZ[j] = cz;
        // celulas candidatas: mesma formula de regiao do seedfinder_crack (floor-div)
        int rs = c.regionSize;
        long long lox = (cx - tolerance - (rs - 1)) / rs;
        long long hix = (cx + tolerance >= 0) ? (cx + tolerance) / rs
                                              : (cx + tolerance - (rs - 1)) / rs;
        long long loz = (cz - tolerance - (rs - 1)) / rs;
        long long hiz = (cz + tolerance >= 0) ? (cz + tolerance) / rs
                                              : (cz + tolerance - (rs - 1)) / rs;
        int k = 0;
        for (long long x = lox; x <= hix && k < 64; x++)
            for (long long z = loz; z <= hiz && k < 64; z++)
                { a->regX[j][k] = (int)x; a->regZ[j][k] = (int)z; k++; }
        if (k == 0) return -1;
        a->nRegions[j] = k;
    }
    return 0;
}

// 1 se a ancora j tem alguma regiao cujo placement cai dentro de maxD2 do alvo
static inline int anchorHit(const Anchor48 *a, int j, uint64_t s48) {
    for (int r = 0; r < a->nRegions[j]; r++) {
        long long cx, cz;
        javaChunk(&a->cfg[j], s48, a->regX[j][r], a->regZ[j][r], &cx, &cz);
        long long dx = cx - a->chunkX[j], dz = cz - a->chunkZ[j];
        if (dx * dx + dz * dz <= (long long)a->maxD2) return 1;
    }
    return 0;
}

void sweep48(const Anchor48 *a, uint64_t s48Start, uint64_t s48End,
             double deadlineMs, U64Vec *out, int *timedOut) {
    *timedOut = 0; out->n = 0;
    for (uint64_t s = s48Start; s < s48End; s++) {
        int ok = 1;
        for (int j = 0; j < a->nJava && ok; j++) ok = anchorHit(a, j, s);
        if (ok) u64Push(out, s);
        if ((s & 0xFFFFULL) == 0 && deadlineMs > 0.0 && nowms_s() > deadlineMs) {
            *timedOut = 1; return;
        }
    }
}

// --- sweep48 multi-thread (Task 5) -------------------------------------
// Mesmo padrao de seedfinder_crack (seedfinder_wrapper.c): stop volatil
// compartilhado + deadline absoluto em ms (0.0 = sem deadline); cada worker
// tem U64Vec proprio, mergeado so' depois do join (sem locks, sem races).

typedef struct {
    const Anchor48 *a; uint64_t start, end; double deadline;
    volatile int *stop; U64Vec out; uint64_t checked;
} SweepWorker;

static void *sweep48Worker(void *arg) {
    SweepWorker *w = (SweepWorker *)arg;
    for (uint64_t s = w->start; s < w->end && !*w->stop; s++) {
        int ok = 1;
        for (int j = 0; j < w->a->nJava && ok; j++) ok = anchorHit(w->a, j, s);
        if (ok) u64Push(&w->out, s);
        w->checked++;
        if ((s & 0xFFFFULL) == 0 && w->deadline > 0.0 && nowms_s() > w->deadline)
            { *w->stop = 1; break; }
    }
    return NULL;
}

Sweep48Result sweep48MT(const Anchor48 *a, uint64_t start, uint64_t end,
                        double budgetSec, int numThreads, U64Vec *out) {
#if defined(__EMSCRIPTEN__)
    // Sem -pthread o pthread_create aborta em runtime; o JS paraleliza
    // via Web Workers (uma fatia [start,end) por worker). Igual ao crack 32.
    numThreads = 1;
#endif
    Sweep48Result r = {0, 0, numThreads < 1 ? 1 : numThreads};
    if (numThreads < 1) numThreads = 1;
    if (start >= end) return r; // range vazio/invalido: sem particionamento
    // Fatia >= 1 s48 por worker: garante part > 0 (sem dupplicacao/perda de
    // cobertura) e protege a divisao. Range minusculo vai todo num worker so'.
    if ((uint64_t)numThreads > end - start) numThreads = 1;
    volatile int stop = 0;
    double deadline = budgetSec > 0.0 ? nowms_s() + budgetSec * 1000.0 : 0.0;
    SweepWorker *ws = calloc((size_t)numThreads, sizeof(SweepWorker));
    CrackThread *th = calloc((size_t)numThreads, sizeof(CrackThread));
    uint64_t part = (end - start) / (uint64_t)numThreads;
    for (int i = 0; i < numThreads; i++) {
        ws[i].a = a; ws[i].start = start + (uint64_t)i * part;
        ws[i].end = (i == numThreads - 1) ? end : start + (uint64_t)(i + 1) * part;
        ws[i].deadline = deadline; ws[i].stop = &stop;
    }
    if (numThreads == 1) sweep48Worker(&ws[0]); // inline (WASM e range pequeno)
    else { for (int i = 0; i < numThreads; i++) th[i] = crackThreadCreate(sweep48Worker, &ws[i]);
           for (int i = 0; i < numThreads; i++) crackThreadJoin(th[i]); }
    // timedOut espelha o stop compartilhado: um worker que estourou o deadline
    // marca todos, mesmo os que completaram a propria fatia.
    r.timedOut = stop; r.threads = numThreads;
    // checked somado dos workers sob timeout e' contagem parcial (mesmo
    // comportamento aceito do seedfinder_crack de 32 bits).
    for (int i = 0; i < numThreads; i++) {
        r.checked += ws[i].checked;
        for (int k = 0; k < ws[i].out.n; k++) u64Push(out, ws[i].out.v[k]);
        free(ws[i].out.v);
    }
    free(ws); free(th);
    return r;
}

void liftJavaHi(const Anchor48 *anc, const U64Vec *lo32s,
                double deadline, U64Vec *out, int *timedOut) {
    *timedOut = 0;
    out->n = 0;
    uint64_t probes = 0;
    for (int i = 0; i < lo32s->n; i++) {
        uint64_t lo32 = lo32s->v[i];
        for (uint64_t hi16 = 0; hi16 < (1ULL << 16); hi16++) {
            uint64_t s48 = ((uint64_t)hi16 << 32) | lo32;
            int ok = 1;
            for (int j = 0; j < anc->nJava && ok; j++) ok = anchorHit(anc, j, s48);
            if (ok) u64Push(out, s48);
            probes++;
            if ((probes & 0xFFFFF) == 0 && deadline > 0.0 && nowms_s() > deadline) {
                *timedOut = 1; return;
            }
        }
    }
}

// --- pipeline de seed completa (Task 6) -----------------------------------
// Estagio 1: ancora Java + alvos MT (celulas de regiao pre-computadas uma vez,
// espelhando CrackTarget do crack de 32 bits).
// Estagio 2: sweep48MT na janela de s48 INDUZIDA por [startSeed,endSeed)
//   (ruling do controller: span < 2^48 nunca varre o espaco inteiro). Com
//   span < 2^48 a janela e' [start&M48, ((end-1)&M48)+1) mod 2^48 — ate' duas
//   pecas quando envolve; span >= 2^48 => varredura completa [0,2^48).
//   Limite: para span < 2^48 cada s48 admite NO MAXIMO 1 hi (full = hi*2^48 +
//   s48; dois hi distintos difeririam em >= 2^48 fora do span) — o loop de
//   lift abaixo aceita ate' hiHi-hiLo+1 valores com filtro de range, entao
//   vale tambem no caso geral (span grande => ate' 2^16 hi por s48, carreados
//   pelo deadline global).
// Estagios 3+4 (fused por candidato, para nao materializar Cand[]): cruzamento
//   do placement MT (so' depende de s48 & M32 — mix_seed->mSetSeed truncam em
//   32 bits, ver Brng.h) com o alvo, GUARDANDO todas as celulas dentro de
//   tolerancia ordenadas por d²; o lift dos 16 bits altos escolhe entao, por
//   ancora, o placement viavel MAIS PROXIMO sob a seed completa (bioma = unica
//   coisa que depende de hi) — irmao mais proximo em bioma ruim nao esconde o
//   placement verdadeiro mais distante (mesma preferencia do loop de
//   validacao final do crack de 32 bits, seedfinder_wrapper.c:666-686).

#ifdef C64_TRACE
/* gcc -DC64_TRACE ... => timing por estagio no stderr (diagnostico local;
 * codigo zero quando nao definido). */
static double g_trMs;
#define TR0()        (g_trMs = nowms_s())
#define TR_STAGE(s)  do { double n_ = nowms_s(); \
    fprintf(stderr, "[c64] %s: %.0f ms\n", (s), n_ - g_trMs); g_trMs = n_; } while (0)
#else
#define TR0()        ((void)0)
#define TR_STAGE(s)  ((void)0)
#endif

// Matches por hit: (nMt + nJava) pares de blocos, em ordem de entrada
// (MTs primeiro, depois Java-style) — 4*C64_MAX ints de folga.
typedef struct { uint64_t seed; int64_t score; int match[4 * C64_MAX]; } Hit64;

static long long c64FloorChunk(double x) { // bloco -> chunk, floor (conv. crack 32)
    return (x >= 0) ? (long long)x / 16 : ((long long)x - 15) / 16;
}

static void c64SwapJavaRow(Anchor48 *a, int i, int j) {
    if (i == j) return;
    JavaCfg c = a->cfg[i]; a->cfg[i] = a->cfg[j]; a->cfg[j] = c;
    long long v = a->chunkX[i]; a->chunkX[i] = a->chunkX[j]; a->chunkX[j] = v;
    v = a->chunkZ[i]; a->chunkZ[i] = a->chunkZ[j]; a->chunkZ[j] = v;
    int k = a->nRegions[i]; a->nRegions[i] = a->nRegions[j]; a->nRegions[j] = k;
    int tmp[CRACK_MAX_REGIONS];
    memcpy(tmp, a->regX[i], sizeof tmp);
    memcpy(a->regX[i], a->regX[j], sizeof tmp);
    memcpy(a->regX[j], tmp, sizeof tmp);
    memcpy(tmp, a->regZ[i], sizeof tmp);
    memcpy(a->regZ[i], a->regZ[j], sizeof tmp);
    memcpy(a->regZ[j], tmp, sizeof tmp);
}

char *seedfinder_crack64(const int *mtTypes, const double *mtX, const double *mtZ, int nMt,
                         const int *jTypes, const double *jX, const double *jZ, int nJava,
                         int tolerance, uint64_t startSeed, uint64_t endSeed,
                         int maxResults, double budgetSec, int numThreads) {
    if (nMt < 1 || nMt > C64_MAX) return strdup("{\"error\":\"1-24 MT structures required\"}");
    if (nJava < 1 || nJava > C64_MAX)
        return strdup("{\"error\":\"64-bit mode requires at least one Java-style anchor "
                      "(Trail Ruins=23 or Trial Chambers=24)\"}");
    if (tolerance < 0 || tolerance > 8)
        return strdup("{\"error\":\"tolerance must be between 0 and 8 chunks\"}");
    if (maxResults <= 0 || maxResults > 2000)
        return strdup("{\"error\":\"maxResults must be between 1 and 2000\"}");
    if (numThreads < 1 || numThreads > 64) numThreads = 8;
    if (endSeed > (1ULL << 63)) endSeed = 1ULL << 63; // seeds Bedrock sao signed
    if (startSeed >= endSeed)   return strdup("{\"error\":\"empty seed range\"}");

    /* --- Estagio 1: alvos ------------------------------------------------ */
    int maxD2 = tolerance * tolerance;
    CrackTarget mt[C64_MAX];
    for (int i = 0; i < nMt; i++) {
        int t = mtTypes[i];
        if (t == Mineshaft)
            return strdup("{\"error\":\"Mineshaft is not supported by SeedCracker\"}");
        if (t == Trail_Ruins || t == Trial_Chambers)
            return strdup("{\"error\":\"Trail Ruins and Trial Chambers must be passed as "
                          "Java-style anchors in 64-bit mode\"}");
        StructureConfig sc;
        if (!getBedrockStructureConfig(t, MC_NEWEST, &sc))
            return strdup("{\"error\":\"unknown structure type\"}");
        long long cx = c64FloorChunk(mtX[i]), cz = c64FloorChunk(mtZ[i]);
        if (cx < -100000000LL || cx > 100000000LL ||
            cz < -100000000LL || cz > 100000000LL)
            return strdup("{\"error\":\"coordinates out of range\"}");
        int rs = sc.regionSize;
        mt[i].type = t; mt[i].maxD2 = maxD2; mt[i].numRegions = 0;
        mt[i].chunkX = cx; mt[i].chunkZ = cz;
        // celulas candidatas: mesma formula floor-div do seedfinder_crack/anchor48Build
        long long lox = (cx - tolerance - (rs - 1)) / rs;
        long long hix = (cx + tolerance >= 0) ? (cx + tolerance) / rs
                                              : (cx + tolerance - (rs - 1)) / rs;
        long long loz = (cz - tolerance - (rs - 1)) / rs;
        long long hiz = (cz + tolerance >= 0) ? (cz + tolerance) / rs
                                              : (cz + tolerance - (rs - 1)) / rs;
        int k = 0;
        for (long long x = lox; x <= hix && k < CRACK_MAX_REGIONS; x++)
            for (long long z = loz; z <= hiz && k < CRACK_MAX_REGIONS; z++)
                { mt[i].regX[k] = (int)x; mt[i].regZ[k] = (int)z; k++; }
        if (k == 0) return strdup("{\"error\":\"invalid coordinates\"}");
        mt[i].numRegions = k;
    }
    for (int i = 0; i < nJava; i++) {
        long long cx = c64FloorChunk(jX[i]), cz = c64FloorChunk(jZ[i]);
        if (cx < -100000000LL || cx > 100000000LL ||
            cz < -100000000LL || cz > 100000000LL)
            return strdup("{\"error\":\"coordinates out of range\"}");
    }
    Anchor48 anc;
    if (anchor48Build(&anc, jTypes, jX, jZ, nJava, tolerance) != 0)
        return strdup("{\"error\":\"invalid coordinates\"}");
    // ordem de varredura: menos celulas primeiro (filtro mais forte cedo,
    // espelhando crackTargetCompare); jOrder mapeia linha -> indice de entrada.
    int jOrder[C64_MAX];
    for (int j = 0; j < anc.nJava; j++) jOrder[j] = j;
    for (int i = 0; i < anc.nJava; i++) {
        int best = i;
        for (int j = i + 1; j < anc.nJava; j++)
            if (anc.nRegions[j] < anc.nRegions[best]) best = j;
        if (best != i) {
            c64SwapJavaRow(&anc, i, best);
            int o = jOrder[i]; jOrder[i] = jOrder[best]; jOrder[best] = o;
        }
    }
    const CrackTarget *mOrd[C64_MAX]; // idem para o cruzamento MT
    for (int i = 0; i < nMt; i++) mOrd[i] = &mt[i];
    for (int i = 1; i < nMt; i++) {
        const CrackTarget *key = mOrd[i];
        int j = i - 1;
        while (j >= 0 && mOrd[j]->numRegions > key->numRegions) { mOrd[j + 1] = mOrd[j]; j--; }
        mOrd[j + 1] = key;
    }

    /* --- Estagio 2: sweep 2^48 na janela bounded ------------------------- */
    uint64_t span = endSeed - startSeed;
    uint64_t wLo[2], wHi[2]; int nWin;
    if (span >= (1ULL << 48)) { wLo[0] = 0; wHi[0] = 1ULL << 48; nWin = 1; }
    else {
        uint64_t sLo = startSeed & C64_M48;
        uint64_t sHi = ((endSeed - 1) & C64_M48) + 1; // (0, 2^48]
        if (sLo < sHi) { wLo[0] = sLo; wHi[0] = sHi; nWin = 1; }
        else { // janela envolve mod 2^48: duas pecas disjuntas, sem duplicata
            wLo[0] = sLo; wHi[0] = 1ULL << 48;
            wLo[1] = 0;   wHi[1] = sHi;       nWin = 2;
        }
    }
    // deadline GLOBAL: cobre sweep + cross + lift (ruling 7).
    TR0();
    double deadline = budgetSec > 0.0 ? nowms_s() + budgetSec * 1000.0 : 0.0;
    U64Vec s48s = {0};
    uint64_t swept = 0; int effThreads = numThreads, sweepTo = 0;
    for (int i = 0; i < nWin && !sweepTo; i++) {
        double rem = deadline > 0.0 ? (deadline - nowms_s()) / 1000.0 : budgetSec;
        if (deadline > 0.0 && rem <= 0.0) { sweepTo = 1; break; }
        Sweep48Result r = sweep48MT(&anc, wLo[i], wHi[i], rem, numThreads, &s48s);
        swept += r.checked; effThreads = r.threads;
        if (r.timedOut) sweepTo = 1;
    }
#ifdef C64_TRACE
    fprintf(stderr, "[c64] sweep %llu s48s -> %d survivors\n",
            (unsigned long long)swept, s48s.n);
#endif
    TR_STAGE("stage2 sweep");
    if (sweepTo) {
        free(s48s.v);
        return strdup("{\"error\":\"2^48 sweep exceeded budget - raise max_seconds, "
                      "provide 4+ Trial Chambers, or narrow start/end\"}");
    }

    /* --- Estagios 3+4 fused: cross MT + lift 2^16 via bioma --------------
     * O cross (estagio 3) nao depende de hi: coleta, por ancora MT, TODAS as
     * celulas cujo placement cai dentro de tolerancia (par d²,bloco). O lift
     * (estagio 4) escolhe, por ancora, o placement viavel de MENOR d² sob a
     * seed completa — um irmao mais proximo em bioma ruim nao pode esconder o
     * placement verdadeiro mais distante que gera in-game (mesma semantica do
     * loop de validacao final do crack de 32 bits: seedfinder_wrapper.c
     * :666-686). score = soma dos d² escolhidos. */
    Hit64 *hits = malloc((size_t)maxResults * sizeof(Hit64));
    int nHits = 0, worstIdx = -1;
    int64_t worst = INT64_MAX; // mesmo processo record-min do crackInsert de 32b
    Generator g;
    setupGenerator(&g, MC_NEWEST, 0);
    uint64_t hiLo = startSeed >> 48, hiHi = (endSeed - 1) >> 48;
    uint64_t lifted = 0; int liftTo = 0;
    typedef struct { int d2; int x; int z; } C64Cand;
    for (int i = 0; i < s48s.n && !liftTo; i++) {
        // o cross pode eliminar TODOS os candidatos sem nunca entrar no lift —
        // checar o deadline aqui tambem (a cada ~1M s48, ~0.5 s de cross).
        if ((i & 0xFFFFF) == 0xFFFFF && deadline > 0.0 && nowms_s() > deadline) { liftTo = 1; break; }
        uint64_t s48 = s48s.v[i];
        uint32_t s32 = (uint32_t)(s48 & 0xFFFFFFFFULL);

        /* Estagio 3: coleta de celulas in-tolerance por ancora (ordem de
         * entrada preservada via indice k). */
        C64Cand *cands[C64_MAX] = {0};  // arrays empilhados por ancora
        C64Cand candStore[C64_MAX][4];  // fast path: ate' 4 celulas/ancora
        int nCand[C64_MAX];
        int ok = 1;
        for (int m = 0; m < nMt && ok; m++) {
            const CrackTarget *t = mOrd[m];
            int k = (int)(t - mt);
            // celulas in-tolerance desta ancora
            C64Cand *list = candStore[k];
            int cap = 4;
            if (t->numRegions > cap) {                   // caminho raro (>4 celulas)
                cap = t->numRegions;
                list = malloc((size_t)cap * sizeof(C64Cand));
                if (!list) { ok = 0; break; }
            }
            int cnt = 0;
            for (int r = 0; r < t->numRegions; r++) {
                Pos pos;
                if (!getBedrockStructurePos(t->type, MC_NEWEST, s32,
                                            t->regX[r], t->regZ[r], &pos))
                    continue;
                long long cx = ((long long)pos.x - 8) >> 4; // chunk anchor (conv. 32b)
                long long cz = ((long long)pos.z - 8) >> 4;
                long long dx = cx - t->chunkX, dz = cz - t->chunkZ;
                long long d2 = dx * dx + dz * dz;
                if (d2 > t->maxD2) continue;
                list[cnt].d2 = (int)d2; list[cnt].x = pos.x; list[cnt].z = pos.z;
                cnt++;
            }
            if (cnt == 0) {                              // ancora sem placement in-range
                if (list != candStore[k]) free(list);
                ok = 0; break;
            }
            cands[k] = list; nCand[k] = cnt;
        }
        if (!ok) {                                       // descarta heap alocado neste s48
            for (int m = 0; m < nMt; m++) {
                int k = (int)(mOrd[m] - mt);
                if (cands[k] && cands[k] != candStore[k]) free(cands[k]);
            }
            continue;
        }

        /* Estagio 4: para cada hi, escolhe o placement viavel de menor d². */
        for (uint64_t hi = hiLo; hi <= hiHi; hi++) {
            uint64_t full = (hi << 48) | s48;
            if (full < startSeed || full >= endSeed) continue;
            lifted++;
            applySeed(&g, DIM_OVERWORLD, full);
            int mx[2 * C64_MAX];        // placement MT escolhido (blocos) por entrada
            int64_t score = 0; int viable = 1;
            for (int k = 0; k < nMt && viable; k++) {
                int bestD = INT32_MAX, bx = 0, bz = 0;
                for (int c = 0; c < nCand[k]; c++) {
                    const C64Cand *cd = &cands[k][c];
                    if (cd->d2 > bestD) continue;        // ja' temos um mais proximo
                    if (!structureIsViable(mtTypes[k], &g, cd->x, cd->z)) continue;
                    bestD = cd->d2; bx = cd->x; bz = cd->z;
                }
                if (bestD == INT32_MAX) { viable = 0; break; } // sem placement viavel
                mx[k * 2] = bx; mx[k * 2 + 1] = bz;
                score += bestD;
            }
            if (viable && (nHits < maxResults || score < worst)) {
                int slot = (nHits < maxResults) ? nHits++ : worstIdx;
                Hit64 *h = &hits[slot];
                h->seed = full; h->score = score;
                memcpy(h->match, mx, (size_t)nMt * 2 * sizeof(int));
                // matches Java-style: placement real sob a seed completa na
                // melhor celula da ancora (o sweep garante d² <= maxD2).
                for (int j = 0; j < anc.nJava; j++) {
                    int bestR = 0; long long bestD = LLONG_MAX;
                    for (int r = 0; r < anc.nRegions[j]; r++) {
                        long long cx, cz;
                        javaChunk(&anc.cfg[j], s48, anc.regX[j][r], anc.regZ[j][r], &cx, &cz);
                        long long dx = cx - anc.chunkX[j], dz = cz - anc.chunkZ[j];
                        long long d2 = dx * dx + dz * dz;
                        if (d2 < bestD) { bestD = d2; bestR = r; }
                    }
                    int px, pz;
                    Pos pos;
                    if (getStructurePos(jTypes[jOrder[j]], MC_NEWEST, full,
                                        anc.regX[j][bestR], anc.regZ[j][bestR], &pos)) {
                        px = pos.x; pz = pos.z;
                    } else { // cinto de seguranca: bloco implícito em javaChunk
                        long long cx, cz;
                        javaChunk(&anc.cfg[j], s48, anc.regX[j][bestR], anc.regZ[j][bestR], &cx, &cz);
                        px = (int)((cx + 1) << 4); pz = (int)((cz + 1) << 4);
                    }
                    int *mm = h->match + (nMt + jOrder[j]) * 2;
                    mm[0] = px; mm[1] = pz;
                }
                worst = INT64_MAX; worstIdx = -1;
                for (int q = 0; q < nHits; q++)
                    if (hits[q].score < worst) { worst = hits[q].score; worstIdx = q; }
            }
            if ((lifted & 1023ULL) == 0 && deadline > 0.0 && nowms_s() > deadline)
                { liftTo = 1; break; }
        }
        // libera quaisquer listas heap do cross deste s48
        for (int m = 0; m < nMt; m++) {
            int k = (int)(mOrd[m] - mt);
            if (cands[k] && cands[k] != candStore[k]) free(cands[k]);
        }
    }
    free(s48s.v);
    TR_STAGE("stages 3+4 cross+lift");

    /* insertion sort ascending por score (mesmo padrao do crack de 32 bits) */
    for (int i = 1; i < nHits; i++) {
        Hit64 key = hits[i];
        int j = i - 1;
        while (j >= 0 && hits[j].score > key.score) { hits[j + 1] = hits[j]; j--; }
        hits[j + 1] = key;
    }

    /* envelope identico ao do seedfinder_crack + seed_str + "bits":64.
     * piores casos por item: 104 fixos (seed/seed_str/score %lld ate' 20
     * digitos) + 26 por par de match + 3; folga generosa. */
    int nPairs = nMt + nJava;
    size_t cap = 256 + (size_t)nHits * ((size_t)nPairs * 28 + 160);
    char *buf = malloc(cap);
    int off = sprintf(buf, "{\"results\":[");
    for (int i = 0; i < nHits; i++) {
        off += sprintf(buf + off,
                       "%s{\"seed\":%lld,\"seed_str\":\"%lld\",\"score\":%lld,\"matches\":[",
                       i ? "," : "", (long long)hits[i].seed, (long long)hits[i].seed,
                       (long long)hits[i].score);
        for (int k = 0; k < nPairs; k++)
            off += sprintf(buf + off, "%s[%d,%d]", k ? "," : "",
                           hits[i].match[k * 2], hits[i].match[k * 2 + 1]);
        off += sprintf(buf + off, "]}");
    }
    sprintf(buf + off, "],\"checked\":%llu,\"timed_out\":%s,\"threads\":%d,\"bits\":64}",
            (unsigned long long)swept, liftTo ? "true" : "false", effThreads);
    free(hits);
    return buf;
}
