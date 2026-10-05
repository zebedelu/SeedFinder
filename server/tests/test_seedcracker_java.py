"""Java SeedCracker route tests - run with: python server/tests/test_seedcracker_java.py
Needs the DLL built (server/start.bat) with seedfinder_crack_java."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from server.app import create_app  # noqa: E402
from server.app import native  # noqa: E402

app = create_app()
native.load_lib(r"build_server\seedfinder_lib.dll")

# Real Java structures of seed 4294972605 (generated via getStructurePos,
# MC_NEWEST): corner BLOCK coordinates, tolerance 0, exact placement.
FIXTURE = [
    {"type": 3, "x": 1088, "z": 688},     # Swamp Hut  (chunk 68, 43)
    {"type": 4, "x": 256, "z": 80},       # Igloo      (chunk 16, 5)
    {"type": 5, "x": 320, "z": 64},       # Village    (chunk 20, 4)
    {"type": 8, "x": 208, "z": -816},     # Monument   (chunk 13, -51)
]

WINDOW = {"start": 4294972605 - 5000, "end": 4294972605 + 5000,
          "tolerance": 0, "max": 2000, "max_seconds": 120}


def main():
    assert hasattr(native.lib, "seedfinder_crack_java"), (
        "seedfinder_crack_java ausente - rebuild o DLL")

    # 1. /seedcracker com mc=java recupera o fixture
    r = app.test_client().post("/seedcracker", json={
        **WINDOW, "mc": "java",
        "structures": FIXTURE})
    body = r.get_json()
    assert r.status_code == 200, (r.status_code, body)
    assert isinstance(body, list), body
    assert body[0]["status"] == "ok", body[0]
    assert body[0]["edition"] == "java", body[0]
    assert body[0]["bits"] == 48, body[0]
    seeds = [s["seed"] for s in body[1:]]
    assert 4294972605 in seeds, f"seed missing: {seeds[:20]}"

    # 2. /seedcracker/java (POST) ignora mc e funciona igual
    r = app.test_client().post("/seedcracker/java", json={
        **WINDOW, "mc": "bedrock",
        "structures": FIXTURE})
    body = r.get_json()
    assert r.status_code == 200 and 4294972605 in [s["seed"] for s in body[1:]], body

    # 3. /seedcracker/java (GET query) mesmo resultado
    q = "3,1088,688;4,256,80;5,320,64;8,208,-816"
    r = app.test_client().get("/seedcracker/java", query_string={
        **WINDOW, "structures": q})
    body = r.get_json()
    assert r.status_code == 200 and 4294972605 in [s["seed"] for s in body[1:]], body

    # 4. /seedcracker/bedrock continua no comportamento Bedrock (fixture 8675309)
    r = app.test_client().post("/seedcracker", json={
        "tolerance": 0, "start": 0, "end": 16_000_000,
        "max": 2000, "max_seconds": 120,
        "structures": [
            {"type": 5, "x": -280, "z": 152}, {"type": 5, "x": -280, "z": -360},
            {"type": 8, "x": 696, "z": 360}, {"type": 8, "x": 712, "z": 760},
        ]})
    body = r.get_json()
    assert r.status_code == 200 and 8675309 in [s["seed"] for s in body[1:]], body

    # 5. default (sem mc) = bedrock (mesmo fixture acima)
    # 6. mc=java + mode=64 -> 400
    r = app.test_client().post("/seedcracker", json={
        "mc": "java", "mode": "64", "structures": FIXTURE})
    body = r.get_json()
    assert r.status_code == 400 and "Bedrock-only" in body[0]["message"], body

    # 7. mc=java + Mineshaft -> 400
    r = app.test_client().post("/seedcracker", json={
        "mc": "java", "structures": FIXTURE[:3] + [{"type": 15, "x": 0, "z": 0}]})
    body = r.get_json()
    assert r.status_code == 400 and "mineshaft" in body[0]["message"].lower(), body

    # 8. mc invalido -> 400
    r = app.test_client().post("/seedcracker", json={
        "mc": "x", "structures": FIXTURE})
    body = r.get_json()
    assert r.status_code == 400 and "mc" in body[0]["message"], body

    print("java seedcracker routes + mc dispatch OK")

    # 9. tolerance > 0 + full-range com estruturas grandes: o novo LIFTING
    #    tolerante responde com o guard acionavel (~1 s), nao um hang.
    #    (4a estrutura adicionada: a rota exige MIN_STRUCTURES=4 - o C aceita
    #    1-24, mas a API valida 4+ antes de chegar ao motor.)
    r = app.test_client().post("/seedcracker/java", json={
        "tolerance": 8, "start": 0, "end": 1 << 48, "max": 2000, "max_seconds": 30,
        "structures": [
            {"type": 5, "x": -176, "z": 48},      # Village
            {"type": 9, "x": 336, "z": -80},      # Mansion
            {"type": 8, "x": 208, "z": -816},     # Monument
            {"type": 4, "x": 256, "z": 80},       # Igloo
        ]})
    body = r.get_json()
    assert r.status_code == 200, (r.status_code, body)
    assert body[0]["status"] == "error" and "infeasible" in body[0]["message"], body[0]

    # 10. budget Java alto (full-range leva minutos) aceita max_seconds=2400
    #     (o clamp de 120 da rota 32-bit nao se aplica mais ao Java).
    import server.app.seedcracker as sc
    assert sc.MAX_MAX_SECONDS_JAVA == 2400.0, sc.MAX_MAX_SECONDS_JAVA

    print("java seedcracker routes + tolerant lift guard OK")


if __name__ == "__main__":
    main()
