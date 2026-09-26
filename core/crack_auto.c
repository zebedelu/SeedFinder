/* core/crack_auto.c — modo automático do SeedCracker. A varredura é
 * placement-only: NÃO chama crackValidateSeed / gate de bioma (fix do defeito
 * 2 — o gate descarta seeds 64-bit reais cujo bioma sob o lo32 diverge). */
#include "crack_auto.h"
#include "crack_mt.h"
#include "crack64.h"
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
