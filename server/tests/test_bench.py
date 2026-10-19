"""Testes do benchmark F0 - run with: python server/tests/test_bench.py"""
import argparse
import contextlib
import ctypes
import io
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from server import bench  # noqa: E402
from server import bench_fixtures  # noqa: E402


def test_names_and_shape():
    assert bench_fixtures.names() == ["bedrock32", "auto64", "java_mitm"]
    for name in bench_fixtures.names():
        fx = bench_fixtures.load(name)
        assert fx["seed"] > 0, name
        assert fx["source"], f"{name}: fixture sem procedencia"
        assert isinstance(fx["tolerance"], int) and fx["tolerance"] >= 0, name


def test_bedrock32_fixture():
    fx = bench_fixtures.load("bedrock32")
    assert fx["seed"] == 8675309
    assert fx["edition"] == "bedrock"
    assert len(fx["structures"]) == 4
    assert {s["type"] for s in fx["structures"]} == {5, 8}
    assert all(s["type"] not in (15, 23, 24) for s in fx["structures"])


def test_auto64_fixture_has_anchor():
    fx = bench_fixtures.load("auto64")
    assert fx["seed"] == 4294972605
    assert fx["seed"] & 0xFFFFFFFF == 5309, "lo32 documentado em test_crack_auto.py"
    assert len(fx["mt_structures"]) >= 2
    anchors = [s for s in fx["java_structures"] if s["type"] in (23, 24)]
    assert anchors, "auto/64-bit sem ancora Java nao cobre os bits altos"
    assert not any(s["type"] in (23, 24) for s in fx["mt_structures"])


def test_java_mitm_fixture():
    fx = bench_fixtures.load("java_mitm")
    assert fx["seed"] == 4294967300
    assert fx["tolerance"] == 0, "MITM exige tolerancia 0"
    assert len(fx["structures"]) == 6
    assert fx["seed"] >> 24 in range(*fx["u_hit"]), "a seed tem de estar na janela de acerto"
    assert not (fx["seed"] >> 24 in range(*fx["u_timing"])), "janela de timing isenta de acerto"


def test_validate_and_unknown_name():
    bench_fixtures.validate()
    try:
        bench_fixtures.load("nope")
    except KeyError as e:
        assert "nope" in str(e)
    else:
        raise AssertionError("load() deveria levantar KeyError")


def test_load_returns_isolated_copy():
    fx = bench_fixtures.load("bedrock32")
    fx["structures"].pop()
    fx["name"] = "corrompido"
    assert len(bench_fixtures.load("bedrock32")["structures"]) == 4
    assert bench_fixtures.load("bedrock32")["name"] == "bedrock32"


def test_rate_and_median():
    rows = [
        {"runner": "native", "checked": 100, "elapsed_ms": 1000.0, "timed_out": False},  # 100/s
        {"runner": "native", "checked": 400, "elapsed_ms": 1000.0, "timed_out": False},  # 400/s
        {"runner": "native", "checked": 400, "elapsed_ms": 2000.0, "timed_out": False},  # 200/s
    ]
    assert bench.rate(rows[0]) == 100.0
    best = bench.median_rows(rows)
    assert best["checked"] == 400 and best["elapsed_ms"] == 2000.0, best
    assert bench.rate(best) == 200.0


def test_median_keeps_real_measurement_over_failures():
    # Taxas [0, 0, R]: a mediana ingênua cai numa falha e joga fora a única
    # medição real. Prefere-se o subconjunto com taxa > 0.
    def failed():
        return {"runner": "native", "checked": 0, "elapsed_ms": 0.0,
                "timed_out": False, "valid": False, "error": "engine error: boom"}

    real = {"runner": "native", "checked": 1 << 20, "elapsed_ms": 1000.0,
            "timed_out": False, "valid": True}
    best = bench.median_rows([failed(), failed(), real])
    assert best is real, best
    assert bench.rate(best) == float(1 << 20)
    # Todas falharam: não há medição a preservar, cai para o conjunto todo.
    all_bad = [failed(), failed()]
    assert bench.median_rows(all_bad).get("error"), all_bad


