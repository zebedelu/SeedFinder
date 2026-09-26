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

#endif
