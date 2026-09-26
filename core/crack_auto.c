/* core/crack_auto.c — modo automático do SeedCracker. A varredura é
 * placement-only: NÃO chama crackValidateSeed / gate de bioma (fix do defeito
 * 2 — o gate descarta seeds 64-bit reais cujo bioma sob o lo32 diverge). */
#include "crack_auto.h"
#include "crack_mt.h"
#include "crack64.h"
#include "ChunkBiomesGUI/Bfinders.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include "platform_threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *seedfinder_crack_auto_sweep(
    const int *types, int numTypes,
    const double *xBlocks, const double *zBlocks,
    int tolerance, uint64_t startSeed, uint64_t endSeed,
    int maxResults, double timeBudgetSec, int numThreads)
{
    /* Só no modo auto: o seedfinder_crack continua aceitando 23/24. */
    for (int i = 0; i < numTypes; i++)
        if (types[i] == Trail_Ruins || types[i] == Trial_Chambers)
            return strdup("{\"error\":\"Trail Ruins and Trial Chambers cannot be swept - "
                          "pass them as Java anchors\"}");

    if (maxResults <= 0 || maxResults > 2000)
        return strdup("{\"error\":\"maxResults must be between 1 and 2000\"}");
    if (numThreads < 1 || numThreads > 64)
        numThreads = 8;
    if (endSeed > CRACK_SEED_SPACE)
        endSeed = CRACK_SEED_SPACE;
    if (startSeed >= endSeed)
        return strdup("{\"error\":\"empty seed range\"}");

    CrackTarget targets[CRACK_MAX_STRUCTURES];
    int n = 0;
    char *err = NULL;
    if (crackTargetsBuild(types, numTypes, xBlocks, zBlocks, tolerance,
                          targets, &n, &err))
        return err;

#if defined(__EMSCRIPTEN__)
    /* Sem -pthread: uma fatia por Web Worker (mesmo caminho do seedfinder_crack). */
    numThreads = 1;
#endif
    if ((endSeed - startSeed) < (uint64_t)numThreads)
        numThreads = 1;

    /* Strongest filter first. */
    qsort(targets, n, sizeof(CrackTarget), crackTargetCompare);

    double deadline = (timeBudgetSec > 0.0) ? nowms_s() + timeBudgetSec * 1000.0 : 0.0;
    CrackHit *hits = NULL;
    int nHits = 0;
    uint64_t checked = 0;
    int timedOut = 0;
    if (crackSweepRanked(targets, n, startSeed, endSeed, maxResults, deadline,
                         numThreads, &hits, &nHits, &checked, &timedOut) != 0)
        return strdup("{\"error\":\"out of memory\"}");

    size_t cap = 64 + (size_t)nHits * 80 + 64;
    char *buf = malloc(cap);
    if (!buf) {
        free(hits);
        return strdup("{\"error\":\"out of memory\"}");
    }
    int off = sprintf(buf, "{\"results\":[");
    for (int i = 0; i < nHits; i++)
        off += sprintf(buf + off, "%s{\"seed\":%llu,\"score\":%lld}",
                       i ? "," : "", (unsigned long long)hits[i].seed,
                       (long long)hits[i].score);
    sprintf(buf + off, "],\"checked\":%llu,\"timed_out\":%s,\"threads\":%d}",
            (unsigned long long)checked, timedOut ? "true" : "false", numThreads);
    free(hits);
    return buf;
}

