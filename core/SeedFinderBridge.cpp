#include "SeedFinderBridge.h"
#include "ChunkBiomesGUI/Bfinders.h"
#include "ChunkBiomesGUI/cubiomes/generator.h"
#include "ChunkBiomesGUI/cubiomes/finders.h"
#include "ChunkBiomesGUI/cubiomes/biomes.h"
#include "ChunkBiomesGUI/cubiomes/util.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

// Structure to hold scan results
struct FoundStructure {
    const char* name;
    int x;
    int z;
    double distance;
};

// Floor division helper — C integer division truncates toward zero,
// but Minecraft uses floor division for block-to-chunk conversion.
static inline int floorDiv(int a, int b) {
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

// The core scan function — mirrors Scanner:tick() logic but in C
static int l_scanStructures(lua_State *L) {
    // Argument validation: seed, playerX, playerZ, radius, maxResults, structureTypes (table)
    uint64_t seed = (uint64_t)luaL_checkinteger(L, 1);
    double playerX = luaL_checknumber(L, 2);
    double playerZ = luaL_checknumber(L, 3);
    int radius = (int)luaL_checkinteger(L, 4);
    int maxResults = (int)luaL_checkinteger(L, 5);

    // structureTypes is a table of integer structure type IDs
    luaL_checktype(L, 6, LUA_TTABLE);

    // Collect enabled structure types
    std::vector<int> enabledStructures;
    lua_pushnil(L);
    while (lua_next(L, 6) != 0) {
        if (lua_toboolean(L, -1)) {
            enabledStructures.push_back((int)lua_tointeger(L, -2));
        }
        lua_pop(L, 1); // pop value, keep key
    }

    // Set up biome generator for viability checks
    Generator g;
    setupGenerator(&g, MC_NEWEST, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    // Calculate player chunk using floor division (Minecraft convention)
    // floor(x / 16.0) is correct for negative coordinates too
    int playerChunkX = (int)std::floor(playerX / 16.0);
    int playerChunkZ = (int)std::floor(playerZ / 16.0);

    // Collect results
    std::vector<FoundStructure> results;
    StructureConfig sconf;
    Pos pos;

    for (int structType : enabledStructures) {
        if (!getBedrockStructureConfig(structType, MC_NEWEST, &sconf))
            continue;

        int regionSize = sconf.regionSize;
        if (regionSize <= 0)
            continue;

        // Use floor division for region bounds to handle negative coordinates correctly
        int regionMinX = floorDiv(playerChunkX - radius, regionSize) - 1;
        int regionMaxX = floorDiv(playerChunkX + radius, regionSize) + 1;
        int regionMinZ = floorDiv(playerChunkZ - radius, regionSize) - 1;
        int regionMaxZ = floorDiv(playerChunkZ + radius, regionSize) + 1;

        for (int regX = regionMinX; regX <= regionMaxX; regX++) {
            for (int regZ = regionMinZ; regZ <= regionMaxZ; regZ++) {
                if (!getBedrockStructurePos(structType, MC_NEWEST, seed, regX, regZ, &pos))
                    continue;

                int blockX = pos.x;
                int blockZ = pos.z;

                // Biome viability check — filters out structures in incompatible biomes
                if (!isViableBedrockStructurePos(structType, &g, blockX, blockZ, 0))
                    continue;

                double dx = blockX - playerX;
                double dz = blockZ - playerZ;
                double dist = std::sqrt(dx * dx + dz * dz);
                double chunkDist = dist / 16.0;

                if (chunkDist <= radius) {
                    const char* name = struct2str(structType);
                    // Guard against NULL from struct2str for unknown types
                    if (!name) name = "unknown";
                    results.push_back({name, blockX, blockZ, chunkDist});
                }
            }
        }
    }

    // Sort by distance
    std::sort(results.begin(), results.end(),
        [](const FoundStructure& a, const FoundStructure& b) {
            return a.distance < b.distance;
        });

    // Cap results
    if ((int)results.size() > maxResults) {
        results.resize(maxResults);
    }

    // Return as Lua table: array of {name, x, z, distance}
    lua_createtable(L, (int)results.size(), 0);
    for (int i = 0; i < (int)results.size(); i++) {
        lua_createtable(L, 0, 4);
        lua_pushstring(L, results[i].name);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, results[i].x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, results[i].z);
        lua_setfield(L, -2, "z");
        lua_pushnumber(L, results[i].distance);
        lua_setfield(L, -2, "distance");
        lua_seti(L, -2, i + 1); // Lua arrays are 1-indexed
    }

    return 1; // one return value (the table)
}

// Register the seedfinder_bridge global
int seedfinder_bridge_register(lua_State *L) {
    lua_createtable(L, 0, 1);

    lua_pushcfunction(L, l_scanStructures);
    lua_setfield(L, -2, "scanStructures");

    lua_setglobal(L, "seedfinder_bridge");
    return 0;
}
