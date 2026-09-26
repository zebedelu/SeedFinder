#ifndef SEEDFINDER_CRACK_MT_H_
#define SEEDFINDER_CRACK_MT_H_
#include <stdint.h>
#include "crack_simd.h"

#define CRACK_MAX_STRUCTURES 24
#define CRACK_MAX_REGIONS 64
#define CRACK_SEED_SPACE (0x100000000ULL)

enum { PLACE_FEATURE = 0, PLACE_LARGE = 1, PLACE_OTHER = 2 };

/* Per-input-structure: everything that does NOT depend on the seed.
 * Candidate region cells whose generated structure could land within
 * `tolerance` chunks of the requested anchor chunk (chunk = floor(x/16)). */
typedef struct {
    int       type;
    long long chunkX, chunkZ;
    int       maxD2;
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

void      crackInsert(CrackWorker *w, uint64_t seed, int64_t score);
void     *crackWorker(void *arg);

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

/* Extrai a validação + construção de CrackTarget que hoje vive inline em
 * seedfinder_crack. out vem em ORDEM DE ENTRADA (o qsort por numRegions fica
 * com o chamador: a serialização de matches depende da ordem original).
 * Retorna 0 ok; *err = malloc'd JSON {"error":...} se falhar. */
int  crackTargetsBuild(const int *types, int numTypes,
                       const double *xBlocks, const double *zBlocks,
                       int tolerance, CrackTarget out[], int *nOut, char **err);

/* Extrai o núcleo worker/top-N (crackWorker + crackInsert + merge) do
 * seedfinder_crack. *out vem ordenado por score asc, capado em maxResults.
 * Clamps (maxResults/numThreads/range) ficam no chamador; deadlineMs é ms
 * absoluto (0.0 = sem deadline). 0 ok, -1 OOM (*out fica NULL). */
int  crackSweepRanked(const CrackTarget *targets, int nTargets,
                      uint64_t start, uint64_t end, int maxResults,
                      double deadlineMs, int numThreads,
                      CrackHit **out, int *nOut, uint64_t *checked, int *timedOut);

/* Extrai o loop de validação final (best viable placement + matches) do
 * seedfinder_crack. Recebe targets na ORDEM DE ENTRADA; escreve
 * matchesOut = 2*nTargets chunks e o score viável. 1 = seed válida. */
int  crackValidateSeed(const CrackTarget *origTargets, int nTargets,
                       uint64_t seed, int *matchesOut, int64_t *scoreOut);
#endif