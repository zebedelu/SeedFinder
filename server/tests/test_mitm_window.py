"""MITM windowing + candidate guard.

Run with:  python server/tests/test_mitm_window.py

NOTE: the returned char* is malloc'd by the C engine; we MUST bind the export as
c_void_p and free the raw pointer via seedfinder_free_result. Binding as
c_char_p turns the return into a Python bytes and the later free() would be a
bogus pointer (heap corruption). Same discipline as server/app/native.py.
"""
import ctypes
import json
import os
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
lib = ctypes.CDLL(os.path.join(ROOT, "build_server", "seedfinder_lib.dll"))

lib.seedfinder_crack_java.restype = ctypes.c_void_p
lib.seedfinder_crack_java.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
                                      ctypes.c_void_p, ctypes.c_int, ctypes.c_uint64,
                                      ctypes.c_uint64, ctypes.c_int, ctypes.c_double, ctypes.c_int]
lib.seedfinder_free_result.argtypes = [ctypes.c_void_p]
lib.seedfinder_free_result.restype = None

HAS_WINDOW = hasattr(lib, "seedfinder_crack_mitm_window")
if HAS_WINDOW:
    lib.seedfinder_crack_mitm_window.restype = ctypes.c_void_p
    lib.seedfinder_crack_mitm_window.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
                                                 ctypes.c_void_p, ctypes.c_int, ctypes.c_double,
                                                 ctypes.c_int, ctypes.c_uint64, ctypes.c_uint64]

DEFAULT_T = [1, 2, 3, 4, 6, 7, 10]
DEFAULT_X = [224., 2704., -2240., 368., -288., -224., 1696.]
DEFAULT_Z = [96., -928., 1296., -2256., 64., 32., 800.]
ROUND1_T = [6, 7, 24, 11, 6, 7, 6]
ROUND1_X = [176., 64., 248., 224., 112., 64., 464.]
ROUND1_Z = [1376., 1312., 1224., 1552., 1648., 1648., 1600.]


def _call(fn, *args):
    p = fn(*args)
    try:
        return ctypes.string_at(p).decode("utf-8") if p else "<null>"
    finally:
        if p:
            lib.seedfinder_free_result(p)


def crack_java(types, xs, zs, start, end, budget=300):
    n = len(types)
    T = (ctypes.c_int * n)(*types); X = (ctypes.c_double * n)(*xs); Z = (ctypes.c_double * n)(*zs)
    t0 = time.perf_counter()
    body = _call(lib.seedfinder_crack_java, T, n, X, Z, 0, start, end, 50, budget, 8)
    return time.perf_counter() - t0, body


def mitm_window(types, xs, zs, startU, endU, budget=300):
    n = len(types)
    T = (ctypes.c_int * n)(*types); X = (ctypes.c_double * n)(*xs); Z = (ctypes.c_double * n)(*zs)
    body = _call(lib.seedfinder_crack_mitm_window, T, n, X, Z, 50, budget, 1, startU, endU)
    return body


if __name__ == "__main__":
    if not HAS_WINDOW:
        print("FAIL: seedfinder_crack_mitm_window not exported (run before Task 2 impl)")
        raise SystemExit(1)

    # Timings depend on the machine (this laptop is ~5x slower than the
    # diagnosis box: full default sweep ~265-305s). Use budgets that complete
    # and generous ceilings; the substAntive checks are the invariants.
    FULL = 600
    HALF = 300

    # 1. full-range DEVE retornar a seed 123456 (nao trava/nao crasha)
    dt, body = crack_java(DEFAULT_T, DEFAULT_X, DEFAULT_Z, 0, 1 << 48, budget=FULL)
    assert dt < FULL + 120, f"default travou: {dt:.1f}s"
    assert '"seed"' in body and "123456" in body, body[:300]
    sf = {s["seed"] for s in json.loads(body)["results"]}
    print(f"[ok] default full-range {dt:.1f}s ({len(sf)} seeds): {body[:100]}")

    # 2. janela: duas metades == full-range (disjuntas, mesma uniao)
    a = mitm_window(DEFAULT_T, DEFAULT_X, DEFAULT_Z, 0, 1 << 23, budget=HALF)
    b = mitm_window(DEFAULT_T, DEFAULT_X, DEFAULT_Z, 1 << 23, 1 << 24, budget=HALF)
    sa = {s["seed"] for s in json.loads(a)["results"]}
    sb = {s["seed"] for s in json.loads(b)["results"]}
    assert sa and sa.isdisjoint(sb), "janelas se sobrepõem"
    assert sf == sa | sb, (sf, sa | sb)
    print(f"[ok] window split == full-range: {sf}")

    # 3. ROUND1 (set colhido do /scan, seed > 2^32): guard/early-stop impede hang
    dt, body = crack_java(ROUND1_T, ROUND1_X, ROUND1_Z, 0, 1 << 48, budget=HALF)
    assert dt < HALF + 120, f"ROUND1 hangou: {dt:.1f}s"
    print(f"[ok] round1 retornou {dt:.1f}s: {body[:160]}")
    print("ALL MITM WINDOW TESTS OK")
