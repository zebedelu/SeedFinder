/* core/crack_mitm.c — MITM Java (structure seed 48-bit, tolerance 0).
 *
 * Algoritmo de Matthew Bolan (mjtb49): "meetStructuresInMiddle.py".
 * Reimplementacao clean-room em C (o gist nao tem licenca). Ver README.md.
 *
 * Modelo (finders.h getFeatureChunkInRegion/getFeaturePos):
 *   v_i     = rx_i*A2 + rz_i*B2 + S + salt_i          (S = structure seed 48b)
 *   t_i     = ((v_i & M48) ^ A)
 *   st1_i   = (A*t_i + b) & M48 ; ox_i = (st1_i>>17) % n_i
 *   st2_i   = (A*st1_i + b) & M48 ; oz_i = (st2_i>>17) % n_i
 *
 * Split S = U*2^L + Lo (L=24). Escrevendo t = t_hi*2^L + t_lo:
 *   t_lo = ((c_i + Lo) & (2^L-1)) ^ (A & (2^L-1))        [c_i = region+salt]
 *   t_hi = ((U + (c_i>>L) + carry_i) & (2^(48-L)-1)) ^ (A>>L)
 *   carry_i = [ ((c_i & (2^L-1)) + Lo) > 2^L-1 ]
 *   X1=(A*t_lo+b)&M48, X2=(A*X1+b)&M48        (lower: so' Lo)
 *   Y1=(A*(t_hi<<L))&M48, Y2=(A*Y1)&M48       (upper: U + carry)
 *   st1=(X1+Y1)&M48, st2=(X2+Y2)&M48
 *   ox ≡ lc0 + uc0 + ov1*C (mod n); oz ≡ lc1 + uc1 + ov2*C (mod n)
 *   lck=(Xk>>17)%n, uck=(Yk>>17)%n, C=2^31 mod n, ov∈{0,1} (2^48 overflow).
 *
 * A LUT guarda para cada Lo a assinatura (carry_i, lc0_i, lc1_i) empacotada em
 * radix misto (uint64). A varredura de U faz DFS com busca binaria sobre o
 * vetor ordenado, ramificando carry (2) e overflow (2 por eixo) e podando
 * ranges vazios. Cada folha e' confirmada por javaCheckSeed (exato).
 *
 * ponytail: v1 = vetor ordenado + busca binaria, single-thread, sem SIMD/
 * trie. Se medir lento: prefix-index dos 1-2 primeiros slots (FloatInTheMiddle)
 * ou threading do sweep (faixas disjuntas de U). */
#include "crack_mitm.h"
#include "ChunkBiomesGUI/cubiomes/generator.h"
#include "platform_threads.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define MITM_L        24
#define MITM_MAXJ     6
#define MITM_MASK48   0xFFFFFFFFFFFFULL
#define MITM_A        0x5DEECE66DULL
#define MITM_B        0xBULL
#define MITM_A2       341873128712ULL
#define MITM_B2       132897987541ULL
#define MITM_GATE_BITS 40.0
#define MITM_WANT     4
#define MITM_GUARD_SAMPLE_U  (1u << 20)      /* amostra do guard de candidatos */
#define MITM_GUARD_MAX_EST   (1ull << 20)    /* teto de candidatos estimados  */
#define MITM_DEFAULT_BUDGET_MS 300000.0      /* cap default quando budget<=0  */

static inline long long mitmFloorDiv(long long a, long long b)
{
    return (a >= 0) ? a / b : (a - b + 1) / b;
}

typedef struct { int n; uint64_t c; int tx, tz; } MitmSlot;

typedef struct { uint64_t key; uint32_t lo; } MitmEnt;

typedef struct {
    MitmSlot slot[MITM_MAXJ];
    int J;
    int radix[3 * MITM_MAXJ];
    uint64_t weight[3 * MITM_MAXJ];
    MitmEnt *ent;      /* ordenado por key */
    size_t nEnt;
} MitmLut;

static int mitmUniform(int t)
{
    switch (t) {
    case Desert_Pyramid: case Jungle_Temple: case Swamp_Hut: case Igloo:
    case Village: case Ocean_Ruin: case Shipwreck: case Outpost:
    case Ruined_Portal: case Ancient_City: case Trail_Ruins: case Trial_Chambers:
        return 1;
    default:               /* Monument/Mansion/Treasure: placement nao-uniforme */
        return 0;
    }
}

/* Seleciona ate' MITM_MAXJ estruturas uniformes nao-pot2, das mais ricas em
 * bits para as mais fracas. 1 ok / -1 gate de bits / 0 sem slots. */
