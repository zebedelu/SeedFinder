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
                if (!isViableBedrockStructurePos(structType, &g, pos.x, pos.z, 0))
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
