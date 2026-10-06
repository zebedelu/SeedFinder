/* core/test_java_tol.c — testes do LIFTING tolerante (span > 2^33).
 * Build (MSYS2 UCRT64 gcc, PATH deve incluir C:\msys64\ucrt64\bin):
 *   gcc -O2 -std=c11 -I. -I.. -I../ChunkBiomesGUI/cubiomes -o test_java_tol.exe \
 *       test_java_tol.c crack_java.c crack_mitm.c \
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
 * de tolerance (simula jogador lendo Chunkbase de forma aproximada).
 * So o eixo X e' desviado: desviar X e Z juntos daria d2 = 2*d^2, que excede
 * maxD2 = tolerance^2 quando d = 1 (um sweep tolerante deixaria de casar o
 * seed verdadeiro). */
static void jitterPos(double *xs, double *zs, int i, int tol)
{
    int d = (tol > 0) ? (i % tol) : 0;        /* 0..tol-1, deterministico */
    xs[i] += (double)(d * 16);
    (void)zs;
}

/* FULL-RANGE com estruturas grandes + tolerance 1: o LIFTING tolerante foi
 * REMOVIDO por ser unsound (sRep com hiBase fixo filtra seeds cujo hi16 difere).
 * O full-range tol>0 agora retorna imediatamente o erro acionavel "not supported"
 * (sem hang nem sweep 2^32). */
static int testTolFullRejected(void)
{
    int types[] = { Village, Monument, Ancient_City, Mansion, Igloo };
    Pos p[5][8];
    double xs[5], zs[5];
    for (int i = 0; i < 5; i++) {
        if (genStructures(types[i], FIX1, p[i], 8) < 1) return 0;
        xs[i] = (double)p[i][0].x; zs[i] = (double)p[i][0].z;
        jitterPos(xs, zs, i, 2);
    }
    char *r = seedfinder_crack_java(types, 5, xs, zs, 1,
                                    0, 1ULL << 48, 2000, 120.0, 8);
    if (!r) return 0;
    int ok = (strstr(r, "not supported") != NULL);
    if (!ok) printf("  full-range tol1 should be rejected: %s\n", r);
    free(r);
    return ok;
}

/* GUARD: tolerance 8, 3 estruturas grandes, full-range -> rejeitado com o erro
 * acionavel "not supported" imediatamente (o LIFTING tolerante foi removido;
 * nao ha mais pre-check "infeasible", nem hang). */
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
    int ok = (strstr(r, "not supported") != NULL);
    if (!ok) printf("  guard tol8: %s\n", r);
    free(r);
    return ok;
}

int main(void)
{
    if (!testTolFullRejected())    { printf("FAIL tolerant full-range rejection\n"); return 1; }
    if (!testTolInfeasibleGuard()) { printf("FAIL tolerant infeasible guard\n");   return 1; }
    printf("OK tolerant lifting (full-range rejected + guard)\n");
    return 0;
}