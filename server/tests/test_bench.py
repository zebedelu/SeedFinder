"""Testes do benchmark F0 - run with: python server/tests/test_bench.py"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

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


def main():
    for fn in (test_names_and_shape, test_bedrock32_fixture,
               test_auto64_fixture_has_anchor, test_java_mitm_fixture,
               test_validate_and_unknown_name):
        fn()
        print(f"  ok  {fn.__name__}")
    print("test_bench (fixtures): OK")


if __name__ == "__main__":
    main()
