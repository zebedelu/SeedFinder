/* core/crack_auto.c — modo automático do SeedCracker. A varredura é
 * placement-only: NÃO chama crackValidateSeed / gate de bioma (fix do defeito
 * 2 — o gate descarta seeds 64-bit reais cujo bioma sob o lo32 diverge). */
#include "crack_auto.h"
#include "crack_mt.h"
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
