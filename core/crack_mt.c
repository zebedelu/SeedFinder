// core/crack_mt.c — núcleo compartilhado do placement MT (Bedrock) do
// SeedCracker: usado pelo crack 32-bit (seedfinder_wrapper.c) e pelo stage A
// do crack 64-bit full-range (core/crack64.c). Extraído de seedfinder_wrapper.c
// sem alterar o corpo das funções — mover + tornar não-estático.
#include "crack_mt.h"
#include "crack64.h"
#include "ChunkBiomesGUI/Bfinders.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include "platform_threads.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

int crack_g_simd = 0; /* disponibilidade SIMD em runtime; setada por crackSimdDetect() */

void crackSimdDetect(void) {
#if CRACK_SIMD && (defined(__x86_64__) || defined(__i386__))
    crack_g_simd = __builtin_cpu_supports("avx2");
#elif CRACK_SIMD
    crack_g_simd = 1; /* WASM SIMD128: garantido em compile time */
#else
    crack_g_simd = 0;
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

#if CRACK_SIMD

#if defined(__x86_64__) || defined(__i386__)
__attribute__((target("avx2")))
#endif
static void crack_mt_block(const uint32_t seedlo[CRACK_WIDTH], uint32_t cbase,
                           uint32_t out[4][CRACK_WIDTH])
{
    crack_vec v[CRACK_GROUPS][401];
    const crack_vec mtA   = CV_SPLAT(MT_MATRIX_A);
    const crack_vec upper = CV_SPLAT(MT_UPPER_MASK);
    const crack_vec lower = CV_SPLAT(MT_LOWER_MASK);
    const crack_vec one   = CV_SPLAT(1);
    const crack_vec initA = CV_SPLAT(1812433253U);
    const crack_vec zero  = CV_SPLAT(0);

    /* 8 lanes = 8 seeds por init (AVX2) / 4 lanes por init (WASM SIMD128).
     * CRACK_GROUPS>1 roda grupos independentes no mesmo laco (ILP). */
    for (int g = 0; g < CRACK_GROUPS; g++)
        v[g][0] = CV_ADD(CV_SPLAT(cbase),
                         CV_SEEDS(seedlo + (size_t)g * CRACK_LANES));
    for (int i = 1; i <= 400; i++) {
        const crack_vec ci = CV_SPLAT(i);
        for (int g = 0; g < CRACK_GROUPS; g++) {
            crack_vec prev = v[g][i - 1];
            crack_vec x = CV_XOR(prev, CV_SHR(prev, 30));
            v[g][i] = CV_ADD(CV_MUL(initA, x), ci);
        }
    }
    for (int i = 0; i < 4; i++) {
        for (int g = 0; g < CRACK_GROUPS; g++) {
            crack_vec y = CV_OR(CV_AND(v[g][i], upper), CV_AND(v[g][i + 1], lower));
            crack_vec mag = CV_AND(mtA, CV_SUB(zero, CV_AND(y, one)));
            crack_vec t = CV_XOR(v[g][i + 397], CV_XOR(CV_SHR(y, 1), mag));
            t = CV_XOR(t, CV_SHR(t, 11));
            t = CV_XOR(t, CV_AND(CV_SHL(t, 7), CV_SPLAT(0x9d2c5680U)));
            t = CV_XOR(t, CV_AND(CV_SHL(t, 15), CV_SPLAT(0xefc60000U)));
            t = CV_XOR(t, CV_SHR(t, 18));
            CV_STORE(out[i] + (size_t)g * CRACK_LANES, t);
        }
    }
}

int crackScoreSimd(const CrackTarget *targets, int nTargets,
                   const uint64_t seeds[CRACK_WIDTH], int64_t score[CRACK_WIDTH])
{
    int alive[CRACK_WIDTH], total[CRACK_WIDTH];
    uint32_t seedlo[CRACK_WIDTH];
    for (int l = 0; l < CRACK_WIDTH; l++) {
        alive[l] = 1; total[l] = 0;
        seedlo[l] = (uint32_t)seeds[l];
    }

    for (int ti = 0; ti < nTargets; ti++) {
        int any = 0;
        for (int l = 0; l < CRACK_WIDTH; l++) any |= alive[l];
        if (!any) break;
        const CrackTarget *t = &targets[ti];
        int bestD[CRACK_WIDTH];
        for (int l = 0; l < CRACK_WIDTH; l++) bestD[l] = INT32_MAX;
        int place = crackPlacement(t->type);

        if (place != PLACE_OTHER) {
            StructureConfig sconf;
            if (!getBedrockStructureConfig(t->type, MC_NEWEST, &sconf)) {
                for (int l = 0; l < CRACK_WIDTH; l++) alive[l] = 0;
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
                uint32_t w[4][CRACK_WIDTH];
                crack_mt_block(seedlo, cbase, w);

                for (int l = 0; l < CRACK_WIDTH; l++) {
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
                for (int l = 0; l < CRACK_WIDTH; l++)
                    if (alive[l] && bestD[l] != 0) done = 0;
                if (done) break;
            }
        } else {
            for (int r = 0; r < t->numRegions; r++) {
                for (int l = 0; l < CRACK_WIDTH; l++) {
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

        for (int l = 0; l < CRACK_WIDTH; l++) {
            if (!alive[l]) continue;
            if (bestD[l] == INT32_MAX || bestD[l] > t->maxD2) {
                alive[l] = 0;
            } else {
                total[l] += bestD[l];
            }
        }
    }

    int n = 0;
    for (int l = 0; l < CRACK_WIDTH; l++) {
        score[l] = alive[l] ? total[l] : -1;
        n += alive[l];
    }
    return n;
}
#endif /* CRACK_SIMD */

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
#if CRACK_SIMD
    if (crack_g_simd) {
        const uint64_t simdEnd = w->end - ((w->end - w->start) % (uint64_t)CRACK_WIDTH);
        for (; seed < simdEnd; seed += CRACK_WIDTH) {
            if (((seed - w->start) & 0xFFFFULL) == 0) {
                if (*w->stop) break;
                if (w->deadline > 0.0 && nowms_s() > w->deadline) { *w->stop = 1; break; }
            }
            uint64_t batch[CRACK_WIDTH]; int64_t scores[CRACK_WIDTH];
            for (int l = 0; l < CRACK_WIDTH; l++) batch[l] = seed + (uint64_t)l;
            crackScoreSimd(w->targets, w->nTargets, batch, scores);
            for (int l = 0; l < CRACK_WIDTH; l++) {
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

/* --- Extraído de seedfinder_wrapper.c: validação + construção ------------
 * Mesmas checagens e mesma ordem de seedfinder_crack (≥4, ≤24, tol 0–8,
 * tipo desconhecido, Mineshaft, coordenadas). out fica em ordem de entrada. */
int crackTargetsBuild(const int *types, int numTypes,
                      const double *xBlocks, const double *zBlocks,
                      int tolerance, CrackTarget out[], int *nOut, char **err)
{
    *err = NULL;
    *nOut = 0;
    if (numTypes < 4) {
        *err = strdup("{\"error\":\"at least 4 structures are required\"}");
        return 1;
    }
    if (numTypes > CRACK_MAX_STRUCTURES) {
        *err = strdup("{\"error\":\"too many structures (max 24)\"}");
        return 1;
    }
    if (tolerance < 0 || tolerance > 8) {
        *err = strdup("{\"error\":\"tolerance must be between 0 and 8 chunks\"}");
        return 1;
    }

    int maxD2 = tolerance * tolerance;
    for (int i = 0; i < numTypes; i++) {
        StructureConfig sconf;
        if (!getBedrockStructureConfig(types[i], MC_NEWEST, &sconf)) {
            *err = strdup("{\"error\":\"unknown structure type\"}");
            return 1;
        }
        if (types[i] == Mineshaft) {
            *err = strdup("{\"error\":\"Mineshaft is not supported by SeedCracker\"}");
            return 1;
        }

        CrackTarget *t = &out[i];
        int64_t chunkX = (xBlocks[i] >= 0) ? (int64_t)xBlocks[i] / 16
                                           : ((int64_t)xBlocks[i] - 15) / 16;
        int64_t chunkZ = (zBlocks[i] >= 0) ? (int64_t)zBlocks[i] / 16
                                           : ((int64_t)zBlocks[i] - 15) / 16;
        if (chunkX < -100000000LL || chunkX > 100000000LL ||
            chunkZ < -100000000LL || chunkZ > 100000000LL) {
            *err = strdup("{\"error\":\"coordinates out of range\"}");
            return 1;
        }

        t->type = types[i];
        t->chunkX = chunkX;
        t->chunkZ = chunkZ;
        t->maxD2 = maxD2;

        int64_t rLoX = (chunkX - tolerance - (sconf.regionSize - 1)) / sconf.regionSize;
        int64_t rHiX = (chunkX + tolerance >= 0) ? (chunkX + tolerance) / sconf.regionSize
                                                 : (chunkX + tolerance - (sconf.regionSize - 1)) / sconf.regionSize;
        int64_t rLoZ = (chunkZ - tolerance - (sconf.regionSize - 1)) / sconf.regionSize;
        int64_t rHiZ = (chunkZ + tolerance >= 0) ? (chunkZ + tolerance) / sconf.regionSize
                                                 : (chunkZ + tolerance - (sconf.regionSize - 1)) / sconf.regionSize;

        int n = 0;
        for (int64_t rx = rLoX; rx <= rHiX && n < CRACK_MAX_REGIONS; rx++) {
            for (int64_t rz = rLoZ; rz <= rHiZ && n < CRACK_MAX_REGIONS; rz++) {
                t->regX[n] = (int)rx;
                t->regZ[n] = (int)rz;
                n++;
            }
        }
        t->numRegions = n;
        if (n == 0) {
            *err = strdup("{\"error\":\"invalid coordinates\"}");
            return 1;
        }
    }
    *nOut = numTypes;
    return 0;
}

/* Keep the best maxResults hits. Replacing the current worst keeps the table
 * bounded; the record-minimum process keeps actual replacements few. */
void crackInsert(CrackWorker *w, uint64_t seed, int64_t score)
{
    if (w->hitCount < w->maxResults) {
        w->hits[w->hitCount].seed = seed;
        w->hits[w->hitCount].score = score;
        w->hitCount++;
    } else if (score < w->worst) {
        w->hits[w->worstIdx].seed = seed;
        w->hits[w->worstIdx].score = score;
    } else {
        return;
    }
    w->worst = INT64_MAX;
    w->worstIdx = -1;
    for (int i = 0; i < w->hitCount; i++) {
        if (w->hits[i].score < w->worst) {
            w->worst = w->hits[i].score;
            w->worstIdx = i;
        }
    }
}

void *crackWorker(void *arg)
{
    CrackWorker *w = (CrackWorker *)arg;
    uint64_t seed = w->start;

#if CRACK_SIMD
    if (crack_g_simd) {
        const uint64_t simdEnd = w->end - ((w->end - w->start) % (uint64_t)CRACK_WIDTH);
        for (; seed < simdEnd; seed += CRACK_WIDTH) {
            if (((seed - w->start) & 0xFFFFULL) == 0) {
                if (*w->stop)
                    break;
                if (w->deadline > 0.0 && nowms_s() > w->deadline) {
                    *w->stop = 1;
                    break;
                }
            }
            uint64_t batch[CRACK_WIDTH];
            int64_t scores[CRACK_WIDTH];
            for (int l = 0; l < CRACK_WIDTH; l++)
                batch[l] = seed + (uint64_t)l;
            crackScoreSimd(w->targets, w->nTargets, batch, scores);
            for (int l = 0; l < CRACK_WIDTH; l++) {
                w->checked++;
                if (scores[l] >= 0)
                    crackInsert(w, batch[l], scores[l]);
            }
        }
    }
#endif
    /* Scalar path: seed-range remainder, non-AVX2 CPUs, and bail after stop. */
    for (; seed < w->end && !*w->stop; seed++) {
        if ((seed & 0xFFFFULL) == 0) {
            if (*w->stop)
                break;
            if (w->deadline > 0.0 && nowms_s() > w->deadline) {
                *w->stop = 1;
                break;
            }
        }
        int64_t score = crackScore(w->targets, w->nTargets, seed);
        if (score >= 0)
            crackInsert(w, seed, score);
        w->checked++;
    }
    return NULL;
}

/* Extraído de seedfinder_wrapper.c: partição + workers + merge/top-N do
 * seedfinder_crack. O chamador já validou/clampou tudo. */
int crackSweepRanked(const CrackTarget *targets, int nTargets,
                     uint64_t start, uint64_t end, int maxResults,
                     double deadlineMs, int numThreads,
                     CrackHit **out, int *nOut, uint64_t *checked, int *timedOut)
{
    *out = NULL;
    *nOut = 0;
    *checked = 0;
    *timedOut = 0;
    crackSimdDetect();

    uint64_t range = end - start;
    uint64_t part = range / (uint64_t)numThreads;

    CrackThread *pids = calloc((size_t)numThreads, sizeof(CrackThread));
    CrackWorker *workers = calloc((size_t)numThreads, sizeof(CrackWorker));
    CrackHit *hits = calloc((size_t)numThreads * maxResults, sizeof(CrackHit));
    volatile int stop = 0;
    if (!pids || !workers || !hits) {
        free(pids); free(workers); free(hits);
        return -1;
    }

    for (int i = 0; i < numThreads; i++) {
        CrackWorker *w = &workers[i];
        w->targets = targets;
        w->nTargets = nTargets;
        w->start = start + i * part;
        w->end = (i == numThreads - 1) ? end : start + (i + 1) * part;
        w->maxResults = maxResults;
        w->deadline = deadlineMs;
        w->stop = &stop;
        w->hits = hits + (size_t)i * maxResults;
        w->hitCount = 0;
        w->worst = INT64_MAX;
        w->worstIdx = -1;
        w->checked = 0;
        if (numThreads == 1)
            crackWorker(w); /* inline: mesma partição, mesmo deadline, mesmo stop */
        else
            pids[i] = crackThreadCreate(crackWorker, w);
    }
    if (numThreads > 1)
        for (int i = 0; i < numThreads; i++)
            crackThreadJoin(pids[i]);

    int total = 0;
    for (int i = 0; i < numThreads; i++)
        total += workers[i].hitCount;

    CrackHit *all = malloc(((size_t)total > 0 ? (size_t)total : 1) * sizeof(CrackHit));
    if (!all) {
        free(pids); free(workers); free(hits);
        return -1;
    }
    int c = 0;
    for (int i = 0; i < numThreads; i++)
        for (int j = 0; j < workers[i].hitCount; j++)
            all[c++] = workers[i].hits[j];

    /* Insertion sort ascending by score (c is small). */
    for (int i = 1; i < c; i++) {
        CrackHit key = all[i];
        int j = i - 1;
        while (j >= 0 && all[j].score > key.score) { all[j + 1] = all[j]; j--; }
        all[j + 1] = key;
    }
    if (c > maxResults)
        c = maxResults;

    uint64_t sum = 0;
    for (int i = 0; i < numThreads; i++)
        sum += workers[i].checked;

    free(pids); free(workers); free(hits);
    *out = all;
    *nOut = c;
    *checked = sum;
    *timedOut = stop;
    return 0;
}

/* Extraído do loop de validação final de seedfinder_crack: melhor placement
 * viável (bioma na célula própria) por estrutura, na ordem de entrada. */
int crackValidateSeed(const CrackTarget *origTargets, int nTargets,
                      uint64_t seed, int *matchesOut, int64_t *scoreOut)
{
    Generator g;
    setupGenerator(&g, MC_NEWEST, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    int64_t viableScore = 0;
    for (int k = 0; k < nTargets; k++) {
        const CrackTarget *t = &origTargets[k];
        int bx = 0, bz = 0, bd = INT32_MAX;
        for (int r = 0; r < t->numRegions; r++) {
            Pos pos;
            if (!getBedrockStructurePos(t->type, MC_NEWEST, seed,
                                        t->regX[r], t->regZ[r], &pos))
                continue;
            if (!structureIsViable(t->type, &g, pos.x, pos.z))
                continue;
            int cx = (pos.x - 8) >> 4;
            int cz = (pos.z - 8) >> 4;
            int dx = cx - (int)t->chunkX;
            int dz = cz - (int)t->chunkZ;
            int d2 = dx * dx + dz * dz;
            if (d2 < bd) { bd = d2; bx = cx; bz = cz; }
        }
        if (bd == INT32_MAX || bd > t->maxD2)
            return 0;
        matchesOut[k * 2] = bx;
        matchesOut[k * 2 + 1] = bz;
        viableScore += bd;
    }
    *scoreOut = viableScore;
    return 1;
}