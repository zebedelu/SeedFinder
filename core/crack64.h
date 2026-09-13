#ifndef SEEDFINDER_CRACK64_H_
#define SEEDFINDER_CRACK64_H_
#include <stdint.h>
#include "ChunkBiomesGUI/cubiomes/finders.h"

#define C64_M48   0xFFFFFFFFFFFFULL
#define C64_MUL   0x5DEECE66DULL
#define C64_ADD   0xBULL
#define C64_MAX   24

typedef struct { int regionSize, chunkRange; uint64_t salt; } JavaCfg;
int  javaCfgFor(int structureType, JavaCfg *out);
void javaChunk(const JavaCfg *c, uint64_t s48, int regX, int regZ,
               long long *chunkX, long long *chunkZ);

typedef struct {
    int       nJava;                                  // 1..C64_MAX
    JavaCfg   cfg[C64_MAX];
    long long chunkX[C64_MAX], chunkZ[C64_MAX];
    int       maxD2;                                  // tolerance^2 em chunks
    int       nRegions[C64_MAX];
    int       regX[C64_MAX][64], regZ[C64_MAX][64];   // celulas candidatas (mesma formula do crack 32)
} Anchor48;

typedef struct { uint64_t *v; int n, cap; } U64Vec;

// Varre s48 in [s48Start, s48End) e coleta os que satisfazem TODAS as ancoras.
// deadlineMs > 0 interrompe e devolve 1 (*timedOut = 1).
int  anchor48Build(Anchor48 *a, const int *types, const double *xb, const double *zb,
                   int n, int tolerance);             // 0 ok / -1 erro
void sweep48(const Anchor48 *a, uint64_t s48Start, uint64_t s48End,
             double deadlineMs, U64Vec *out, int *timedOut);

typedef struct { uint64_t checked; int timedOut, threads; } Sweep48Result;

// Igual a sweep48 mas particionando [start,end) em numThreads workers, cada
// um com vetor proprio (merge pos-join). budgetSec <= 0 => sem deadline.
Sweep48Result sweep48MT(const Anchor48 *a, uint64_t start, uint64_t end,
                        double budgetSec, int numThreads, U64Vec *out);
#endif
