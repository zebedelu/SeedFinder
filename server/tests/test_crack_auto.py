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
import time

DLL = r"build_server\seedfinder_lib.dll"
FIXTURE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "fixtures_crack64.json")
LO32_SEED = 5309
TOLERANCE = 6
# Tol do pipeline automatico: o guard do lift48 (nS48 > 1024) rejeita os
# yields maiores (medidos com 4 Trial Chambers + lo32 5309): tol 4 -> 1134,
# tol 5 -> 2626, tol 6 -> 3391 (todas > guard). tol 2 -> 68 s48 com a seed
# verdadeira presente. O modo auto usa UM campo de tolerancia (sweep + lift).
E2E_TOLERANCE = 2
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
    assert "checked" in data and "timed_out" in data, sorted(data)
    hit = next((r for r in data["results"] if r["seed"] == LO32_SEED), None)
    assert hit is not None, (
        f"seed {LO32_SEED} missing from top results "
        f"({[r['seed'] for r in data['results'][:20]]}, "
        f"checked={data['checked']}, timed_out={data['timed_out']})")
    # Contrato placement-only (defeito 2): o auto JSON nunca carrega matches
    # nem passa pelo gate de bioma.
    assert "matches" not in hit, f"auto sweep must stay placement-only: {hit}"
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


def load_java_structures():
    with open(FIXTURE_PATH, encoding="utf-8") as f:
        fx = json.load(f)
    return [dict(s) for s in fx["java_structures"]]


def lift48(lib, mt, lo32_seeds, java=None, tolerance=0, max_results=2000):
    n = len(mt)
    types = (ctypes.c_int * n)(*(s["type"] for s in mt))
    xs = (ctypes.c_double * n)(*(float(s["x"]) for s in mt))
    zs = (ctypes.c_double * n)(*(float(s["z"]) for s in mt))
    lo = (ctypes.c_uint64 * max(len(lo32_seeds), 1))(
        *(int(s) & 0xFFFFFFFFFFFFFFFF for s in lo32_seeds))
    java = java or []
    jn = len(java)
    if jn:
        jt = (ctypes.c_int * jn)(*(s["type"] for s in java))
        jx = (ctypes.c_double * jn)(*(float(s["x"]) for s in java))
        jz = (ctypes.c_double * jn)(*(float(s["z"]) for s in java))
    else:
        jt = jx = jz = None
    lib.seedfinder_crack_auto_lift48.argtypes = [
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double),
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_uint64), ctypes.c_int,
        ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_double),
        ctypes.POINTER(ctypes.c_double),
        ctypes.c_int, ctypes.c_int, ctypes.c_int,
    ]
    lib.seedfinder_crack_auto_lift48.restype = ctypes.c_void_p
    lib.seedfinder_free_result.argtypes = [ctypes.c_void_p]
    lib.seedfinder_free_result.restype = None
    p = lib.seedfinder_crack_auto_lift48(
        types, xs, zs, n, lo, len(lo32_seeds), jt, jx, jz, jn,
        tolerance, max_results)
    if not p:
        raise RuntimeError("seedfinder_crack_auto_lift48 returned NULL")
    try:
        return json.loads(ctypes.string_at(p).decode("utf-8"))
    finally:
        lib.seedfinder_free_result(p)


def test_lift48_direct32_hit(lib):
    # FIXTURE_8675309 em tolerance 0: validacao completa (placement + bioma)
    # sob seed = lo32 deve aceitar 8675309 com os 4 matches na ORDEM DE
    # ENTRADA (chunks exatos dos alvos quando tol = 0) e sem lift (sem ancoras).
    from test_native_crack import FIXTURE_8675309
    data = lift48(lib, FIXTURE_8675309, [8675309], tolerance=0)
    assert "error" not in data, data
    assert data["s48"] == [], data
    hit = next((r for r in data["direct32"] if r["seed"] == 8675309), None)
    assert hit is not None, f"seed 8675309 missing from direct32: {data}"
    assert hit["matches"] == [[-18, 9], [-18, -23], [43, 22], [44, 47]], hit
    assert hit["score"] == 0, hit
    print(f"  lift48 direct32: seed 8675309 score={hit['score']} "
          f"matches={hit['matches']}")


