// core/test_crack64.c — paridade do fast-path vs cubiomes getStructurePos.
//
// Build & run (Windows, MSYS2 UCRT64; a partir de build_server/):
//   & "C:\Program Files\Git\bin\bash.exe" -lc "cd build_server && /c/msys64/ucrt64/bin/gcc.exe -std=c11 -I.. -I../ChunkBiomesGUI -I../ChunkBiomesGUI/cubiomes ../core/test_crack64.c ../core/crack64.c ../core/seedfinder_wrapper.c ../ChunkBiomesGUI/Bfinders.c ../ChunkBiomesGUI/cubiomes/biomes.c ../ChunkBiomesGUI/cubiomes/layers.c ../ChunkBiomesGUI/cubiomes/generator.c ../ChunkBiomesGUI/cubiomes/finders.c ../ChunkBiomesGUI/cubiomes/util.c ../ChunkBiomesGUI/cubiomes/noise.c ../ChunkBiomesGUI/cubiomes/biomenoise.c ../ChunkBiomesGUI/cubiomes/quadbase.c -lm -o test_crack64 && ./test_crack64"
// seedfinder_wrapper.c entra no link pela Task 6 (structureIsViable compartilhada
// do pipeline crack64; SEEDFINDER_API e' macro vazio sem SEEDFINDER_BRIDGE_SHARED).
// Esperado: imprime PARITY_OK e sai 0.
#include "crack64.h"
#include "crack_mt.h"
#include "ChunkBiomesGUI/Bfinders.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    assert(a.nJava >= 2 && "fixture precisa de multiplas ancoras (esperado 3)");
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

static int cmpu64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

// Range de 2^24 s48s contem a fixture; 4 threads; budget folgado.
#define MT_LO (7777777777777777777ULL & C64_M48) - (1ULL << 23)
#define MT_LEN (1ULL << 24)

static int testSweepMT(void) {
    uint64_t full = 7777777777777777777ULL;
    Anchor48 a = mkAnchors(full, 0);
    assert(a.nJava >= 2 && "fixture precisa de multiplas ancoras (esperado 3)");
    uint64_t s48 = full & C64_M48;
    U64Vec out = {0};
    Sweep48Result r = sweep48MT(&a, MT_LO, MT_LO + MT_LEN, 120.0, 4, &out);
    int found = 0;
    for (int i = 0; i < out.n; i++) if (out.v[i] == s48) found = 1;
    assert(found); assert(!r.timedOut); assert(r.threads == 4); assert(r.checked > 0);
    // Cobertura exata: uniao das fatias == [start,end), sem sobreposicao.
    assert(r.checked == MT_LEN);
    // Paridade com o sweep single-thread no mesmo range (mesmo conjunto de hits).
    U64Vec st = {0}; int to = 0;
    sweep48(&a, MT_LO, MT_LO + MT_LEN, 0.0, &st, &to);
    assert(!to && st.n == out.n);
    qsort(st.v, (size_t)st.n, sizeof(uint64_t), cmpu64);
    qsort(out.v, (size_t)out.n, sizeof(uint64_t), cmpu64);
    for (int i = 0; i < st.n; i++) assert(st.v[i] == out.v[i]);
    free(st.v); free(out.v);
    printf("SWEEP_MT_OK\n");
    return 0;
}

// Ramo de resto da particao: len = 2^24+1 com 3 threads => part = (2^24+1)/3
// com sobra de 2 seeds absorvidas pela Ultima fatia (o ternario `i ==
// numThreads-1 ? end`). Sem ele, cobertura ficaria em 2^24+1 - 2.
static int testSweepMTRemainder(void) {
    uint64_t full = 7777777777777777777ULL;
    Anchor48 a = mkAnchors(full, 0);
    uint64_t s48 = full & C64_M48;
    const uint64_t len = MT_LEN + 1;
    U64Vec out = {0};
    Sweep48Result r = sweep48MT(&a, MT_LO, MT_LO + len, 120.0, 3, &out);
    assert(r.threads == 3);
    assert(r.checked == len); // exato: sem perda nem duplicacao no resto
    int found = 0;
    for (int i = 0; i < out.n; i++) if (out.v[i] == s48) found = 1;
    assert(found);
    U64Vec st = {0}; int to = 0;
    sweep48(&a, MT_LO, MT_LO + len, 0.0, &st, &to);
    assert(!to && st.n == out.n);
    qsort(st.v, (size_t)st.n, sizeof(uint64_t), cmpu64);
    qsort(out.v, (size_t)out.n, sizeof(uint64_t), cmpu64);
    for (int i = 0; i < st.n; i++) assert(st.v[i] == out.v[i]);
    free(st.v); free(out.v);
    printf("SWEEP_MT_REMAINDER_OK\n");
    return 0;
}

