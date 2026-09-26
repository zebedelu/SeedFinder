"""ctypes test for seedfinder_crack_auto_sweep (placement-only ranked sweep).

Regression do defeito 2: o gate de bioma do seedfinder_crack descarta seeds
64-bit reais; o auto_sweep roda so o placement e deve achar o lo32 5309
(4294972605 & 0xFFFFFFFF) a partir das 4 estruturas MT compostas do
fixtures_crack64.json. Rode da raiz do repo:
    python server/tests/test_crack_auto.py
"""
import ctypes
import json
import os
import sys

DLL = r"build_server\seedfinder_lib.dll"
FIXTURE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "fixtures_crack64.json")
LO32_SEED = 5309
TOLERANCE = 6
END = 16_000_000


def load_mt_structures():
    """mt_structures + as entradas nao-duplicadas de all_data.extra_mt."""
    with open(FIXTURE_PATH, encoding="utf-8") as f:
        fx = json.load(f)
    mt = [dict(s) for s in fx["mt_structures"]]
    for e in fx["all_data"]["extra_mt"]:
        s = {"type": e["cubiomes_type"], "x": e["x"], "z": e["z"]}
        if s not in mt:
            mt.append(s)
    assert len(mt) == 4, f"expected 4 composite MT structures, got {mt}"
    return mt


def sweep(lib, structures, tolerance=TOLERANCE, start=0, end=END,
          max_results=2000, budget=0.0, threads=8):
    n = len(structures)
    types = (ctypes.c_int * n)(*(s["type"] for s in structures))
    xs = (ctypes.c_double * n)(*(float(s["x"]) for s in structures))
    zs = (ctypes.c_double * n)(*(float(s["z"]) for s in structures))
    lib.seedfinder_crack_auto_sweep.argtypes = [
        ctypes.POINTER(ctypes.c_int), ctypes.c_int,
        ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double),
        ctypes.c_int, ctypes.c_uint64, ctypes.c_uint64,
        ctypes.c_int, ctypes.c_double, ctypes.c_int,
    ]
    lib.seedfinder_crack_auto_sweep.restype = ctypes.c_void_p
    lib.seedfinder_free_result.argtypes = [ctypes.c_void_p]
    lib.seedfinder_free_result.restype = None
    p = lib.seedfinder_crack_auto_sweep(types, n, xs, zs, tolerance, start, end,
                                        max_results, budget, threads)
    if not p:
        raise RuntimeError("seedfinder_crack_auto_sweep returned NULL")
    try:
        return json.loads(ctypes.string_at(p).decode("utf-8"))
    finally:
        lib.seedfinder_free_result(p)


def test_sweep_finds_lo32_without_biome_gate(lib, mt):
    data = sweep(lib, mt, tolerance=TOLERANCE, start=0, end=END, max_results=2000)
    assert "error" not in data, data
    hit = next((r for r in data["results"] if r["seed"] == LO32_SEED), None)
    assert hit is not None, (
        f"seed {LO32_SEED} missing from top results "
        f"({[r['seed'] for r in data['results'][:20]]}, "
        f"checked={data['checked']}, timed_out={data['timed_out']})")
    assert hit["score"] <= 36, f"score {hit['score']} > tolerance^2"
    print(f"  lo32 {LO32_SEED} score={hit['score']} "
          f"checked={data['checked']} threads={data['threads']}")


def test_sweep_sloppy_coords(lib, mt):
    # Review Focus #1: +2 chunks em X, -2 chunks em Z (coordenadas imprecisas
    # do usuario) nao podem empurrar a seed verdadeira fora do top-2000.
    sloppy = [{"type": s["type"], "x": s["x"] + 32, "z": s["z"] - 32} for s in mt]
    data = sweep(lib, sloppy, tolerance=TOLERANCE, start=0, end=END, max_results=2000)
    assert "error" not in data, data
    seeds = [r["seed"] for r in data["results"]]
    assert LO32_SEED in seeds, (
        f"seed {LO32_SEED} missing with sloppy coords "
        f"({seeds[:20]}, checked={data['checked']})")
    print(f"  sloppy coords: lo32 {LO32_SEED} present "
          f"({len(seeds)} results, checked={data['checked']})")


def test_sweep_sorted_and_capped(lib, mt):
    data = sweep(lib, mt, tolerance=TOLERANCE, start=0, end=END, max_results=50)
    assert "error" not in data, data
    scores = [r["score"] for r in data["results"]]
    assert len(scores) > 0, "expected at least one hit"
    assert scores == sorted(scores), f"results not sorted by score: {scores[:20]}"
    assert len(scores) <= 50, f"cap violated: {len(scores)} > 50"
    print(f"  sorted+cap: {len(scores)} results, score range "
          f"{scores[0]}..{scores[-1]}")


def test_sweep_validation(lib, mt):
    data = sweep(lib, mt[:3])
    assert "error" in data and "at least 4 structures" in data["error"], data

    ms = [dict(s) for s in mt]
    ms[0] = {"type": 15, "x": 0, "z": 0}
    data = sweep(lib, ms)
    assert "error" in data and "Mineshaft" in data["error"], data

    for t in (23, 24):
        bad = [dict(s) for s in mt]
        bad[0] = {"type": t, "x": 0, "z": 0}
        data = sweep(lib, bad)
        assert data.get("error") == (
            "Trail Ruins and Trial Chambers cannot be swept - "
            "pass them as Java anchors"), data

    data = sweep(lib, mt, start=1000, end=1000)
    assert "error" in data and "empty seed range" in data["error"], data
    print("  validation errors OK")


def main():
    lib = ctypes.CDLL(DLL)
    mt = load_mt_structures()
    print("MT structures:", mt)
    test_sweep_finds_lo32_without_biome_gate(lib, mt)
    test_sweep_sloppy_coords(lib, mt)
    test_sweep_sorted_and_capped(lib, mt)
    test_sweep_validation(lib, mt)
    print("ALL AUTO SWEEP TESTS PASSED")


if __name__ == "__main__":
    sys.exit(main())
