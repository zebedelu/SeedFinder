#include "ChunkBiomesGUI/Bfinders.h"
#include "ChunkBiomesGUI/cubiomes/generator.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include "ChunkBiomesGUI/cubiomes/util.h"
#include "crack64.h"
#include "crack_mt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef SEEDFINDER_API
#ifdef _WIN32
#ifdef SEEDFINDER_BRIDGE_SHARED
#define SEEDFINDER_API __declspec(dllexport)
#else
#define SEEDFINDER_API
#endif
#else
#define SEEDFINDER_API __attribute__((visibility("default")))
#endif
#endif

/* Floor division — correct for negative numbers */
static inline int floorDiv(int a, int b)
{
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

/* Internal structure for collecting results */
typedef struct {
    const char *name;
    int x;
    int z;
    double distance;
} FoundStructure;

/* Compare function for qsort — ascending by distance */
static int compareByDistance(const void *a, const void *b)
{
    double da = ((const FoundStructure *)a)->distance;
    double db = ((const FoundStructure *)b)->distance;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* Bedrock checks the biome at the structure's OWN position cell; the upstream
 * Java-style gate samples an offset door corner and passes positions sitting
 * on swamp/river/etc. Shared by /scan and the crack result filter. Declared
 * in crack64.h — seedfinder_crack64's 16-bit biome lift calls it too. */
int structureIsViable(int structureType, Generator *g, int x, int z)
{
    if (!isViableBedrockStructurePos(structureType, g, x, z, 0))
        return 0;
    if (structureType == Outpost) {
        int cellX = (x >> 4) * 4 + 2;
        int cellZ = (z >> 4) * 4 + 2;
        int bio = getBiomeAt(g, 0, cellX, 319 >> 2, cellZ);
        if (bio < 0 || !isViableFeatureBiome(MC_NEWEST, Outpost, bio))
            return 0;
    }
    return 1;
}

SEEDFINDER_API const char *seedfinder_status(void)
{
    return "{\"status\": \"ok\"}";
}

SEEDFINDER_API char *seedfinder_scan(
    uint64_t seed,
    double playerX, double playerZ,
    int radius, int maxResults,
    const int *types, int numTypes)
{
    Generator g;
    setupGenerator(&g, MC_NEWEST, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    int playerChunkX = (int)floor(playerX / 16.0);
    int playerChunkZ = (int)floor(playerZ / 16.0);

    /* Collect results */
    FoundStructure *results = NULL;
    int resultCount = 0;
    int resultCapacity = 0;

    for (int ti = 0; ti < numTypes; ti++) {
        int structType = types[ti];
        StructureConfig sconf;

        if (!getBedrockStructureConfig(structType, MC_NEWEST, &sconf))
            continue;

        int regionSize = sconf.regionSize;
        int regionMinX = floorDiv(playerChunkX - radius, regionSize) - 1;
        int regionMaxX = floorDiv(playerChunkX + radius, regionSize) + 1;
        int regionMinZ = floorDiv(playerChunkZ - radius, regionSize) - 1;
        int regionMaxZ = floorDiv(playerChunkZ + radius, regionSize) + 1;

        for (int regX = regionMinX; regX <= regionMaxX; regX++) {
            for (int regZ = regionMinZ; regZ <= regionMaxZ; regZ++) {
                Pos pos;
                if (!getBedrockStructurePos(structType, MC_NEWEST, seed, regX, regZ, &pos))
                    continue;

                /* Biome viability check */
                if (!structureIsViable(structType, &g, pos.x, pos.z))
                    continue;

                int dx = pos.x / 16 - playerChunkX;
                int dz = pos.z / 16 - playerChunkZ;
                double distance = sqrt((double)(dx * dx + dz * dz));

                if (distance > radius)
                    continue;

                const char *name = struct2str(structType);
                if (!name) name = "unknown";

                /* Grow array if needed */
                if (resultCount >= resultCapacity) {
                    resultCapacity = resultCapacity == 0 ? 64 : resultCapacity * 2;
                    results = (FoundStructure *)realloc(results, resultCapacity * sizeof(FoundStructure));
                }

                results[resultCount].name = name;
                results[resultCount].x = pos.x;
                results[resultCount].z = pos.z;
                results[resultCount].distance = distance;
                resultCount++;
            }
        }
    }

    /* Sort by distance */
    if (resultCount > 1)
        qsort(results, resultCount, sizeof(FoundStructure), compareByDistance);

    /* Cap results */
    if (resultCount > maxResults)
        resultCount = maxResults;

    /* Build JSON string */
    /* Worst case: each entry ~80 chars, plus overhead */
    int jsonSize = 32 + resultCount * 100 + 1;
    char *json = (char *)malloc(jsonSize);
    int offset = 0;

    offset += sprintf(json + offset, "{\"results\": [");

    for (int i = 0; i < resultCount; i++) {
        if (i > 0)
            offset += sprintf(json + offset, ", ");

        offset += sprintf(json + offset,
            "{\"name\": \"%s\", \"x\": %d, \"z\": %d, \"distance\": %.1f}",
            results[i].name, results[i].x, results[i].z, results[i].distance);
    }

    offset += sprintf(json + offset, "]}");

    free(results);
    return json;
}

SEEDFINDER_API void seedfinder_free_result(char *result)
{
    free(result);
}

/* ============================ SeedCracker ============================ */

#include "platform_threads.h"

#define CRACK_SEED_SPACE (0x100000000ULL)

/* Per-input-structure: everything that does NOT depend on the seed.
 * Candidate region cells whose generated structure could land within
 * `tolerance` chunks of the requested anchor chunk (chunk = floor(x/16)).
 * Tipo e metodos (CrackTarget, crackScore, ...) vivem em core/crack_mt.h/.c. */

typedef struct {
    uint64_t seed;
    int64_t  score;
} CrackHit;

typedef struct {
    const CrackTarget *targets;
    int                nTargets;
    uint64_t           start, end;
    int                maxResults;
    double             deadline;   /* ms since epoch, 0.0 = unlimited */
    volatile int      *stop;
    CrackHit          *hits;       /* thread-local top-N (cap maxResults) */
    int                hitCount;
    int64_t            worst;
    int                worstIdx;
    uint64_t           checked;
} CrackWorker;

/* Keep the best maxResults hits. Replacing the current worst keeps the table
 * bounded; the record-minimum process keeps actual replacements few. */
static void crackInsert(CrackWorker *w, uint64_t seed, int64_t score)
{
    if (w->hitCount < w->maxResults) {
        w->hits[w->hitCount].seed = seed;
        w->hits[w->hitCount].score = score;
        w->hitCount++;
    } else if (score < w->worst) {
        w->hits[w->worstIdx].seed = seed;
        w->hits[w->worstIdx].score = score;
    } else {
        return;
    }
    w->worst = INT64_MAX;
    w->worstIdx = -1;
    for (int i = 0; i < w->hitCount; i++) {
        if (w->hits[i].score < w->worst) {
            w->worst = w->hits[i].score;
            w->worstIdx = i;
        }
    }
}

static void *crackWorker(void *arg)
{
    CrackWorker *w = (CrackWorker *)arg;
    uint64_t seed = w->start;

#if CRACK_SIMD
    if (crack_g_simd) {
        const uint64_t simdEnd = w->end - ((w->end - w->start) % (uint64_t)CRACK_WIDTH);
        for (; seed < simdEnd; seed += CRACK_WIDTH) {
            if (((seed - w->start) & 0xFFFFULL) == 0) {
                if (*w->stop)
                    break;
                if (w->deadline > 0.0 && nowms_s() > w->deadline) {
                    *w->stop = 1;
                    break;
                }
            }
            uint64_t batch[CRACK_WIDTH];
            int64_t scores[CRACK_WIDTH];
            for (int l = 0; l < CRACK_WIDTH; l++)
                batch[l] = seed + (uint64_t)l;
            crackScoreSimd(w->targets, w->nTargets, batch, scores);
            for (int l = 0; l < CRACK_WIDTH; l++) {
                w->checked++;
                if (scores[l] >= 0)
                    crackInsert(w, batch[l], scores[l]);
            }
        }
    }
#endif
    /* Scalar path: seed-range remainder, non-AVX2 CPUs, and bail after stop. */
    for (; seed < w->end && !*w->stop; seed++) {
        if ((seed & 0xFFFFULL) == 0) {
            if (*w->stop)
                break;
            if (w->deadline > 0.0 && nowms_s() > w->deadline) {
                *w->stop = 1;
                break;
            }
        }
        int64_t score = crackScore(w->targets, w->nTargets, seed);
        if (score >= 0)
            crackInsert(w, seed, score);
        w->checked++;
    }
    return NULL;
}

SEEDFINDER_API char *seedfinder_crack(
    const int *types, int numTypes,
    const double *xBlocks, const double *zBlocks,
    int tolerance,
    uint64_t startSeed, uint64_t endSeed,
    int maxResults, double timeBudgetSec, int numThreads)
{
    if (numTypes < 4)
        return strdup("{\"error\":\"at least 4 structures are required\"}");
    if (numTypes > CRACK_MAX_STRUCTURES)
        return strdup("{\"error\":\"too many structures (max 24)\"}");
    if (tolerance < 0 || tolerance > 8)
        return strdup("{\"error\":\"tolerance must be between 0 and 8 chunks\"}");
    if (maxResults <= 0 || maxResults > 2000)
        return strdup("{\"error\":\"maxResults must be between 1 and 2000\"}");
    if (numThreads < 1 || numThreads > 64)
        numThreads = 8;
    if (endSeed > CRACK_SEED_SPACE)
        endSeed = CRACK_SEED_SPACE;
    if (startSeed >= endSeed)
        return strdup("{\"error\":\"empty seed range\"}");

#if CRACK_SIMD
    crackSimdDetect();
#endif

    CrackTarget targets[CRACK_MAX_STRUCTURES];
    int maxD2 = tolerance * tolerance;

    for (int i = 0; i < numTypes; i++) {
        StructureConfig sconf;
        if (!getBedrockStructureConfig(types[i], MC_NEWEST, &sconf))
            return strdup("{\"error\":\"unknown structure type\"}");
        if (types[i] == Mineshaft)
            return strdup("{\"error\":\"Mineshaft is not supported by SeedCracker\"}");

        CrackTarget *t = &targets[i];
        int64_t chunkX = (xBlocks[i] >= 0) ? (int64_t)xBlocks[i] / 16
                                           : ((int64_t)xBlocks[i] - 15) / 16;
        int64_t chunkZ = (zBlocks[i] >= 0) ? (int64_t)zBlocks[i] / 16
                                           : ((int64_t)zBlocks[i] - 15) / 16;
        if (chunkX < -100000000LL || chunkX > 100000000LL ||
            chunkZ < -100000000LL || chunkZ > 100000000LL)
            return strdup("{\"error\":\"coordinates out of range\"}");

        t->type = types[i];
        t->chunkX = chunkX;
        t->chunkZ = chunkZ;
        t->maxD2 = maxD2;

        int64_t rLoX = (chunkX - tolerance - (sconf.regionSize - 1)) / sconf.regionSize;
        int64_t rHiX = (chunkX + tolerance >= 0) ? (chunkX + tolerance) / sconf.regionSize
                                                 : (chunkX + tolerance - (sconf.regionSize - 1)) / sconf.regionSize;
        int64_t rLoZ = (chunkZ - tolerance - (sconf.regionSize - 1)) / sconf.regionSize;
        int64_t rHiZ = (chunkZ + tolerance >= 0) ? (chunkZ + tolerance) / sconf.regionSize
                                                 : (chunkZ + tolerance - (sconf.regionSize - 1)) / sconf.regionSize;

        int n = 0;
        for (int64_t rx = rLoX; rx <= rHiX && n < CRACK_MAX_REGIONS; rx++) {
            for (int64_t rz = rLoZ; rz <= rHiZ && n < CRACK_MAX_REGIONS; rz++) {
                t->regX[n] = (int)rx;
                t->regZ[n] = (int)rz;
                n++;
            }
        }
        t->numRegions = n;
        if (n == 0)
            return strdup("{\"error\":\"invalid coordinates\"}");
    }

    /* Keep an input-ordered copy for serializing matches back to the caller. */
    CrackTarget orig[CRACK_MAX_STRUCTURES];
    memcpy(orig, targets, (size_t)numTypes * sizeof(CrackTarget));

    /* Strongest filter first. */
    qsort(targets, numTypes, sizeof(CrackTarget), crackTargetCompare);

#if defined(__EMSCRIPTEN__)
    /* Sem -pthread o pthread_create aborta em runtime; o JS paraleliza
       via Web Workers (uma fatia [startSeed,endSeed) por worker). */
    numThreads = 1;
#endif
    if ((endSeed - startSeed) < (uint64_t)numThreads)
        numThreads = 1;

    uint64_t range = endSeed - startSeed;
    uint64_t part = range / (uint64_t)numThreads;

    CrackThread *pids = calloc((size_t)numThreads, sizeof(CrackThread));
    CrackWorker *workers = calloc((size_t)numThreads, sizeof(CrackWorker));
    CrackHit *hits = calloc((size_t)numThreads * maxResults, sizeof(CrackHit));
    volatile int stop = 0;
    double deadline = (timeBudgetSec > 0.0) ? nowms_s() + timeBudgetSec * 1000.0 : 0.0;

    for (int i = 0; i < numThreads; i++) {
        CrackWorker *w = &workers[i];
        w->targets = targets;
        w->nTargets = numTypes;
        w->start = startSeed + i * part;
        w->end = (i == numThreads - 1) ? endSeed : startSeed + (i + 1) * part;
        w->maxResults = maxResults;
        w->deadline = deadline;
        w->stop = &stop;
        w->hits = hits + (size_t)i * maxResults;
        w->hitCount = 0;
        w->worst = INT64_MAX;
        w->worstIdx = -1;
        w->checked = 0;
        if (numThreads == 1)
            crackWorker(w); /* inline: mesma partição, mesmo deadline, mesmo stop */
        else
            pids[i] = crackThreadCreate(crackWorker, w);
    }
    if (numThreads > 1)
        for (int i = 0; i < numThreads; i++)
            crackThreadJoin(pids[i]);

    int total = 0;
    for (int i = 0; i < numThreads; i++)
        total += workers[i].hitCount;

    CrackHit *all = malloc(((size_t)total > 0 ? (size_t)total : 1) * sizeof(CrackHit));
    int c = 0;
    for (int i = 0; i < numThreads; i++)
        for (int j = 0; j < workers[i].hitCount; j++)
            all[c++] = workers[i].hits[j];

    /* Insertion sort ascending by score (c is small). */
    for (int i = 1; i < c; i++) {
        CrackHit key = all[i];
        int j = i - 1;
        while (j >= 0 && all[j].score > key.score) { all[j + 1] = all[j]; j--; }
        all[j + 1] = key;
    }
    if (c > maxResults)
        c = maxResults;

    uint64_t checked = 0;
    for (int i = 0; i < numThreads; i++)
        checked += workers[i].checked;

    /* Serialize: recompute per-structure match chunk for each winner. Only
     * seeds whose matches are biome-viable are kept — a match the RNG would
     * place but the game never generates (wrong biome) is a false positive
     * (e.g. seed 254568). Score is recomputed from the viable matches. */
    Generator g;
    setupGenerator(&g, MC_NEWEST, 0);

    int *m = malloc(((size_t)c > 0 ? (size_t)c : 1) * (size_t)numTypes * 2 * sizeof(int));
    int kept = 0;
    for (int i = 0; i < c; i++) {
        applySeed(&g, DIM_OVERWORLD, all[i].seed);
        int64_t viableScore = 0;
        int valid = 1;
        int *mm = m + (size_t)kept * (size_t)numTypes * 2;
        for (int k = 0; k < numTypes && valid; k++) {
            const CrackTarget *t = &orig[k];
            int bx = 0, bz = 0, bd = INT32_MAX;
            for (int r = 0; r < t->numRegions; r++) {
                Pos pos;
                if (!getBedrockStructurePos(t->type, MC_NEWEST, all[i].seed,
                                            t->regX[r], t->regZ[r], &pos))
                    continue;
                if (!structureIsViable(t->type, &g, pos.x, pos.z))
                    continue;
                int cx = (pos.x - 8) >> 4;
                int cz = (pos.z - 8) >> 4;
                int dx = cx - (int)t->chunkX;
                int dz = cz - (int)t->chunkZ;
                int d2 = dx * dx + dz * dz;
                if (d2 < bd) { bd = d2; bx = cx; bz = cz; }
            }
            if (bd == INT32_MAX || bd > t->maxD2) { valid = 0; break; }
            mm[k * 2] = bx; mm[k * 2 + 1] = bz;
            viableScore += bd;
        }
        if (!valid)
            continue;
        all[kept].seed = all[i].seed;
        all[kept].score = viableScore;
        kept++;
    }
    c = kept;

    /* Re-rank survivors by their recomputed (viable) score. */
    for (int i = 1; i < c; i++) {
        CrackHit key = all[i];
        int kmrow[CRACK_MAX_STRUCTURES * 2];
        const int *src = m + (size_t)i * (size_t)numTypes * 2;
        for (int k = 0; k < numTypes * 2; k++) kmrow[k] = src[k];
        int j = i - 1;
        while (j >= 0 && all[j].score > key.score) {
            all[j + 1] = all[j];
            for (int k = 0; k < numTypes * 2; k++)
                m[(size_t)(j + 1) * numTypes * 2 + k] = m[(size_t)j * numTypes * 2 + k];
            j--;
        }
        all[j + 1] = key;
        for (int k = 0; k < numTypes * 2; k++)
            m[(size_t)(j + 1) * numTypes * 2 + k] = kmrow[k];
    }

    size_t cap = 64 + (size_t)c * ((size_t)numTypes * 32 + 80) + 64;
    char *buf = malloc(cap);
    int off = sprintf(buf, "{\"results\":[");
    for (int i = 0; i < c; i++) {
        off += sprintf(buf + off, "%s{\"seed\":%llu,\"score\":%lld,\"matches\":[",
                       i ? "," : "", (unsigned long long)all[i].seed,
                       (long long)all[i].score);
        const int *mm = m + (size_t)i * (size_t)numTypes * 2;
        for (int k = 0; k < numTypes; k++) {
            off += sprintf(buf + off, "%s[%d,%d]", k ? "," : "",
                           mm[k * 2], mm[k * 2 + 1]);
        }
        off += sprintf(buf + off, "]}");
    }
    off += sprintf(buf + off,
                   "],\"checked\":%llu,\"timed_out\":%s,\"threads\":%d}",
                   (unsigned long long)checked,
                   stop ? "true" : "false", numThreads);

    free(m);
    free(pids); free(workers); free(hits); free(all);
    return buf;
}

/* ============================ SeedCracker 64-bit ============================
 * Thin export shim: the whole pipeline lives in core/crack64.c (it needs the
 * shared structureIsViable defined above). The real symbol is kept non-static
 * so the C test binary (which links this TU) can call it directly too. */

SEEDFINDER_API char *seedfinder_crack64_shim(
    const int *mtTypes, const double *mtX, const double *mtZ, int nMt,
    const int *jTypes, const double *jX, const double *jZ, int nJava,
    int tolerance, uint64_t startSeed, uint64_t endSeed,
    int maxResults, double budgetSec, int numThreads)
{
    return seedfinder_crack64(mtTypes, mtX, mtZ, nMt, jTypes, jX, jZ, nJava,
                              tolerance, startSeed, endSeed, maxResults,
                              budgetSec, numThreads);
}