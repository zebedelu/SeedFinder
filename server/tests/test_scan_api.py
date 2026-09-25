"""Scan route tests - run with: python server/tests/test_scan_api.py  (repo root)"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from server.app import create_app  # noqa: E402
from server.app import native  # noqa: E402

app = create_app()

# Force the local build. Must run AFTER create_app() because the factory
# re-runs bootstrap_lib().
native.load_lib(r"build_server\seedfinder_lib.dll")

client = app.test_client()

BASE = {"seed": "31415", "x": "0", "z": "0", "radius": "100",
        "max": "50", "types": "5,8,9"}


def get(path, **params):
    return client.get(path, query_string=params)


def main():
    test_routes_exist()
    test_first_letter_version()
    test_path_fixed_wins()
    test_edition_dispatch()
    test_all_17_ids_java()
    test_negative_seed()
    test_stale_lib_503()
    print("scan API GET tests OK")


def test_routes_exist():
    for path in ("/scan", "/scan/java", "/scan/bedrock"):
        r = get(path, **BASE)
        assert r.status_code == 200, (path, r.status_code, r.get_json())
        assert "results" in r.get_json(), path


def test_first_letter_version():
    bed = get("/scan/bedrock", **BASE).get_json()
    jav = get("/scan/java", **BASE).get_json()

    # j... -> java (typo tolerated), b... -> bedrock, case-insensitive
    for v in ("java", "jova", "JAVA"):
        assert get("/scan", **BASE, version=v).get_json() == jav, v
    for v in ("bedrock", "b"):
        assert get("/scan", **BASE, version=v).get_json() == bed, v

    # absent -> bedrock (default)
    assert get("/scan", **BASE).get_json() == bed

    # any other first letter -> 400 with the version flagged
    r = get("/scan", **BASE, version="x")
    assert r.status_code == 400, r.status_code
    assert r.get_json()["missing_or_invalid"] == ["version"], r.get_json()


def test_path_fixed_wins():
    # version is silently ignored on fixed paths (spec decision B)
    a = get("/scan/java", **BASE, version="bedrock").get_json()
    b = get("/scan/java", **BASE).get_json()
    assert a == b, "version must be ignored on /scan/java"
    a = get("/scan/bedrock", **BASE, version="java").get_json()
    b = get("/scan/bedrock", **BASE).get_json()
    assert a == b, "version must be ignored on /scan/bedrock"


def test_edition_dispatch():
    bed = get("/scan/bedrock", **BASE).get_json()
    jav = get("/scan/java", **BASE).get_json()
    assert bed["results"], "bedrock empty for seed 31415"
    assert jav["results"], "java empty for seed 31415 - try radius=300"
    assert bed != jav, "editions returned identical results"
    for r in bed["results"] + jav["results"]:
        assert set(r) == {"name", "x", "z", "distance"}, r


def test_all_17_ids_java():
    ids = "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,23,24"
    r = get("/scan/java", seed="8675309", x="0", z="0",
            radius="50", max="20", types=ids)
    assert r.status_code == 200, (r.status_code, r.get_json())


def test_negative_seed():
    r = get("/scan/java", seed="-12345", x="0", z="0",
            radius="100", max="20", types="5")
    assert r.status_code == 200, (r.status_code, r.get_json())


def test_stale_lib_503():
    real = native.lib

    class StaleLib:
        """Simula um .so antigo: so tem os symbols Bedrock."""
        def seedfinder_status(self):
            return b'{"status": "ok"}'

    native.lib = StaleLib()
    try:
        r = get("/scan/java", **BASE)
        assert r.status_code == 503, r.status_code
        body = r.get_json()
        assert "Java" in body["error"] or "java" in body.get("hint", "").lower(), body
        assert body["results"] == []
        # Bedrock route degrades differently: symbol lookup on the stub would
        # fail -> the route must still not return java results. It returns
        # 500 (broken lib) which is acceptable; only Java's 503 is asserted.
    finally:
        native.lib = real


if __name__ == "__main__":
    main()
