#ifndef SEEDFINDER_CRACK_MT_H_
#define SEEDFINDER_CRACK_MT_H_
#include <stdint.h>
#include "crack_simd.h"

#define CRACK_MAX_STRUCTURES 24
#define CRACK_MAX_REGIONS 64

enum { PLACE_FEATURE = 0, PLACE_LARGE = 1, PLACE_OTHER = 2 };

typedef struct {
    int       type;
    long long chunkX, chunkZ;
    int       maxD2;
    int       numRegions;
    int       regX[CRACK_MAX_REGIONS];
    int       regZ[CRACK_MAX_REGIONS];
} CrackTarget;

typedef struct { uint64_t *v; int n, cap; } U64Vec;
int  u64Push(U64Vec *v, uint64_t x);        /* retorna 0 ok, 1 OOM (n intacto) */

int       crackPlacement(int structureType);       /* PLACE_FEATURE/LARGE/OTHER */
int64_t   crackScore(const CrackTarget *t, int n, uint64_t seed);
int       crackTargetCompare(const void *a, const void *b);

extern int crack_g_simd;
void      crackSimdDetect(void);            /* preenche crack_g_simd */

#if CRACK_SIMD
int crackScoreSimd(const CrackTarget *targets, int nTargets,
                   const uint64_t seeds[CRACK_WIDTH], int64_t score[CRACK_WIDTH]);
#endif

typedef struct { uint64_t checked; int timedOut, threads; } MtSweepResult;
MtSweepResult sweepMtSurvivors(const CrackTarget *targets, int nTargets,
                               uint64_t start, uint64_t end,
                               double budgetSec, int numThreads, U64Vec *out);
#endif