def test_lift48_s48_lift(lib, mt):
    # Discriminador do defeito 2: o lo32 5309 NAO passa validacao completa
    # (bioma sob 5309 difere do bioma sob a seed real), mas o lift 32->48
    # pelas 4 Trial Chambers do fixture deve recuperar 4294972605.
    # tol 3, nao 6 (ruling): em tol 6 as mesmas 4 ancoras rendem 3391 s48
    # (> guard 1024 - rendimento medido, replicate por probe_lift48_yield.py).
    data = lift48(lib, mt, [5309], java=load_java_structures(), tolerance=3)
    assert "error" not in data, data
    assert 4294972605 in data["s48"], f"s48 lift failed: {data}"
    seeds32 = [r["seed"] for r in data["direct32"]]
    assert 5309 not in seeds32, (
        f"biome gate must reject lo32 5309 (diverges from full seed): {data}")
    print(f"  lift48: s48={len(data['s48'])} candidates, "
          f"4294972605 present, direct32={seeds32}")


def test_lift48_loose_anchor_guard(lib, mt):
    # 1 Trial Chamber com coordenada frouxa (tol 8) gera mais de 1024 s48 ->
    # guard de viabilidade com a mensagem exata.
    java = [{"type": 24, "x": -505, "z": -281}]
    data = lift48(lib, mt, [5309], java=java, tolerance=8)
    assert "error" in data, data
    assert data["error"].startswith("too many 48-bit candidates ("), data
    print(f"  lift48 guard: {data['error'][:80]}...")


def test_lift48_bad_inputs(lib):
    from test_native_crack import FIXTURE_8675309
    data = lift48(lib, FIXTURE_8675309, [], tolerance=0)
    assert "error" in data and "lo32" in data["error"], data

    bad = [dict(s) for s in FIXTURE_8675309]
    bad[0] = {"type": 23, "x": 0, "z": 0}
    data = lift48(lib, bad, [8675309], tolerance=0)
    assert data.get("error") == (
        "Trail Ruins and Trial Chambers cannot be swept - "
        "pass them as Java anchors"), data
    print("  lift48 validation errors OK")


def lift63(lib, s48_seeds, mt, tolerance=TOLERANCE, max_results=2000,
           budget=0.0, java=None):
    n = len(mt)
    types = (ctypes.c_int * n)(*(s["type"] for s in mt))
    xs = (ctypes.c_double * n)(*(float(s["x"]) for s in mt))
    zs = (ctypes.c_double * n)(*(float(s["z"]) for s in mt))
    seeds = (ctypes.c_uint64 * max(len(s48_seeds), 1))(
        *(int(s) & 0xFFFFFFFFFFFFFFFF for s in s48_seeds))
    java = java or []
    jn = len(java)
    if jn:
        jt = (ctypes.c_int * jn)(*(s["type"] for s in java))
        jx = (ctypes.c_double * jn)(*(float(s["x"]) for s in java))
        jz = (ctypes.c_double * jn)(*(float(s["z"]) for s in java))
    else:
        jt = jx = jz = None
    lib.seedfinder_crack_auto_lift63.argtypes = [
        ctypes.POINTER(ctypes.c_uint64), ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double),
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_double),
        ctypes.POINTER(ctypes.c_double),
        ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_double,
    ]
    lib.seedfinder_crack_auto_lift63.restype = ctypes.c_void_p
    lib.seedfinder_free_result.argtypes = [ctypes.c_void_p]
    lib.seedfinder_free_result.restype = None
    p = lib.seedfinder_crack_auto_lift63(
        seeds, len(s48_seeds), types, xs, zs, n, jt, jx, jz, jn,
        tolerance, max_results, budget)
    if not p:
        raise RuntimeError("seedfinder_crack_auto_lift63 returned NULL")
    try:
        return json.loads(ctypes.string_at(p).decode("utf-8"))
    finally:
        lib.seedfinder_free_result(p)


