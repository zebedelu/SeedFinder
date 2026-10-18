"""Benchmark nativo x WASM do motor SeedCracker (F0).

Uso (da raiz do repo):
    python server/bench.py                     # nativo, janela padrao 2^26
    python server/bench.py --repeat 5
    python server/bench.py --full              # espaco inteiro (lento)
    python server/bench.py --wasm              # inclui o build WASM via node
    python server/bench.py --json build_bench/out.json

Saida bruta em build_bench/ (gitignored via build_*/). As conclusoes vao para
server/BENCHMARK.md (versionado).
"""
import argparse
import ctypes
import json
import os
import platform
import statistics
import subprocess
import sys
import time

# Rodar `python server/bench.py` poe server/ no sys.path, nao a raiz do repo;
# sem isto o `from server import ...` falha (os testes ja fazem o mesmo insert).
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

from server import bench_fixtures  # noqa: E402

OUT_DIR = os.path.join(ROOT, "build_bench")
DEFAULT_WINDOW = 1 << 26
FULL_BUDGET = 3600.0


def dll_candidates():
    """Mesma ordem de server/app/native.py, mas sem depender do pacote do app
    (F0 tem de sobreviver ao split do F2)."""
    return [
        os.path.join(ROOT, "build_server", "seedfinder_lib.dll"),
        os.path.join(ROOT, "build_server", "seedfinder_lib.so"),
        os.path.join(ROOT, "build_server", "libseedfinder_lib.so"),
    ]


def find_lib():
    for path in dll_candidates():
        if os.path.isfile(path):
            return path
    raise SystemExit("DLL nao encontrada. Construa com server\\start.bat (ou server/start.sh).")


_CRACK_ARGTYPES = [
    ctypes.POINTER(ctypes.c_int),    # structure type ids
    ctypes.c_int,                    # number of structures
    ctypes.POINTER(ctypes.c_double), # x block coordinates
    ctypes.POINTER(ctypes.c_double), # z block coordinates
    ctypes.c_int,                    # tolerance in chunks
    ctypes.c_uint64,                 # start seed (inclusive)
    ctypes.c_uint64,                 # end seed (exclusive)
    ctypes.c_int,                    # max results
    ctypes.c_double,                 # time budget in seconds
    ctypes.c_int,                    # number of threads
]

_AUTO_SWEEP_ARGTYPES = [
    ctypes.POINTER(ctypes.c_int),    # structure type ids
    ctypes.c_int,                    # number of structures
    ctypes.POINTER(ctypes.c_double), # x block coordinates
    ctypes.POINTER(ctypes.c_double), # z block coordinates
    ctypes.POINTER(ctypes.c_int),    # tolerances (per structure - nao escalar)
    ctypes.c_uint64,                 # start seed (inclusive)
    ctypes.c_uint64,                 # end seed (exclusive)
    ctypes.c_int,                    # max results
    ctypes.c_double,                 # time budget in seconds
    ctypes.c_int,                    # number of threads
]

_AUTO_LIFT48_ARGTYPES = [
    ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_double),
    ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_int), ctypes.c_int,
    ctypes.POINTER(ctypes.c_uint64), ctypes.c_int,
    ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_double),
    ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_int), ctypes.c_int,
    ctypes.c_int,
]

_AUTO_LIFT63_ARGTYPES = [
    ctypes.POINTER(ctypes.c_uint64), ctypes.c_int,
    ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_double),
    ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_int), ctypes.c_int,
    ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_double),
    ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_int), ctypes.c_int,
    ctypes.c_int, ctypes.c_double,
]

_MITM_WINDOW_ARGTYPES = [
    ctypes.POINTER(ctypes.c_int), ctypes.c_int,
    ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double),
    ctypes.c_int, ctypes.c_double, ctypes.c_int,
    ctypes.c_uint64, ctypes.c_uint64,
]


def _bind(lib):
    """Amarra so os simbolos necessarios; mensagem clara se a lib for velha."""
    need = ["seedfinder_crack", "seedfinder_crack_auto_sweep",
            "seedfinder_crack_auto_lift48", "seedfinder_crack_auto_lift63",
            "seedfinder_crack_mitm_window", "seedfinder_free_result"]
    missing = [n for n in need if not hasattr(lib, n)]
    if missing:
        raise RuntimeError(f"lib sem os simbolos: {', '.join(missing)}")
    # argtypes/restype sao obrigatorios: sem eles o ctypes nao converte o
    # timeBudgetSec (float -> double) e trunca o ponteiro devolvido em 32 bits
    # (restype default = c_int). Assinaturas identicas a server/app/native.py.
    lib.seedfinder_free_result.argtypes = [ctypes.c_void_p]
    lib.seedfinder_free_result.restype = None
    lib.seedfinder_crack.argtypes = _CRACK_ARGTYPES
    lib.seedfinder_crack.restype = ctypes.c_void_p
    lib.seedfinder_crack_auto_sweep.argtypes = _AUTO_SWEEP_ARGTYPES
    lib.seedfinder_crack_auto_sweep.restype = ctypes.c_void_p
    lib.seedfinder_crack_auto_lift48.argtypes = _AUTO_LIFT48_ARGTYPES
    lib.seedfinder_crack_auto_lift48.restype = ctypes.c_void_p
    lib.seedfinder_crack_auto_lift63.argtypes = _AUTO_LIFT63_ARGTYPES
    lib.seedfinder_crack_auto_lift63.restype = ctypes.c_void_p
    lib.seedfinder_crack_mitm_window.argtypes = _MITM_WINDOW_ARGTYPES
    lib.seedfinder_crack_mitm_window.restype = ctypes.c_void_p
    return lib


