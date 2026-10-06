/* core/test_mitm_world.c — lift dos hi16 (worldseed) apos o MITM. */
#include "crack_java.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUGSEED 4294967300ULL
static const int MITM_TYPES[] = { Village, Trail_Ruins, Trial_Chambers,
                                   Igloo, Desert_Pyramid, Outpost };
#define N_TYPES ((int)(sizeof(MITM_TYPES)/sizeof(MITM_TYPES[0])))

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

int main(void)
{
    static Pos p[N_TYPES][8];
    double xs[N_TYPES], zs[N_TYPES];
    for (int i = 0; i < N_TYPES; i++) {
        if (genStructures(MITM_TYPES[i], BUGSEED, p[i], 8) < 1) { printf("FAIL gen\n"); return 1; }
        xs[i] = (double)p[i][0].x; zs[i] = (double)p[i][0].z;
    }
    char *r = seedfinder_crack_java(MITM_TYPES, N_TYPES, xs, zs, 0,
                                    0, 1ULL << 48, 50, 120.0, 8);
    if (!r) { printf("FAIL null\n"); return 1; }
    int ok = (strstr(r, "\"seed_str\":") != NULL);
    if (ok) {
        unsigned long long s48 = 0, world = 0;
        const char *ps = strstr(r, "\"seed\":");
        const char *pw = strstr(r, "\"seed_str\":\"");
        if (!ps || !pw) ok = 0;
        else {
            sscanf(ps + 7, "%llu", &s48);
            sscanf(pw + 12, "%llu", &world);
            if ((world & 0xFFFFFFFFFFFFULL) != s48) {
                printf("  mismatch seed=%llu world=%llu\n", s48, world); ok = 0;
            }
        }
    }
    if (!ok) printf("FAIL worldseed: %.200s\n", r);
    free(r);
    if (!ok) return 1;
    printf("OK mitm worldseed\n");
    return 0;
}