static int testSweepMTTimeout(void) {
    uint64_t full = 7777777777777777777ULL;
    Anchor48 a = mkAnchors(full, 0);
    U64Vec out = {0};
    // Range gigantesco (2^40) com budget de 0.5 s => deadline compartilhado deve
    // parar todos os workers (timedOut via stop, mesmo que um worker termine a
    // propria fatia antes).
    Sweep48Result r = sweep48MT(&a, MT_LO, MT_LO + (1ULL << 40), 0.5, 4, &out);
    assert(r.timedOut); assert(r.checked < (1ULL << 40));
    free(out.v);
    printf("SWEEP_MT_TIMEOUT_OK\n");
    return 0;
}

// --- seedfinder_crack64 end-to-end (Task 6) ------------------------------
// Fixture sintética autoconsistente: escolhe seed completa, gera alvos com o
// próprio motor (MT p/ resíduo + cubiomes p/ Java-style), depois recupera.
// Ruling 4 (probe build_server/probe64a.c): o Desert_Pyramid região (1,0) do
// brief NÃO é viável de bioma sob FULL (viable=0) — trocado apenas o DADO da
// fixture (Igloo em (2,-3), pos (1096,-1448), viable=1 sob FULL; igloo/desert/
// jungle pyramids compartilham o placement MT e divergem só no gate de bioma).
// Forma e asserções preservadas: 1 âncora MT + 1 Trial Chambers, tol 0,
// FULL±2^24, max 100, 120 s, 2 threads.
static int testCrack64(void) {
    const uint64_t FULL = 7777777777777777777ULL;
    // 1 trial chamber alvo (âncora 2^48):
    Pos p; assert(getStructurePos(Trial_Chambers, MC_NEWEST, FULL, 0, 0, &p));
    int    jT[1] = { Trial_Chambers };
    double jX[1] = { (double)((p.x - 8) >> 4) * 16 }, jZ[1] = { (double)((p.z - 8) >> 4) * 16 };
    // 1 estrutura MT alvo (âncora mod 2^32) — posiciona com seed32 = FULL & M32:
    Pos q; assert(getBedrockStructurePos(Igloo, MC_NEWEST, FULL & 0xFFFFFFFFULL, 2, -3, &q));
    int    mT[1] = { Igloo };
    double mX[1] = { (double)q.x }, mZ[1] = { (double)q.z };

    char *json = seedfinder_crack64(mT, mX, mZ, 1, jT, jX, jZ, 1, 0,
                                    FULL - 0x1000000ULL, FULL + 0x1000000ULL,
                                    100, 120.0, 2);
    assert(json);
    int ok = strstr(json, "7777777777777777777") != NULL;
    if (!ok) fprintf(stderr, "CRACK64 FAIL: %s\n", json);
    free(json);
    assert(ok);
    printf("CRACK64_OK\n"); return 0;
}

