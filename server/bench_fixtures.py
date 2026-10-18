"""Fixtures do benchmark nativo x WASM (F0 do app SeedCracker).

Tres conjuntos reais, cada um com a seed verdadeira conhecida e ja usada pelos
harnesses existentes. Nenhuma fixture e gerada pelo cracker: dado que sai do
proprio motor nao prova nada.

NAO e a tabela da verdade do produto. Dados reais de campo vivem so no repo
privado (../mineseedfinder/static/wasm/ground_truth.json).
"""

import copy

# 1. 32-bit Bedrock: mesmas coords de wasm/test_wasm.mjs:11-15 e
#    server/tests/test_native_crack.py:33-38 (Chunkbase, seed 8675309).
# 2. 64-bit Bedrock: server/tests/fixtures_crack64.json (Chunkbase 26.0,
#    seed 4294972605; lo32 = 5309, confirmado em test_crack_auto.py). As duas
#    MT extra (tipos 1 e 3) saem do all_data.extra_mt do mesmo arquivo
#    (probe "recovers") e existem so porque o engine exige >= 4 estruturas
#    (core/crack_mt.c:361); sem elas o modo auto e' imensuravel.
# 3. Java/MITM: mesmas coords de wasm/test_mitm.mjs:39-41 (bug report
#    seed 4294967300 = 2^32 + 4; U = seed >> 24 = 256).
FIXTURES = {
    "bedrock32": {
        "name": "bedrock32",
        "edition": "bedrock",
        "mode": "bedrock32",
        "seed": 8675309,
        "tolerance": 6,
        "space": 1 << 32,
        "structures": [
            {"type": 5, "x": -280, "z": 152},
            {"type": 5, "x": -280, "z": -360},
            {"type": 8, "x": 696, "z": 360},
            {"type": 8, "x": 712, "z": 760},
        ],
        "source": "Chunkbase; wasm/test_wasm.mjs:11-15 + test_native_crack.py:33-38",
    },
    "auto64": {
        "name": "auto64",
        "edition": "bedrock",
        "mode": "auto",
        "seed": 4294972605,
        "tolerance": 6,
        "space": 1 << 32,
        "mt_structures": [
            {"type": 4, "x": -280, "z": 104},
            {"type": 2, "x": 2584, "z": -1288},
            {"type": 1, "x": -3048, "z": -296},
            {"type": 3, "x": 2136, "z": -280},
        ],
        "java_structures": [
            {"type": 24, "x": -505, "z": -281},
            {"type": 24, "x": 263, "z": -311},
            {"type": 24, "x": -359, "z": 199},
            {"type": 24, "x": 231, "z": 169},
        ],
        "source": ("Chunkbase 26.0; server/tests/fixtures_crack64.json "
                   "(+2 MT de all_data.extra_mt - tipos 1 e 3 - para satisfazer "
                   "o minimo de 4 estruturas do engine)"),
    },
    "java_mitm": {
        "name": "java_mitm",
        "edition": "java",
        "mode": "java_mitm",
        "seed": 4294967300,
        "tolerance": 0,
        "space": 1 << 24,          # espaco de U explorado pelo MITM
        "structures": [
            {"type": 5, "x": -4080, "z": -4080},
            {"type": 23, "x": -4064, "z": -4048},
            {"type": 24, "x": -4256, "z": -4240},
            {"type": 4, "x": -3824, "z": -4064},
            {"type": 1, "x": -3744, "z": -4064},
            {"type": 10, "x": -3312, "z": -4096},
        ],
        # U = seed >> 24 = 256: a janela de acerto tem de conter 256; a de
        # timing tem de excluir (mede trabalho sem early-exit).
        "u_hit": [0, 1 << 16],
        "u_timing": [1 << 16, 1 << 17],
        "source": "bug report 2^32+4; wasm/test_mitm.mjs:39-41",
    },
}

_SUPPORTED = set(range(1, 25)) - {12, 15}


def names():
    return list(FIXTURES)


def load(name):
    if name not in FIXTURES:
        raise KeyError(f"fixture desconhecida: {name!r} (tenho: {names()})")
    return copy.deepcopy(FIXTURES[name])


def _structures(fx):
    out = list(fx.get("structures", []))
    out += list(fx.get("mt_structures", []))
    out += list(fx.get("java_structures", []))
    return out


def validate():
    for name, fx in FIXTURES.items():
        for s in _structures(fx):
            assert s["type"] in _SUPPORTED, f"{name}: tipo {s['type']} nao suportado"
            assert isinstance(s["x"], int) and isinstance(s["z"], int), name
        if fx["mode"] == "auto":
            assert any(s["type"] in (23, 24) for s in fx["java_structures"]), name
            assert fx["mt_structures"], name
        else:
            assert len(fx["structures"]) >= 4, f"{name}: minimo de 4 estruturas"