def test_extrapolate_refuses_timeout():
    ok = {"checked": 1 << 20, "elapsed_ms": 1000.0, "timed_out": False}
    assert bench.extrapolate(ok, 1 << 32) == 4096.0 * 1000.0
    cut = dict(ok, timed_out=True)
    assert bench.extrapolate(cut, 1 << 32) is None


def test_render_marks_invalid_rows():
    rows = [
        {"runner": "native", "fixture": "bedrock32", "phase": "crack32", "unit": "seeds",
         "threads": 7, "checked": 1 << 20, "elapsed_ms": 1000.0, "rate": 1048576.0,
         "timed_out": False, "valid": True},
        {"runner": "wasm", "fixture": "bedrock32", "phase": "crack32", "unit": "seeds",
         "threads": 1, "checked": 1 << 18, "elapsed_ms": 4000.0, "rate": 65536.0,
         "timed_out": True, "valid": False},
    ]
    md = bench.render_markdown(rows)
    assert "| bedrock32 | crack32 |" in md
    assert "parcial" in md, "linha com timed_out tem de ser marcada, nao somada"


def test_native_runner_requires_symbols():
    class FakeLib:
        pass
    try:
        bench._bind(FakeLib())
    except RuntimeError as e:
        assert "seedfinder_crack" in str(e)
    else:
        raise AssertionError("lib sem simbolos deveria falhar com mensagem clara")


def test_auto64_has_engine_minimum_mt():
    # crackTargetsBuild exige >= 4 estruturas (core/crack_mt.c:361); com 2 o
    # modo auto devolve {"error":...} e nenhuma fase e' mensuravel.
    fx = bench_fixtures.load("auto64")
    assert len(fx["mt_structures"]) >= 4, fx["mt_structures"]
    coords = {(s["type"], s["x"], s["z"]) for s in fx["mt_structures"]}
    assert {(1, -3048, -296), (3, 2136, -280)} <= coords, coords


def test_rate_zero_when_no_elapsed():
    assert bench.rate({"checked": 10, "elapsed_ms": 0.0}) == 0.0
    assert bench.rate({"checked": 10, "elapsed_ms": -1.0}) == 0.0
    cut = {"checked": 0, "elapsed_ms": 0.0, "timed_out": False}
    assert bench.extrapolate(cut, 1 << 32) is None


def test_call_raises_on_engine_error():
    buf = ctypes.create_string_buffer(b'{"error":"at least 4 structures are required"}')
    ptr = ctypes.cast(buf, ctypes.c_void_p).value
    orig = bench.lib_free
    bench.lib_free = lambda _p: None
    try:
        try:
            bench._call(lambda *a: ptr)
        except RuntimeError as e:
            assert "at least 4 structures" in str(e), e
        else:
            raise AssertionError("erro do engine deveria virar RuntimeError")
    finally:
        bench.lib_free = orig


def test_phase_error_row_has_no_fake_rate():
    def boom(*a):
        raise RuntimeError("engine error: at least 4 structures are required")
    data, row = bench._timed_row("native", "auto64", "sweep", "seeds", 5, boom)
    assert data is None, data
    assert row["valid"] is False and row["checked"] == 0 and row["elapsed_ms"] == 0.0, row
    assert "at least 4 structures" in row["error"], row
    assert bench.rate(row) == 0.0


def test_render_shows_engine_error():
    rows = [{"runner": "native", "fixture": "auto64", "phase": "sweep", "unit": "seeds",
             "threads": 5, "checked": 0, "elapsed_ms": 0.0, "rate": 0.0,
             "timed_out": False, "valid": False,
             "error": "engine error: at least 4 structures are required"}]
    md = bench.render_markdown(rows)
    assert "**erro: engine error: at least 4 structures are required**" in md, md


