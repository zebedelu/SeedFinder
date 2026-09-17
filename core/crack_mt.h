#ifndef SEEDFINDER_CRACK_MT_H_
#define SEEDFINDER_CRACK_MT_H_
#include <stdint.h>

#if (defined(__x86_64__) || defined(__i386__)) && defined(__GNUC__)
#include <immintrin.h>
#define SEEDFINDER_SIMD 1
#else
#define SEEDFINDER_SIMD 0
#endif

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

extern int crack_g_avx2;
void      crackDetectAvx2(void);            /* preenche crack_g_avx2 */

#if SEEDFINDER_SIMD
void crack_mt4_block(const uint32_t seedlo[4], uint32_t cbase, uint32_t out[4][8]);
int  crackScore4(const CrackTarget *targets, int nTargets,
                 const uint64_t seeds[4], int64_t score[4]);
#endif

typedef struct { uint64_t checked; int timedOut, threads; } MtSweepResult;
MtSweepResult sweepMtSurvivors(const CrackTarget *targets, int nTargets,
                               uint64_t start, uint64_t end,
                               double budgetSec, int numThreads, U64Vec *out);
#endif