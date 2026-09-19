#ifndef SEEDFINDER_VIABILITY_H_
#define SEEDFINDER_VIABILITY_H_

/* Viabilidade de bioma Bedrock por tipo de estrutura.
 * Retorna 1 se `bio` (id de bioma do cubiomes) e' valido para `structureType`
 * na celula da propria estrutura; 0 caso contrario (inclusive bio < 0). */
int bedrockViableBiome(int structureType, int bio);

#endif
