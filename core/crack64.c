// core/crack64.c (Task 3 — só o fast-path)
#include "crack64.h"

int javaCfgFor(int structureType, JavaCfg *out) {
    StructureConfig sconf;
    if (structureType != Trail_Ruins && structureType != Trial_Chambers) return 0;
    // Config JAVA (placement idêntico ao Java Edition — ver Bfinders.c):
    if (!getStructureConfig(structureType, MC_NEWEST, &sconf)) return 0;
    out->regionSize = sconf.regionSize;
    out->chunkRange = sconf.chunkRange;
    out->salt       = sconf.salt;
    return 1;
}

void javaChunk(const JavaCfg *c, uint64_t s48, int regX, int regZ,
               long long *chunkX, long long *chunkZ) {
    // setLargeFeatureWithSalt: seed = (regX*C1 + regZ*C2 + worldSeed + salt) ^ MUL, & M48
    uint64_t st = ((uint64_t)regX * 341873128712ULL
                 + (uint64_t)regZ * 132897987541ULL
                 + s48 + c->salt) ^ C64_MUL;
    st &= C64_M48;
    // nextInt(chunkRange): bits = state >>> 17 (31 bits); bound pequeno => sem retry.
    st = (st * C64_MUL + C64_ADD) & C64_M48;
    int ox = (int)((st >> 17) % (uint32_t)c->chunkRange);
    st = (st * C64_MUL + C64_ADD) & C64_M48;
    int oz = (int)((st >> 17) % (uint32_t)c->chunkRange);
    // getStructurePos devolve o bloco NO CANTO do chunk: (reg*RS + o) << 4
    // (finders.h:790, sem o +8 de centroide). O crack engine extrai o chunk
    // anchor como (blockX - 8) >> 4 (seedfinder_wrapper.c:235). Reproduzir a
    // mesma extracao para que javaChunk seja comparavel 1:1 com o oraculo.
    *chunkX = ((((long long)regX * c->regionSize + ox) << 4) - 8) >> 4;
    *chunkZ = ((((long long)regZ * c->regionSize + oz) << 4) - 8) >> 4;
}