def test_staleness_warnings():
    stale = bench.staleness_warnings(1.0, 2.0)
    assert len(stale) == 1 and "seedfinder_lib.dll" in stale[0], stale
    assert bench.staleness_warnings(2.0, 1.0) == []
    both = bench.staleness_warnings(1.0, 2.0, wasm_mtime=1.5)
    assert len(both) == 2 and any("wasm/out" in m for m in both), both
    assert len(bench.staleness_warnings(1.0, 2.0, wasm_mtime=3.0)) == 1


class _FakeLib:
    """Lib falsa: devolve JSON por ponteiro e registra as chamadas (sem DLL)."""

    def __init__(self, sweep_results, lift48_seq):
        self.calls = []
        self._bufs = []
        self._sweep_results = sweep_results
        self._lift48_seq = list(lift48_seq)

    def _ret(self, obj):
        buf = ctypes.create_string_buffer(json.dumps(obj).encode("utf-8"))
        self._bufs.append(buf)  # mantem vivo ate o string_at do _call
        return ctypes.cast(buf, ctypes.c_void_p).value

    def seedfinder_crack_auto_sweep(self, *a):
        self.calls.append(("sweep", a))
        return self._ret({"results": [{"seed": s, "score": i}
                                      for i, s in enumerate(self._sweep_results)],
                          "checked": 1 << 20, "timed_out": False, "threads": 4})

    def seedfinder_crack_auto_lift48(self, *a):
        self.calls.append(("lift48", a))
        return self._ret(self._lift48_seq.pop(0))

    def seedfinder_crack_auto_lift63(self, *a):
        self.calls.append(("lift63", a))
        return self._ret({"results": [], "checked": 1, "timed_out": False})

    def seedfinder_free_result(self, ptr):
        pass


def _run_fake(fx, fake):
    orig = bench._lib_handle
    bench._lib_handle = fake
    try:
        return bench.run_native(fx, 1 << 20, 1, fake, full=False)
    finally:
        bench._lib_handle = orig


def _assert_non_empty_arrays(calls):
    for name, args in calls:
        if name == "sweep":
            assert args[1] > 0, (name, args[1])
        elif name == "lift48":
            assert args[6] > 0 and len(args[5]) == args[6], (name, args[6])
        else:  # lift63
            assert args[1] > 0 and len(args[0]) == args[1], (name, args[1])


def _assert_fixture_tolerances(calls, tol):
    for name, args in calls:
        if name == "sweep":
            assert all(t == tol for t in args[4]), list(args[4])
        elif name == "lift48":
            assert all(t == tol for t in args[3]), list(args[3])
            assert all(t == tol for t in args[10]), list(args[10])


def test_auto_pipeline_skips_lift63_without_s48():
    fx = bench_fixtures.load("auto64")
    guard = ("engine error: too many 48-bit candidates (3229) - add more Trial "
             "Chambers/Trail Ruins or enter their corner coordinates more precisely")
    fake = _FakeLib([5309, 41840], [{"direct32": [], "s48": []},
                                    {"error": guard[len("engine error: "):]}])
    rows = _run_fake(fx, fake)
    assert [r["phase"] for r in rows] == ["sweep", "lift48", "lift48_rank1", "lift63"]
    assert rows[0]["found_lo32"] is True, rows[0]
    assert rows[0]["top_seeds"] == ["5309", "41840"], rows[0]
    assert "contrato" in rows[1]["note"], rows[1]
    assert rows[2]["skipped"] is True and "too many 48-bit" in rows[2]["note"], rows[2]
    assert rows[3]["skipped"] is True, rows[3]
    assert [c[0] for c in fake.calls] == ["sweep", "lift48", "lift48"], fake.calls
    nlo32 = [c[1][6] for c in fake.calls if c[0] == "lift48"]
    assert nlo32 == [2, 1], nlo32  # fase 2 = lista inteira; fase 3 = rank-1
    _assert_non_empty_arrays(fake.calls)
    _assert_fixture_tolerances(fake.calls, fx["tolerance"])