static int mitmSelect(const JavaTarget *t, int n, MitmSlot slots[], int *Jout,
                      double *bitsOut)
{
    MitmSlot tmp[JAVA_MAX_STRUCTURES];
    double bits[JAVA_MAX_STRUCTURES];
    int m = 0;
    for (int i = 0; i < n; i++) {
        int ty = t[i].type, nr = t[i].cfg.chunkRange;
        if (!mitmUniform(ty)) continue;
        if (nr < 2 || (nr & (nr - 1)) == 0) continue;   /* pot2: caso especial */
        int rs = t[i].cfg.regionSize;
        long long rx = mitmFloorDiv(t[i].chunkX, rs);
        long long rz = mitmFloorDiv(t[i].chunkZ, rs);
        MitmSlot s;
        s.n = nr;
        s.c = ((uint64_t)rx * MITM_A2 + (uint64_t)rz * MITM_B2
               + (uint64_t)(int64_t)t[i].cfg.salt) & MITM_MASK48;
        s.tx = (int)(t[i].chunkX - rx * rs);
        s.tz = (int)(t[i].chunkZ - rz * rs);
        tmp[m] = s;
        bits[m] = 2.0 * log2((double)nr) + (ty == Outpost ? log2(5.0) : 0.0);
        m++;
    }
    for (int i = 0; i < m; i++)                     /* ordena desc por bits */
        for (int j = i + 1; j < m; j++)
            if (bits[j] > bits[i]) {
                double db = bits[i]; bits[i] = bits[j]; bits[j] = db;
                MitmSlot ds = tmp[i]; tmp[i] = tmp[j]; tmp[j] = ds;
            }
    int J = m < MITM_MAXJ ? m : MITM_MAXJ;
    if (J == 0) { *Jout = 0; *bitsOut = 0.0; return 0; }
    double tot = 0.0;
    for (int i = 0; i < J; i++) { slots[i] = tmp[i]; tot += bits[i]; }
    *Jout = J; *bitsOut = tot;
    return (tot >= MITM_GATE_BITS) ? 1 : -1;
}

int mitmCanRun(const JavaTarget *targets, int n)
{
    /* != 0: com >=1 estrutura uniforme nao-pot2 o caso E' de MITM; se os bits
     * nao bastarem, mitmRun devolve o gate acionavel (nao o erro generico). */
    MitmSlot s[MITM_MAXJ]; int J = 0; double b = 0.0;
    return mitmSelect(targets, n, s, &J, &b) != 0;
}

static void mitmLower(const MitmSlot *s, uint32_t lo, int *carry, int *lc0, int *lc1)
{
    const uint64_t maskL = (1ULL << MITM_L) - 1;
    uint64_t sum = (s->c & maskL) + (uint64_t)lo;
    *carry = (sum > maskL) ? 1 : 0;
    uint64_t tlo = ((sum & maskL) ^ (MITM_A & maskL));
    uint64_t x1 = (MITM_A * tlo + MITM_B) & MITM_MASK48;
    uint64_t x2 = (MITM_A * x1 + MITM_B) & MITM_MASK48;
    *lc0 = (int)((x1 >> 17) % (uint64_t)s->n);
    *lc1 = (int)((x2 >> 17) % (uint64_t)s->n);
}

/* uc para a hipotese de carry; C = 2^31 mod n. */
static void mitmUpper(const MitmSlot *s, uint64_t U, int carry,
                      int *uc0, int *uc1, int *cmod)
{
    const uint64_t maskU = MITM_MASK48 >> MITM_L;
    uint64_t thi = ((U + (s->c >> MITM_L) + (uint64_t)carry) & maskU)
                   ^ (MITM_A >> MITM_L);
    uint64_t y1 = (MITM_A * (thi << MITM_L)) & MITM_MASK48;
    uint64_t y2 = (MITM_A * y1) & MITM_MASK48;
    *uc0 = (int)((y1 >> 17) % (uint64_t)s->n);
    *uc1 = (int)((y2 >> 17) % (uint64_t)s->n);
    *cmod = (int)((1ULL << 31) % (uint64_t)s->n);
}

static int mitmEntCmp(const void *a, const void *b)
{
    const MitmEnt *x = a, *y = b;
    if (x->key < y->key) return -1;
    if (x->key > y->key) return 1;
    return (x->lo < y->lo) ? -1 : (x->lo > y->lo) ? 1 : 0;
}

static size_t mitmLowerBound(const MitmEnt *e, size_t lo, size_t hi, uint64_t key)
{
    while (lo < hi) {
        size_t mid = lo + ((hi - lo) >> 1);
        if (e[mid].key < key) lo = mid + 1; else hi = mid;
    }
    return lo;
}

