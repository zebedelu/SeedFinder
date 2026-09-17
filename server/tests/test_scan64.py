"""/scan 64-bit fix - run: python server/tests/test_scan64.py"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
from server.app import create_app
from server.app import native
app = create_app()
native.load_lib(r"build_server\seedfinder_lib.dll")
client = app.test_client()

TC = [(-505, -281), (263, -311), (-359, 199), (231, 169)]  # Chunkbase, blocos

def test_scan_64bit_trial_chambers():
    r = client.get("/scan?seed=4294972605&x=0&z=0&radius=1000&max=1000&types=24")
    assert r.status_code == 200, r.get_json()
    found = r.get_json()["results"]
    assert found, "scan 64 deve achar trial chambers"
    # cada ponto Chunkbase deve aparecer (d <= 64 blocos; corner->centro ~13)
    for (ex, ez) in TC:
        assert any(abs(res["x"] - ex) <= 64 and abs(res["z"] - ez) <= 64
                   for res in found), f"ponto TC ({ex},{ez}) ausente: {found[:10]}"

def main():
    test_scan_64bit_trial_chambers()
    print("SCAN64 TESTS OK")

if __name__ == "__main__":
    main()