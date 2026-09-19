"""ctypes smoke test for seedfinder_crack. Uses the repo DLL."""
import ctypes
import json
import sys
import time

DLL = r"build_server\seedfinder_lib.dll"


def crack(lib, structures, tolerance=0, end=16_000_000, budget=120.0, threads=8):
    n = len(structures)
    types = (ctypes.c_int * n)(*(s["type"] for s in structures))
    xs = (ctypes.c_double * n)(*(float(s["x"]) for s in structures))
    zs = (ctypes.c_double * n)(*(float(s["z"]) for s in structures))
    lib.seedfinder_crack.argtypes = [
        ctypes.POINTER(ctypes.c_int), ctypes.c_int,
        ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double),
        ctypes.c_int, ctypes.c_uint64, ctypes.c_uint64,
        ctypes.c_int, ctypes.c_double, ctypes.c_int,
    ]
    lib.seedfinder_crack.restype = ctypes.c_void_p
    lib.seedfinder_free_result.argtypes = [ctypes.c_void_p]
    lib.seedfinder_free_result.restype = None
    p = lib.seedfinder_crack(types, n, xs, zs, tolerance, 0, end, 2000, budget, threads)
    if not p:
        raise RuntimeError("seedfinder_crack returned NULL")
    try:
        return json.loads(ctypes.string_at(p).decode("utf-8"))
    finally:
        lib.seedfinder_free_result(p)


FIXTURE_8675309 = [
    {"type": 5, "x": -280, "z": 152},
    {"type": 5, "x": -280, "z": -360},
    {"type": 8, "x": 696, "z": 360},
    {"type": 8, "x": 712, "z": 760},
]

# (712,-520) foi removida: era um falso positivo do gate Java antigo (bioma na
# propria celula invalido), nao uma vila real do seed 31415. Substituida por
# (-1448,-264), vila real confirmada pelo gate Bedrock.
FIXTURE_31415 = [
    {"type": 5, "x": -360, "z": -840},
    {"type": 5, "x": 168, "z": 1176},
    {"type": 5, "x": 136, "z": -1352},
    {"type": 5, "x": -1448, "z": -264},
]

# Fictitious structures from the WASM false-positive report: the cracker
# matched pure RNG positions for seed 254568 that never generate in-game.
USER_STRUCTURES = [
    {"type": 4, "x": 136, "z": 136},
    {"type": 14, "x": 264, "z": -104},
    {"type": 1, "x": -408, "z": -184},
    {"type": 11, "x": -424, "z": 152},
    {"type": 13, "x": -248, "z": -664},
]
KNOWN_FALSE_SEED = 254568


def scan(lib, seed, type_id, block_x, block_z, radius=1):
    """seedfinder_scan wrapper; returns the result list."""
    types = (ctypes.c_int * 1)(type_id)
    lib.seedfinder_scan.argtypes = [
        ctypes.c_uint64, ctypes.c_double, ctypes.c_double,
        ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.c_int,
    ]
    lib.seedfinder_scan.restype = ctypes.c_void_p
    p = lib.seedfinder_scan(seed, block_x, block_z, radius, 20, types, 1)
    if not p:
        raise RuntimeError("seedfinder_scan returned NULL")
    try:
        return json.loads(ctypes.string_at(p).decode("utf-8"))["results"]
    finally:
        lib.seedfinder_free_result(p)


def verify_matches_viable(lib, seed, structures, matches):
    """Every crack match (chunk coords) must be a real, biome-viable structure."""
    for s, (cx, cz) in zip(structures, matches):
        bx, bz = cx * 16 + 8, cz * 16 + 8
        found = scan(lib, seed, s["type"], bx, bz)
        assert found, (
            f"seed {seed}: type {s['type']} match chunk ({cx},{cz}) "
            f"block ({bx},{bz}) does not generate (no scan hit)")


def test_no_false_positives():
    lib = ctypes.CDLL(DLL)
    data = crack(lib, USER_STRUCTURES, tolerance=6, end=16_000_000, budget=120)
    print(f"USER fixture: checked={data['checked']} timed_out={data['timed_out']} "
          f"found={len(data['results'])}")
    seeds = [r["seed"] for r in data["results"]]
    print("  seeds:", seeds[:12])
    assert KNOWN_FALSE_SEED not in seeds, (
        f"false-positive seed {KNOWN_FALSE_SEED} returned: {seeds[:20]}")
    for r in data["results"]:
        verify_matches_viable(lib, r["seed"], USER_STRUCTURES, r["matches"])
    print("  OK: no false positives; every returned match generates in-game")


def main():
    lib = ctypes.CDLL(DLL)
    for label, structs in (("8675309", FIXTURE_8675309), ("31415", FIXTURE_31415)):
        for end in (16_000_000,):
            t0 = time.monotonic()
            data = crack(lib, structs, tolerance=0, end=end, budget=120)
            seeds = [r["seed"] for r in data["results"]]
            print(f"{label}: checked={data['checked']} timed_out={data['timed_out']} "
                  f"found={len(seeds)} in {time.monotonic()-t0:.1f}s")
            print("  seeds:", seeds[:12])
            expect = int(label)
            assert expect in seeds, f"seed {label} not found: {seeds[:20]}"
            print(f"  OK: seed {label} recovered")
    test_no_false_positives()
    print("ALL C SMOKE TESTS PASSED")


if __name__ == "__main__":
    sys.exit(main())