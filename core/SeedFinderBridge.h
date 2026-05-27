#ifndef SEEDFINDER_BRIDGE_H_
#define SEEDFINDER_BRIDGE_H_

#include <lua.hpp>

#ifdef __cplusplus
extern "C" {
#endif

// Call this once during Flarial DLL initialization to inject the
// "seedfinder_bridge" global table into the Lua state.
// After registration, Lua scripts can call:
// seedfinder_bridge.scanStructures(seed, playerX, playerZ, radius, maxResults, structureTypes)
// which returns a table of {name, x, z, distance} results.
int seedfinder_bridge_register(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif // SEEDFINDER_BRIDGE_H_