char *seedfinder_crack_auto_lift48(
    const int *mtTypes, const double *mtX, const double *mtZ, int nMt,
    const uint64_t *lo32Seeds, int nLo32,
    const int *jTypes, const double *jX, const double *jZ, int nJava,
    int tolerance, int maxResults)
{
    /* Só no modo auto: 23/24 são ancoras Java, nunca alvos de sweep —
     * checado antes de crackTargetsBuild (que continua aceitando-os). */
    for (int i = 0; i < nMt; i++)
        if (mtTypes[i] == Trail_Ruins || mtTypes[i] == Trial_Chambers)
            return strdup("{\"error\":\"Trail Ruins and Trial Chambers cannot be swept - "
                          "pass them as Java anchors\"}");
    if (nLo32 < 1)
        return strdup("{\"error\":\"at least one lo32 seed required\"}");
    if (maxResults <= 0 || maxResults > 2000)
        return strdup("{\"error\":\"maxResults must be between 1 and 2000\"}");

    CrackTarget targets[CRACK_MAX_STRUCTURES];
    int n = 0;
    char *err = NULL;
    if (crackTargetsBuild(mtTypes, nMt, mtX, mtZ, tolerance, targets, &n, &err))
        return err;

    /* direct32: validação completa (placement + bioma) sob seed = lo32,
     * matches na ordem de entrada, cap em maxResults. */
    typedef struct { uint64_t seed; int64_t score; int match[2 * CRACK_MAX_STRUCTURES]; } AutoHit;
    AutoHit *hits = malloc((size_t)maxResults * sizeof(AutoHit));
    if (!hits)
        return strdup("{\"error\":\"out of memory\"}");
    int nHits = 0;
    for (int i = 0; i < nLo32 && nHits < maxResults; i++) {
        int matches[2 * CRACK_MAX_STRUCTURES];
        int64_t score;
        if (crackValidateSeed(targets, n, lo32Seeds[i], matches, &score)) {
            AutoHit *h = &hits[nHits++];
            h->seed = lo32Seeds[i];
            h->score = score;
            memcpy(h->match, matches, (size_t)n * 2 * sizeof(int));
        }
    }

    /* Lift 32->48 pelas âncoras Java (nJava = 0 => sem lift). Chamada única
     * com a lista inteira; numThreads fixo 1 (inline — sem criação de
     * thread neste export, mesmo padrão do caminho Emscripten). */
    U64Vec s48 = {0};
    if (nJava > 0) {
        Anchor48 anc;
        if (anchor48Build(&anc, jTypes, jX, jZ, nJava, tolerance) != 0) {
            free(hits);
            return strdup("{\"error\":\"invalid Java anchors (Trail Ruins=23 / "
                          "Trial Chambers=24 with valid coordinates)\"}");
        }
        U64Vec lo32s = {0};
        for (int i = 0; i < nLo32; i++)
            if (u64Push(&lo32s, lo32Seeds[i])) {
                free(lo32s.v); free(hits);
                return strdup("{\"error\":\"out of memory\"}");
            }
        int to = 0;
        liftJavaHi(&anc, &lo32s, 0.0, 1, &s48, &to);
        free(lo32s.v);
        if (s48.n > 1024) {
            char msg[256];
            snprintf(msg, sizeof msg,
                     "{\"error\":\"too many 48-bit candidates (%d) - add more "
                     "Trial Chambers/Trail Ruins or enter their corner "
                     "coordinates more precisely\"}", s48.n);
            free(s48.v); free(hits);
            return strdup(msg);
        }
    }

    /* Casos por item: seed/score ≤ 20 dígitos, par de match ≤ 26 chars,
     * s48 ≤ 21 — folga generosa. */
    size_t cap = 128 + (size_t)nHits * ((size_t)n * 2 * 28 + 96)
               + (size_t)s48.n * 24;
    char *buf = malloc(cap);
    if (!buf) {
        free(s48.v); free(hits);
        return strdup("{\"error\":\"out of memory\"}");
    }
    int off = sprintf(buf, "{\"direct32\":[");
    for (int i = 0; i < nHits; i++) {
        off += sprintf(buf + off, "%s{\"seed\":%llu,\"score\":%lld,\"matches\":[",
                       i ? "," : "", (unsigned long long)hits[i].seed,
                       (long long)hits[i].score);
        for (int k = 0; k < n; k++)
            off += sprintf(buf + off, "%s[%d,%d]", k ? "," : "",
                           hits[i].match[k * 2], hits[i].match[k * 2 + 1]);
        off += sprintf(buf + off, "]}");
    }
    off += sprintf(buf + off, "],\"s48\":[");
    for (int i = 0; i < s48.n; i++)
        off += sprintf(buf + off, "%s%llu", i ? "," : "",
                       (unsigned long long)s48.v[i]);
    sprintf(buf + off, "]}");
    free(s48.v);
    free(hits);
    return buf;
}

/* Célula de placement in-tolerance de uma âncora (estágio 3 do lift63). */
typedef struct { int d2, x, z; } AutoCand;

static int autoCandCmp(const void *a, const void *b)
{
    const AutoCand *x = (const AutoCand *)a, *y = (const AutoCand *)b;
    return (x->d2 > y->d2) - (x->d2 < y->d2);
}

