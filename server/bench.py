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
        data = json.loads(ctypes.string_at(ptr).decode("utf-8"))
    finally:
        lib_free(ptr)
    # Erro do engine ({"error":...}) nao e' resultado: um leitor poderia confundir
    # com uma fase lenta. Propaga como excecao para virar linha invalida, nunca taxa.
    if isinstance(data, dict) and "error" in data:
        raise RuntimeError(f"engine error: {data['error']}")
    return data


_lib_handle = None


def lib_free(ptr):
    _lib_handle.seedfinder_free_result(ptr)


def _arrays(structures, default_tol=0):
    n = len(structures)
    types = (ctypes.c_int * n)(*[s["type"] for s in structures])
    xs = (ctypes.c_double * n)(*[float(s["x"]) for s in structures])
    zs = (ctypes.c_double * n)(*[float(s["z"]) for s in structures])
    # Tolerancia por estrutura; sem chave propria herda a do fixture (o modo
    # auto usa ponteiros, nao o escalar do seedfinder_crack).
    tols = (ctypes.c_int * n)(*[s.get("tolerance", default_tol) for s in structures])
    return n, types, xs, zs, tols


def rate(row):
    elapsed_ms = row["elapsed_ms"]
    if elapsed_ms <= 0:
        return 0.0
    return row["checked"] / (elapsed_ms / 1000.0)


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
    """Linha cuja taxa e' a mediana das taxas (empate -> menor tempo).

    Linhas invalidas (taxa 0) nao podem vencer a disputa: com --repeat 3 e duas
    falhas as taxas ordenam [0, 0, R] e a mediana apontaria para uma falha,
    jogando fora a unica medicao real. Prefere-se o subconjunto com taxa > 0;
    so' quando nenhuma mediu nada (todas falharam) cai para o conjunto todo
    (que devolve a falha, honestamente, em vez de inventar uma taxa)."""
    pool = [r for r in rows if rate(r) > 0] or rows
    rates = sorted(rate(r) for r in pool)
    med = rates[len(rates) // 2]
    return min(pool, key=lambda r: (abs(rate(r) - med), r["elapsed_ms"]))


def _row(runner, fixture, phase, unit, threads, data, elapsed_ms):
    timed_out = bool(data.get("timed_out"))
    return {
        "runner": runner, "fixture": fixture, "phase": phase, "unit": unit,
        "threads": threads, "checked": int(data.get("checked", 0)),
        "elapsed_ms": round(elapsed_ms, 3), "timed_out": timed_out,
        "valid": not timed_out,
    }


def _error_row(runner, fixture, phase, unit, threads, message):
    """Fase que o engine recusou: sem taxa e sem tempo (nao houve trabalho
    medido), so a mensagem, marcada invalida."""
    return {
        "runner": runner, "fixture": fixture, "phase": phase, "unit": unit,
        "threads": threads, "checked": 0, "elapsed_ms": 0.0,
        "timed_out": False, "valid": False, "error": message,
    }


def _skip_row(runner, fixture, phase, unit, threads, message):
    """Fase que nao roda (entrada vazia por contrato, ou guard do motor pedindo
    mais ancoras): nao e' taxa nem erro do bench - e' ausencia de medicao."""
    return {
        "runner": runner, "fixture": fixture, "phase": phase, "unit": unit,
        "threads": threads, "checked": 0, "elapsed_ms": 0.0,
        "timed_out": False, "valid": False, "skipped": True, "note": message,
    }


def _timed(fn, *args):
    t0 = time.perf_counter()
    data = _call(fn, *args)
    return data, (time.perf_counter() - t0) * 1000.0


# Mensagens do motor que sao um PEDIDO (guard/recusa por falta de ancoras) e nao
# uma falha de medicao: so' elas viram `skipped`. Qualquer outro RuntimeError
# (ex.: "returned NULL" num ponteiro perdido) e' falha e vira linha de erro -
# "skipped" diz "nao havia o que medir", nao "a medicao quebrou".
_ENGINE_GUARD_MARKERS = ("too many 48-bit candidates",)


def _is_engine_guard(message):
    return any(m in str(message) for m in _ENGINE_GUARD_MARKERS)


def _timed_row(runner, fixture, phase, unit, threads, fn, *args, checked=None):
    """Roda uma fase e devolve (dados, linha). Erro do engine vira linha
    invalida (nunca uma taxa plausivel); `checked` sobrepoe o valor do JSON
    quando a fase nao o reporta (lift48, mitm)."""
    try:
        data, ms = _timed(fn, *args)
    except RuntimeError as e:
        return None, _error_row(runner, fixture, phase, unit, threads, str(e))
    if checked is not None:
        data = dict(data, checked=checked)
    return data, _row(runner, fixture, phase, unit, threads, data, ms)


def newest_core_mtime():
    """mtime mais recente das fontes do motor (base do aviso de artefato velho)."""
    core = os.path.join(ROOT, "core")
    return max(os.path.getmtime(os.path.join(core, f))
               for f in os.listdir(core) if f.endswith((".c", ".h")))


def staleness_warnings(dll_mtime, core_mtime, wasm_mtime=None):
    """Avisos de artefato mais antigo que core/. Pura (sem FS) para ser testavel.

    Um build nativo velho invalida os numeros: em 18/10 a ABI de
    auto_sweep/lift48/lift63 mudou (commit 980b406) e a DLL de 13/10 fazia o
    auto64 falhar inteiro sem aviso."""
    out = []
    if dll_mtime < core_mtime:
        out.append("AVISO: build_server/seedfinder_lib.dll mais antigo que core/ - "
                   "rode server\\start.bat antes de confiar nestes numeros")
    if wasm_mtime is not None and wasm_mtime < core_mtime:
        out.append("AVISO: wasm/out mais antigo que core/ - rode wasm/build_wasm.bat")
    return out


def default_threads():
    """Padrao do lado nativo: cpu_count - 1 (mesma escolha de sempre)."""
    return max(1, (os.cpu_count() or 4) - 1)


def run_native(fixture, window, repeat, lib, full=False, threads=None):
    """Roda o modo da fixture `repeat` vezes e devolve as linhas da mediana.

    `threads` vale para as fases que RECEBEM o argumento (crack32 e auto sweep);
    None usa o padrao (cpu_count - 1). A fase mitm mantem o 1 fixo de sempre."""
    name = fixture["name"]
    if threads is None:
        threads = default_threads()
    threads = max(1, int(threads))
    rows = []

    if fixture["mode"] == "bedrock32":
        n, types, xs, zs, _ = _arrays(fixture["structures"])
        for _ in range(repeat):
            _, row = _timed_row("native", name, "crack32", "seeds", threads,
                                lib.seedfinder_crack, types, n, xs, zs,
                                fixture["tolerance"], 0, window, 2000, FULL_BUDGET, threads)
            rows.append(row)
        return [median_rows(rows)]

    if fixture["mode"] == "auto":
        mt = fixture["mt_structures"]
        jv = fixture["java_structures"]
        nmt, mt_types, mt_x, mt_z, mt_tol = _arrays(mt, fixture["tolerance"])
        nj, j_types, j_x, j_z, j_tol = _arrays(jv, fixture["tolerance"])
        true_lo32 = fixture["seed"] & 0xFFFFFFFF
        best = None
        for _ in range(repeat):
            # Fase 1: sweep ranqueado (placement-only).
            sweep, sweep_row = _timed_row("native", name, "sweep", "seeds", threads,
                                          lib.seedfinder_crack_auto_sweep,
                                          mt_types, nmt, mt_x, mt_z, mt_tol,
                                          0, window, 2000, FULL_BUDGET, threads)
            results = (sweep or {}).get("results", []) if sweep else []
            lo32 = [r["seed"] for r in results[:2000]]
            if sweep is not None:
                sweep_row["top_seeds"] = [str(s) for s in lo32[:5]]
                sweep_row["found_lo32"] = true_lo32 in lo32

            # Fase 2: lift48 com a LISTA. nLo32 > 1 => s48 vazio POR CONTRATO
            # (core/crack_auto.h:29-37); serve para direct32, nao alimenta lift63.
            if not lo32:
                s48_data, s48_row = None, _skip_row(
                    "native", name, "lift48", "candidates", threads,
                    "sweep sem candidatos lo32 - fase 2 nao roda")
            else:
                p_lo = (ctypes.c_uint64 * len(lo32))(*lo32)
                s48_data, s48_row = _timed_row(
                    "native", name, "lift48", "candidates", threads,
                    lib.seedfinder_crack_auto_lift48,
                    mt_types, mt_x, mt_z, mt_tol, nmt,
                    p_lo, len(lo32),
                    j_types, j_x, j_z, j_tol, nj, 2000,
                    checked=len(lo32))
                if len(lo32) > 1:
                    s48_row["note"] = ("s48 vazio por contrato (nLo32 > 1): "
                                       "a fase 2 so' produz direct32")

            # Fase 3: lift48 com rank-1 - e' aqui que o lift Java roda. O guard
            # de 1024 candidatos e' um pedido de mais ancoras, nao falha: skip.
            if not lo32:
                rank1_data, rank1_row = None, _skip_row(
                    "native", name, "lift48_rank1", "candidates", threads,
                    "sweep sem candidatos lo32 - sem rank-1")
            else:
                p_rank1 = (ctypes.c_uint64 * 1)(lo32[0])
                try:
                    rank1_data, rank1_ms = _timed(
                        lib.seedfinder_crack_auto_lift48,
                        mt_types, mt_x, mt_z, mt_tol, nmt,
                        p_rank1, 1,
                        j_types, j_x, j_z, j_tol, nj, 2000)
                except RuntimeError as e:
                    if _is_engine_guard(e):
                        rank1_data, rank1_row = None, _skip_row(
                            "native", name, "lift48_rank1", "candidates", threads,
                            str(e))
                    else:
                        # Falha real (ponteiro NULL, entrada invalida, OOM): nao
                        # e' ausencia de trabalho. Marca invalida com a mensagem,
                        # nunca com uma taxa.
                        rank1_data, rank1_row = None, _error_row(
                            "native", name, "lift48_rank1", "candidates", threads,
                            str(e))
                else:
                    rank1_row = _row("native", name, "lift48_rank1", "candidates",
                                     threads, dict(rank1_data, checked=1), rank1_ms)

            # Fase 4: lift63 so' roda com o s48 da fase 3 - nunca com array vazio.
            s48 = (rank1_data or {}).get("s48", [])
            if not s48:
                lift63_row = _skip_row("native", name, "lift63", "hi", threads,
                                       "sem s48 da fase 3 - lift63 nao roda")
            else:
                p_s48 = (ctypes.c_uint64 * len(s48))(*s48)
                _, lift63_row = _timed_row("native", name, "lift63", "hi", threads,
                                           lib.seedfinder_crack_auto_lift63,
                                           p_s48, len(s48),
                                           mt_types, mt_x, mt_z, mt_tol, nmt,
                                           j_types, j_x, j_z, j_tol, nj, 2000, FULL_BUDGET)

            run = [sweep_row, s48_row, rank1_row, lift63_row]
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
            _, row = _timed_row("native", name, "mitm", "u", 1,
                                lib.seedfinder_crack_mitm_window,
                                types, n, xs, zs, 50, FULL_BUDGET, 1, start_u, end_u,
                                checked=end_u - start_u)
            rows.append(row)
        return [median_rows(rows)]

    raise ValueError(f"modo desconhecido: {fixture['mode']}")


WASM_RUNNER_MARGIN = 300.0
# Chamadas com orcamento proprio no runner WASM (o JS passa FULL_BUDGET por fase,
# como o nativo): o subprocesso TEM de sobreviver a elas para o motor conseguir
# reportar o proprio `timed_out` em vez de ser morto de fora.
_WASM_BUDGETED_CALLS = {"bedrock32": 1, "auto": 2, "java_mitm": 1}


def _wasm_timeout(mode, repeat):
    """Timeout do subprocesso derivado do orcamento do motor (por chamada) x
    repeticoes, com margem para startup/drenagem do Node."""
    calls = _WASM_BUDGETED_CALLS.get(mode, 1)
    return WASM_RUNNER_MARGIN + max(1, repeat) * calls * FULL_BUDGET


def _stderr_head(stderr, limit=300):
    """Primeiras linhas uteis do stderr, para caber numa mensagem de erro."""
    lines = [ln.strip() for ln in (stderr or "").splitlines() if ln.strip()]
    return (" | ".join(lines) or "(sem stderr)")[:limit]


def _wasm_run_error(fixture, out):
    """Linha de erro para uma execucao que nao entregou rows, ou None se ok.

    Exit != 0, stdout vazio ou stdout que nao e' a lista de linhas viram linha
    invalida com o motivo: o lado WASM nao pode DESAPARECER do JSON/markdown so'
    porque o runner quebrou (o stderr nao faz parte do artefato)."""
    def bad(detail):
        return _error_row("wasm", fixture["name"], "runner", "-", 0,
                          f"runner WASM (exit {out.returncode}): {detail}")

    if out.returncode != 0:
        return bad(_stderr_head(out.stderr))
    if not out.stdout.strip():
        return bad("stdout vazio")
    try:
        rows = json.loads(out.stdout)
    except ValueError as e:
        return bad(f"stdout nao e' JSON ({e})")
    if not isinstance(rows, list) or not all(isinstance(r, dict) for r in rows):
        return bad("stdout nao e' uma lista de linhas")
    if not rows:
        return bad("runner nao devolveu nenhuma linha")
    return None


def _reduce_wasm_runs(runs):
    """Reduz as `repeat` execucoes a uma linha por fase (mediana das taxas).

    Mesma reducao do lado nativo (`median_rows`), que prefere linhas com taxa
    > 0: duas execucoes que falharam nao jogam fora a terceira que mediu, e uma
    fase que so' falhou devolve a propria linha invalida. A ordem das fases e' a
    do runner."""
    order, grouped = [], {}
    for run in runs:
        for r in run:
            phase = r.get("phase")
            if phase not in grouped:
                grouped[phase] = []
                order.append(phase)
            grouped[phase].append(r)
    return [median_rows(grouped[p]) for p in order]


def _ensure_wasm_esm(out_dir):
    """Marca wasm/out como ESM para o Node parar de reparsear o bundle.

    O Emscripten emite ESM em wasm/out/ sem package type, entao o Node imprime
    MODULE_TYPELESS_PACKAGE_JSON e reparseia o arquivo inteiro em todo run. Um
    package.json {"type":"module"} resolve. Nunca sobrescreve um existente
    (wasm/ e' untracked: o arquivo fica local). True se criou."""
    pkg = os.path.join(out_dir, "package.json")
    if os.path.exists(pkg):
        return False
    with open(pkg, "w", encoding="utf-8") as f:
        f.write('{"type": "module"}\n')
    return True


def run_wasm(fixture, space, repeat=1, full=False):
    """Roda a fixture `repeat` vezes no build WASM via node e devolve as linhas
    da mediana. None SO' quando falta build ou Node.

    O runner fala JSON em stdout (rows no mesmo formato do lado nativo, com
    `runner: "wasm"`); logs dele vao para stderr. Falha do runner (exit != 0,
    stdout vazio/invalido ou timeout) devolve linha de erro - nunca None, senao
    o lado WASM sumiria do artefato em silencio."""
    if os.environ.get("SEEDFINDER_BENCH_NO_WASM"):
        return None
    js = os.path.join(ROOT, "wasm", "out", "seedfinder.js")
    runner = os.path.join(ROOT, "server", "bench_wasm.mjs")
    if not (os.path.isfile(js) and os.path.isfile(runner)):
        print("  wasm: build ausente (rode wasm/build_wasm.bat) - pulando")
        return None
    _ensure_wasm_esm(os.path.dirname(js))
    os.makedirs(OUT_DIR, exist_ok=True)
    fx_path = os.path.join(OUT_DIR, "fixtures.json")
    with open(fx_path, "w", encoding="utf-8") as f:
        # O modo vai no payload: o runner precisa medir a MESMA janela do
        # nativo (java_mitm com --full usa [0, 2^24), nao u_timing).
        json.dump({"fixtures": [fixture], "space": space, "full": bool(full)}, f)
    timeout = _wasm_timeout(fixture.get("mode"), repeat)
    runs = []
    for _ in range(max(1, repeat)):
        try:
            out = subprocess.run(["node", runner, "--fixtures", fx_path],
                                 capture_output=True, text=True, timeout=timeout)
        except FileNotFoundError:
            print("  wasm: node nao encontrado no PATH - pulando")
            return None
        except subprocess.TimeoutExpired:
            # Timeout e' falha de medicao, nao "build ausente": vira linha
            # invalida para nao sumir do relatorio nem derrubar os outros runners.
            print(f"  wasm: runner excedeu {timeout:.0f}s - abortado")
            return [_error_row("wasm", fixture["name"], "runner", "-", 0,
                               f"runner WASM excedeu {timeout:.0f}s")]
        error = _wasm_run_error(fixture, out)
        if error:
            # A linha de erro e' o registro (o main a imprime); nada de None.
            return [error]
        runs.append(json.loads(out.stdout))
    rows = _reduce_wasm_runs(runs)
    for r in rows:
        r["space"] = space
        r["extrapolated_full_ms"] = None
    return rows


def _print_row(name, row):
    if row.get("error"):
        print(f"  {name:10s} {row['phase']:12s} ERRO: {row['error']}")
    elif row.get("skipped"):
        print(f"  {name:10s} {row['phase']:12s} skipped: {row['note']}")
    else:
        print(f"  {name:10s} {row['phase']:12s} {rate(row):14,.0f}/s"
              f" ({row['checked']} em {row['elapsed_ms'] / 1000.0:.2f}s)")


def render_markdown(rows):
    lines = [
        "| runner | fixture | fase | unidade | threads | checked | tempo (s) | taxa/s |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for r in rows:
        if r.get("error"):
            rate_cell = f"**erro: {r['error']}**"
        elif r.get("skipped"):
            rate_cell = f"**skipped: {r['note']}**"
        else:
            rate_cell = f"{rate(r):,.0f}"
            if not r.get("valid"):
                rate_cell += " **parcial**"
        lines.append(
            f"| {r['runner']} | {r['fixture']} | {r['phase']} | {r['unit']} | "
            f"{r['threads']} | {r['checked']} | {r['elapsed_ms'] / 1000.0:.2f} | "
            f"{rate_cell} |"
        )
    return "\n".join(lines) + "\n"


def _positive_int(text):
    """--repeat <= 0 nao e' "sem repeticoes": viraria IndexError/None no
    median_rows. Recusa na entrada, com mensagem clara."""
    try:
        value = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"esperava um inteiro (recebi {text!r})")
    if value < 1:
        raise argparse.ArgumentTypeError(f"precisa ser >= 1 (recebi {text!r})")
    return value


def main():
    ap = argparse.ArgumentParser(description="Benchmark nativo x WASM (F0)")
    ap.add_argument("--repeat", type=_positive_int, default=3,
                    help="repeticoes por fase (>= 1)")
    ap.add_argument("--threads", type=_positive_int, default=None,
                    help="threads das fases de varredura nativa (crack32/auto "
                         "sweep); padrao cpu_count - 1")
    ap.add_argument("--window", type=lambda s: int(s, 0), default=DEFAULT_WINDOW)
    ap.add_argument("--full", action="store_true", help="varre o espaco inteiro")
    ap.add_argument("--wasm", action="store_true", help="inclui o build WASM (node)")
    ap.add_argument("--fixtures", default=None, help="so estas (virgula)")
    ap.add_argument("--json", default=None, help="caminho do JSON de saida")
    args = ap.parse_args()

    bench_fixtures.validate()
    chosen = ([n for n in args.fixtures.split(",") if n] if args.fixtures
              else bench_fixtures.names())
    native_threads = args.threads if args.threads is not None else default_threads()

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
        "threads": native_threads,
        "window": (1 << 32) if args.full else args.window,
        "repeat": args.repeat,
    }
    print(f"lib: {meta['dll']} ({meta['dll_bytes']} B)  threads={native_threads}")

    # Artefato velho vs core/ invalida os numeros (a ABI do modo auto mudou em
    # 980b406 e uma DLL velha fazia o auto64 falhar inteiro, em silencio).
    for msg in staleness_warnings(meta["dll_mtime"], newest_core_mtime(),
                                  meta["wasm_mtime"] if args.wasm else None):
        print(msg)

    rows = []
    for name in chosen:
        fx = bench_fixtures.load(name)
        space = fx["space"] if args.full else min(args.window, fx["space"])
        for row in run_native(fx, space, args.repeat, _lib_handle, args.full,
                              native_threads):
            row["space"] = space
            full_ms = extrapolate(row, fx["space"])
            row["extrapolated_full_ms"] = None if full_ms is None else round(full_ms, 1)
            rows.append(row)
            _print_row(name, row)

    # WASM so' depois do nativo: os dois lados competem pela mesma CPU, e um
    # runner lento (ou ausente) nao pode contaminar a medicao nativa. Mesmo
    # `--repeat` dos dois lados: mediana das taxas (median_rows).
    if args.wasm:
        for name in chosen:
            fx = bench_fixtures.load(name)
            space = fx["space"] if args.full else min(args.window, fx["space"])
            for row in run_wasm(fx, space, args.repeat, args.full) or []:
                row["space"] = space
                row["extrapolated_full_ms"] = None
                rows.append(row)
                _print_row(name, row)

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
