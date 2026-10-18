"""Testes do benchmark F0 - run with: python server/tests/test_bench.py"""
import ctypes
import json
import os
import sys

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


def main():
    for fn in (test_names_and_shape, test_bedrock32_fixture,
               test_auto64_fixture_has_anchor, test_java_mitm_fixture,
               test_validate_and_unknown_name, test_load_returns_isolated_copy,
               test_auto64_has_engine_minimum_mt, test_rate_and_median,
               test_extrapolate_refuses_timeout, test_rate_zero_when_no_elapsed,
               test_render_marks_invalid_rows, test_render_shows_engine_error,
               test_staleness_warnings, test_native_runner_requires_symbols,
               test_call_raises_on_engine_error,
               test_phase_error_row_has_no_fake_rate,
               test_auto_pipeline_skips_lift63_without_s48,
               test_auto_pipeline_runs_lift63_when_s48_present):
        fn()
        print(f"  ok  {fn.__name__}")
    print("test_bench (fixtures): OK")


if __name__ == "__main__":
    main()
