"""Regressao: jungle temple em bamboo jungle tem de ser reportado.

Java: seed=3, templo em (-400, 16), bioma bamboo_jungle (validado com cubiomes
oficial). Bedrock: seed=3, templos em (-216,-280) e (-392,104).
Roda com: python server/tests/test_scan_jungle_bamboo.py
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
from server.app import create_app, native  # noqa: E402

app = create_app()
native.load_lib(r"build_server\seedfinder_lib.dll")


def coords(url):
    r = app.test_client().get(url)
    assert r.status_code == 200, r.status_code
    return {(it["x"], it["z"]) for it in r.get_json()["results"]}


def main():
    # radius=1000: o servidor do SeedFinder clampa em 1000 (server/app/api.py:131),
    # entao 6000 seria silenciosamente reduzido. Os templos estao a ~25 chunks.
    java = coords("/scan/java?seed=3&x=0&z=0&radius=1000&max=300&types=2")
    assert (-400, 16) in java, "java: templo em bamboo jungle sumiu"

    bedrock = coords("/scan?seed=3&x=0&z=0&radius=1000&max=300&types=2")
    assert (-216, -280) in bedrock, "bedrock: templo em bamboo jungle sumiu"
    assert (-392, 104) in bedrock, "bedrock: segundo templo sumiu"
    print("jungle bamboo regression OK")


if __name__ == "__main__":
    main()
