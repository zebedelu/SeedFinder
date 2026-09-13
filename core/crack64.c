// core/crack64.c (Task 3 fast-path + Task 4 sweep48)
#include "crack64.h"
#include <stdlib.h>
#include <assert.h>
#include "platform_threads.h" // (Task 2) — usado na Task 5; ja incluido aqui sem custo

int javaCfgFor(int structureType, JavaCfg *out) {
    StructureConfig sconf;
    if (structureType != Trail_Ruins && structureType != Trial_Chambers) return 0;
    // Config JAVA (placement idêntico ao Java Edition — ver Bfinders.c):
    if (!getStructureConfig(structureType, MC_NEWEST, &sconf)) return 0;
    out->regionSize = sconf.regionSize;
    out->chunkRange = sconf.chunkRange;
    out->salt       = sconf.salt;
    return 1;
}

void javaChunk(const JavaCfg *c, uint64_t s48, int regX, int regZ,
               long long *chunkX, long long *chunkZ) {
    // setLargeFeatureWithSalt: seed = (regX*C1 + regZ*C2 + worldSeed + salt) ^ MUL, & M48
    uint64_t st = ((uint64_t)regX * 341873128712ULL
                 + (uint64_t)regZ * 132897987541ULL
                 + s48 + c->salt) ^ C64_MUL;
    st &= C64_M48;
    // nextInt(chunkRange): bits = state >>> 17 (31 bits); bound pequeno => sem retry.
    st = (st * C64_MUL + C64_ADD) & C64_M48;
    int ox = (int)((st >> 17) % (uint32_t)c->chunkRange);
    st = (st * C64_MUL + C64_ADD) & C64_M48;
    int oz = (int)((st >> 17) % (uint32_t)c->chunkRange);
    // getStructurePos devolve o bloco NO CANTO do chunk: (reg*RS + o) << 4
    // (finders.h:790, sem o +8 de centroide). O crack engine extrai o chunk
    // anchor como (blockX - 8) >> 4 (seedfinder_wrapper.c:235). Como o bloco
    // e' multiplo exato de 16, ((a*16) - 8) >> 4 == a - 1 para todo inteiro
    // ( ate' negativos), logo o chunk anchor sai direto sem o round-trip
    // de shift (evita shift assinado e acelera o hot loop do sweep).
    *chunkX = (long long)regX * c->regionSize + ox - 1;
    *chunkZ = (long long)regZ * c->regionSize + oz - 1;
}

static void u64Push(U64Vec *v, uint64_t x) {
    assert(v->n <= v->cap); // invariante do vetor (n<cap => ha' folga; n==cap => cresce)
    if (v->n == v->cap) {
        int newCap = v->cap ? v->cap * 2 : 256;
        uint64_t *nv = realloc(v->v, (size_t)newCap * sizeof(uint64_t));
        // Cinto de seguranca: se realloc falhar, nao perder o buffer antigo nem
        // escrever em NULL. Nao-liberar na falha deixa vazamento intencional —
        // melhor que corromper (o chamador so acumula candidatos; janela gigante
        // e' evitada pelos clamps do caller, Task 5).
        assert(nv && "u64Push: realloc falhou");
        if (!nv) return;
        v->v = nv;
        v->cap = newCap;
    }
    v->v[v->n++] = x;
}

int anchor48Build(Anchor48 *a, const int *types, const double *xb, const double *zb,
                  int n, int tolerance) {
    if (n < 1 || n > C64_MAX) return -1;
    a->nJava = 0;
    a->maxD2 = tolerance * tolerance;
    for (int i = 0; i < n; i++) {
        JavaCfg c;
        if (!javaCfgFor(types[i], &c)) return -1; // chamador valida antes; cinto de seguranca
        long long cx = (xb[i] >= 0) ? (long long)xb[i] / 16 : ((long long)xb[i] - 15) / 16;
        long long cz = (zb[i] >= 0) ? (long long)zb[i] / 16 : ((long long)zb[i] - 15) / 16;
        int j = a->nJava++;
        a->cfg[j] = c; a->chunkX[j] = cx; a->chunkZ[j] = cz;
        // celulas candidatas: mesma formula de regiao do seedfinder_crack (floor-div)
        int rs = c.regionSize;
        long long lox = (cx - tolerance - (rs - 1)) / rs;
        long long hix = (cx + tolerance >= 0) ? (cx + tolerance) / rs
                                              : (cx + tolerance - (rs - 1)) / rs;
        long long loz = (cz - tolerance - (rs - 1)) / rs;
        long long hiz = (cz + tolerance >= 0) ? (cz + tolerance) / rs
                                              : (cz + tolerance - (rs - 1)) / rs;
        int k = 0;
        for (long long x = lox; x <= hix && k < 64; x++)
            for (long long z = loz; z <= hiz && k < 64; z++)
                { a->regX[j][k] = (int)x; a->regZ[j][k] = (int)z; k++; }
        if (k == 0) return -1;
        a->nRegions[j] = k;
    }
    return 0;
}

