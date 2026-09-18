// core/crack_mt.c — núcleo compartilhado do placement MT (Bedrock) do
// SeedCracker: usado pelo crack 32-bit (seedfinder_wrapper.c) e pelo stage A
// do crack 64-bit full-range (core/crack64.c). Extraído de seedfinder_wrapper.c
// sem alterar o corpo das funções — mover + tornar não-estático.
#include "crack_mt.h"
#include "ChunkBiomesGUI/Bfinders.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include "platform_threads.h"
#include <stdlib.h>
#include <limits.h>

int crack_g_avx2 = 0; /* runtime AVX2 flag, set once by crackDetectAvx2() */

void crackDetectAvx2(void) {
#if SEEDFINDER_SIMD
    crack_g_avx2 = __builtin_cpu_supports("avx2");
#else
    crack_g_avx2 = 0; /* WASM/scalar: sem AVX2 */
#endif
}

int u64Push(U64Vec *v, uint64_t x) {
    if (v->n == v->cap) {
        int newCap = v->cap ? v->cap * 2 : 256;
        uint64_t *nv = realloc(v->v, (size_t)newCap * sizeof(uint64_t));
        if (!nv) return 1; /* OOM: buffer antigo intacto, n nao muda */
        v->v = nv;
        v->cap = newCap;
    }
    v->v[v->n++] = x;
    return 0;
}

int crackPlacement(int structureType)
{
    switch (structureType) {
    case Desert_Pyramid: case Igloo: case Jungle_Pyramid:
    case Ruined_Portal: case Swamp_Hut: case Shipwreck: case Ocean_Ruin:
    case Bastion: case Fortress: case Ruined_Portal_N:
        return PLACE_FEATURE;
    case Ancient_City: case Mansion: case Monument: case Outpost:
    case Treasure: case Village:
        return PLACE_LARGE;
    default:
        return PLACE_OTHER;
    }
}

/* Score a candidate seed: sum over structures of the best squared-chunk
 * deviation across its candidate regions. Returns < 0 when any structure
 * has no region match within tolerance. */
int64_t crackScore(const CrackTarget *targets, int nTargets, uint64_t seed)
{
    int64_t total = 0;
    for (int i = 0; i < nTargets; i++) {
        const CrackTarget *t = &targets[i];
        int bestD = INT32_MAX;
        for (int k = 0; k < t->numRegions; k++) {
            Pos pos;
            if (!getBedrockStructurePos(t->type, MC_NEWEST, seed,
                                        t->regX[k], t->regZ[k], &pos))
                continue;
            int cx = (pos.x - 8) >> 4;   /* anchor chunk */
            int cz = (pos.z - 8) >> 4;
            int dx = cx - (int)t->chunkX;
            int dz = cz - (int)t->chunkZ;
            int d2 = dx * dx + dz * dz;
            if (d2 < bestD) bestD = d2;
        }
        if (bestD == INT32_MAX || bestD > t->maxD2)
            return -1;
        total += bestD;
    }
    return total;
}

int crackTargetCompare(const void *a, const void *b)
{
    const CrackTarget *ta = (const CrackTarget *)a;
    const CrackTarget *tb = (const CrackTarget *)b;
    /* Fewest candidate regions first -> stronger filter earlier. */
    if (ta->numRegions != tb->numRegions) return ta->numRegions - tb->numRegions;
    return 0;
}

#if SEEDFINDER_SIMD

__attribute__((target("avx2")))
void crack_mt8_block(const uint32_t seedlo[8], uint32_t cbase, uint32_t out[4][8])
{
    __m256i v[401];
    const __m256i mtA   = _mm256_set1_epi32((int)MT_MATRIX_A);
    const __m256i upper = _mm256_set1_epi32((int)MT_UPPER_MASK);
    const __m256i lower = _mm256_set1_epi32((int)MT_LOWER_MASK);
    const __m256i one   = _mm256_set1_epi32(1);
    const __m256i initA = _mm256_set1_epi32(1812433253U);

    /* 8 lanes = 8 seeds por init (o __m256i tem 8 x int32). */
    v[0] = _mm256_add_epi32(_mm256_set1_epi32((int)cbase),
                            _mm256_setr_epi32((int)seedlo[0], (int)seedlo[1],
                                              (int)seedlo[2], (int)seedlo[3],
                                              (int)seedlo[4], (int)seedlo[5],
                                              (int)seedlo[6], (int)seedlo[7]));
    for (int i = 1; i <= 400; i++) {
        __m256i prev = v[i - 1];
        __m256i x = _mm256_xor_si256(prev, _mm256_srli_epi32(prev, 30));
        v[i] = _mm256_add_epi32(_mm256_mullo_epi32(initA, x), _mm256_set1_epi32(i));
    }
    for (int i = 0; i < 4; i++) {
        __m256i y = _mm256_or_si256(_mm256_and_si256(v[i], upper),
                                    _mm256_and_si256(v[i + 1], lower));
        __m256i mag = _mm256_and_si256(mtA, _mm256_sub_epi32(_mm256_setzero_si256(),
                                       _mm256_and_si256(y, one)));
        __m256i t = _mm256_xor_si256(v[i + 397],
                    _mm256_xor_si256(_mm256_srli_epi32(y, 1), mag));
        t = _mm256_xor_si256(t, _mm256_srli_epi32(t, 11));
        t = _mm256_xor_si256(t, _mm256_and_si256(_mm256_slli_epi32(t, 7),
                              _mm256_set1_epi32((int)0x9d2c5680U)));
        t = _mm256_xor_si256(t, _mm256_and_si256(_mm256_slli_epi32(t, 15),
                              _mm256_set1_epi32((int)0xefc60000U)));
        t = _mm256_xor_si256(t, _mm256_srli_epi32(t, 18));
        _mm256_storeu_si256((__m256i *)out[i], t);
    }
}

