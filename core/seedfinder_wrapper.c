#include "ChunkBiomesGUI/Bfinders.h"
#include "ChunkBiomesGUI/cubiomes/generator.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include "ChunkBiomesGUI/cubiomes/util.h"
#include "crack64.h"
#include "crack_mt.h"
#include "viability.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

/* Floor division — correct for negative numbers */
static inline int floorDiv(int a, int b)
{
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

/* Internal structure for collecting results */
typedef struct {
    const char *name;
    int x;
    int z;
    double distance;
} FoundStructure;

/* Compare function for qsort — ascending by distance */
static int compareByDistance(const void *a, const void *b)
{
    double da = ((const FoundStructure *)a)->distance;
    double db = ((const FoundStructure *)b)->distance;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* Bioma na celula da PROPRIa estrutura (checagem Bedrock): centro do chunk em
 * unidades de 4 blocos, no topo do mundo. x,z sao a posicao em blocos
 * (chunk*16+8), entao equivale a x>>2. */
static int bedrockOwnCellBiome(Generator *g, int x, int z)
{
    int cellX = (x >> 4) * 4 + 2;
    int cellZ = (z >> 4) * 4 + 2;
    return getBiomeAt(g, 0, cellX, 319 >> 2, cellZ);
}

/* Gate de viabilidade compartilhado por /scan, seedfinder_crack e o lift de
 * bioma do seedfinder_crack64. Declarado em crack64.h.
 *
 * O pre-filtro Java (`isViableBedrockStructurePos`) cobre regras que o Bedrock
 * compartilha (ex. Ruined_Portal sempre viavel, Trail_Ruins/Trial_Chambers
 * gerados identicamente a Java), mas ele amostra o bioma no canto da
 * bounding box via LCG Java. O Bedrock confere o bioma na celula da propria
 * estrutura; sem a segunda checagem, posicoes em rio/pantano/oceano passam
 * como vila — os falsos positivos relatados. */
int structureIsViable(int structureType, Generator *g, int x, int z)
{
    if (!isViableBedrockStructurePos(structureType, g, x, z, 0))
        return 0;
    int bio = bedrockOwnCellBiome(g, x, z);
    return bedrockViableBiome(structureType, bio);
}

SEEDFINDER_API const char *seedfinder_status(void)
{
    return "{\"status\": \"ok\"}";
}

#define SCAN_EDITION_BEDROCK 0
#define SCAN_EDITION_JAVA    1

/* Loop de scan compartilhado pelas duas edicoes. A diferenca sao exatamente
 * 4 pontos selecionados por `edition`: generator, config, placer e gate de
 * bioma. mcLabel (so Java): rotulo futuro de versao, ex. "1.18" - NULL,
 * vazio ou invalido => MC_NEWEST. Hoje o servidor sempre manda NULL. */
static char *scan_impl(
    uint64_t seed,
    double playerX, double playerZ,
    int radius, int maxResults,
    const int *types, int numTypes,
    int edition, const char *mcLabel)
{
    int mc = MC_NEWEST;
    if (edition == SCAN_EDITION_JAVA && mcLabel && mcLabel[0]) {
        int parsed = str2mc(mcLabel);
        if (parsed != MC_UNDEF)
            mc = parsed;
    }

    Generator g;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    int playerChunkX = (int)floor(playerX / 16.0);
    int playerChunkZ = (int)floor(playerZ / 16.0);

    /* Collect results */
    FoundStructure *results = NULL;
    int resultCount = 0;
    int resultCapacity = 0;

    for (int ti = 0; ti < numTypes; ti++) {
        int structType = types[ti];
        StructureConfig sconf;

        int hasConfig = (edition == SCAN_EDITION_JAVA)
            ? getStructureConfig(structType, mc, &sconf)
            : getBedrockStructureConfig(structType, mc, &sconf);
        if (!hasConfig)
            continue;

        int regionSize = sconf.regionSize;
        int regionMinX = floorDiv(playerChunkX - radius, regionSize) - 1;
        int regionMaxX = floorDiv(playerChunkX + radius, regionSize) + 1;
        int regionMinZ = floorDiv(playerChunkZ - radius, regionSize) - 1;
        int regionMaxZ = floorDiv(playerChunkZ + radius, regionSize) + 1;

        for (int regX = regionMinX; regX <= regionMaxX; regX++) {
            for (int regZ = regionMinZ; regZ <= regionMaxZ; regZ++) {
                Pos pos;
                int hasPos = (edition == SCAN_EDITION_JAVA)
                    ? getStructurePos(structType, mc, seed, regX, regZ, &pos)
                    : getBedrockStructurePos(structType, mc, seed, regX, regZ, &pos);
                if (!hasPos)
                    continue;

                /* Gate de bioma: Java usa o gate nativo do cubiomes;
                 * Bedrock usa o gate de dois estagios (celula propria). */
                int viable = (edition == SCAN_EDITION_JAVA)
                    ? isViableStructurePos(structType, &g, pos.x, pos.z, 0)
                    : structureIsViable(structType, &g, pos.x, pos.z);
                if (!viable)
                    continue;

                int dx = pos.x / 16 - playerChunkX;
                int dz = pos.z / 16 - playerChunkZ;
                double distance = sqrt((double)(dx * dx + dz * dz));

                if (distance > radius)
                    continue;

                const char *name = struct2str(structType);
                if (!name) name = "unknown";

                if (resultCount >= resultCapacity) {
                    resultCapacity = resultCapacity == 0 ? 64 : resultCapacity * 2;
                    results = (FoundStructure *)realloc(results, resultCapacity * sizeof(FoundStructure));
                }

                results[resultCount].name = name;
                results[resultCount].x = pos.x;
                results[resultCount].z = pos.z;
                results[resultCount].distance = distance;
                resultCount++;
            }
        }
    }

    /* Sort by distance */
    if (resultCount > 1)
        qsort(results, resultCount, sizeof(FoundStructure), compareByDistance);

    /* Cap results */
    if (resultCount > maxResults)
        resultCount = maxResults;

    /* Build JSON string */
    /* Worst case: each entry ~80 chars, plus overhead */
    int jsonSize = 32 + resultCount * 100 + 1;
    char *json = (char *)malloc(jsonSize);
    int offset = 0;

    offset += sprintf(json + offset, "{\"results\": [");

    for (int i = 0; i < resultCount; i++) {
        if (i > 0)
            offset += sprintf(json + offset, ", ");

        offset += sprintf(json + offset,
            "{\"name\": \"%s\", \"x\": %d, \"z\": %d, \"distance\": %.1f}",
            results[i].name, results[i].x, results[i].z, results[i].distance);
    }

    offset += sprintf(json + offset, "]}");

    free(results);
    return json;
}

SEEDFINDER_API char *seedfinder_scan(
    uint64_t seed,
    double playerX, double playerZ,
    int radius, int maxResults,
    const int *types, int numTypes)
{
    return scan_impl(seed, playerX, playerZ, radius, maxResults,
                     types, numTypes, SCAN_EDITION_BEDROCK, NULL);
}

SEEDFINDER_API char *seedfinder_scan_java(
    uint64_t seed,
    double playerX, double playerZ,
    int radius, int maxResults,
    const int *types, int numTypes,
    const char *mcLabel)
{
    return scan_impl(seed, playerX, playerZ, radius, maxResults,
                     types, numTypes, SCAN_EDITION_JAVA, mcLabel);
}

SEEDFINDER_API void seedfinder_free_result(char *result)
{
    free(result);
}

/* ---- Mapa de biomas (WASM) ---------------------------------------------- */

/* Grade de biomas do Overworld em MC_NEWEST (biomas idênticos Java/Bedrock;
 * só as estruturas diferem). Preenche out[0 .. sx*sz) row-major
 * (out[j*sx + i]); cada célula cobre `scale` blocos a partir de
 * (x0 + i*scale, z0 + j*scale).
 *  x0,z0: canto NO em BLOCOS, múltiplos de scale
 *  scale: 4 | 16 | 64 | 256   y: altura em unidades 1:4 (79 = 319>>2)
 * Retorna 0 ok, -1 args inválidos, -2 alloc, -3 genBiomes. */
SEEDFINDER_API int seedfinder_biome_grid(uint64_t seed, int x0, int z0,
    int sx, int sz, int scale, int y, int *out)
{
    if (!out || sx <= 0 || sz <= 0 || sx > 4096 || sz > 4096)
        return -1;
    if ((long long) sx * (long long) sz > 4000000)
        return -1;
    if (scale != 4 && scale != 16 && scale != 64 && scale != 256)
        return -1;
    if (x0 % scale != 0 || z0 % scale != 0) /* divisão exata também p/ negativos */
        return -1;

    Generator g;
    setupGenerator(&g, MC_NEWEST, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    Range r = { scale, x0 / scale, z0 / scale, sx, sz, y, 1 };
    int *cache = allocCache(&g, r);
    if (!cache)
        return -2;
    int rc = genBiomes(&g, cache, r);
    if (rc == 0)
        memcpy(out, cache, (size_t) sx * (size_t) sz * sizeof(int));
    free(cache);
    return rc == 0 ? 0 : -3;
}

/* Paleta de biomas: cores do initBiomeColors (AMIDST) + nomes do biome2str.
 * JSON {"colors":["#rrggbb", x256],"names":{"0":"ocean",...}} — ponteiro
 * malloc'd, liberar com seedfinder_free_result (NULL em OOM).
 * Cap aritmético (house style): 256*(#rrggbb,) + 256*(123:"nome",) + envelope. */
SEEDFINDER_API char *seedfinder_biome_palette(void)
{
    unsigned char colors[256][3];
    initBiomeColors(colors);
    size_t cap = 64 + 256 * 10 + 256 * 48;
    char *buf = (char *) malloc(cap);
    if (!buf)
        return NULL;
    char *p = buf;
    p += sprintf(p, "{\"colors\":[");
    for (int i = 0; i < 256; i++)
        p += sprintf(p, "%s\"#%02x%02x%02x\"", i ? "," : "",
                     colors[i][0], colors[i][1], colors[i][2]);
    p += sprintf(p, "],\"names\":{");
    int first = 1;
    for (int id = 0; id < 256; id++) {
        const char *nm = biome2str(MC_NEWEST, id);
        if (!nm)
            continue;
        p += sprintf(p, "%s\"%d\":\"%s\"", first ? "" : ",", id, nm);
        first = 0;
    }
    sprintf(p, "}}");
    return buf;
}

/* ============================ SeedCracker ============================ */

#include "platform_threads.h"

SEEDFINDER_API char *seedfinder_crack(
    const int *types, int numTypes,
    const double *xBlocks, const double *zBlocks,
    int tolerance,
    uint64_t startSeed, uint64_t endSeed,
    int maxResults, double timeBudgetSec, int numThreads)
{
    CrackTarget targets[CRACK_MAX_STRUCTURES];
    int n = 0;
    char *err = NULL;
    if (crackTargetsBuild(types, numTypes, xBlocks, zBlocks, tolerance,
                          targets, &n, &err))
        return err;
    if (maxResults <= 0 || maxResults > 2000)
        return strdup("{\"error\":\"maxResults must be between 1 and 2000\"}");
    if (numThreads < 1 || numThreads > 64)
        numThreads = 8;
    if (endSeed > CRACK_SEED_SPACE)
        endSeed = CRACK_SEED_SPACE;
    if (startSeed >= endSeed)
        return strdup("{\"error\":\"empty seed range\"}");

    /* Keep an input-ordered copy for serializing matches back to the caller. */
    CrackTarget orig[CRACK_MAX_STRUCTURES];
    memcpy(orig, targets, (size_t)n * sizeof(CrackTarget));

    /* Strongest filter first. */
    qsort(targets, n, sizeof(CrackTarget), crackTargetCompare);

#if defined(__EMSCRIPTEN__)
    /* Sem -pthread o pthread_create aborta em runtime; o JS paraleliza
       via Web Workers (uma fatia [startSeed,endSeed) por worker). */
    numThreads = 1;
#endif
    if ((endSeed - startSeed) < (uint64_t)numThreads)
        numThreads = 1;

    double deadline = (timeBudgetSec > 0.0) ? nowms_s() + timeBudgetSec * 1000.0 : 0.0;
    CrackHit *all = NULL;
    int c = 0;
    uint64_t checked = 0;
    int timedOut = 0;
    if (crackSweepRanked(targets, n, startSeed, endSeed, maxResults, deadline,
                         numThreads, &all, &c, &checked, &timedOut) != 0)
        return strdup("{\"error\":\"out of memory\"}");

    /* Serialize: recompute per-structure match chunk for each winner. Only
     * seeds whose matches are biome-viable are kept — a match the RNG would
     * place but the game never generates (wrong biome) is a false positive
     * (e.g. seed 254568). Score is recomputed from the viable matches. */
    int *m = malloc(((size_t)c > 0 ? (size_t)c : 1) * (size_t)n * 2 * sizeof(int));
    int kept = 0;
    for (int i = 0; i < c; i++) {
        int64_t viableScore = 0;
        int *mm = m + (size_t)kept * (size_t)n * 2;
        if (!crackValidateSeed(orig, n, all[i].seed, mm, &viableScore))
            continue;
        all[kept].seed = all[i].seed;
        all[kept].score = viableScore;
        kept++;
    }
    c = kept;

    /* Re-rank survivors by their recomputed (viable) score. */
    for (int i = 1; i < c; i++) {
        CrackHit key = all[i];
        int kmrow[CRACK_MAX_STRUCTURES * 2];
        const int *src = m + (size_t)i * (size_t)n * 2;
        for (int k = 0; k < n * 2; k++) kmrow[k] = src[k];
        int j = i - 1;
        while (j >= 0 && all[j].score > key.score) {
            all[j + 1] = all[j];
            for (int k = 0; k < n * 2; k++)
                m[(size_t)(j + 1) * n * 2 + k] = m[(size_t)j * n * 2 + k];
            j--;
        }
        all[j + 1] = key;
        for (int k = 0; k < n * 2; k++)
            m[(size_t)(j + 1) * n * 2 + k] = kmrow[k];
    }

    size_t cap = 64 + (size_t)c * ((size_t)n * 32 + 80) + 64;
    char *buf = malloc(cap);
    int off = sprintf(buf, "{\"results\":[");
    for (int i = 0; i < c; i++) {
        off += sprintf(buf + off, "%s{\"seed\":%llu,\"score\":%lld,\"matches\":[",
                       i ? "," : "", (unsigned long long)all[i].seed,
                       (long long)all[i].score);
        const int *mm = m + (size_t)i * (size_t)n * 2;
        for (int k = 0; k < n; k++) {
            off += sprintf(buf + off, "%s[%d,%d]", k ? "," : "",
                           mm[k * 2], mm[k * 2 + 1]);
        }
        off += sprintf(buf + off, "]}");
    }
    off += sprintf(buf + off,
                   "],\"checked\":%llu,\"timed_out\":%s,\"threads\":%d}",
                   (unsigned long long)checked,
                   timedOut ? "true" : "false", numThreads);

    free(m);
    free(all);
    return buf;
}

/* ============================ SeedCracker 64-bit ============================
 * Thin export shim: the whole pipeline lives in core/crack64.c (it needs the
 * shared structureIsViable defined above). The real symbol is kept non-static
 * so the C test binary (which links this TU) can call it directly too. */

SEEDFINDER_API char *seedfinder_crack64_shim(
    const int *mtTypes, const double *mtX, const double *mtZ, int nMt,
    const int *jTypes, const double *jX, const double *jZ, int nJava,
    int tolerance, uint64_t startSeed, uint64_t endSeed,
    int maxResults, double budgetSec, int numThreads)
{
    return seedfinder_crack64(mtTypes, mtX, mtZ, nMt, jTypes, jX, jZ, nJava,
                              tolerance, startSeed, endSeed, maxResults,
                              budgetSec, numThreads);
}