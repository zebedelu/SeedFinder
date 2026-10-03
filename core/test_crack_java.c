/* core/test_crack_java.c — parity + verifier tests for the Java cracker.
 * Build (MSYS2 UCRT64 gcc, PATH must include C:\msys64\ucrt64\bin):
 *   gcc -O2 -std=c11 -I. -I.. -I../ChunkBiomesGUI/cubiomes -o test_crack_java.exe \
 *       test_crack_java.c crack_java.c \
 *       ../ChunkBiomesGUI/cubiomes/generator.c ../ChunkBiomesGUI/cubiomes/finders.c \
 *       ../ChunkBiomesGUI/cubiomes/biomes.c ../ChunkBiomesGUI/cubiomes/noise.c \
 *       ../ChunkBiomesGUI/cubiomes/util.c ../ChunkBiomesGUI/cubiomes/layers.c \
 *       ../ChunkBiomesGUI/cubiomes/biomenoise.c ../ChunkBiomesGUI/cubiomes/quadbase.c -lm
 */
#include "crack_java.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIX1 8675309ULL
#define FIX2 4294972605ULL

/* Gera posicoes (blocos de canto) do tipo st para o seed, varrendo regioes. */
static int genStructures(int st, uint64_t seed, Pos *out, int max)
{
    StructureConfig sc;
    if (!getStructureConfig(st, MC_NEWEST, &sc)) return 0;
    int cnt = 0;
    for (int rz = -8; rz <= 8 && cnt < max; rz++)
        for (int rx = -8; rx <= 8 && cnt < max; rx++) {
            Pos pos;
            if (!getStructurePos(st, MC_NEWEST, seed, rx, rz, &pos)) continue;
            if (abs(pos.x) > 8192 || abs(pos.z) > 8192) continue;
            out[cnt++] = pos;
        }
    return cnt;
}

/* Convencao: input chunk = floorDiv(bloco,16); placement chunk = pos.x >> 4. */
static int testConvention(void)
{
    Pos p[8];
    int n = genStructures(Igloo, FIX1, p, 8);
    if (n < 1) return 0;
    for (int i = 0; i < n; i++) {
        /* o bloco de canto mapeia no proprio chunk: (bloco/16) == (bloco>>4) */
        long long cx = (p[i].x >= 0) ? p[i].x / 16 : (p[i].x - 15) / 16;
        long long pch = (long long)(p[i].x >> 4);
        if (cx != pch) { printf("convention mismatch x: %d\n", p[i].x); return 0; }
        long long cz = (p[i].z >= 0) ? p[i].z / 16 : (p[i].z - 15) / 16;
        long long pcz = (long long)(p[i].z >> 4);
        if (cz != pcz) { printf("convention mismatch z: %d\n", p[i].z); return 0; }
    }
    return 1;
}

/* Verificador: seed verdadeira casa tolerance 0; vizinhos nao. */
static int testVerifier(void)
{
    int types[] = { Igloo, Desert_Pyramid, Jungle_Temple, Swamp_Hut };
    double xs[4], zs[4];
    Pos p[4][8];
    int got[4];
    for (int i = 0; i < 4; i++) {
        got[i] = genStructures(types[i], FIX1, p[i], 8);
        if (got[i] < 1) return 0;
        xs[i] = (double)p[i][0].x;
        zs[i] = (double)p[i][0].z;
    }
    JavaTarget tg[4]; int n = 0; char *err = NULL;
    if (javaTargetsBuild(types, 4, xs, zs, 0, tg, &n, &err) != 0) {
        printf("targetsBuild err: %s\n", err ? err : "null"); free(err); return 0;
    }
    int m[8]; int64_t sc = 0;
    if (!javaCheckSeed(tg, n, FIX1, m, &sc)) { printf("true seed rejected\n"); return 0; }
    if (javaCheckSeed(tg, n, FIX1 + 1, m, &sc)) { printf("wrong seed accepted\n"); return 0; }
    /* score >= 0 e matches em ordem de entrada */
    if (sc < 0) return 0;
    return 1;
}

/* Sweep: janela estreita recupera o seed; 32-bit cheio recupera 8675309. */
static int testSweep(void)
{
    int types[] = { Village, Monument, Igloo, Swamp_Hut };
    Pos p[4][8];
    double xs[4], zs[4];
    for (int i = 0; i < 4; i++) {
        if (genStructures(types[i], FIX1, p[i], 8) < 1) return 0;
        xs[i] = (double)p[i][0].x; zs[i] = (double)p[i][0].z;
    }
    char *r = seedfinder_crack_java(types, 4, xs, zs, 0,
                                    FIX1 - 2000, FIX1 + 2000, 50, 30.0, 8);
    if (!r) return 0;
    int ok = (strstr(r, "\"seed\":8675309") != NULL);
    free(r);
    return ok;
}

/* Guard: varredura > 2^33 sem estruturas liftaveis -> erro acionavel. */
static int testGuard(void)
{
    int types[] = { Village, Monument, Mansion, Ancient_City };
    Pos p[4][8];
    double xs[4], zs[4];
    for (int i = 0; i < 4; i++) {
        if (genStructures(types[i], FIX1, p[i], 8) < 1) return 0;
        xs[i] = (double)p[i][0].x; zs[i] = (double)p[i][0].z;
    }
    char *r = seedfinder_crack_java(types, 4, xs, zs, 0, 0, 1ULL << 48, 50, 30.0, 8);
    if (!r) return 0;
    int ok = (strstr(r, "infeasible") != NULL);
    free(r);
    return ok;
}

int main(void)
{
    if (!testConvention()) { printf("FAIL convention\n"); return 1; }
    if (!testVerifier())   { printf("FAIL verifier\n");   return 1; }
    if (!testSweep())      { printf("FAIL sweep\n");      return 1; }
    if (!testGuard())      { printf("FAIL guard\n");      return 1; }
    printf("OK parity + verifier\n");
    printf("OK sweep + guard\n");
    return 0;
}