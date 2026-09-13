// core/test_invariants.c — prova as invariantes do motor usadas pelo crack 64-bit.
//
// Build & run (Windows, MSYS2 UCRT64; a partir de build_server/):
//   & "C:\Program Files\Git\bin\bash.exe" -lc "cd build_server && /c/msys64/ucrt64/bin/gcc.exe -std=c11 -I.. -I../ChunkBiomesGUI -I../ChunkBiomesGUI/cubiomes ../core/test_invariants.c ../ChunkBiomesGUI/Bfinders.c ../ChunkBiomesGUI/cubiomes/biomes.c ../ChunkBiomesGUI/cubiomes/layers.c ../ChunkBiomesGUI/cubiomes/generator.c ../ChunkBiomesGUI/cubiomes/finders.c ../ChunkBiomesGUI/cubiomes/util.c ../ChunkBiomesGUI/cubiomes/noise.c ../ChunkBiomesGUI/cubiomes/biomenoise.c ../ChunkBiomesGUI/cubiomes/quadbase.c -lm -o test_invariants && ./test_invariants"
// Esperado: imprime INVARIANTS_OK e sai 0.
#include "ChunkBiomesGUI/Bfinders.h"
#include "ChunkBiomesGUI/cubiomes/generator.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const uint64_t LOW = 8675309ULL;          // fixture do repo
    const uint64_t HIGH = LOW + (1ULL << 32); // difere só nos bits altos

    // Invariante 1: placement MT-based é função de seed mod 2^32.
    // getBedrockStructurePos recebe uint64_t mas sementeia o MT com
    // (mix_seed(...) & 0xFFFFFFFF); aritmética de mix_seed é congruente
    // mod 2^32, logo HIGH e LOW devem produzir placement idêntico.
    for (int t = 1; t <= 14; t++) {
        if (t == 15 || t == 16 || t == 17) continue;
        StructureConfig sconf;
        if (!getBedrockStructureConfig(t, MC_NEWEST, &sconf)) continue;
        for (int rx = -2; rx <= 2; rx++) for (int rz = -2; rz <= 2; rz++) {
            Pos p1, p2;
            int v1 = getBedrockStructurePos(t, MC_NEWEST, LOW,  rx, rz, &p1);
            int v2 = getBedrockStructurePos(t, MC_NEWEST, HIGH, rx, rz, &p2);
            if (v1 != v2 || (v1 && (p1.x != p2.x || p1.z != p2.z))) {
                fprintf(stderr, "FAIL: type %d placement difere entre LOW/HIGH\n", t);
                return 1;
            }
        }
    }

    // Invariante 2: Java-style (23/24) depende de seed mod 2^48 — checar via cubiomes.
    // getStructurePos com seeds congruentes mod 2^48 deve coincidir
    // (getFeatureChunkInRegion mascara com (1<<48)-1).
    {
        uint64_t base = 7777777777777777777ULL; // < 2^63, bits altos > 2^48
        uint64_t twin = (base & 0xFFFFFFFFFFFFULL); // mesmo mod 2^48, bits altos diferentes
        for (int rx = -2; rx <= 2; rx++) for (int rz = -2; rz <= 2; rz++) {
            Pos p1, p2;
            int v1 = getStructurePos(Trial_Chambers, MC_NEWEST, base, rx, rz, &p1);
            int v2 = getStructurePos(Trial_Chambers, MC_NEWEST, twin,  rx, rz, &p2);
            if (v1 != v2 || (v1 && (p1.x != p2.x || p1.z != p2.z))) {
                fprintf(stderr, "FAIL: Trial Chambers nao depende so de mod 2^48\n");
                return 1;
            }
        }
    }
    printf("INVARIANTS_OK\n");
    return 0;
}