// 1 se a ancora j tem alguma regiao cujo placement cai dentro de maxD2 do alvo
static inline int anchorHit(const Anchor48 *a, int j, uint64_t s48) {
    for (int r = 0; r < a->nRegions[j]; r++) {
        long long cx, cz;
        javaChunk(&a->cfg[j], s48, a->regX[j][r], a->regZ[j][r], &cx, &cz);
        long long dx = cx - a->chunkX[j], dz = cz - a->chunkZ[j];
        if (dx * dx + dz * dz <= (long long)a->maxD2) return 1;
    }
    return 0;
}

void sweep48(const Anchor48 *a, uint64_t s48Start, uint64_t s48End,
             double deadlineMs, U64Vec *out, int *timedOut) {
    *timedOut = 0; out->n = 0;
    for (uint64_t s = s48Start; s < s48End; s++) {
        int ok = 1;
        for (int j = 0; j < a->nJava && ok; j++) ok = anchorHit(a, j, s);
        if (ok) u64Push(out, s);
        if ((s & 0xFFFFULL) == 0 && deadlineMs > 0.0 && nowms_s() > deadlineMs) {
            *timedOut = 1; return;
        }
    }
}

// --- sweep48 multi-thread (Task 5) -------------------------------------
// Mesmo padrao de seedfinder_crack (seedfinder_wrapper.c): stop volatil
// compartilhado + deadline absoluto em ms (0.0 = sem deadline); cada worker
// tem U64Vec proprio, mergeado so' depois do join (sem locks, sem races).

typedef struct {
    const Anchor48 *a; uint64_t start, end; double deadline;
    volatile int *stop; U64Vec out; uint64_t checked;
} SweepWorker;

static void *sweep48Worker(void *arg) {
    SweepWorker *w = (SweepWorker *)arg;
    for (uint64_t s = w->start; s < w->end && !*w->stop; s++) {
        int ok = 1;
        for (int j = 0; j < w->a->nJava && ok; j++) ok = anchorHit(w->a, j, s);
        if (ok) u64Push(&w->out, s);
        w->checked++;
        if ((s & 0xFFFFULL) == 0 && w->deadline > 0.0 && nowms_s() > w->deadline)
            { *w->stop = 1; break; }
    }
    return NULL;
}

Sweep48Result sweep48MT(const Anchor48 *a, uint64_t start, uint64_t end,
                        double budgetSec, int numThreads, U64Vec *out) {
#if defined(__EMSCRIPTEN__)
    // Sem -pthread o pthread_create aborta em runtime; o JS paraleliza
    // via Web Workers (uma fatia [start,end) por worker). Igual ao crack 32.
    numThreads = 1;
#endif
    Sweep48Result r = {0, 0, numThreads < 1 ? 1 : numThreads};
    if (numThreads < 1) numThreads = 1;
    if (start >= end) return r; // range vazio/invalido: sem particionamento
    // Fatia >= 1 s48 por worker: garante part > 0 (sem dupplicacao/perda de
    // cobertura) e protege a divisao. Range minusculo vai todo num worker so'.
    if ((uint64_t)numThreads > end - start) numThreads = 1;
    volatile int stop = 0;
    double deadline = budgetSec > 0.0 ? nowms_s() + budgetSec * 1000.0 : 0.0;
    SweepWorker *ws = calloc((size_t)numThreads, sizeof(SweepWorker));
    CrackThread *th = calloc((size_t)numThreads, sizeof(CrackThread));
    uint64_t part = (end - start) / (uint64_t)numThreads;
    for (int i = 0; i < numThreads; i++) {
        ws[i].a = a; ws[i].start = start + (uint64_t)i * part;
        ws[i].end = (i == numThreads - 1) ? end : start + (uint64_t)(i + 1) * part;
        ws[i].deadline = deadline; ws[i].stop = &stop;
    }
    if (numThreads == 1) sweep48Worker(&ws[0]); // inline (WASM e range pequeno)
    else { for (int i = 0; i < numThreads; i++) th[i] = crackThreadCreate(sweep48Worker, &ws[i]);
           for (int i = 0; i < numThreads; i++) crackThreadJoin(th[i]); }
    // timedOut espelha o stop compartilhado: um worker que estourou o deadline
    // marca todos, mesmo os que completaram a propria fatia.
    r.timedOut = stop; r.threads = numThreads;
    // checked somado dos workers sob timeout e' contagem parcial (mesmo
    // comportamento aceito do seedfinder_crack de 32 bits).
    for (int i = 0; i < numThreads; i++) {
        r.checked += ws[i].checked;
        for (int k = 0; k < ws[i].out.n; k++) u64Push(out, ws[i].out.v[k]);
        free(ws[i].out.v);
    }
    free(ws); free(th);
    return r;
}
