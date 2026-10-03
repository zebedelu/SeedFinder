#ifndef SEEDFINDER_CRACK_JAVA_H_
#define SEEDFINDER_CRACK_JAVA_H_
#include <stdint.h>

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

#include "ChunkBiomesGUI/cubiomes/finders.h"

#define JAVA_MAX_STRUCTURES 24
#define JAVA_MAX_REGIONS    512   /* (2*8+1)^2 = 289 < 512 (Treasure regionSize 1) */
#define JAVA_MASK48         0xFFFFFFFFFFFFULL

typedef struct {
    int            type;
    StructureConfig cfg;       /* getStructureConfig(type, MC_NEWEST, &cfg) */
    long long      chunkX, chunkZ;  /* input chunk = floorDiv(block,16) */
    int            maxD2;      /* tolerance^2 */
    int            tolerance;
    int            numRegions;
    int            regX[JAVA_MAX_REGIONS];
    int            regZ[JAVA_MAX_REGIONS];
} JavaTarget;

/* 0 ok; *err malloc'd {"error":...} on failure (free by caller). */
int javaTargetsBuild(const int *types, int numTypes, const double *xb, const double *zb,
                     int tolerance, JavaTarget out[], int *nOut, char **err);

/* 1 = match (placement-only); matchesOut = 2*n chunks in input order; score = sum d2. */
int javaCheckSeed(const JavaTarget *targets, int n, uint64_t s48,
                  int *matchesOut, int64_t *scoreOut);

SEEDFINDER_API char *seedfinder_crack_java(
    const int *types, int numTypes,
    const double *xBlocks, const double *zBlocks,
    int tolerance,
    uint64_t startSeed, uint64_t endSeed,
    int maxResults, double timeBudgetSec, int numThreads);
#endif