def test_auto_pipeline_runs_lift63_when_s48_present():
    fx = bench_fixtures.load("auto64")
    fake = _FakeLib([5309], [{"s48": [5309]}, {"s48": [4294967296]}])
    rows = _run_fake(fx, fake)
    assert [r["phase"] for r in rows] == ["sweep", "lift48", "lift48_rank1", "lift63"]
    assert not rows[3].get("skipped"), rows[3]
    assert [c[0] for c in fake.calls] == ["sweep", "lift48", "lift48", "lift63"]
    _assert_non_empty_arrays(fake.calls)
    _assert_fixture_tolerances(fake.calls, fx["tolerance"])


class _NullRank1Lib(_FakeLib):
    """Fase 3 devolve NULL (ponteiro perdido): falha real, não pedido de âncoras."""

    def seedfinder_crack_auto_lift48(self, *a):
        self.calls.append(("lift48", a))
        if a[6] == 1:            # nLo32 == 1 -> rank-1 (fase 3)
            return 0
        return self._ret({"direct32": [], "s48": []})


def test_phase3_guard_message_is_skip():
    fx = bench_fixtures.load("auto64")
    guard = ("too many 48-bit candidates (3229) - add more Trial Chambers/Trail "
             "Ruins or enter their corner coordinates more precisely")
    fake = _FakeLib([5309], [{"s48": []}, {"error": guard}])
    rows = _run_fake(fx, fake)
    assert rows[2]["skipped"] is True and "too many 48-bit" in rows[2]["note"], rows[2]
    assert "skipped" in bench.render_markdown([rows[2]])


def test_phase3_null_pointer_is_error_not_skip():
    fx = bench_fixtures.load("auto64")
    fake = _NullRank1Lib([5309], [])
    rows = _run_fake(fx, fake)
    rank1 = rows[2]
    assert rank1["phase"] == "lift48_rank1", rank1
    assert not rank1.get("skipped"), rank1
    assert rank1["valid"] is False and rank1["checked"] == 0, rank1
    assert rank1["elapsed_ms"] == 0.0, rank1
    assert "returned NULL" in rank1["error"], rank1
    assert bench.rate(rank1) == 0.0
    # Skip diz "nada a medir"; NULL é falha de medição -> coluna de erro.
    md = bench.render_markdown([rank1])
    assert "**erro:" in md and "skipped" not in md, md


def test_repeat_must_be_positive():
    assert bench._positive_int("3") == 3
    for bad in ("0", "-2"):
        try:
            bench._positive_int(bad)
        except argparse.ArgumentTypeError as e:
            assert ">= 1" in str(e), e
        else:
            raise AssertionError(f"--repeat {bad} deveria ser recusado com mensagem")


def test_wasm_out_gets_esm_package_json():
    with tempfile.TemporaryDirectory() as tmp:
        assert bench._ensure_wasm_esm(tmp) is True
        pkg = os.path.join(tmp, "package.json")
        with open(pkg, encoding="utf-8") as f:
            assert json.load(f) == {"type": "module"}
        # Um package.json existente nunca é sobrescrito.
        with open(pkg, "w", encoding="utf-8") as f:
            f.write('{"type": "commonjs"}')
        assert bench._ensure_wasm_esm(tmp) is False
        with open(pkg, encoding="utf-8") as f:
            assert json.load(f) == {"type": "commonjs"}


def test_wasm_runner_degrades_without_build():
    os.environ["SEEDFINDER_BENCH_NO_WASM"] = "1"
    try:
        assert bench.run_wasm({"name": "bedrock32"}, 1 << 26) is None
    finally:
        del os.environ["SEEDFINDER_BENCH_NO_WASM"]


class _FakeRun:
    """CompletedProcess de mentira: so' o que run_wasm le'."""

    def __init__(self, returncode, stdout, stderr=""):
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr


