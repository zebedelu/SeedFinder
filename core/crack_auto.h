/* core/crack_auto.h — exportações do modo automático do SeedCracker
 * (varredura placement-only + lifts). Consumidas pelos Tasks seguintes. */
#ifndef SEEDFINDER_CRACK_AUTO_H_
#define SEEDFINDER_CRACK_AUTO_H_
#include <stdint.h>

#ifndef SEEDFINDER_API
#ifdef _WIN32
#ifdef SEEDFINDER_BRIDGE_SHARED
#define SEEDFINDER_API __declspec(dllexport)
#else
#define SEEDFINDER_API
#endif
#else
#define SEEDFINDER_API __attribute__((visibility("default")))
#endif
#endif

/* Varredura completa do espaço lo32 com matching de placement SEM gate de
 * bioma, ranqueada por score asc, capada em maxResults. JSON:
 * {"results":[{"seed":S,"score":N},...],"checked":N,"timed_out":b,"threads":T}
 * Retorno malloc'd; liberar com seedfinder_free_result. */
SEEDFINDER_API char *seedfinder_crack_auto_sweep(
    const int *types, int numTypes,
    const double *xBlocks, const double *zBlocks,
    int tolerance, uint64_t startSeed, uint64_t endSeed,
    int maxResults, double timeBudgetSec, int numThreads);

/* Deteccao 32-bit + lift de ancoras Java. A validacao completa (placement +
 * bioma) sob seed = lo32 usa os alvos COMBINADOS mt→java (sem as ancoras o
 * placement-only ruidoso comitava "32-bit world" sem lift); o lift 32->48 via
 * liftJavaHi so roda quando nJava > 0 && nLo32 == 1 (fase 3 = rank1 — a fase 2
 * passa a lista do sweep e recebe s48 vazio). numThreads fixo 1 (inline).
 * JSON: {"direct32":[{"seed":S,"score":N,"matches":[[cx,cz],...]}],"s48":[S48,...]}
 * Guard: > 1024 s48 => {"error":"too many 48-bit candidates (N) - ..."}.
 * nJava = 0 => sem ancoras (s48 vazio). Retorno malloc'd; liberar com
 * seedfinder_free_result. */
SEEDFINDER_API char *seedfinder_crack_auto_lift48(
    const int *mtTypes, const double *mtX, const double *mtZ, int nMt,
    const uint64_t *lo32Seeds, int nLo32,
    const int *jTypes, const double *jX, const double *jZ, int nJava,
    int tolerance, int maxResults);

/* Lift 48->63 via bioma: para cada s48 itera hi in [0, 2^15)
 * (seed < 2^63 => bits 48..62), full = (hi << 48) | (s48 & 0xFFFFFFFFFFFF),
 * e escolhe por ancora MT o placement VIAVEL de menor d2 (best-viable, mesma
 * semantica dos estagios 3+4 de crack64.c - um irmao mais proximo em bioma
 * morto nao esconde o verdadeiro). Cross (celulas in-tolerance sob o lo32)
 * antes do lift; top-N via crackInsert; matches/chunks re-derivados na ordem
 * de entrada mt→java por crackValidateSeed + as ancoras (chunk de canto =
 * javaChunk + 1, a UI reconstrui o bloco com *16). O score final soma o d2
 * das MT com o d2 das ancoras Java (constante por s48): sem as ancoras todas
 * as candidatas empatavam em 0 (o placement MT so depende do lo32) e o
 * ranking virava empate decidido pela ordem das seeds. nJava = 0 => score so
 * das MT. Inline 1 thread (sem argumento numThreads - mesmo caminho do
 * __EMSCRIPTEN__). JSON:
 * {"results":[{"seed":S,"seed_str":"...","score":N,"matches":[[cx,cz],...]}],
 * "checked":N,"timed_out":b} - vazio (nao erro) quando nada casa.
 * Retorno malloc'd; liberar com seedfinder_free_result. */
SEEDFINDER_API char *seedfinder_crack_auto_lift63(
    const uint64_t *s48Seeds, int nS48,
    const int *mtTypes, const double *mtX, const double *mtZ, int nMt,
    const int *jTypes, const double *jX, const double *jZ, int nJava,
    int tolerance,
    int maxResults, double timeBudgetSec);

#endif
