"""Regressao: /scan nao deve reportar falsos positivos de village.

Ground truth do mundo seed=6666 com o jogador em (0,0), validado in-game.
Roda com: python server/tests/test_scan_village_groundtruth.py
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from server.app import create_app  # noqa: E402
from server.app import native  # noqa: E402

app = create_app()

# Forca a lib local. Precisa vir DEPOIS de create_app() (o factory re-roda
# bootstrap_lib()).
native.load_lib(r"build_server\seedfinder_lib.dll")

# Vilas reais (confirmadas in-game) dentro de radius=100 de (0,0).
TRUE_VILLAGES = {
    (-168, 56),
    (744, -344),
    (-312, -1000),
    (744, -872),
    (248, 1144),
    (-312, 1208),
    (1256, -328),
    (56, -1480),
}

# Reportadas pelo /scan mas SEM vila no jogo. Bioma na celula da estrutura:
# (264,-296)=frozen_river, (-776,-872)=river, (776,-1368)=frozen_river.
FALSE_POSITIVES = {
    (264, -296),
    (-776, -872),
    (776, -1368),
}


def scan(seed, radius):
    r = app.test_client().get(
        f"/scan?seed={seed}&x=0&z=0&radius={radius}&max=500&types=5"
    )
    assert r.status_code == 200, r.status_code
    return {(it["x"], it["z"]) for it in r.get_json()["results"]}


def main():
    got = scan(6666, 100)

    missing = TRUE_VILLAGES - got
    assert not missing, f"vilas verdadeiras sumiram do /scan: {sorted(missing)}"

    extra = FALSE_POSITIVES & got
    assert not extra, f"falsos positivos ainda reportados: {sorted(extra)}"

    print(f"village ground-truth regression OK ({len(got)} resultados)")


if __name__ == "__main__":
    main()