// Fix-round-1 regression (reviewer finding 2): the lift must accept the best
// VIABLE placement per MT anchor across cells, not only the single nearest.
// Scenario found by brute-force probe (build_server/verify_viable.c) and
// re-asserted inline below: with an Ocean_Ruin anchor observed near chunk
// (-37,-25) at tolerance 8 under FULL, the NEAREST in-tolerance placement
// (cell (-2,-2), d²=52) is biome-DEAD while the true placement (cell (-2,-1),
// d²=58) is biome-VIABLE. A single-best lift tests only the dead nearest and
// PRUNES the true seed; the best-viable lift finds the farther live one. If
// the biome premise ever stops holding the asserts fail loudly (test never
// degrades into a silent no-op), and the crack64 recovery assert is the point.
static int testCrack64Viable(void) {
    const uint64_t FULL = 7777777777777777777ULL;
    uint32_t s32 = (uint32_t)(FULL & 0xFFFFFFFFULL);
    Generator g; setupGenerator(&g, MC_NEWEST, 0); applySeed(&g, DIM_OVERWORLD, FULL);

    // premise (recomputed, not hardcoded): true cell(-2,-1) viable, dead
    // sibling cell(-2,-2) is the NEAREST to observed anchor O=(-584,-392).
    Pos q; assert(getBedrockStructurePos(Ocean_Ruin, MC_NEWEST, s32, -2, -1, &q));
    Pos d; assert(getBedrockStructurePos(Ocean_Ruin, MC_NEWEST, s32, -2, -2, &d));
    assert(isViableBedrockStructurePos(Ocean_Ruin, &g, q.x, q.z, 0));   // true one VIABLE
    assert(!isViableBedrockStructurePos(Ocean_Ruin, &g, d.x, d.z, 0));  // nearer one DEAD
    long long ox = -584, oz = -392; // observed anchor blocks (chunk -37,-25)
    long long ochx = (ox >= 0) ? ox / 16 : (ox - 15) / 16; // floorDiv to chunk
    long long ochz = (oz >= 0) ? oz / 16 : (oz - 15) / 16;
    // confirm d(dead nearest) < d(viable true), both within tol=8
    long long dqx = (((long long)q.x - 8) >> 4) - ochx;
    long long dqz = (((long long)q.z - 8) >> 4) - ochz;
    long long ddx = (((long long)d.x - 8) >> 4) - ochx;
    long long ddz = (((long long)d.z - 8) >> 4) - ochz;
    long long d2viable = dqx * dqx + dqz * dqz, d2dead = ddx * ddx + ddz * ddz;
    assert(d2dead < d2viable);          // single-best would pick the DEAD one
    assert(d2viable <= 64 && d2dead <= 64); // both in-tolerance (tol=8)

    // Trial Chambers anchor (same as testCrack64) pins the s48 window.
    Pos p; assert(getStructurePos(Trial_Chambers, MC_NEWEST, FULL, 0, 0, &p));
    int    jT[1] = { Trial_Chambers };
    double jX[1] = { (double)((p.x - 8) >> 4) * 16 }, jZ[1] = { (double)((p.z - 8) >> 4) * 16 };
    int    mT[1] = { Ocean_Ruin };
    double mX[1] = { (double)ox }, mZ[1] = { (double)oz };

    uint64_t W = 0x2000;
    char *json = seedfinder_crack64(mT, mX, mZ, 1, jT, jX, jZ, 1, 8,
                                    FULL - W, FULL + W, 2000, 60.0, 2);
    assert(json);
    int ok = strstr(json, "7777777777777777777") != NULL;
    if (!ok) fprintf(stderr, "CRACK64_VIABLE FAIL: %s\n", json);
    free(json);
    assert(ok); // best-viable lift recovered the seed the single-best lift would prune
    printf("CRACK64_VIABLE_OK\n"); return 0;
}

static int testSweepMtSurvivorsRange(void) {
    const uint64_t FULL = 7777777777777777777ULL;
    const uint32_t lo32 = (uint32_t)(FULL & 0xFFFFFFFFULL);
    const int TYPES[3] = { Igloo, Jungle_Pyramid, Swamp_Hut };
    const long long CELLS[3][2] = { {2, -3}, {-1, 4}, {3, 2} };
    CrackTarget t[3];
    for (int i = 0; i < 3; i++) {
        StructureConfig sc;
        assert(getBedrockStructureConfig(TYPES[i], MC_NEWEST, &sc));
        Pos q;
        assert(getBedrockStructurePos(TYPES[i], MC_NEWEST, lo32,
                                      (int)CELLS[i][0], (int)CELLS[i][1], &q));
        t[i].type = TYPES[i];
        t[i].chunkX = ((long long)q.x - 8) >> 4;
        t[i].chunkZ = ((long long)q.z - 8) >> 4;
        t[i].maxD2 = 0;
        t[i].numRegions = 1;
        t[i].regX[0] = (int)CELLS[i][0];
        t[i].regZ[0] = (int)CELLS[i][1];
    }
    U64Vec out = {0};
    uint64_t lo = (uint64_t)lo32 - (1ULL << 19);
    MtSweepResult r = sweepMtSurvivors(t, 3, lo, lo + (1ULL << 20), 120.0, 4, &out);
    int found = 0;
    for (int i = 0; i < out.n; i++) if (out.v[i] == (uint64_t)lo32) found = 1;
    assert(found); assert(!r.timedOut); assert(r.threads == 4);
    assert(r.checked == (1ULL << 20)); // cobertura exata da particao
    free(out.v);
    printf("SWEEP_MT_SURVIVORS_OK\n");
    return 0;
}

int main(void) {
    if (testParity()) return 1;
    if (testSweep()) return 1;
    if (testSweepMT()) return 1;
    if (testSweepMTRemainder()) return 1;
    if (testSweepMTTimeout()) return 1;
    if (testCrack64()) return 1;
    if (testSweepMtSurvivorsRange()) return 1;
    return testCrack64Viable();
}