def _call(fn, *args):
    ptr = fn(*args)
    if not ptr:
        raise RuntimeError(f"{fn.__name__} returned NULL")
    try:
        return json.loads(ctypes.string_at(ptr).decode("utf-8"))
    finally:
        lib_free(ptr)


_lib_handle = None


def lib_free(ptr):
    _lib_handle.seedfinder_free_result(ptr)


def _arrays(structures):
    n = len(structures)
    types = (ctypes.c_int * n)(*[s["type"] for s in structures])
    xs = (ctypes.c_double * n)(*[float(s["x"]) for s in structures])
    zs = (ctypes.c_double * n)(*[float(s["z"]) for s in structures])
    tols = (ctypes.c_int * n)(*[s.get("tolerance", 0) for s in structures])
    return n, types, xs, zs, tols


def rate(row):
    return row["checked"] / (row["elapsed_ms"] / 1000.0)


def extrapolate(row, space):
    if row.get("timed_out"):
        return None
    r = rate(row)
    if not r:
        # Sem trabalho medido (checked == 0) nao ha taxa: extrapolar seria
        # divisao por zero e um numero inventado.
        return None
    return space / r * 1000.0


def median_rows(rows):
    """Linha cuja taxa e' a mediana das taxas (empate -> menor tempo)."""
    rates = sorted(rate(r) for r in rows)
    med = rates[len(rates) // 2]
    return min(rows, key=lambda r: (abs(rate(r) - med), r["elapsed_ms"]))


def _row(runner, fixture, phase, unit, threads, data, elapsed_ms):
    timed_out = bool(data.get("timed_out"))
    return {
        "runner": runner, "fixture": fixture, "phase": phase, "unit": unit,
        "threads": threads, "checked": int(data.get("checked", 0)),
        "elapsed_ms": round(elapsed_ms, 3), "timed_out": timed_out,
        "valid": not timed_out,
    }


def _timed(fn, *args):
    t0 = time.perf_counter()
    data = _call(fn, *args)
    return data, (time.perf_counter() - t0) * 1000.0


def run_native(fixture, window, repeat, lib, full=False):
    """Roda o modo da fixture `repeat` vezes e devolve as linhas da mediana."""
    name = fixture["name"]
    threads = max(1, (os.cpu_count() or 4) - 1)
    rows = []

    if fixture["mode"] == "bedrock32":
        n, types, xs, zs, tols = _arrays(fixture["structures"])
        for _ in range(repeat):
            data, ms = _timed(lib.seedfinder_crack, types, n, xs, zs,
                              fixture["tolerance"], 0, window, 2000, FULL_BUDGET, threads)
            rows.append(_row("native", name, "crack32", "seeds", threads, data, ms))
        return [median_rows(rows)]

    if fixture["mode"] == "auto":
        mt = fixture["mt_structures"]
        jv = fixture["java_structures"]
        nmt, mt_types, mt_x, mt_z, mt_tol = _arrays(mt)
        nj, j_types, j_x, j_z, j_tol = _arrays(jv)
        best = None
        for _ in range(repeat):
            sweep, sweep_ms = _timed(lib.seedfinder_crack_auto_sweep,
                                     mt_types, nmt, mt_x, mt_z, mt_tol,
                                     0, window, 2000, FULL_BUDGET, threads)
            lo32 = [r["seed"] for r in sweep.get("results", [])[:2000]]
            p_lo = (ctypes.c_uint64 * max(1, len(lo32)))(*lo32)
            s48_data, s48_ms = _timed(lib.seedfinder_crack_auto_lift48,
                                      mt_types, mt_x, mt_z, mt_tol, nmt,
                                      p_lo, len(lo32),
                                      j_types, j_x, j_z, j_tol, nj, 2000)
            s48 = s48_data.get("s48", [])
            p_s48 = (ctypes.c_uint64 * max(1, len(s48)))(*s48)
            lift63, lift63_ms = _timed(lib.seedfinder_crack_auto_lift63,
                                       p_s48, len(s48),
                                       mt_types, mt_x, mt_z, mt_tol, nmt,
                                       j_types, j_x, j_z, j_tol, nj, 2000, FULL_BUDGET)
            run = [
                _row("native", name, "sweep", "seeds", threads, sweep, sweep_ms),
                _row("native", name, "lift48", "candidates", threads,
                     {"checked": len(lo32), "timed_out": False}, s48_ms),
                _row("native", name, "lift63", "hi", threads,
                     {"checked": lift63.get("checked", 0),
                      "timed_out": lift63.get("timed_out")}, lift63_ms),
            ]
            if best is None or rate(run[0]) > rate(best[0]):
                best = run
        return best

    if fixture["mode"] == "java_mitm":
        n, types, xs, zs, _ = _arrays(fixture["structures"])
        # Espaco de U e' [0, 2^24). --full mede o fim-a-fim real (a seed esta
        # em U=256, entao pode haver early-exit); o padrao janelado mede trabalho
        # uniforme numa janela isenta de acerto.
        start_u, end_u = ((0, 1 << 24) if full else fixture["u_timing"])
        for _ in range(repeat):
            data, ms = _timed(lib.seedfinder_crack_mitm_window,
                              types, n, xs, zs, 50, FULL_BUDGET, 1, start_u, end_u)
            rows.append(_row("native", name, "mitm", "u", 1,
                             {"checked": end_u - start_u,
                              "timed_out": data.get("timed_out")}, ms))
        return [median_rows(rows)]

    raise ValueError(f"modo desconhecido: {fixture['mode']}")


def render_markdown(rows):
    lines = [
        "| runner | fixture | fase | unidade | threads | checked | tempo (s) | taxa/s |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for r in rows:
        mark = "" if r.get("valid") else " **parcial**"
        lines.append(
            f"| {r['runner']} | {r['fixture']} | {r['phase']} | {r['unit']} | "
            f"{r['threads']} | {r['checked']} | {r['elapsed_ms'] / 1000.0:.2f} | "
            f"{rate(r):,.0f}{mark} |"
        )
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description="Benchmark nativo x WASM (F0)")
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--window", type=lambda s: int(s, 0), default=DEFAULT_WINDOW)
    ap.add_argument("--full", action="store_true", help="varre o espaco inteiro")
    ap.add_argument("--wasm", action="store_true", help="inclui o build WASM (node)")
    ap.add_argument("--fixtures", default=None, help="so estas (virgula)")
    ap.add_argument("--json", default=None, help="caminho do JSON de saida")
    args = ap.parse_args()

    bench_fixtures.validate()
    chosen = ([n for n in args.fixtures.split(",") if n] if args.fixtures
              else bench_fixtures.names())

    path = find_lib()
    global _lib_handle
    _lib_handle = _bind(ctypes.CDLL(os.path.abspath(path)))
    wasm = os.path.join(ROOT, "wasm", "out", "seedfinder.wasm")
    have_wasm = os.path.isfile(wasm)
    meta = {
        "dll": os.path.relpath(path, ROOT),
        "dll_bytes": os.path.getsize(path),
        "dll_mtime": os.path.getmtime(path),
        "wasm": os.path.relpath(wasm, ROOT) if have_wasm else None,
        "wasm_bytes": os.path.getsize(wasm) if have_wasm else None,
        "wasm_mtime": os.path.getmtime(wasm) if have_wasm else None,
        "python": platform.python_version(),
        "cpu_count": os.cpu_count(),
        "window": (1 << 32) if args.full else args.window,
        "repeat": args.repeat,
    }
    print(f"lib: {meta['dll']} ({meta['dll_bytes']} B)  threads=native cpu-1")

    # Comparar nativo novo com WASM velho invalida a comparacao. Avisa se
    # qualquer fonte do core for mais nova que o build WASM.
    if args.wasm and have_wasm:
        newest = max(os.path.getmtime(os.path.join(ROOT, "core", f))
                     for f in os.listdir(os.path.join(ROOT, "core"))
                     if f.endswith((".c", ".h")))
        if meta["wasm_mtime"] < newest:
            print("AVISO: wasm/out mais antigo que core/ - rode wasm/build_wasm.bat")

    rows = []
    for name in chosen:
        fx = bench_fixtures.load(name)
        space = fx["space"] if args.full else min(args.window, fx["space"])
        for row in run_native(fx, space, args.repeat, _lib_handle, args.full):
            row["space"] = space
            full_ms = extrapolate(row, fx["space"])
            row["extrapolated_full_ms"] = None if full_ms is None else round(full_ms, 1)
            rows.append(row)
            print(f"  {name:10s} {row['phase']:8s} {rate(row):14,.0f}/s"
                  f" ({row['checked']} em {row['elapsed_ms'] / 1000.0:.2f}s)")

    os.makedirs(OUT_DIR, exist_ok=True)
    out_json = args.json or os.path.join(
        OUT_DIR, time.strftime("bench-%Y%m%d-%H%M%S.json"))
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump({"meta": meta, "rows": rows}, f, indent=2)
    md = render_markdown(rows)
    with open(os.path.splitext(out_json)[0] + ".md", "w", encoding="utf-8") as f:
        f.write(md)
    print()
    print(md)
    print(f"json: {os.path.relpath(out_json, ROOT)}")


if __name__ == "__main__":
    main()
