/* core/test_java_tol.c — testes do LIFTING tolerante (span > 2^33).
 * Build (MSYS2 UCRT64 gcc, PATH deve incluir C:\msys64\ucrt64\bin):
 *   gcc -O2 -std=c11 -I. -I.. -I../ChunkBiomesGUI/cubiomes -o test_java_tol.exe \
 *       test_java_tol.c crack_java.c \
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

#define FIX1 8675309ULL   /* < 2^32: bits 32-47 = 0 -> Stage A rep-hi=0 e' exato */

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

/* Especifica as coordenadas aproximadas: adiciona um desvio em blocos dentro
 * de tolerance (simula jogador lendo Chunkbase de forma aproximada). */
static void jitterPos(double *xs, double *zs, int i, int tol)
{
    int d = (tol > 0) ? (i % tol) : 0;        /* 0..tol-1, deterministico */
    xs[i] += (double)(d * 16);
    zs[i] += (double)(d * 16);
}

/* FULL-RANGE com estruturas grandes + tolerance 1: o caminho SWEEP seria
 * inviavel (span 2^48) -> hoje retorna o guard "infeasible"; o novo LIFTING
 * tolerante deve recuperar a seed. Roda ~2-4 min (Stage A = 2^32) e devolve
 * partial (budget 120 s) com o seed presente (Stage B intercalado acha
 * FIX1 nos primeiros segundos). */
static int testTolFullRecovery(void)
{
    int types[] = { Village, Monument, Ancient_City, Mansion, Igloo };
    Pos p[5][8];
    double xs[5], zs[5];
    for (int i = 0; i < 5; i++) {
        if (genStructures(types[i], FIX1, p[i], 8) < 1) return 0;
        xs[i] = (double)p[i][0].x; zs[i] = (double)p[i][0].z;
        jitterPos(xs, zs, i, 2);              /* tolerance 1, desvios 0..16 bl */
    }
    char *r = seedfinder_crack_java(types, 5, xs, zs, 1,
                                    0, 1ULL << 48, 2000, 120.0, 8);
    if (!r) return 0;
    int ok = (strstr(r, "\"seed\":8675309") != NULL);
    if (!ok) printf("  full-range tol1: %s\n", r);
    free(r);
    return ok;
}

/* GUARD amostral: tolerance 8, 3 estruturas grandes, full-range -> pre-check
 * 2^22 prediz ~1e8 sobreviventes -> erro "infeasible" em ~1 s (NAO hang). */
static int testTolInfeasibleGuard(void)
{
    int types[] = { Village, Mansion, Monument };
    Pos p[3][8];
    double xs[3], zs[3];
    for (int i = 0; i < 3; i++) {
        if (genStructures(types[i], FIX1, p[i], 8) < 1) return 0;
        xs[i] = (double)p[i][0].x; zs[i] = (double)p[i][0].z;
    }
    char *r = seedfinder_crack_java(types, 3, xs, zs, 8,
                                    0, 1ULL << 48, 2000, 30.0, 8);
    if (!r) return 0;
    int ok = (strstr(r, "infeasible") != NULL);
    if (!ok) printf("  guard tol8: %s\n", r);
    free(r);
    return ok;
}

int main(void)
{
    if (!testTolFullRecovery())  { printf("FAIL tolerant full-range recovery\n");  return 1; }
    if (!testTolInfeasibleGuard()) { printf("FAIL tolerant infeasible guard\n");   return 1; }
    printf("OK tolerant lifting (full-range tol1 recovery + guard)\n");
    return 0;
}