typedef struct { uint64_t seed; int64_t score; int *matches; } MitmHit;

typedef struct {
    const MitmLut *L;
    const JavaTarget *all; int nAll;
    uint64_t U;
    MitmHit *hits; int nHits; int maxResults;
    uint64_t totalHits;
} MitmCtx;

static void mitmPush(MitmCtx *c, uint64_t seed, int64_t score, const int *mm)
{
    size_t n = (size_t)c->nAll * 2;
    c->totalHits++;
    if (c->nHits < c->maxResults) {
        int *m = malloc(n * sizeof(int));
        if (!m) return;                 /* OOM: nao conta um hit sem matches */
        memcpy(m, mm, n * sizeof(int));
        MitmHit *h = &c->hits[c->nHits++];
        h->seed = seed; h->score = score; h->matches = m;
        return;
    }
    int worst = 0;
    for (int i = 1; i < c->nHits; i++)
        if (c->hits[i].score > c->hits[worst].score) worst = i;
    if (score < c->hits[worst].score) {
        c->hits[worst].seed = seed; c->hits[worst].score = score;
        if (c->hits[worst].matches) memcpy(c->hits[worst].matches, mm, n * sizeof(int));
    }
}

static void mitmDfs(MitmCtx *c, int i, size_t lo, size_t hi, uint64_t prefix)
{
    const MitmLut *L = c->L;
    if (i == L->J) {
        int mm[2 * JAVA_MAX_STRUCTURES];
        for (size_t k = lo; k < hi; k++) {
            uint64_t s48 = (c->U << MITM_L) | L->ent[k].lo;
            int64_t sc = 0;
            if (javaCheckSeed(c->all, c->nAll, s48, mm, &sc))
                mitmPush(c, s48, sc, mm);
        }
        return;
    }
    const MitmSlot *s = &L->slot[i];
    int base = 3 * i;
    uint64_t wC = L->weight[base], w0 = L->weight[base + 1], w1 = L->weight[base + 2];
    for (int carry = 0; carry < 2; carry++) {
        uint64_t p1 = prefix + (uint64_t)carry * wC;
        size_t a1 = mitmLowerBound(L->ent, lo, hi, p1);
        size_t b1 = mitmLowerBound(L->ent, lo, hi, p1 + wC);
        if (a1 >= b1) continue;
        int uc0, uc1, C; mitmUpper(s, c->U, carry, &uc0, &uc1, &C);
        for (int ov0 = 0; ov0 < 2; ov0++) {
            int d0 = (s->tx - uc0 + ov0 * C) % s->n; if (d0 < 0) d0 += s->n;
            uint64_t p2 = p1 + (uint64_t)d0 * w0;
            size_t a2 = mitmLowerBound(L->ent, a1, b1, p2);
            size_t b2 = mitmLowerBound(L->ent, a1, b1, p2 + w0);
            if (a2 >= b2) continue;
            for (int ov1 = 0; ov1 < 2; ov1++) {
                int d1 = (s->tz - uc1 + ov1 * C) % s->n; if (d1 < 0) d1 += s->n;
                uint64_t p3 = p2 + (uint64_t)d1 * w1;
                size_t a3 = mitmLowerBound(L->ent, a2, b2, p3);
                size_t b3 = mitmLowerBound(L->ent, a2, b2, p3 + w1);
                if (a3 >= b3) continue;
                mitmDfs(c, i + 1, a3, b3, p3);
            }
        }
    }
}

static char *mitmError(const char *msg)
{
    size_t n = strlen(msg) + 16;
    char *s = malloc(n);
    if (s) snprintf(s, n, "{\"error\":\"%s\"}", msg);
    return s;
}

/* Fase 2: para um s48, testa os 2^16 hi16: aplica a seed 64-bit e exige
 * isViableStructurePos na celula de cada estrutura observada. Devolve quantos
 * worldseeds achou (<= want). */
static int mitmLiftHi16(const JavaTarget *all, int n, uint64_t s48,
                        uint64_t *out, int64_t *outScore, int want)
{
    Generator g;
    setupGenerator(&g, MC_NEWEST, 0);
    int got = 0;
    for (uint64_t hi = 0; hi < (1ULL << 16) && got < want; hi++) {
        uint64_t full = (hi << 48) | s48;
        applySeed(&g, DIM_OVERWORLD, full);
        int ok = 1;
        for (int k = 0; k < n && ok; k++) {
            long long px = (long long)all[k].chunkX * 16;
            long long pz = (long long)all[k].chunkZ * 16;
            if (!isViableStructurePos(all[k].type, &g, (int)px, (int)pz, 0))
                ok = 0;
        }
        if (ok) { out[got] = full; outScore[got] = 0; got++; }
    }
    return got;
}