def test_lift63_recovers_full_seed(lib, mt):
    # Lift 48->63 via bioma: um unico s48 (o do fixture 64-bit) com hi=0
    # deve recuperar a seed completa com os 4 matches (chunks, ordem de
    # entrada) e score pequeno (soma de d^2 <= 4 * tolerance^2).
    data = lift63(lib, [4294972605], mt, tolerance=TOLERANCE, budget=120.0)
    assert "error" not in data, data
    assert "checked" in data and "timed_out" in data, sorted(data)
    assert data["timed_out"] is False, data
    hit = next((r for r in data["results"]
                if r.get("seed_str") == "4294972605"), None)
    assert hit is not None, (
        f"seed 4294972605 missing from lift63 results "
        f"({[r.get('seed_str') for r in data['results'][:10]]}, "
        f"checked={data['checked']})")
    assert len(hit["matches"]) == 4, hit
    assert hit["score"] <= 4 * TOLERANCE * TOLERANCE, hit
    for pair in hit["matches"]:
        assert len(pair) == 2, hit
    print(f"  lift63: seed 4294972605 score={hit['score']} "
          f"matches={hit['matches']} checked={data['checked']}")


def test_lift63_wrong_s48_empty(lib, mt):
    # s48 errado (lo32 5310 em vez de 5309): cross + gate de bioma devem
    # descartar tudo - resultados vazios, NAO um erro.
    data = lift63(lib, [4294972605 + 1], mt, tolerance=TOLERANCE,
                  budget=120.0)
    assert "error" not in data, data
    assert data["results"] == [], data
    print(f"  lift63 wrong s48: empty results "
          f"(checked={data['checked']})")


def test_lift63_32bit_seed_via_s48(lib):
    # Convergencia dos dois caminhos: s48 = 8675309 com hi=0 tambem acha
    # mundos 32-bit. tolerance 0 => matches exatos na ordem de entrada.
    from test_native_crack import FIXTURE_8675309
    data = lift63(lib, [8675309], FIXTURE_8675309, tolerance=0, budget=120.0)
    assert "error" not in data, data
    hit = next((r for r in data["results"]
                if r.get("seed_str") == "8675309"), None)
    assert hit is not None, (
        f"seed 8675309 missing from lift63 results "
        f"({[r.get('seed_str') for r in data['results'][:10]]}, "
        f"checked={data['checked']})")
    assert hit["matches"] == [[-18, 9], [-18, -23], [43, 22], [44, 47]], hit
    assert hit["score"] == 0, hit
    # seed_str e' o canal seguro p/ JS: confere o decimal de TODOS os
    # resultados (incluindo hits > 2^53 vindos de hi != 0) contra o valor.
    assert all(r["seed_str"] == str(r["seed"]) for r in data["results"]), data
    assert any(r["seed"] >= 2 ** 53 for r in data["results"]), (
        "expected at least one hit above 2^53 to cover seed_str precision")
    print(f"  lift63 32-bit via s48: score={hit['score']} "
          f"matches={hit['matches']}")


