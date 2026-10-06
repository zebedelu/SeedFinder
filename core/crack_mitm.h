/* core/crack_mitm.h — Meet-in-the-middle Java structure-seed cracker.
 * Algoritmo: Matthew Bolan (mjtb49), meetStructuresInMiddle.py (reimplementacao
 * clean-room — o gist nao tem licenca; algoritmo/ideia nao e' copyrightavel).
 * Recupera os 48 bits baixos da seed Java a partir de coordenadas EXATAS
 * (tolerance 0) de estruturas uniformes (getFeaturePos). */
#ifndef SEEDFINDER_CRACK_MITM_H_
#define SEEDFINDER_CRACK_MITM_H_
#include <stdint.h>
#include "crack_java.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 se da' para rodar o MITM (>=1 estrutura uniforme nao-pot2); 0 senao. */
int mitmCanRun(const JavaTarget *targets, int n);

/* Executa o MITM sobre `targets` (tolerance 0). Verifica com javaCheckSeed
 * sobre TODAS as estruturas. Devolve JSON malloc'd (envelope do crack_java)
 * ou {"error":...}. deadline em ms (0.0 = sem limite). */
char *mitmRun(const JavaTarget *targets, int n, int maxResults, double deadline);

SEEDFINDER_API char *seedfinder_crack_mitm(
    const int *types, int numTypes,
    const double *xBlocks, const double *zBlocks,
    int maxResults, double timeBudgetSec, int numThreads);

#ifdef __cplusplus
}
#endif
#endif