char *mitmRun(const JavaTarget *targets, int n, int maxResults, double deadline,
              uint64_t startU, uint64_t endU)
{
    MitmSlot slots[MITM_MAXJ]; int J = 0; double bits = 0.0;
    int sel = mitmSelect(targets, n, slots, &J, &bits);
    if (sel == 0)
        return mitmError("MITM requires uniform structure types (Village, "
                         "temples, igloo, ocean ruin, shipwreck, outpost, ruined "
                         "portal, ancient city, trail ruins, trial chambers)");
    if (sel < 0) {
        char msg[192];
        snprintf(msg, sizeof msg, "MITM needs ~40 bits of exact structure info "
                 "(got %.1f): add more uniform structures or use exact "
                 "coordinates (tolerance 0)", bits);
        return mitmError(msg);
    }

    MitmLut L; memset(&L, 0, sizeof L); L.J = J;
    for (int i = 0; i < J; i++) L.slot[i] = slots[i];
    for (int i = 0; i < J; i++) {
        L.radix[3 * i] = 2;
        L.radix[3 * i + 1] = slots[i].n;
        L.radix[3 * i + 2] = slots[i].n;
    }
    int D = 3 * J; uint64_t w = 1;
    for (int d = D - 1; d >= 0; d--) {
        L.weight[d] = w;
        if (w > UINT64_MAX / (uint64_t)L.radix[d])
            return mitmError("MITM key overflow (too many structures)");
        w *= (uint64_t)L.radix[d];
    }

    /* Build da LUT (2^L). */
    size_t N = (size_t)1 << MITM_L;
    MitmEnt *ent = malloc(N * sizeof(MitmEnt));
    if (!ent) return mitmError("out of memory");
    for (size_t lo = 0; lo < N; lo++) {
        uint64_t key = 0;
        for (int i = 0; i < J; i++) {
            int car, lc0, lc1;
            mitmLower(&L.slot[i], (uint32_t)lo, &car, &lc0, &lc1);
            key += (uint64_t)car * L.weight[3 * i]
                 + (uint64_t)lc0 * L.weight[3 * i + 1]
                 + (uint64_t)lc1 * L.weight[3 * i + 2];
        }
        ent[lo].key = key; ent[lo].lo = (uint32_t)lo;
        if ((lo & 0xFFFFFULL) == 0xFFFFFULL && deadline > 0.0 &&
            nowms_s() > deadline) {
            free(ent);
            return mitmError("MITM LUT build exceeded the time budget - raise "
                             "max_seconds");
        }
    }
    qsort(ent, N, sizeof(MitmEnt), mitmEntCmp);
    L.ent = ent; L.nEnt = N;

    if (startU >= endU || endU > (1ULL << MITM_L)) {
        free(ent);
        return mitmError("invalid MITM window [startU,endU)");
    }
    /* Cap default: sem budget, limita o sweep (evita varredura ilimitada). */
    if (deadline <= 0.0) deadline = nowms_s() + MITM_DEFAULT_BUDGET_MS;

    /* Guard amostrado: conta hits nas primeiras MITM_GUARD_SAMPLE_U janelas U e
     * extrapola; sets fracamente determinados sao rejeitados sem varredura. */
    uint64_t nU = endU - startU;
    if (nU > MITM_GUARD_SAMPLE_U) {
        MitmCtx probe; memset(&probe, 0, sizeof probe);
        probe.L = &L; probe.all = targets; probe.nAll = n; probe.maxResults = 0;
        probe.hits = calloc(1, sizeof(MitmHit));
        if (!probe.hits) { free(ent); return mitmError("out of memory"); }
        for (uint64_t U = startU; U < startU + MITM_GUARD_SAMPLE_U; U++) {
            probe.U = U;
            mitmDfs(&probe, 0, 0, L.nEnt, 0);
            if (probe.totalHits > 0 &&
                probe.totalHits * (nU / MITM_GUARD_SAMPLE_U) > MITM_GUARD_MAX_EST)
                break;
        }
        uint64_t est = probe.totalHits * (nU / MITM_GUARD_SAMPLE_U);
        free(probe.hits);
        if (est > MITM_GUARD_MAX_EST) {
            char msg[224];
            snprintf(msg, sizeof msg, "~%llu candidate seeds are expected to "
                     "match these structures (weakly determined input) - add "
                     "stronger or more spread-out structures, or give a "
                     "Start/End window", (unsigned long long)est);
            free(ent);
            return mitmError(msg);
        }
    }

    /* Sweep de U na janela [startU,endU), com early-stop em maxResults. */
    MitmCtx c; c.L = &L; c.all = targets; c.nAll = n; c.U = 0;
    c.maxResults = maxResults; c.nHits = 0; c.totalHits = 0;
    c.hits = calloc((size_t)maxResults, sizeof(MitmHit));
    if (!c.hits) { free(ent); return mitmError("out of memory"); }
    int timedOut = 0;
    for (uint64_t U = startU; U < endU && c.nHits < maxResults; U++) {
        c.U = U;
        mitmDfs(&c, 0, 0, L.nEnt, 0);
        if ((U & 0xFFFFFULL) == 0xFFFFFULL && nowms_s() > deadline) {
            timedOut = 1; break;
        }
    }

    /* insertion sort asc por score + serializa (envelope do crack_java). */
    for (int i = 1; i < c.nHits; i++) {
        MitmHit key = c.hits[i];
        int j = i - 1;
        while (j >= 0 && c.hits[j].score > key.score) { c.hits[j + 1] = c.hits[j]; j--; }
        c.hits[j + 1] = key;
    }
    /* Fase 2: lift dos hi16 por structure seed -> worldseeds (com seed_str).
     * ponytail: no maximo MITM_WANT worldseeds por s48; sem nenhum, o proprio
     * s48 vira seed_str (fallback placement-only). */
    size_t maxOut = (size_t)c.nHits * MITM_WANT + 1;
    uint64_t *oSeed  = malloc(maxOut * sizeof(uint64_t));
    uint64_t *oWorld = malloc(maxOut * sizeof(uint64_t));
    int64_t  *oScore = malloc(maxOut * sizeof(int64_t));
    int      *oIdx   = malloc(maxOut * sizeof(int));
    if (!oSeed || !oWorld || !oScore || !oIdx) {
        free(oSeed); free(oWorld); free(oScore); free(oIdx);
        for (int i = 0; i < c.nHits; i++) free(c.hits[i].matches);
        free(c.hits); free(ent);
        return mitmError("out of memory");
    }
    size_t nOut = 0;
    for (int i = 0; i < c.nHits && nOut < maxOut; i++) {
        uint64_t ws[MITM_WANT]; int64_t wsc[MITM_WANT];
        int got = mitmLiftHi16(targets, n, c.hits[i].seed, ws, wsc, MITM_WANT);
        if (got > 0) {
            for (int q = 0; q < got && nOut < maxOut; q++) {
                oSeed[nOut] = c.hits[i].seed; oWorld[nOut] = ws[q];
                oScore[nOut] = c.hits[i].score; oIdx[nOut] = i; nOut++;
            }
        } else {
            oSeed[nOut] = c.hits[i].seed; oWorld[nOut] = c.hits[i].seed;
            oScore[nOut] = c.hits[i].score; oIdx[nOut] = i; nOut++;
        }
    }
    size_t cap = 256 + nOut * ((size_t)n * 28 + 160);
    char *buf = malloc(cap);
    if (!buf) {
        free(oSeed); free(oWorld); free(oScore); free(oIdx);
        for (int i = 0; i < c.nHits; i++) free(c.hits[i].matches);
        free(c.hits); free(ent);
        return mitmError("out of memory");
    }
    int off = sprintf(buf, "{\"results\":[");
    for (size_t i = 0; i < nOut; i++) {
        const int *mm = c.hits[oIdx[i]].matches;
        off += sprintf(buf + off,
            "%s{\"seed\":%llu,\"seed_str\":\"%llu\",\"score\":%lld,\"matches\":[",
            i ? "," : "", (unsigned long long)oSeed[i],
            (unsigned long long)oWorld[i], (long long)oScore[i]);
        for (int k = 0; k < n; k++)
            off += sprintf(buf + off, "%s[%d,%d]", k ? "," : "",
                           mm[k * 2], mm[k * 2 + 1]);
        off += sprintf(buf + off, "]}");
    }
    sprintf(buf + off, "],\"checked\":%llu,\"timed_out\":%s,\"threads\":1,"
                       "\"window\":[%llu,%llu]}",
            (unsigned long long)(N + (timedOut ? 0 : nU)),
            timedOut ? "true" : "false",
            (unsigned long long)startU, (unsigned long long)endU);
    free(oSeed); free(oWorld); free(oScore); free(oIdx);
    for (int i = 0; i < c.nHits; i++) free(c.hits[i].matches);
    free(c.hits); free(ent);
    return buf;
}
