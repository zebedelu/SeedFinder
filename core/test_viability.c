/* Teste unitario da tabela de biomas Bedrock (core/viability.c).
 *
 * Build (raiz do repo):
 *   & C:\msys64\ucrt64\bin\gcc.exe -O2 -I. -IChunkBiomesGUI\cubiomes ^
 *       core\viability.c core\test_viability.c ^
 *       build_server\libcubiomes_static.a -o build_server\test_viability.exe -lm
 * Roda com: build_server\test_viability.exe
 */
#include "viability.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"   /* StructureType, isViableFeatureBiome */
#include "ChunkBiomesGUI/cubiomes/biomes.h"    /* enums de bioma, isOceanic/isDeepOcean */
#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* Village: os 5 biomas base + variantes do Chunkbase. */
    assert(bedrockViableBiome(Village, plains));
    assert(bedrockViableBiome(Village, desert));
    assert(bedrockViableBiome(Village, savanna));
    assert(bedrockViableBiome(Village, taiga));
    assert(bedrockViableBiome(Village, snowy_tundra));   /* == snowy_plains */
    assert(bedrockViableBiome(Village, meadow));
    assert(bedrockViableBiome(Village, snowy_taiga));

    /* Village: rejeita os biomas dos 3 falsos positivos do seed 6666. */
    assert(!bedrockViableBiome(Village, river));
    assert(!bedrockViableBiome(Village, frozen_river));
    assert(!bedrockViableBiome(Village, ocean));
    assert(!bedrockViableBiome(Village, beach));
    assert(!bedrockViableBiome(Village, forest));

    /* Monument so' em oceano profundo; Shipwreck/Ocean_Ruin em oceano/praia. */
    assert(bedrockViableBiome(Monument, deep_ocean));
    assert(!bedrockViableBiome(Monument, ocean));
    assert(bedrockViableBiome(Ocean_Ruin, lukewarm_ocean));
    assert(bedrockViableBiome(Shipwreck, beach));
    assert(!bedrockViableBiome(Shipwreck, river));

    /* bio invalido nunca e' viavel. */
    assert(!bedrockViableBiome(Village, -1));

    printf("bedrockViableBiome OK\n");
    return 0;
}