def test_auto_pipeline_e2e(lib, mt):
    """Cadeia automatica end-to-end: sweep full-range -> lift48 -> lift63.

    Gated por SEEDFINDER_AUTO_E2E=1 (padrao do SEEDFINDER_FULL_RANGE_TEST);
    sem a env imprime AUTO_E2E_SKIPPED e retorna. Encadeia os tres exports
    via ctypes exatamente como o orquestrador do modo auto (regra do Task 4):

    1. sweep do espaco lo32 INTEIRO [0, 2^32) -> lo32 ranqueados por score;
    2. lift48 de TODOS os lo32 SEM ancoras -> direct32: deteccao barata de
       mundo 32-bit, discriminada por bioma (~0.2 s / 2000 lo32);
    3. direto vazio + ancoras -> lift48 APENAS com o lo32 de melhor score
       (rank1) -> s48 -> lift63 -> seed completa.

    O passo 3 nao recebe os 2000 de proposito: o guard do lift48 (nS48 >
    1024) estouraria (medido: K=1 -> 68 s48 em tol 2; K=2000 -> 325841 em
    tol 6) e o lift63 custa ~3.3 s por s48 single-thread, entao o custo do
    lift limita o que o lift48 pode devolver.

    Tolerancia E2E_TOLERANCE = 2: em tol 4 (default do produto) o mesmo
    lo32 ja produz 1134 s48 > guard; em tol 6, 3391.

    Custo de referencia medido (i5, 6 threads, 26/09/2026): sweep64 628 s,
    sweep32 529 s, direct32 (2000 lo32) 0.1 s, lift48 68 s48 <0.1 s, lift63
    163 s (68 s48 x 32768 hi) -> 1339 s no total. O sweep de 2^32 domina
    (~9 min/seed) e o lift63 e' 3.3 s por s48 single-thread.
    """
    if not os.environ.get("SEEDFINDER_AUTO_E2E"):
        print("AUTO_E2E_SKIPPED (set SEEDFINDER_AUTO_E2E=1)")
        return
    failures = []
    threads = 6

    # --- fixture 64-bit: sweep full-range + lift 32->48->63 -------------
    java = load_java_structures()
    t0 = time.time()
    sw = sweep(lib, mt, tolerance=E2E_TOLERANCE, start=0, end=1 << 32,
               max_results=2000, threads=threads)
    dt64 = time.time() - t0
    lo64 = [r["seed"] for r in sw.get("results", [])]
    hit = next((r for r in sw.get("results", []) if r["seed"] == LO32_SEED),
               None)
    print(f"  e2e sweep64: {len(lo64)} lo32 em {dt64:.0f}s "
          f"(checked={sw.get('checked')}, timed_out={sw.get('timed_out')}, "
          f"score do lo32 verdadeiro {LO32_SEED}: "
          f"{hit['score'] if hit else 'AUSENTE'})")
    if "error" in sw:
        failures.append(f"sweep 64-bit: {sw['error']}")
    elif hit is None:
        failures.append(f"lo32 {LO32_SEED} fora do top-2000 do sweep full-range")

    if lo64:
        # Passo 1 da regra: deteccao 32-bit em TODOS os lo32, sem ancoras.
        # Barata (~0.2 s / 2000 lo32) e discriminada por bioma; num mundo
        # 64-bit tem de voltar vazia (Review Focus #3).
        t0 = time.time()
        d32all = lift48(lib, mt, lo64, java=None, tolerance=E2E_TOLERANCE)
        print(f"  e2e direct32 (2000 lo32, sem ancoras): "
              f"{len(d32all.get('direct32', []))} em {time.time() - t0:.1f}s")
        if d32all.get("direct32"):
            failures.append(
                "mundo 64-bit devolveu candidatos 32-bit falsos: "
                f"{[x['seed'] for x in d32all['direct32'][:5]]}")
        # Passo 2: ancoras Java APENAS com o lo32 de melhor score (rank1).
        # Os 2000 juntos estourariam o guard de 1024 (K=1 ja da 68 em tol 2;
        # K=2000, 325841 em tol 6) e o lift63 custa 3.3 s por s48.
        t0 = time.time()
        d48 = lift48(lib, mt, [lo64[0]], java=java, tolerance=E2E_TOLERANCE)
        if "error" in d48:
            failures.append("cadeia 64-bit parou no lift48: " + d48["error"])
        elif 4294972605 not in d48["s48"]:
            failures.append(
                f"lift48 nao liftou 4294972605 "
                f"({len(d48['s48'])} candidatos s48)")
        else:
            print(f"  e2e lift48: s48={len(d48['s48'])} "
                  f"rank1={lo64[0]} em {time.time() - t0:.1f}s")
            t0 = time.time()
            d63 = lift63(lib, d48["s48"], mt, tolerance=E2E_TOLERANCE,
                         budget=0.0, java=java)
            dt63 = time.time() - t0
            if "error" in d63:
                failures.append(f"lift63: {d63['error']}")
            else:
                hit63 = next((r for r in d63["results"]
                              if r.get("seed_str") == "4294972605"), None)
                print(f"  e2e lift63: {dt63:.0f}s "
                      f"(nS48={len(d48['s48'])}, checked={d63['checked']}, "
                      f"timed_out={d63['timed_out']}, "
                      f"hit={'sim' if hit63 else 'NAO'})")
                if hit63 is None:
                    failures.append(
                        "seed 4294972605 ausente do lift63 "
                        f"({[r.get('seed_str') for r in d63['results'][:10]]})")

    # --- fixture 32-bit: sweep full-range -> lift48 sem ancoras ---------
    from test_native_crack import FIXTURE_8675309
    t0 = time.time()
    sw32 = sweep(lib, FIXTURE_8675309, tolerance=E2E_TOLERANCE, start=0,
                 end=1 << 32, max_results=2000, threads=threads)
    dt32 = time.time() - t0
    lo32 = [r["seed"] for r in sw32.get("results", [])]
    hit32 = next((r for r in sw32.get("results", []) if r["seed"] == 8675309),
                 None)
    print(f"  e2e sweep32: {len(lo32)} lo32 em {dt32:.0f}s "
          f"(checked={sw32.get('checked')}, timed_out={sw32.get('timed_out')}, "
          f"score do lo32 verdadeiro 8675309: "
          f"{hit32['score'] if hit32 else 'AUSENTE'})")
    if "error" in sw32:
        failures.append(f"sweep 32-bit: {sw32['error']}")
    elif hit32 is None:
        failures.append("lo32 8675309 fora do top-2000 do sweep full-range")

    if lo32:
        d48 = lift48(lib, FIXTURE_8675309, lo32, tolerance=E2E_TOLERANCE)
        if "error" in d48:
            failures.append(f"cadeia 32-bit parou no lift48: {d48['error']}")
        else:
            d32 = next((r for r in d48["direct32"] if r["seed"] == 8675309),
                       None)
            print(f"  e2e lift48 (32-bit, sem ancoras): "
                  f"direct32={len(d48['direct32'])}, s48={len(d48['s48'])}, "
                  f"hit={'sim' if d32 else 'NAO'}")
            if d32 is None:
                failures.append(
                    "8675309 ausente do direct32 "
                    f"({[r['seed'] for r in d48['direct32'][:10]]})")

    assert not failures, "\n".join(failures)
    print(f"  e2e total: sweep64 {dt64:.0f}s + sweep32 {dt32:.0f}s "
          f"(6 threads)")


def main():
    lib = ctypes.CDLL(DLL)
    mt = load_mt_structures()
    print("MT structures:", mt)
    test_sweep_finds_lo32_without_biome_gate(lib, mt)
    test_sweep_sloppy_coords(lib, mt)
    test_sweep_sorted_and_capped(lib, mt)
    test_sweep_validation(lib, mt)
    test_lift48_direct32_hit(lib)
    test_lift48_s48_lift(lib, mt)
    test_lift48_loose_anchor_guard(lib, mt)
    test_lift48_bad_inputs(lib)
    test_lift63_recovers_full_seed(lib, mt)
    test_lift63_wrong_s48_empty(lib, mt)
    test_lift63_32bit_seed_via_s48(lib)
    test_auto_pipeline_e2e(lib, mt)
    print("ALL AUTO SWEEP TESTS PASSED")


if __name__ == "__main__":
    sys.exit(main())
