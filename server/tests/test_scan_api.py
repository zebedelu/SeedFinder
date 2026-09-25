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
    test_first_letter_mc()
    test_path_fixed_wins()
    test_edition_dispatch()
    test_all_17_ids_java()
    test_negative_seed()
    test_stale_lib_503()
    test_post_get_parity()
    test_post_edition_rules()
    test_post_malformed_bodies()
    test_golden_java()
    print("scan API GET tests OK")


def test_routes_exist():
    for path in ("/scan", "/scan/java", "/scan/bedrock"):
        r = get(path, **BASE)
        assert r.status_code == 200, (path, r.status_code, r.get_json())
        assert "results" in r.get_json(), path


def test_first_letter_mc():
    bed = get("/scan/bedrock", **BASE).get_json()
    jav = get("/scan/java", **BASE).get_json()

    # j... -> java (typo tolerated), b... -> bedrock, case-insensitive
    for v in ("java", "jova", "JAVA"):
        assert get("/scan", **BASE, mc=v).get_json() == jav, v
    for v in ("bedrock", "b"):
        assert get("/scan", **BASE, mc=v).get_json() == bed, v

    # absent -> bedrock (default)
    assert get("/scan", **BASE).get_json() == bed

    # any other first letter -> 400 with the mc param flagged
    r = get("/scan", **BASE, mc="x")
    assert r.status_code == 400, r.status_code
    assert r.get_json()["missing_or_invalid"] == ["mc"], r.get_json()


def test_path_fixed_wins():
    # mc is silently ignored on fixed paths (spec decision B)
    a = get("/scan/java", **BASE, mc="bedrock").get_json()
    b = get("/scan/java", **BASE).get_json()
    assert a == b, "mc must be ignored on /scan/java"
    a = get("/scan/bedrock", **BASE, mc="java").get_json()
    b = get("/scan/bedrock", **BASE).get_json()
    assert a == b, "mc must be ignored on /scan/bedrock"


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


POST_PAYLOAD = {"seed": 31415, "x": 0, "z": 0, "radius": 100,
                "max": 50, "types": "5,8,9"}


def test_post_get_parity():
    g = get("/scan", **BASE).get_json()
    p = client.post("/scan", json=POST_PAYLOAD)
    assert p.status_code == 200, (p.status_code, p.get_json())
    assert p.get_json() == g, "GET and POST must return identical results"

    g = get("/scan/java", **BASE).get_json()
    p = client.post("/scan/java", json=POST_PAYLOAD)
    assert p.status_code == 200, (p.status_code, p.get_json())
    assert p.get_json() == g

    g = get("/scan/bedrock", **BASE).get_json()
    p = client.post("/scan/bedrock", json=POST_PAYLOAD)
    assert p.status_code == 200, (p.status_code, p.get_json())
    assert p.get_json() == g


def test_post_edition_rules():
    # path wins: mc in the body is silently ignored
    a = client.post("/scan/java", json={**POST_PAYLOAD, "mc": "bedrock"})
    b = client.post("/scan/java", json=POST_PAYLOAD)
    assert a.get_json() == b.get_json(), "mc must be ignored on POST /scan/java"

    # first-letter rule on POST /scan
    jav = client.post("/scan", json={**POST_PAYLOAD, "mc": "jova"})
    bed = client.post("/scan", json={**POST_PAYLOAD, "mc": "bedrock"})
    ref_j = client.post("/scan/java", json=POST_PAYLOAD)
    ref_b = client.post("/scan/bedrock", json=POST_PAYLOAD)
    assert jav.get_json() == ref_j.get_json()
    assert bed.get_json() == ref_b.get_json()

    # bad first letter -> 400
    r = client.post("/scan", json={**POST_PAYLOAD, "mc": "x"})
    assert r.status_code == 400
    assert r.get_json()["missing_or_invalid"] == ["mc"]

    # non-string mc -> 400
    r = client.post("/scan", json={**POST_PAYLOAD, "mc": 123})
    assert r.status_code == 400
    assert r.get_json()["missing_or_invalid"] == ["mc"]


def test_post_malformed_bodies():
    # non-object JSON body -> 400
    r = client.post("/scan", json=[1, 2, 3])
    assert r.status_code == 400, r.status_code
    r = client.post("/scan", data="not json",
                    content_type="application/json")
    assert r.status_code == 400, r.status_code
    # form-encoded non-empty body -> 400 (POST is JSON-body only)
    r = client.post("/scan", data="seed=1",
                    content_type="application/x-www-form-urlencoded")
    assert r.status_code == 400, r.status_code

    # empty/absent body -> defaults, 200
    r = client.post("/scan")
    assert r.status_code == 200, r.status_code

    # types must be a string in JSON (comma-separated, like the query string)
    r = client.post("/scan", json={"types": [5, 8, 9]})
    assert r.status_code == 400
    assert "comma-separated" in r.get_json()["error"]
    r = client.post("/scan", json={"types": 5})
    assert r.status_code == 400

    # bool rejected for numeric params
    r = client.post("/scan", json={"seed": True})
    assert r.status_code == 400, r.status_code

    # native JSON numbers accepted (int seed, int x/z)
    r = client.post("/scan/java", json={"seed": 8675309, "x": 0, "z": 0,
                                        "radius": 50, "max": 5, "types": "5"})
    assert r.status_code == 200, (r.status_code, r.get_json())


# Verificado contra o Chunkbase Seed Map (plataforma Java, versao 1.21),
# seed 8675309, jogador (0,0), radius 100 chunks, em 2026-09-24 por
# comparacao manual humana: as 16 posicoes casaram com as camadas do
# Chunkbase (Village + Ocean Monument) dentro de ~10 blocos (offset de
# ancora do Chunkbase); nenhuma Mansion dentro do radius.
# Ver tambem Task 4 do plano para o procedimento.
GOLDEN_JAVA_QUERY = {"seed": "8675309", "x": "0", "z": "0",
                     "radius": "100", "max": "50", "types": "5,8,9"}
GOLDEN_JAVA_RESULTS = [
    {"name": "village", "x": -304, "z": -320, "distance": 27.6},
    {"name": "village", "x": -272, "z": 352, "distance": 27.8},
    {"name": "monument", "x": 544, "z": 176, "distance": 35.7},
    {"name": "village", "x": -192, "z": -704, "distance": 45.6},
    {"name": "village", "x": -736, "z": 176, "distance": 47.3},
    {"name": "village", "x": -336, "z": 768, "distance": 52.4},
    {"name": "village", "x": -880, "z": -352, "distance": 59.2},
    {"name": "monument", "x": 752, "z": 752, "distance": 66.5},
    {"name": "village", "x": -224, "z": 1232, "distance": 78.3},
    {"name": "village", "x": -704, "z": 1120, "distance": 82.7},
    {"name": "monument", "x": 1296, "z": 272, "distance": 82.8},
    {"name": "village", "x": -1424, "z": 144, "distance": 89.5},
    {"name": "village", "x": -1312, "z": 592, "distance": 90.0},
    {"name": "monument", "x": 1280, "z": 736, "distance": 92.3},
    {"name": "village", "x": -544, "z": -1424, "distance": 95.3},
    {"name": "monument", "x": 896, "z": 1280, "distance": 97.7},
]


def test_golden_java():
    r = get("/scan/java", **GOLDEN_JAVA_QUERY)
    assert r.status_code == 200, r.status_code
    assert r.get_json()["results"] == GOLDEN_JAVA_RESULTS, (
        "saida Java divergiu do golden verificado"
    )


if __name__ == "__main__":
    main()
