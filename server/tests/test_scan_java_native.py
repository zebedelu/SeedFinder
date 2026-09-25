"""CTypes smoke test do export seedfinder_scan_java.

Roda com: python server/tests/test_scan_java_native.py  (repo root)
Requer o DLL build: ver Global Constraints (build command).
"""
import ctypes
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

from server.app import native  # noqa: E402

native.load_lib(r"build_server\seedfinder_lib.dll")

_JAVA_ARGTYPES = [
    ctypes.c_uint64,               # seed
    ctypes.c_double,               # player x
    ctypes.c_double,               # player z
    ctypes.c_int,                  # radius
    ctypes.c_int,                  # max results
    ctypes.POINTER(ctypes.c_int),  # structure type ids
    ctypes.c_int,                  # number of types
    ctypes.c_char_p,               # mcLabel (NULL hoje)
]


def _call(fn, seed, types, radius, java=False):
    arr = (ctypes.c_int * len(types))(*types)
    fn.argtypes = _JAVA_ARGTYPES if java else _JAVA_ARGTYPES[:-1]
    fn.restype = ctypes.c_void_p
    args = [
        ctypes.c_uint64(seed & 0xFFFFFFFFFFFFFFFF),
        ctypes.c_double(0.0),
        ctypes.c_double(0.0),
        ctypes.c_int(radius),
        ctypes.c_int(50),
        arr,
        ctypes.c_int(len(types)),
    ]
    if java:
        args.append(None)  # mcLabel = NULL -> MC_NEWEST
    ptr = fn(*args)
    assert ptr, "null pointer from scan"
    try:
        return json.loads(ctypes.string_at(ptr).decode("utf-8"))
    finally:
        native.lib.seedfinder_free_result(ptr)


def main():
    assert hasattr(native.lib, "seedfinder_scan_java"), (
        "seedfinder_scan_java ausente - rebuild o DLL "
        "(core/seedfinder_wrapper.c)"
    )

    seed = 8675309
    bed = _call(native.lib.seedfinder_scan, seed, [5], 300)
    jav = _call(native.lib.seedfinder_scan_java, seed, [5], 300, java=True)

    assert bed["results"], "bedrock scan vazio para seed 8675309"
    assert jav["results"], (
        "java scan vazio para seed 8675309 (raio 300) - escolha outro seed"
    )
    assert bed != jav, "java e bedrock retornaram o mesmo - dispatch falhou"

    for r in jav["results"]:
        assert set(r) == {"name", "x", "z", "distance"}, r
        assert r["name"] == "village", r

    print(f"scan_java OK ({len(bed['results'])} bedrock, "
          f"{len(jav['results'])} java)")


if __name__ == "__main__":
    main()
