"""SeedCracker 64-bit route tests - run: python server/tests/test_crack64_api.py"""
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


def test_mode64_requires_java_anchor():
    # 1. modo 64 sem ancora Java -> 400 com mensagem explicita
    r = client.post("/seedcracker", json={
        "mode": "64", "tolerance": 0, "end": 2**63,
        "structures": [
            {"type": 1, "x": -280, "z": 152}, {"type": 1, "x": -280, "z": -360},
            {"type": 5, "x": 696, "z": 360},  {"type": 5, "x": 712, "z": 760}]})
    body = r.get_json()
    assert r.status_code == 400, (r.status_code, body)
    assert "trial chamber" in body[0]["message"].lower() or \
           "java-style" in body[0]["message"].lower(), body


def test_mode64_list_envelope():
    # 2. modo 64 aceita classes separadas e devolve envelope de lista
    r = client.post("/seedcracker", json={
        "mode": "64", "tolerance": 0,
        "mt_structures": [
            {"type": 1, "x": -280, "z": 152}, {"type": 1, "x": -280, "z": -360}],
        "java_structures": [
            {"type": 24, "x": 8, "z": 8}]})
    body = r.get_json()
    assert r.status_code == 200, (r.status_code, body)
    assert isinstance(body, list) and "status" in body[0], body
    if body[0].get("status") == "ok":
        for cand in body[1:]:
            assert isinstance(cand["seed"], int)      # signed
            assert isinstance(cand["seed_str"], str)  # mesmo valor em string p/ JS


def main():
    fns = [v for k, v in sorted(globals().items())
           if k.startswith("test_") and callable(v)]
    for fn in fns:
        fn()
    print("CRACK64 API TESTS OK")


if __name__ == "__main__":
    main()
