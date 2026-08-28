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

    # 1a. Vercel gate (API path, GET with structures) -> unavailable list
    sc.is_vercel = lambda: True
    r = get(query_string={"structures": "5,0,0;5,100,0;8,0,100;9,200,0"})
    body = r.get_json()
    assert r.status_code == 200, r.status_code
    assert body[0]["status"] == "unavailable", body
    assert "vercel" in body[0]["message"].lower(), body
    assert isinstance(body, list), "response must be a list"
    sc.is_vercel = lambda: False

    # 1b. bare GET renders the seedcracker HTML console page
    r = get()
    assert r.status_code == 200 and r.mimetype == "text/html"
    assert "Seed" in r.get_data(as_text=True)

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

    # 5. happy path: exact crack of the verified fixture (small range)
    r = post({
        "tolerance": 0,
        "start": 0,
        "end": 16_000_000,
        "max": 2000,
        "max_seconds": 120,
        "structures": [
            {"type": 5, "x": -280, "z": 152},
            {"type": 5, "x": -280, "z": -360},
            {"type": 8, "x": 696, "z": 360},
            {"type": 8, "x": 712, "z": 760},
        ],
    })
    body = r.get_json()
    assert r.status_code == 200, (r.status_code, body)
    seeds = [s["seed"] for s in body[1:]]
    assert 8675309 in seeds, f"seed 8675309 missing: {seeds[:30]}"
    assert body[0]["status"] == "ok", body[0]
    assert isinstance(body, list) and body[0]["structures"] == 4, body[0]

    # 6. standard list form (options as the first list item)
    r = post([
        {"tolerance": 0, "start": 0, "end": 16_000_000, "max": 2000, "max_seconds": 120},
        {"type": 5, "x": -280, "z": 152},
        {"type": 5, "x": -280, "z": -360},
        {"type": 8, "x": 696, "z": 360},
        {"type": 8, "x": 712, "z": 760},
    ])
    body = r.get_json()
    assert r.status_code == 200 and 8675309 in [s["seed"] for s in body[1:]], body

    # 7. GET query form returns the same list envelope
    r = get(query_string={
        "tolerance": 0, "start": 0, "end": 16_000_000, "max": 2000, "max_seconds": 120,
        "structures": "5,-280,152;5,-280,-360;8,696,360;8,712,760",
    })
    body = r.get_json()
    assert r.status_code == 200 and 8675309 in [s["seed"] for s in body[1:]], body

    # 8. chunks units convert to blocks -> same result
    r = post({
        "units": "chunks", "tolerance": 0, "start": 0, "end": 16_000_000,
        "max": 2000, "max_seconds": 120,
        "structures": [
            {"type": 5, "x": -18, "z": 9},   # -280, 152 -> chunks
            {"type": 5, "x": -18, "z": -23},
            {"type": 8, "x": 43, "z": 22},
            {"type": 8, "x": 44, "z": 47},
        ],
    })
    body = r.get_json()
    assert r.status_code == 200 and 8675309 in [s["seed"] for s in body[1:]], body

    print("happy path + list/GET/chunks tests OK")


if __name__ == "__main__":
    main()