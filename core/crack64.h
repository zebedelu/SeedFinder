#ifndef SEEDFINDER_CRACK64_H_
#define SEEDFINDER_CRACK64_H_
#include <stdint.h>
#include "crack_mt.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include "ChunkBiomesGUI/cubiomes/generator.h"

#define C64_M48   0xFFFFFFFFFFFFULL
#define C64_MUL   0x5DEECE66DULL
#define C64_ADD   0xBULL
#define C64_MAX   24

// Definida em core/seedfinder_wrapper.c (gate de bioma compartilhado por
// /scan, seedfinder_crack e o lift de seedfinder_crack64).
int structureIsViable(int structureType, Generator *g, int x, int z);

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

// Varre s48 in [s48Start, s48End) e coleta os que satisfazem TODAS as ancoras.
// deadlineMs > 0 interrompe e devolve 1 (*timedOut = 1).
// out: sweep48 RESETA o vetor (out->n = 0); sweep48MT faz APPEND sem resetar
// (merge dos workers) — trocar as duas silenciosmente descartaria sobreviventes.
int  anchor48Build(Anchor48 *a, const int *types, const double *xb, const double *zb,
                   int n, int tolerance);             // 0 ok / -1 erro
void sweep48(const Anchor48 *a, uint64_t s48Start, uint64_t s48End,
             double deadlineMs, U64Vec *out, int *timedOut);

typedef struct { uint64_t checked; int timedOut, threads; } Sweep48Result;

// Igual a sweep48 mas particionando [start,end) em numThreads workers, cada
// um com vetor proprio (merge pos-join). budgetSec <= 0 => sem deadline.
Sweep48Result sweep48MT(const Anchor48 *a, uint64_t start, uint64_t end,
                        double budgetSec, int numThreads, U64Vec *out);

// Stage B do pipeline full-range: para cada lo32 sobrevivente, testa os 2^16
// hi16 e aceita s48 = (hi16<<32)|lo32 quando TODAS as ancoras Java casam.
// Particiona a lista de sobreviventes em numThreads workers (WASM: 1 inline).
// deadline = ms absoluto (0.0 = sem deadline); *timedOut = 1 se estourar.
void liftJavaHi(const Anchor48 *anc, const U64Vec *lo32s, double deadline,
                int numThreads, U64Vec *out, int *timedOut);

// Crack de seed completa Bedrock (0 <= seed < 2^63): varre o residual de 48
// bits com as ancoras Java-style (Trail Ruins/Trial Chambers), cruza com o
// placement MT (mod 2^32) das estruturas Bedrock e levanta os 16 bits altos
// pelo filtro de bioma (structureIsViable). JSON igual ao do seedfinder_crack
// de 32 bits + "seed_str" por item + "bits":64 no envelope. O retorno e'
// malloc'd; liberar com seedfinder_free_result.
char *seedfinder_crack64(const int *mtTypes, const double *mtX, const double *mtZ, int nMt,
                         const int *jTypes, const double *jX, const double *jZ, int nJava,
                         int tolerance, uint64_t startSeed, uint64_t endSeed,
                         int maxResults, double budgetSec, int numThreads);
#endif
