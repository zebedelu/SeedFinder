/* core/test_mitm.c — fixture do caminho MITM (tolerance 0, full-range).
 * Build (MSYS2 UCRT64 gcc, PATH deve incluir C:\msys64\ucrt64\bin):
 *   gcc -O2 -std=c11 -I. -I.. -I../ChunkBiomesGUI/cubiomes -o test_mitm.exe \
 *       test_mitm.c crack_java.c crack_mitm.c \
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
#include <math.h>

#define BUGSEED 4294967300ULL   /* 2^32 + 4 — o seed do relatorio; > 2^32 */
#define FIX1    8675309ULL      /* paridade com test_native_crack/test_wasm */

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

/* Tipos uniformes NAO-potencia-de-2 (Village 26 inclusive: quebra o fast-path
 * lo19 porque 26 % 4 != 0 -> garante que o MITM e' de fato exercitado). */
static const int MITM_TYPES[] = { Village, Trail_Ruins, Trial_Chambers,
                                   Igloo, Desert_Pyramid, Outpost };
#define N_TYPES ((int)(sizeof(MITM_TYPES)/sizeof(MITM_TYPES[0])))

static int runMitm(uint64_t seed, const char **out)
{
    static Pos p[8][8];
    static double xs[8], zs[8];
    int n = 0;
    for (int i = 0; i < N_TYPES; i++) {
        if (genStructures(MITM_TYPES[i], seed, p[i], 8) < 1) return 0;
        xs[n] = (double)p[i][0].x; zs[n] = (double)p[i][0].z; n++;
    }
    char *r = seedfinder_crack_java(MITM_TYPES, n, xs, zs, 0,
                                    0, 1ULL << 48, 2000, 300.0, 8);
    if (!r) return 0;
    *out = r;
    return 1;
}

/* Aceite principal: seed do bug report (2^32+4) recuperada no full-range. */
static int testBugReport(void)
{
    const char *r = NULL;
    if (!runMitm(BUGSEED, &r)) { printf("  gen failed\n"); return 0; }
    char want[32]; snprintf(want, sizeof want, "\"seed\":%llu", BUGSEED);
    int ok = (strstr(r, want) != NULL);
    if (!ok) printf("  bugreport: %.200s\n", r);
    free((void *)r);
    return ok;
}

/* Fixture secundaria: 8675309 (paridade com os outros testes). */
static int testFixture(void)
{
    const char *r = NULL;
    if (!runMitm(FIX1, &r)) { printf("  gen failed\n"); return 0; }
    int ok = (strstr(r, "\"seed\":8675309") != NULL);
    if (!ok) printf("  fixture: %.200s\n", r);
    free((void *)r);
    return ok;
}

/* Gate de bits: 2 Villages (chunkRange 26, nao-liftavel) -> 18.8 bits < 40
 * -> o dispatch entra no MITM e devolve o gate acionavel. */
static int testBitsGate(void)
{
    int types[] = { Village, Village };
    Pos p[8];
    if (genStructures(Village, FIX1, p, 8) < 2) return 0;
    double xs[2] = { (double)p[0].x, (double)p[1].x };
    double zs[2] = { (double)p[0].z, (double)p[1].z };
    char *r = seedfinder_crack_java(types, 2, xs, zs, 0, 0, 1ULL << 48, 50, 10.0, 8);
    if (!r) return 0;
    int ok = (strstr(r, "MITM") != NULL);
    if (!ok) printf("  bitsgate: %.200s\n", r);
    free(r);
    return ok;
}

int main(void)
{
    if (!testBitsGate())    { printf("FAIL bits gate\n");        return 1; }
    if (!testFixture())     { printf("FAIL fixture 8675309\n");   return 1; }
    if (!testBugReport())   { printf("FAIL bug report 2^32+4\n"); return 1; }
    printf("OK mitm (bits gate + fixture + bug report)\n");
    return 0;
}
