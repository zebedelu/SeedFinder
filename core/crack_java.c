/* core/crack_java.c — SeedCracker Java Edition.
 * Placement-only (NÃO usa isViableStructurePos/applySeed: biomas Java dependem
 * da seed 64-bit completa, fora do escopo 48-bit). Mesmo precedente do
 * crack_auto.c (varredura placement-only). */
#include "crack_java.h"
#include "platform_threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define JV_A  341873128712ULL
#define JV_B  132897987541ULL
#define JV_K  0x5DEECE66DULL
#define JV_ADD 0xBULL
#define JV_M19 0x7FFFFULL   /* 2^19 - 1 */

static inline long long jvChunkOf(double block)
{
    long long b = (long long)block;
    return (b >= 0) ? b / 16 : (b - 15) / 16;
}

int javaTargetsBuild(const int *types, int numTypes, const double *xb, const double *zb,
                     int tolerance, JavaTarget out[], int *nOut, char **err)
{
    *err = NULL; *nOut = 0;
    if (numTypes < 1 || numTypes > JAVA_MAX_STRUCTURES) {
        *err = strdup("{\"error\":\"1-24 structures required\"}"); return 1;
    }
    if (tolerance < 0 || tolerance > 8) {
        *err = strdup("{\"error\":\"tolerance must be between 0 and 8 chunks\"}"); return 1;
    }
    int maxD2 = tolerance * tolerance;
    for (int i = 0; i < numTypes; i++) {
        int t = types[i];
        if (t == Mineshaft) {
            *err = strdup("{\"error\":\"Mineshaft is not supported by SeedCracker\"}"); return 1;
        }
        StructureConfig sc;
        if (!getStructureConfig(t, MC_NEWEST, &sc)) {
            *err = strdup("{\"error\":\"unknown structure type\"}"); return 1;
        }
        long long cx = jvChunkOf(xb[i]);
        long long cz = jvChunkOf(zb[i]);
        if (cx < -100000000LL || cx > 100000000LL || cz < -100000000LL || cz > 100000000LL) {
            *err = strdup("{\"error\":\"coordinates out of range\"}"); return 1;
        }
        JavaTarget *o = &out[i];
        o->type = t; o->cfg = sc; o->chunkX = cx; o->chunkZ = cz;
        o->maxD2 = maxD2; o->tolerance = tolerance;
        int rs = sc.regionSize;
        long long lox = (cx - tolerance - (rs - 1)) / rs;
        long long hix = (cx + tolerance >= 0) ? (cx + tolerance) / rs
                                              : (cx + tolerance - (rs - 1)) / rs;
        long long loz = (cz - tolerance - (rs - 1)) / rs;
        long long hiz = (cz + tolerance >= 0) ? (cz + tolerance) / rs
                                              : (cz + tolerance - (rs - 1)) / rs;
        int k = 0;
        for (long long x = lox; x <= hix && k < JAVA_MAX_REGIONS; x++)
            for (long long z = loz; z <= hiz && k < JAVA_MAX_REGIONS; z++) {
                o->regX[k] = (int)x; o->regZ[k] = (int)z; k++;
            }
        if (k == 0) { *err = strdup("{\"error\":\"invalid coordinates\"}"); return 1; }
        o->numRegions = k;
    }
    *nOut = numTypes;
    return 0;
}

/* Ordem de varredura: indices com menos regioes primeiro. */
static void jvOrder(const JavaTarget *targets, int n, int *order)
{
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 0; i < n; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++)
            if (targets[order[j]].numRegions < targets[order[best]].numRegions) best = j;
        int t = order[i]; order[i] = order[best]; order[best] = t;
    }
}

int javaCheckSeed(const JavaTarget *targets, int n, uint64_t s48,
                  int *matchesOut, int64_t *scoreOut)
{
    int order[JAVA_MAX_STRUCTURES];
    jvOrder(targets, n, order);
    typedef struct { int d2, x, z; } JVC;
    int64_t score = 0;
    for (int k = 0; k < n; k++) {
        const JavaTarget *t = &targets[order[k]];
        JVC small[64];
        JVC *cands = small;
        int cap = 64, cnt = 0;
        if (t->numRegions > cap) { cap = t->numRegions; cands = malloc((size_t)cap * sizeof(JVC)); }
        if (!cands) return 0;
        for (int r = 0; r < t->numRegions; r++) {
            Pos pos;
            if (!getStructurePos(t->type, MC_NEWEST, s48, t->regX[r], t->regZ[r], &pos))
                continue;
            long long dx = (long long)(pos.x >> 4) - t->chunkX;
            long long dz = (long long)(pos.z >> 4) - t->chunkZ;
            long long d2 = dx * dx + dz * dz;
            if (d2 > t->maxD2) continue;
            cands[cnt].d2 = (int)d2; cands[cnt].x = pos.x; cands[cnt].z = pos.z; cnt++;
        }
        for (int i = 1; i < cnt; i++) {           /* insertion sort por d2 asc */
            JVC key = cands[i]; int j = i - 1;
            while (j >= 0 && cands[j].d2 > key.d2) { cands[j + 1] = cands[j]; j--; }
            cands[j + 1] = key;
        }
        int ok = cnt > 0;
        int mx = ok ? cands[0].x : 0, mz = ok ? cands[0].z : 0, md = ok ? cands[0].d2 : 0;
        if (cands != small) free(cands);
        if (!ok) return 0;
        score += md;
        matchesOut[order[k] * 2]     = mx >> 4;
        matchesOut[order[k] * 2 + 1] = mz >> 4;
    }
    if (scoreOut) *scoreOut = score;
    return 1;
}