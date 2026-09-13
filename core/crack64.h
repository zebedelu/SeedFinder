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
#endif