int crackScore8(const CrackTarget *targets, int nTargets,
                const uint64_t seeds[8], int64_t score[8])
{
    int alive[8], total[8];
    uint32_t seedlo[8];
    for (int l = 0; l < 8; l++) {
        alive[l] = 1; total[l] = 0;
        seedlo[l] = (uint32_t)seeds[l];
    }

    for (int ti = 0; ti < nTargets; ti++) {
        int any = 0;
        for (int l = 0; l < 8; l++) any |= alive[l];
        if (!any) break;
        const CrackTarget *t = &targets[ti];
        int bestD[8];
        for (int l = 0; l < 8; l++) bestD[l] = INT32_MAX;
        int place = crackPlacement(t->type);

        if (place != PLACE_OTHER) {
            StructureConfig sconf;
            if (!getBedrockStructureConfig(t->type, MC_NEWEST, &sconf)) {
                for (int l = 0; l < 8; l++) alive[l] = 0;
                continue;
            }
            const int range = sconf.chunkRange;
            const int large = (place == PLACE_LARGE);
            const uint32_t pmask = ((range & (range - 1)) == 0) ? (uint32_t)range - 1u : 0u;

            for (int r = 0; r < t->numRegions; r++) {
                const int regX = t->regX[r];
                const int regZ = t->regZ[r];
                const uint32_t cbase = (uint32_t)((uint64_t)regX * REGION_SALT_X
                                                 + (uint64_t)regZ * REGION_SALT_Z
                                                 + sconf.salt);
                uint32_t w[4][8];
                crack_mt8_block(seedlo, cbase, w);

                for (int l = 0; l < 8; l++) {
                    if (!alive[l]) continue;
                    const uint32_t m0 = w[0][l], m1 = w[1][l];
                    const uint32_t m2 = w[2][l], m3 = w[3][l];
                    const int x1 = pmask ? (int)(m0 & pmask) : (int)(m0 % (uint32_t)range);
                    const int x2 = pmask ? (int)(m1 & pmask) : (int)(m1 % (uint32_t)range);
                    int ox, oz;
                    if (large) {
                        const int z1 = pmask ? (int)(m2 & pmask) : (int)(m2 % (uint32_t)range);
                        const int z2 = pmask ? (int)(m3 & pmask) : (int)(m3 % (uint32_t)range);
                        ox = (x1 + x2) >> 1;
                        oz = (z1 + z2) >> 1;
                    } else {
                        ox = x1;
                        oz = x2;
                    }
                    const int posx = (int)((((uint32_t)regX * (uint32_t)sconf.regionSize
                                           + (uint32_t)ox) << 4) + 8);
                    const int posz = (int)((((uint32_t)regZ * (uint32_t)sconf.regionSize
                                           + (uint32_t)oz) << 4) + 8);
                    const int cx = (posx - 8) >> 4;
                    const int cz = (posz - 8) >> 4;
                    const int dx = cx - (int)t->chunkX;
                    const int dz = cz - (int)t->chunkZ;
                    const int d2 = dx * dx + dz * dz;
                    if (d2 < bestD[l])
                        bestD[l] = d2;
                }
                int done = 1;
                for (int l = 0; l < 8; l++)
                    if (alive[l] && bestD[l] != 0) done = 0;
                if (done) break;
            }
        } else {
            for (int r = 0; r < t->numRegions; r++) {
                for (int l = 0; l < 8; l++) {
                    if (!alive[l]) continue;
                    Pos pos;
                    if (!getBedrockStructurePos(t->type, MC_NEWEST, seeds[l],
                                                t->regX[r], t->regZ[r], &pos))
                        continue;
                    const int cx = (pos.x - 8) >> 4;
                    const int cz = (pos.z - 8) >> 4;
                    const int dx = cx - (int)t->chunkX;
                    const int dz = cz - (int)t->chunkZ;
                    const int d2 = dx * dx + dz * dz;
                    if (d2 < bestD[l])
                        bestD[l] = d2;
                }
            }
        }

        for (int l = 0; l < 8; l++) {
            if (!alive[l]) continue;
            if (bestD[l] == INT32_MAX || bestD[l] > t->maxD2) {
                alive[l] = 0;
            } else {
                total[l] += bestD[l];
            }
        }
    }

    int n = 0;
    for (int l = 0; l < 8; l++) {
        score[l] = alive[l] ? total[l] : -1;
        n += alive[l];
    }
    return n;
}
#endif /* SEEDFINDER_SIMD */

