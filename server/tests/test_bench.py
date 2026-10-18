"""Testes do benchmark F0 - run with: python server/tests/test_bench.py"""
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


def main():
    for fn in (test_names_and_shape, test_bedrock32_fixture,
               test_auto64_fixture_has_anchor, test_java_mitm_fixture,
               test_validate_and_unknown_name, test_load_returns_isolated_copy,
               test_rate_and_median, test_extrapolate_refuses_timeout,
               test_render_marks_invalid_rows, test_native_runner_requires_symbols):
        fn()
        print(f"  ok  {fn.__name__}")
    print("test_bench (fixtures): OK")


if __name__ == "__main__":
    main()
