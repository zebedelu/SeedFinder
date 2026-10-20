# server/tests/test_biome_samples.py — paridade do export esparso com a grade.
# Rodar: python server/tests/test_biome_samples.py  (exige build_server/ com a lib)
import ctypes
import os
import random
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DLL = os.path.join(ROOT, "build_server", "seedfinder_lib.dll")
SO = os.path.join(ROOT, "build_server", "seedfinder_lib.so")
path = DLL if os.path.exists(DLL) else SO
if not os.path.exists(path):
    print("SKIP: build_server/ sem a lib (rode o build do Step 4)")
    sys.exit(0)

lib = ctypes.CDLL(path)
samples = lib.seedfinder_biome_samples
samples.argtypes = [ctypes.c_uint64, ctypes.POINTER(ctypes.c_int),
                    ctypes.POINTER(ctypes.c_int), ctypes.c_int, ctypes.c_int,
                    ctypes.POINTER(ctypes.c_int)]
samples.restype = ctypes.c_int
grid = lib.seedfinder_biome_grid
grid.argtypes = [ctypes.c_uint64] + [ctypes.c_int] * 6 + [ctypes.POINTER(ctypes.c_int)]
grid.restype = ctypes.c_int


def grid_cells(seed, x0, z0, n, scale=4):
    buf = (ctypes.c_int * (n * n))()
    assert grid(seed, x0, z0, n, n, scale, 79, buf) == 0
    return list(buf)


def points(seed, coords):
    n = len(coords)
    cx = (ctypes.c_int * n)(*[c[0] for c in coords])
    cz = (ctypes.c_int * n)(*[c[1] for c in coords])
    out = (ctypes.c_int * n)()
    assert samples(seed, cx, cz, n, 79, out) == 0
    return list(out)


# 1) paridade exata: grade 64x64 escala 4, origem negativa, comparada célula a célula
coords = []
for j in range(64):
    for i in range(64):
        coords.append((-512 + 4 * i, -512 + 4 * j))
assert points(1, coords) == grid_cells(1, -512, -512, 64)
assert points(123456789, coords) == grid_cells(123456789, -512, -512, 64)

# 2) paridade em pontos aleatórios (inclui negativos e coordenadas ímpares)
rng = random.Random(7)
pts = [(rng.randint(-200000, 200000), rng.randint(-200000, 200000))
       for _ in range(200)]
got = points(8675309, pts)
for (x, z), want in zip(pts, got):
    cell = (x // 4, z // 4)
    one = (ctypes.c_int * 1)()
    assert grid(8675309, cell[0] * 4, cell[1] * 4, 1, 1, 4, 79, one) == 0
    assert one[0] == want, ((x, z), one[0], want)

# 3) guardas
one = (ctypes.c_int * 1)()
assert samples(1, None, None, 1, 79, one) == -1
assert samples(1, (ctypes.c_int * 1)(0), (ctypes.c_int * 1)(0), 0, 79, one) == -1
assert samples(1, (ctypes.c_int * 1)(0), (ctypes.c_int * 1)(0), 1000001, 79, one) == -1

# 4) determinismo
assert points(42, pts[:50]) == points(42, pts[:50])
print("test_biome_samples: OK")
