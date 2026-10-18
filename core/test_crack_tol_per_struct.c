// core/test_crack_tol_per_struct.c — tolerância POR ESTRUTURA no motor C:
// CrackTarget.maxD2 (crackTargetsBuild) e Anchor48.maxD2[] (anchor48Build).
// Prova que um escalar unico nao expressa um layout misto (uma coordenada
// folgada + as outras exatas).
//
// Build & run (Windows, MSYS2 UCRT64; a partir de build_server/):
//   & "C:\Program Files\Git\bin\bash.exe" -lc "cd build_server && /c/msys64/ucrt64/bin/gcc.exe -O2 -std=c11 -I.. -I../ChunkBiomesGUI -I../ChunkBiomesGUI/cubiomes ../core/test_crack_tol_per_struct.c ../core/crack64.c ../core/crack_auto.c ../core/crack_mt.c ../core/seedfinder_wrapper.c ../ChunkBiomesGUI/Bfinders.c ../ChunkBiomesGUI/cubiomes/biomes.c ../ChunkBiomesGUI/cubiomes/layers.c ../ChunkBiomesGUI/cubiomes/generator.c ../ChunkBiomesGUI/cubiomes/finders.c ../ChunkBiomesGUI/cubiomes/util.c ../ChunkBiomesGUI/cubiomes/noise.c ../ChunkBiomesGUI/cubiomes/biomenoise.c ../ChunkBiomesGUI/cubiomes/quadbase.c -lm -o test_crack_tol_per_struct && ./test_crack_tol_per_struct"
// (crack_auto.c entra no link so' para provar que compila com as assinaturas
// novas; nenhuma dos dois casos abaixo o chama.)
// Esperado: imprime TOL_PER_STRUCT_OK e sai 0.
#include "crack64.h"
#include "crack_mt.h"
#include "ChunkBiomesGUI/Bfinders.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define FULL 7777777777777777777ULL

// (a) MT misto: 3 estruturas exatas + 1 deslocada em 4 chunks.
static int testMtMixed(void) {
    const uint32_t lo32 = (uint32_t)(FULL & 0xFFFFFFFFULL);
    const int TYPES[4] = { Igloo, Jungle_Pyramid, Swamp_Hut, Village };
    const long long CELLS[4][2] = { {2,-3}, {-1,4}, {3,2}, {0,0} };
    int    types[4];
    double xs[4], zs[4];
    for (int i = 0; i < 4; i++) {
        Pos q;
        assert(getBedrockStructurePos(TYPES[i], MC_NEWEST, lo32,
                                      (int)CELLS[i][0], (int)CELLS[i][1], &q));
        types[i] = TYPES[i];
        xs[i] = (double)q.x; zs[i] = (double)q.z;
    }
    xs[3] += 64.0;   // 4 chunks de folga (bloco -> chunk = floor(b/16))

    int tol[4] = { 0, 0, 0, 4 };
    CrackTarget t[4];
    int n = 0;
    char *err = NULL;
    assert(crackTargetsBuild(types, 4, xs, zs, tol, t, &n, &err) == 0);
    assert(n == 4);
    const int exp[4] = { 0, 0, 0, 16 };
    for (int i = 0; i < 4; i++) assert(t[i].maxD2 == exp[i]);

    // d2 real da 4a ancora (menor placement dentro da janela) = 4 chunks = 16.
    int best = INT32_MAX;
    for (int r = 0; r < t[3].numRegions; r++) {
        Pos q;
        if (!getBedrockStructurePos(types[3], MC_NEWEST, lo32,
                                    t[3].regX[r], t[3].regZ[r], &q)) continue;
        int dx = ((q.x - 8) >> 4) - (int)t[3].chunkX;
        int dz = ((q.z - 8) >> 4) - (int)t[3].chunkZ;
        int d2 = dx * dx + dz * dz;
        if (d2 < best) best = d2;
    }
    assert(best == 16 && "fixture: a coordenada deslocada tem de estar a 4 chunks");
    assert(crackScore(t, 4, (uint64_t)lo32) == 16);

    // Mesmo layout com o escalar uniforme 0: a 4a nao casa -> score < 0.
    int tol0[4] = { 0, 0, 0, 0 };
    CrackTarget t0[4];
    assert(crackTargetsBuild(types, 4, xs, zs, tol0, t0, &n, &err) == 0);
    assert(crackScore(t0, 4, (uint64_t)lo32) < 0);

    // Erro por estrutura: a 3a com tol invalida cita o indice 1-based.
    int bad[4] = { 0, 0, 9, 0 };
    assert(crackTargetsBuild(types, 4, xs, zs, bad, t0, &n, &err) == 1);
    assert(err && strstr(err, "structure #3: tolerance must be between 0 and 8 chunks"));
    free(err);

    printf("MT_MIXED_TOL_OK\n");
    return 0;
}

// (b) Ancoras mistas: 1 exata + 1 deslocada em 3 chunks (tol 3).
static int testAnchorMixed(void) {
    const uint64_t s48 = FULL & C64_M48;
    Pos p0, p1;
    assert(getStructurePos(Trial_Chambers, MC_NEWEST, FULL, 0, 0, &p0));
    assert(getStructurePos(Trial_Chambers, MC_NEWEST, FULL, 3, -2, &p1));
    int    types[2] = { Trial_Chambers, Trial_Chambers };
    double xb[2] = { (double)p0.x, (double)p1.x + 48.0 };   // 3 chunks
    double zb[2] = { (double)p0.z, (double)p1.z };

    int tol[2] = { 0, 3 };
    Anchor48 a;
    assert(anchor48Build(&a, types, xb, zb, tol, 2) == 0);
    assert(a.maxD2[0] == 0 && a.maxD2[1] == 9);

    U64Vec out = {0};
    int to = 0;
    sweep48(&a, s48 - 1000, s48 + 1000, 0.0, &out, &to);
    int found = 0;
    for (int i = 0; i < out.n; i++) if (out.v[i] == s48) found = 1;
    assert(found && !to);
    free(out.v);

    // Mesmas ancoras com o escalar uniforme 0: a deslocada nao casa.
    int tol0[2] = { 0, 0 };
    Anchor48 b;
    assert(anchor48Build(&b, types, xb, zb, tol0, 2) == 0);
    assert(b.maxD2[0] == 0 && b.maxD2[1] == 0);
    U64Vec o2 = {0};
    int to2 = 0;
    sweep48(&b, s48 - 1000, s48 + 1000, 0.0, &o2, &to2);
    int found2 = 0;
    for (int i = 0; i < o2.n; i++) if (o2.v[i] == s48) found2 = 1;
    assert(!found2);
    free(o2.v);

    // tol fora de 0..8 por ancora => -1.
    int bad[2] = { 0, 9 };
    assert(anchor48Build(&b, types, xb, zb, bad, 2) == -1);

    printf("ANCHOR_MIXED_TOL_OK\n");
    return 0;
}

int main(void) {
    if (testMtMixed()) return 1;
    if (testAnchorMixed()) return 1;
    printf("TOL_PER_STRUCT_OK\n");
    return 0;
}