@contextlib.contextmanager
def _stub_wasm(calls, results, build=True, raise_exc=None):
    """bench.ROOT num diretorio temporario + subprocess.run stubado: testa o
    caminho do runner sem precisar do build WASM nem do Node."""
    with tempfile.TemporaryDirectory() as tmp:
        if build:
            # Um "build" de mentira: run_wasm so' confere que os arquivos existem.
            os.makedirs(os.path.join(tmp, "wasm", "out"))
            os.makedirs(os.path.join(tmp, "server"))
            for rel in (("wasm", "out", "seedfinder.js"), ("server", "bench_wasm.mjs")):
                with open(os.path.join(tmp, *rel), "w", encoding="utf-8"):
                    pass
        orig_root, orig_run = bench.ROOT, bench.subprocess.run

        def fake_run(*a, **k):
            calls.append(a)
            if raise_exc is not None:
                raise raise_exc
            return results[min(len(calls) - 1, len(results) - 1)]

        bench.ROOT, bench.subprocess.run = tmp, fake_run
        try:
            with contextlib.redirect_stdout(io.StringIO()):
                yield tmp
        finally:
            bench.ROOT, bench.subprocess.run = orig_root, orig_run


_FX32 = {"name": "bedrock32", "mode": "bedrock32"}


def test_wasm_runner_failure_is_error_row_not_none():
    # Runner quebrado nao pode SUMIR do artefato: exit != 0 vira linha invalida
    # com o codigo e o stderr (o stderr nao faz parte do JSON/markdown).
    calls = []
    with _stub_wasm(calls, [_FakeRun(1, "not json{", "boom\nline 2")]):
        rows = bench.run_wasm(_FX32, 1 << 20)
    assert isinstance(rows, list) and len(rows) == 1, rows
    r = rows[0]
    assert r["runner"] == "wasm" and r["valid"] is False, r
    assert r["checked"] == 0 and r["elapsed_ms"] == 0.0, r
    assert bench.rate(r) == 0.0, r
    assert "exit 1" in r["error"] and "boom" in r["error"], r
    assert "**erro:" in bench.render_markdown([r])

    # stdout vazio ou que nao e' a lista de linhas tambem nao pode virar None.
    for bad_stdout in ("", "   ", '{"oi":1}', "[]"):
        with _stub_wasm([], [_FakeRun(0, bad_stdout)]):
            rows = bench.run_wasm(_FX32, 1 << 20)
        assert isinstance(rows, list) and rows[0]["valid"] is False, (bad_stdout, rows)

    # None SO' sem build...
    with _stub_wasm([], [], build=False):
        assert bench.run_wasm(_FX32, 1 << 20) is None
    # ...ou sem node no PATH.
    with _stub_wasm([], [], raise_exc=FileNotFoundError()):
        assert bench.run_wasm(_FX32, 1 << 20) is None


def test_wasm_runner_repeats_and_reduces_with_median():
    def one(checked, ms):
        return json.dumps([{"runner": "wasm", "fixture": "bedrock32",
                            "phase": "crack32", "unit": "seeds", "threads": 1,
                            "checked": checked, "elapsed_ms": ms,
                            "timed_out": False, "valid": True}])

    calls = []
    results = [_FakeRun(0, one(100, 1000.0)),    # 100/s
               _FakeRun(0, one(400, 1000.0)),    # 400/s
               _FakeRun(0, one(400, 2000.0))]    # 200/s
    with _stub_wasm(calls, results):
        rows = bench.run_wasm(_FX32, 1 << 20, repeat=3)
    assert len(calls) == 3, calls                # uma execucao do node por repeticao
    assert len(rows) == 1, rows
    assert bench.rate(rows[0]) == 200.0, rows[0]  # mediana, igual ao lado nativo
    assert rows[0]["space"] == 1 << 20, rows[0]
    assert rows[0]["extrapolated_full_ms"] is None, rows[0]


