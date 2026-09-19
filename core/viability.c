/* core/viability.c — tabela de biomas Bedrock por tipo de estrutura.
 *
 * O Bedrock confere o bioma na celula da propria estrutura (centro do chunk),
 * nao no canto da bounding box escolhido pelo LCG Java. Os conjuntos espelham
 * o `validBiome` do app de referencia ChunkBiomesGUI/ChunkBiomesGUI.cpp
 * (port do Chunkbase); os demais tipos caem na tabela do cubiomes.
 *
 * Mantido fora do wrapper para ser testavel isoladamente (ver
 * core/test_viability.c). */
#include "viability.h"
#include "ChunkBiomesGUI/cubiomes/biomes.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"

int bedrockViableBiome(int structureType, int bio)
{
    if (bio < 0)
        return 0;

    switch (structureType) {
    case Village:
        /* snowy_plains e' alias de snowy_tundra no cubiomes. */
        return bio == desert || bio == plains || bio == meadow ||
               bio == savanna || bio == snowy_plains || bio == taiga ||
               bio == snowy_taiga || bio == sunflower_plains;
    case Shipwreck:
        return bio == beach || bio == snowy_beach || isOceanic(bio);
    case Ocean_Ruin:
        return isOceanic(bio);
    case Monument:
        return isDeepOcean(bio);
    default:
        return isViableFeatureBiome(MC_NEWEST, structureType, bio);
    }
}
