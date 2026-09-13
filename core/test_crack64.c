// core/test_crack64.c — paridade do fast-path vs cubiomes getStructurePos.
//
// Build & run (Windows, MSYS2 UCRT64; a partir de build_server/):
//   & "C:\Program Files\Git\bin\bash.exe" -lc "cd build_server && /c/msys64/ucrt64/bin/gcc.exe -std=c11 -I.. -I../ChunkBiomesGUI -I../ChunkBiomesGUI/cubiomes ../core/test_crack64.c ../core/crack64.c ../ChunkBiomesGUI/Bfinders.c ../ChunkBiomesGUI/cubiomes/biomes.c ../ChunkBiomesGUI/cubiomes/layers.c ../ChunkBiomesGUI/cubiomes/generator.c ../ChunkBiomesGUI/cubiomes/finders.c ../ChunkBiomesGUI/cubiomes/util.c ../ChunkBiomesGUI/cubiomes/noise.c ../ChunkBiomesGUI/cubiomes/biomenoise.c ../ChunkBiomesGUI/cubiomes/quadbase.c -lm -o test_crack64 && ./test_crack64"
// Esperado: imprime PARITY_OK e sai 0.
#include "crack64.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static int testParity(void) {
    // Paridade: javaChunk(cfg, seed & M48) deve dar o MESMO chunk que
    // getStructurePos retorna para a seed completa.
    const int TYPES[] = {23, 24};
    for (unsigned ti = 0; ti < 2; ti++) {
        JavaCfg c;
        if (!javaCfgFor(TYPES[ti], &c)) { fprintf(stderr, "FAIL cfg %d\n", TYPES[ti]); return 1; }
        for (uint64_t s = 1; s < 200000; s++) {
            uint64_t seed = s * 6364136223846793005ULL + 1442695040888963407ULL;
            uint64_t s48 = seed & 0xFFFFFFFFFFFFULL;
            for (int rx = -1; rx <= 1; rx++) for (int rz = -1; rz <= 1; rz++) {
                Pos p;
                if (!getStructurePos(TYPES[ti], MC_NEWEST, seed, rx, rz, &p)) continue;
                long long cx, cz;
                javaChunk(&c, s48, rx, rz, &cx, &cz);
                if ((long long)((p.x - 8) >> 4) != cx || (long long)((p.z - 8) >> 4) != cz) {
                    fprintf(stderr, "PARITY FAIL type=%d seed=%llu reg=(%d,%d) "
                            "cubiomes=(%d,%d) fast=(%lld,%lld)\n", TYPES[ti],
                            (unsigned long long)seed, rx, rz, p.x, p.z, cx, cz);
                    return 1;
                }
            }
        }
    }
    printf("PARITY_OK\n");
    return 0;
}

// --- sweep48 test ---
static Anchor48 mkAnchors(uint64_t fullSeed, long long tol) {
    // Gera âncoras autoconsistentes: posiciona 3 Trial Chambers via cubiomes
    // a partir da seed fixture e usa as próprias coords como alvo.
    Anchor48 a; a.nJava = 0; a.maxD2 = (int)(tol * tol);
    const int T = Trial_Chambers;
    const long long CHUNKS[][2] = {{0,0},{3,-2},{-4,1}};
    for (int i = 0; i < 3; i++) {
        int rx = (int)CHUNKS[i][0], rz = (int)CHUNKS[i][1];
        Pos p;
        if (!getStructurePos(T, MC_NEWEST, fullSeed, rx, rz, &p)) continue;
        int j = a.nJava++;
        javaCfgFor(T, &a.cfg[j]);
        long long cx = (p.x - 8) >> 4, cz = (p.z - 8) >> 4;
        a.chunkX[j] = cx; a.chunkZ[j] = cz;
        // regiões candidatas exatas: a região que gerou este chunk
        a.nRegions[j] = 1; a.regX[j][0] = rx; a.regZ[j][0] = rz;
    }
    return a;
}

static int testSweep(void) {
    uint64_t full = 7777777777777777777ULL;
    Anchor48 a = mkAnchors(full, 0);
    U64Vec out = {0}; int timedOut = 0;
    sweep48(&a, (full & C64_M48) - 1000, (full & C64_M48) + 1000, 0.0, &out, &timedOut);
    int found = 0;
    for (int i = 0; i < out.n; i++) if (out.v[i] == (full & C64_M48)) found = 1;
    assert(found && "sweep48 nao recuperou a seed fixture");
    assert(!timedOut);
    free(out.v);
    printf("SWEEP_OK\n");
    return 0;
}

int main(void) {
    if (testParity()) return 1;
    return testSweep();
}
