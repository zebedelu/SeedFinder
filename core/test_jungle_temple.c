/* core/test_jungle_temple.c — regressao: bamboo jungle e' bioma valido. */
#include <assert.h>
#include <stdio.h>
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include "ChunkBiomesGUI/cubiomes/biomes.h"

int main(void)
{
    assert(isViableFeatureBiome(MC_NEWEST, Jungle_Pyramid, jungle));
    assert(isViableFeatureBiome(MC_NEWEST, Jungle_Pyramid, jungle_hills));
    assert(isViableFeatureBiome(MC_NEWEST, Jungle_Pyramid, bamboo_jungle));
    assert(isViableFeatureBiome(MC_NEWEST, Jungle_Pyramid, bamboo_jungle_hills));
    assert(!isViableFeatureBiome(MC_NEWEST, Jungle_Pyramid, desert));
    printf("jungle temple viability OK\n");
    return 0;
}