def test_wasm_reduce_keeps_phase_order_and_skips():
    def r(phase, checked, ms, **kw):
        row = {"runner": "wasm", "fixture": "auto64", "phase": phase, "unit": "u",
               "threads": 1, "checked": checked, "elapsed_ms": ms,
               "timed_out": False, "valid": True}
        row.update(kw)
        return row

    def skip(phase, note):
        return r(phase, 0, 0.0, valid=False, skipped=True, note=note)

    runs = [[r("sweep", 100, 1000.0), r("lift48", 2, 10.0),
             skip("lift48_rank1", "too many 48-bit candidates (3229)"),
             skip("lift63", "sem s48 da fase 3 - lift63 nao roda")],
            [r("sweep", 200, 1000.0), r("lift48", 4, 10.0),
             r("lift48_rank1", 1, 500.0), r("lift63", 8, 1000.0)]]
    rows = bench._reduce_wasm_runs(runs)
    assert [x["phase"] for x in rows] == ["sweep", "lift48", "lift48_rank1",
                                          "lift63"], rows
    assert bench.rate(rows[0]) == 200.0, rows[0]     # mediana das taxas
    # A execucao que MEDIU vence a que so' pulou: medicao real nao se descarta.
    assert rows[2]["valid"] is True and bench.rate(rows[2]) == 2.0, rows[2]
    assert rows[3]["checked"] == 8, rows[3]
    # Fase que so' foi skipped em todas as execucoes continua skipped.
    only_skip = [[skip("lift63", "sem s48 da fase 3"), skip("lift63", "sem s48 da fase 3")]]
    assert bench._reduce_wasm_runs(only_skip)[0].get("skipped") is True


def test_wasm_timeout_covers_engine_budget():
    # O subprocesso nao pode morrer antes do proprio orcamento do motor acabar:
    # o engine tem de chegar a reportar `timed_out` (o JS passa FULL_BUDGET por
    # chamada com orcamento, como o nativo).
    assert bench._wasm_timeout("bedrock32", 3) > 3 * bench.FULL_BUDGET
    assert bench._wasm_timeout("auto", 1) > 2 * bench.FULL_BUDGET   # sweep + lift63
    assert bench._wasm_timeout("java_mitm", 1) > bench.FULL_BUDGET
    assert bench._wasm_timeout("modo_novo", 1) > bench.FULL_BUDGET  # 1 chamada
    assert bench._wasm_timeout("bedrock32", 2) > bench._wasm_timeout("bedrock32", 1)


def main():
    for fn in (test_names_and_shape, test_bedrock32_fixture,
               test_auto64_fixture_has_anchor, test_java_mitm_fixture,
               test_validate_and_unknown_name, test_load_returns_isolated_copy,
               test_auto64_has_engine_minimum_mt, test_rate_and_median,
               test_median_keeps_real_measurement_over_failures,
               test_extrapolate_refuses_timeout, test_rate_zero_when_no_elapsed,
               test_render_marks_invalid_rows, test_render_shows_engine_error,
               test_staleness_warnings, test_native_runner_requires_symbols,
               test_call_raises_on_engine_error,
               test_phase_error_row_has_no_fake_rate,
               test_auto_pipeline_skips_lift63_without_s48,
               test_auto_pipeline_runs_lift63_when_s48_present,
               test_phase3_guard_message_is_skip,
               test_phase3_null_pointer_is_error_not_skip,
               test_repeat_must_be_positive,
               test_wasm_out_gets_esm_package_json,
               test_wasm_runner_degrades_without_build,
               test_wasm_runner_failure_is_error_row_not_none,
               test_wasm_runner_repeats_and_reduces_with_median,
               test_wasm_reduce_keeps_phase_order_and_skips,
               test_wasm_timeout_covers_engine_budget):
        fn()
        print(f"  ok  {fn.__name__}")
    print("test_bench (fixtures): OK")


if __name__ == "__main__":
    main()
