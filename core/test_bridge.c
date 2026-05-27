// test_bridge.c — standalone test for the SeedFinderBridge Lua module
#include <lua.hpp>
#include "SeedFinderBridge.h"
#include <stdio.h>

int main() {
    printf("[test_bridge] Creating Lua state...\n");
    lua_State *L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "Failed to create Lua state\n");
        return 1;
    }
    luaL_openlibs(L);

    // Register the bridge
    printf("[test_bridge] Registering seedfinder_bridge...\n");
    seedfinder_bridge_register(L);

    // Verify the global exists
    lua_getglobal(L, "seedfinder_bridge");
    if (!lua_istable(L, -1)) {
        fprintf(stderr, "seedfinder_bridge global not found or not a table\n");
        lua_close(L);
        return 1;
    }
    printf("[test_bridge] seedfinder_bridge global registered OK\n");

    // Test 1: scan for villages near origin with seed 8675309
    printf("[test_bridge] Test 1: Scanning for villages (seed=8675309, radius=100 chunks)...\n");
    const char *test1 =
        "local results = seedfinder_bridge.scanStructures(8675309, 0, 0, 100, 20, {[5]=true})\n"
        "print('Found ' .. #results .. ' villages')\n"
        "for i, r in ipairs(results) do\n"
        "    print(string.format('  %d: %s at (%d, %d) distance=%.1f chunks', i, r.name, r.x, r.z, r.distance))\n"
        "end\n"
        "assert(#results > 0, 'Expected at least 1 village')\n"
    ;

    int err = luaL_dostring(L, test1);
    if (err) {
        fprintf(stderr, "Test 1 Lua error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    printf("[test_bridge] Test 1 passed!\n\n");

    // Test 2: multi-type scan (Desert Pyramid, Village, Outpost, Ruined Portal)
    printf("[test_bridge] Test 2: Multi-type scan (seed=8675309, radius=50 chunks)...\n");
    const char *test2 =
        "local results = seedfinder_bridge.scanStructures(8675309, 0, 0, 50, 15, {[1]=true, [5]=true, [10]=true, [11]=true})\n"
        "print('Found ' .. #results .. ' structures (Desert Pyramid + Village + Outpost + Ruined Portal)')\n"
        "for i, r in ipairs(results) do\n"
        "    print(string.format('  %d: %s at (%d, %d) dist=%.1f', i, r.name, r.x, r.z, r.distance))\n"
        "end\n"
        "assert(#results > 0, 'Expected at least 1 structure')\n"
    ;

    err = luaL_dostring(L, test2);
    if (err) {
        fprintf(stderr, "Test 2 Lua error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    printf("[test_bridge] Test 2 passed!\n\n");

    // Test 3: empty results (very small radius)
    printf("[test_bridge] Test 3: Small radius scan (seed=8675309, radius=1 chunk)...\n");
    const char *test3 =
        "local results = seedfinder_bridge.scanStructures(8675309, 0, 0, 1, 10, {[5]=true})\n"
        "print('Found ' .. #results .. ' villages within 1 chunk')\n"
    ;

    err = luaL_dostring(L, test3);
    if (err) {
        fprintf(stderr, "Test 3 Lua error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    printf("[test_bridge] Test 3 passed!\n\n");

    printf("[test_bridge] All tests passed!\n");
    lua_close(L);
    return 0;
}
