#include "ChunkBiomesGUI/Bfinders.h"
#include "ChunkBiomesGUI/cubiomes/generator.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include "ChunkBiomesGUI/cubiomes/util.h"

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

    uint64_t seed32 = seed & 0xFFFFFFFFULL; /* Bedrock world seeds are 32-bit */

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
                if (!getBedrockStructurePos(structType, MC_NEWEST, seed32, regX, regZ, &pos))
                    continue;

                /* Biome viability check */
                if (!isViableBedrockStructurePos(structType, &g, pos.x, pos.z, 0))
                    continue;

                /* Bedrock checks the biome at the structure's OWN position cell;
                   the upstream Java-style gate samples an offset door corner and
                   passes positions sitting on swamp/river/etc. */
                if (structType == Outpost) {
                    int cellX = (pos.x >> 4) * 4 + 2;
                    int cellZ = (pos.z >> 4) * 4 + 2;
                    int bio = getBiomeAt(&g, 0, cellX, 319 >> 2, cellZ);
                    if (bio < 0 || !isViableFeatureBiome(MC_NEWEST, Outpost, bio))
                        continue;
                }

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

/* Portable thread handle: Win32 threads on Windows (keeps the DLL
 * self-contained on MinGW), pthreads elsewhere. */
#ifdef _WIN32
#include <windows.h>
typedef HANDLE CrackThread;
static CrackThread crackThreadCreate(void *(*fn)(void *), void *arg)
{
    return CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fn, arg, 0, NULL);
}
static void crackThreadJoin(CrackThread t)
{
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
#else
#include <pthread.h>
typedef pthread_t CrackThread;
static CrackThread crackThreadCreate(void *(*fn)(void *), void *arg)
{
    pthread_t t;
    pthread_create(&t, NULL, fn, arg);
    return t;
}
static void crackThreadJoin(CrackThread t) { pthread_join(t, NULL); }
#endif

/* Monotonic clock in milliseconds (cubiomes has no portable time helper). */
#ifdef _WIN32
static inline double nowms_s(void) { return (double)GetTickCount64(); }
#else
#include <time.h>
static inline double nowms_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
#endif

#define CRACK_MAX_STRUCTURES 24
#define CRACK_MAX_REGIONS 64
#define CRACK_SEED_SPACE (0x100000000ULL)

/* Per-input-structure: everything that does NOT depend on the seed.
 * Candidate region cells whose generated structure could land within
 * `tolerance` chunks of the requested anchor chunk (chunk = floor(x/16)). */
typedef struct {
    int       type;
    long long chunkX, chunkZ;
    int       maxD2;      /* squared tolerance in chunks */
    int       numRegions;
    int       regX[CRACK_MAX_REGIONS];
    int       regZ[CRACK_MAX_REGIONS];
} CrackTarget;

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

/* Score a candidate seed: sum over structures of the best squared-chunk
 * deviation across its candidate regions. Returns < 0 when any structure
 * has no region match within tolerance. */
static int64_t crackScore(const CrackTarget *targets, int nTargets, uint64_t seed)
{
    int64_t total = 0;
    for (int i = 0; i < nTargets; i++) {
        const CrackTarget *t = &targets[i];
        int bestD = INT32_MAX;
        for (int k = 0; k < t->numRegions; k++) {
            Pos pos;
            if (!getBedrockStructurePos(t->type, MC_NEWEST, seed,
                                        t->regX[k], t->regZ[k], &pos))
                continue;
            int cx = (pos.x - 8) >> 4;   /* anchor chunk */
            int cz = (pos.z - 8) >> 4;
            int dx = cx - (int)t->chunkX;
            int dz = cz - (int)t->chunkZ;
            int d2 = dx * dx + dz * dz;
            if (d2 < bestD) bestD = d2;
        }
        if (bestD == INT32_MAX || bestD > t->maxD2)
            return -1;
        total += bestD;
    }
    return total;
}

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
    for (uint64_t seed = w->start; seed < w->end; seed++) {
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

static int crackTargetCompare(const void *a, const void *b)
{
    const CrackTarget *ta = (const CrackTarget *)a;
    const CrackTarget *tb = (const CrackTarget *)b;
    /* Fewest candidate regions first -> strongest filter earlier. */
    if (ta->numRegions != tb->numRegions) return ta->numRegions - tb->numRegions;
    return 0;
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

    /* Strongest filter first. */
    qsort(targets, numTypes, sizeof(CrackTarget), crackTargetCompare);

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
        pids[i] = crackThreadCreate(crackWorker, w);
    }
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

    /* Serialize: recompute per-structure match chunk for each winner. */
    size_t cap = 64 + (size_t)c * 128 + 64;
    char *buf = malloc(cap);
    int off = sprintf(buf, "{\"results\":[");
    for (int i = 0; i < c; i++) {
        off += sprintf(buf + off, "%s{\"seed\":%llu,\"score\":%lld,\"matches\":[",
                       i ? "," : "", (unsigned long long)all[i].seed,
                       (long long)all[i].score);
        for (int k = 0; k < numTypes; k++) {
            const CrackTarget *t = &targets[k];
            int bx = 0, bz = 0, bd = INT32_MAX;
            for (int r = 0; r < t->numRegions; r++) {
                Pos pos;
                if (!getBedrockStructurePos(t->type, MC_NEWEST, all[i].seed,
                                            t->regX[r], t->regZ[r], &pos))
                    continue;
                int cx = (pos.x - 8) >> 4;
                int cz = (pos.z - 8) >> 4;
                int dx = cx - (int)t->chunkX;
                int dz = cz - (int)t->chunkZ;
                int d2 = dx * dx + dz * dz;
                if (d2 < bd) { bd = d2; bx = cx; bz = cz; }
            }
            off += sprintf(buf + off, "%s[%d,%d]", k ? "," : "", bx, bz);
        }
        off += sprintf(buf + off, "]}");
    }
    off += sprintf(buf + off,
                   "],\"checked\":%llu,\"timed_out\":%s,\"threads\":%d}",
                   (unsigned long long)checked,
                   stop ? "true" : "false", numThreads);

    free(pids); free(workers); free(hits); free(all);
    return buf;
}
