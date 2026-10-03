# server/tests/test_biome_grid.py — smoke dos exports do mapa de biomas.
# Rodar: python server/tests/test_biome_grid.py  (exige build_server/ com a DLL)
import ctypes, json, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DLL = os.path.join(ROOT, "build_server", "seedfinder_lib.dll")
SO = os.path.join(ROOT, "build_server", "seedfinder_lib.so")
path = DLL if os.path.exists(DLL) else SO
if not os.path.exists(path):
    print("SKIP: build_server/ sem a lib (rode o build do Step 4)"); sys.exit(0)
lib = ctypes.CDLL(path)

grid = lib.seedfinder_biome_grid
grid.argtypes = [ctypes.c_uint64, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                 ctypes.c_int, ctypes.c_int, ctypes.c_int,
                 ctypes.POINTER(ctypes.c_int)]
grid.restype = ctypes.c_int
pal = lib.seedfinder_biome_palette
pal.restype = ctypes.c_void_p
free_res = lib.seedfinder_free_result
free_res.argtypes = [ctypes.c_void_p]

# 1) paleta: 256 cores, oceano #000070, nomes
ptr = pal()
assert ptr, "palette returned NULL"
try:
    data = json.loads(ctypes.string_at(ptr).decode())
finally:
    free_res(ptr)
assert len(data["colors"]) == 256, len(data["colors"])
assert data["colors"][0] == "#000070", data["colors"][0]
assert data["names"]["1"] == "plains", data["names"].get("1")  # cubiomes: plains=1 (o brief pedia "5", que é taiga)
assert data["names"]["12"] == "snowy_plains", data["names"].get("12")

# 2) grade: 256x256 @ escala 4 (1024x1024 blocos) — range, variedade, determinismo
def run(x0, z0, sx, sz, scale, y=79, seed=1):
    buf = (ctypes.c_int * (sx * sz))()
    rc = grid(ctypes.c_uint64(seed), x0, z0, sx, sz, scale, y, buf)
    return rc, list(buf)

rc, a = run(-512, -512, 256, 256, 4)
assert rc == 0, rc
assert all(-1 <= v <= 255 for v in a), "id de bioma fora do range"
assert len(set(a)) >= 8, f"poucos biomas distintos: {sorted(set(a))}"
rc, b = run(-512, -512, 256, 256, 4)
assert a == b, "grade não determinística"

# 3) validação (defesa — o JS também valida, INTEGRATION §10.4)
assert run(1, 0, 8, 8, 4)[0] == -1, "x0 desalinhado deveria falhar"
assert run(0, 2, 8, 8, 4)[0] == -1, "z0 desalinhado deveria falhar"
assert run(0, 0, 8, 8, 2)[0] == -1, "escala inválida deveria falhar"
assert run(0, 0, 3000, 3000, 4)[0] == -1, "grade acima do cap deveria falhar"
assert run(0, 0, 0, 8, 4)[0] == -1, "sx=0 deveria falhar"

print("test_biome_grid: OK")
