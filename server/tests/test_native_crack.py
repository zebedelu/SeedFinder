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

FIXTURE_31415 = [
    {"type": 5, "x": 712, "z": -520},
    {"type": 5, "x": -360, "z": -840},
    {"type": 5, "x": 168, "z": 1176},
    {"type": 5, "x": 136, "z": -1352},
]


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
    print("ALL C SMOKE TESTS PASSED")


if __name__ == "__main__":
    sys.exit(main())