/* --- sweepMtSurvivors (novo): coleta TODOS os lo32 sobreviventes ----------
 * Mesmo padrao de sweep48MT/crackWorker: stop volatil + deadline absoluto em
 * ms (0.0 = sem deadline); worker por fatia com U64Vec proprio (merge pos-
 * join, sem locks). Emscripten: numThreads=1 e worker inline (sem -pthread). */
typedef struct {
    const CrackTarget *targets;
    int nTargets;
    uint64_t start, end;
    double deadline;
    volatile int *stop;
    U64Vec out;
    uint64_t checked;
} MtSweepWorker;

static void *mtSweepWorker(void *arg) {
    MtSweepWorker *w = (MtSweepWorker *)arg;
    uint64_t seed = w->start;
#if SEEDFINDER_SIMD
    if (crack_g_avx2) {
        const uint64_t simdEnd = w->end - ((w->end - w->start) & 7ULL);
        for (; seed < simdEnd; seed += 8) {
            if (((seed - w->start) & 0xFFFFULL) == 0) {
                if (*w->stop) break;
                if (w->deadline > 0.0 && nowms_s() > w->deadline) { *w->stop = 1; break; }
            }
            uint64_t batch[8]; int64_t scores[8];
            for (int l = 0; l < 8; l++) batch[l] = seed + (uint64_t)l;
            crackScore8(w->targets, w->nTargets, batch, scores);
            for (int l = 0; l < 8; l++) {
                w->checked++;
                if (scores[l] >= 0) u64Push(&w->out, batch[l]);
            }
        }
    }
#endif
    for (; seed < w->end && !*w->stop; seed++) {
        if ((seed & 0xFFFFULL) == 0) {
            if (*w->stop) break;
            if (w->deadline > 0.0 && nowms_s() > w->deadline) { *w->stop = 1; break; }
        }
        if (crackScore(w->targets, w->nTargets, seed) >= 0)
            u64Push(&w->out, seed);
        w->checked++;
    }
    return NULL;
}

MtSweepResult sweepMtSurvivors(const CrackTarget *targets, int nTargets,
                               uint64_t start, uint64_t end,
                               double budgetSec, int numThreads, U64Vec *out) {
#if defined(__EMSCRIPTEN__)
    numThreads = 1;
#endif
    MtSweepResult r = {0, 0, numThreads < 1 ? 1 : numThreads};
    if (numThreads < 1) numThreads = 1;
    if (start >= end) return r;
    if ((uint64_t)numThreads > end - start) numThreads = 1;
    volatile int stop = 0;
    double deadline = budgetSec > 0.0 ? nowms_s() + budgetSec * 1000.0 : 0.0;
    MtSweepWorker *ws = calloc((size_t)numThreads, sizeof(MtSweepWorker));
    CrackThread *th = calloc((size_t)numThreads, sizeof(CrackThread));
    uint64_t part = (end - start) / (uint64_t)numThreads;
    for (int i = 0; i < numThreads; i++) {
        ws[i].targets = targets; ws[i].nTargets = nTargets;
        ws[i].start = start + (uint64_t)i * part;
        ws[i].end = (i == numThreads - 1) ? end : start + (uint64_t)(i + 1) * part;
        ws[i].deadline = deadline; ws[i].stop = &stop;
    }
    if (numThreads == 1) mtSweepWorker(&ws[0]); /* inline (WASM e range pequeno) */
    else {
        for (int i = 0; i < numThreads; i++) th[i] = crackThreadCreate(mtSweepWorker, &ws[i]);
        for (int i = 0; i < numThreads; i++) crackThreadJoin(th[i]);
    }
    r.timedOut = stop; r.threads = numThreads;
    for (int i = 0; i < numThreads; i++) {
        r.checked += ws[i].checked;
        for (int k = 0; k < ws[i].out.n; k++) u64Push(out, ws[i].out.v[k]);
        free(ws[i].out.v);
    }
    free(ws); free(th);
    return r;
}