char *seedfinder_crack_auto_lift63(
    const uint64_t *s48Seeds, int nS48,
    const int *mtTypes, const double *mtX, const double *mtZ, int nMt,
    int tolerance,
    int maxResults, double timeBudgetSec)
{
    /* Só no modo auto: 23/24 são âncoras Java (já resolvidas pelo lift48),
     * nunca alvos de sweep — checado antes de crackTargetsBuild. */
    for (int i = 0; i < nMt; i++)
        if (mtTypes[i] == Trail_Ruins || mtTypes[i] == Trial_Chambers)
            return strdup("{\"error\":\"Trail Ruins and Trial Chambers cannot be swept - "
                          "pass them as Java anchors\"}");
    if (nS48 < 1)
        return strdup("{\"error\":\"at least one s48 seed required\"}");
    if (maxResults <= 0 || maxResults > 2000)
        return strdup("{\"error\":\"maxResults must be between 1 and 2000\"}");

    CrackTarget targets[CRACK_MAX_STRUCTURES];
    int n = 0;
    char *err = NULL;
    if (crackTargetsBuild(mtTypes, nMt, mtX, mtZ, tolerance, targets, &n, &err))
        return err;

    /* Cross fail-fast com o filtro mais forte primeiro; a serialização usa a
     * cópia em ORDEM DE ENTRADA (crackValidateSeed), então o qsort não vaza
     * para os matches. */
    CrackTarget ord[CRACK_MAX_STRUCTURES];
    memcpy(ord, targets, (size_t)n * sizeof(CrackTarget));
    qsort(ord, n, sizeof(CrackTarget), crackTargetCompare);

    AutoCand *cands = malloc((size_t)n * CRACK_MAX_REGIONS * sizeof(AutoCand));
    int *nCand = malloc((size_t)n * sizeof(int));
    int *cOff = malloc((size_t)n * sizeof(int));
    CrackHit *hits = calloc((size_t)maxResults, sizeof(CrackHit));
    if (!cands || !nCand || !cOff || !hits) {
        free(cands); free(nCand); free(cOff); free(hits);
        return strdup("{\"error\":\"out of memory\"}");
    }

    /* Top-N com o padrão crackInsert/CrackWorker (1 thread inline — este
     * export não tem argumento numThreads, mesmo caminho do __EMSCRIPTEN__). */
    CrackWorker w = {0};
    w.hits = hits;
    w.maxResults = maxResults;
    w.worst = INT64_MAX;
    w.worstIdx = -1;

    double deadline = (timeBudgetSec > 0.0) ? nowms_s() + timeBudgetSec * 1000.0 : 0.0;
    Generator g;
    setupGenerator(&g, MC_NEWEST, 0);
    uint64_t checked = 0;
    int timedOut = 0;

    for (int i = 0; i < nS48 && !timedOut; i++) {
        if (deadline > 0.0 && nowms_s() > deadline) { timedOut = 1; break; }
        uint64_t s48 = s48Seeds[i] & C64_M48;
        uint32_t s32 = (uint32_t)(s48 & 0xFFFFFFFFULL);

        /* Estágio 3 (cross): células in-tolerance por âncora sob o lo32 — o
         * placement Bedrock depende só dos 32 bits baixos (mSetSeed mascara
         * em 32), então o mesmo conjunto serve todos os hi. */
        int base = 0, ok = 1;
        for (int k = 0; k < n && ok; k++) {
            const CrackTarget *t = &ord[k];
            int cnt = 0;
            cOff[k] = base;
            for (int r = 0; r < t->numRegions; r++) {
                Pos pos;
                if (!getBedrockStructurePos(t->type, MC_NEWEST, s32,
                                            t->regX[r], t->regZ[r], &pos))
                    continue;
                long long cx = ((long long)pos.x - 8) >> 4;
                long long cz = ((long long)pos.z - 8) >> 4;
                long long dx = cx - t->chunkX, dz = cz - t->chunkZ;
                long long d2 = dx * dx + dz * dz;
                if (d2 > t->maxD2) continue;
                AutoCand *cd = &cands[base + cnt];
                cd->d2 = (int)d2; cd->x = pos.x; cd->z = pos.z;
                cnt++;
            }
            if (cnt == 0) { ok = 0; break; }   /* âncora sem placement in-range */
            nCand[k] = cnt;
            /* Asc por d²: o primeiro viável já é o de menor distância. */
            qsort(&cands[base], (size_t)cnt, sizeof(AutoCand), autoCandCmp);
            base += cnt;
        }
        if (!ok) continue;

        /* Estágio 4 (lift 48→63): por hi, escolhe por âncora o placement
         * VIÁVEL de menor d² (best-viable, mesma semântica do
         * testCrack64Viable — um irmão mais próximo em bioma morto não pode
         * esconder o verdadeiro). score = soma dos d² escolhidos. */
        for (uint64_t hi = 0; hi < (1ULL << 15); hi++) {
            if (deadline > 0.0 && (checked & 1023ULL) == 0 &&
                nowms_s() > deadline) { timedOut = 1; break; }
            checked++;
            uint64_t full = (hi << 48) | s48;
            applySeed(&g, DIM_OVERWORLD, full);
            int64_t score = 0;
            int viable = 1;
            for (int k = 0; k < n && viable; k++) {
                int bestD = INT32_MAX;
                /* Células em ordem asc de d² (pré-ordenadas acima): o primeiro
                 * viável já é o de menor d², então dá pra sair na hora. */
                for (int c = 0; c < nCand[k]; c++) {
                    const AutoCand *cd = &cands[cOff[k] + c];
                    if (structureIsViable(ord[k].type, &g, cd->x, cd->z)) {
                        bestD = cd->d2;
                        break;
                    }
                }
                if (bestD == INT32_MAX) { viable = 0; break; }
                score += bestD;
            }
            if (viable)
                crackInsert(&w, full, score);
        }
    }
    free(cands); free(nCand); free(cOff);

    /* Re-derivação dos matches dos vencedores: chunks na ORDEM DE ENTRADA
     * (convenção do modo auto — direct32 e lift63 compartilham; o Task 7
     * converte chunks→blocos ao exibir). */
    int c = w.hitCount;
    int *m = malloc(((size_t)c > 0 ? (size_t)c : 1) * (size_t)n * 2 * sizeof(int));
    if (!m) {
        free(hits);
        return strdup("{\"error\":\"out of memory\"}");
    }
    int kept = 0;
    for (int i = 0; i < c; i++) {
        int64_t vs = 0;
        int *mm = m + (size_t)kept * (size_t)n * 2;
        if (!crackValidateSeed(targets, n, hits[i].seed, mm, &vs))
            continue;
        hits[kept].seed = hits[i].seed;
        hits[kept].score = vs;
        kept++;
    }
    c = kept;

    /* Insertion sort asc por score movendo as linhas de matches junto
     * (mesmo padrão do pós-processo do seedfinder_crack). */
    for (int i = 1; i < c; i++) {
        CrackHit key = hits[i];
        int kmrow[CRACK_MAX_STRUCTURES * 2];
        const int *src = m + (size_t)i * (size_t)n * 2;
        for (int k = 0; k < n * 2; k++) kmrow[k] = src[k];
        int j = i - 1;
        while (j >= 0 && hits[j].score > key.score) {
            hits[j + 1] = hits[j];
            for (int k = 0; k < n * 2; k++)
                m[(size_t)(j + 1) * n * 2 + k] = m[(size_t)j * n * 2 + k];
            j--;
        }
        hits[j + 1] = key;
        for (int k = 0; k < n * 2; k++)
            m[(size_t)(j + 1) * n * 2 + k] = kmrow[k];
    }

    /* Piores casos por item: 83 fixos (seed/seed_str/score até 20 dígitos) +
     * 24 por par de match (coords ±1e8) + 2 — folga generosa. */
    size_t cap = 256 + (size_t)c * ((size_t)n * 56 + 160);
    char *buf = malloc(cap);
    if (!buf) {
        free(m); free(hits);
        return strdup("{\"error\":\"out of memory\"}");
    }
    int off = sprintf(buf, "{\"results\":[");
    for (int i = 0; i < c; i++) {
        off += sprintf(buf + off,
                       "%s{\"seed\":%lld,\"seed_str\":\"%lld\",\"score\":%lld,\"matches\":[",
                       i ? "," : "", (long long)hits[i].seed,
                       (long long)hits[i].seed, (long long)hits[i].score);
        const int *mm = m + (size_t)i * (size_t)n * 2;
        for (int k = 0; k < n; k++)
            off += sprintf(buf + off, "%s[%d,%d]", k ? "," : "",
                           mm[k * 2], mm[k * 2 + 1]);
        off += sprintf(buf + off, "]}");
    }
    sprintf(buf + off, "],\"checked\":%llu,\"timed_out\":%s}",
            (unsigned long long)checked, timedOut ? "true" : "false");
    free(m);
    free(hits);
    return buf;
}
