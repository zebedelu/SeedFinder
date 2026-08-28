"""SeedCracker route tests - run with: python server/tests/test_seedcracker.py

Forces the Windows DLL explicitly because bootstrap picks the checked-in
server/seedfinder_lib.so first on a Windows dev machine.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
os.environ.setdefault("VERCEL", "0")

from server.app import create_app  # noqa: E402
from server.app import native  # noqa: E402

app = create_app()

# Force the local build. Must run AFTER create_app() because the factory
# re-runs bootstrap_lib() (which prefers the checked-in Vercel .so on
# Windows and would otherwise reset the global lib to None).
native.load_lib(r"build_server\seedfinder_lib.dll")


def post(payload, **kw):
    return app.test_client().post("/seedcracker", json=payload, **kw)


def get(**kw):
    return app.test_client().get("/seedcracker", **kw)


def main():
    import server.app.seedcracker as sc

    # 1. Vercel gate -> unavailable list, English message
    sc.is_vercel = lambda: True
    r = get()
    body = r.get_json()
    assert r.status_code == 200, r.status_code
    assert body[0]["status"] == "unavailable", body
    assert "vercel" in body[0]["message"].lower(), body
    assert isinstance(body, list), "response must be a list"
    sc.is_vercel = lambda: False

    # 2. fewer than 4 structures -> 400 error list
    r = post({"tolerance": 6, "structures": [
        {"type": 5, "x": 0, "z": 0}, {"type": 5, "x": 100, "z": 0},
        {"type": 8, "x": 0, "z": 100}]})
    body = r.get_json()
    assert r.status_code == 400, (r.status_code, body)
    assert body[0]["status"] == "error", body

    # 3. Mineshaft rejected with a clear message
    r = post({"structures": [
        {"type": 15, "x": 0, "z": 0}, {"type": 5, "x": 100, "z": 0},
        {"type": 5, "x": 0, "z": 100}, {"type": 8, "x": 33, "z": 55}]})
    body = r.get_json()
    assert r.status_code == 400 and "mineshaft" in body[0]["message"].lower(), body

    # 4. bad payload (not list/object) -> 400
    r = app.test_client().post("/seedcracker", json="nope")
    assert r.status_code == 400, r.status_code

    print("gate + validation tests OK")


if __name__ == "__main__":
    main()