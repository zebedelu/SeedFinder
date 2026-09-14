"""Native library loading and ctypes bindings.

All ctypes plumbing lives here so route modules only deal with a single
loadable handle (`native.lib`, or `None` when the library could not load).
"""

import ctypes
import os
import sys

lib = None

_SCAN_ARGTYPES = [
    ctypes.c_uint64,  # seed
    ctypes.c_double,  # player x
    ctypes.c_double,  # player z
    ctypes.c_int,     # radius
    ctypes.c_int,     # max results
    ctypes.POINTER(ctypes.c_int),  # structure type ids
    ctypes.c_int,     # number of types
]

_CRACK_ARGTYPES = [
    ctypes.POINTER(ctypes.c_int),    # structure type ids
    ctypes.c_int,                    # number of structures
    ctypes.POINTER(ctypes.c_double), # x block coordinates
    ctypes.POINTER(ctypes.c_double), # z block coordinates
    ctypes.c_int,                    # tolerance in chunks
    ctypes.c_uint64,                 # start seed (inclusive)
    ctypes.c_uint64,                 # end seed (exclusive)
    ctypes.c_int,                    # max results
    ctypes.c_double,                 # time budget in seconds
    ctypes.c_int,                    # number of threads
]


_CRACK64_ARGTYPES = [
    ctypes.POINTER(ctypes.c_int),    # MT structure type ids
    ctypes.POINTER(ctypes.c_double), # MT x block coordinates
    ctypes.POINTER(ctypes.c_double), # MT z block coordinates
    ctypes.c_int,                    # number of MT structures
    ctypes.POINTER(ctypes.c_int),    # Java anchor type ids (23/24)
    ctypes.POINTER(ctypes.c_double), # Java anchor x block coordinates
    ctypes.POINTER(ctypes.c_double), # Java anchor z block coordinates
    ctypes.c_int,                    # number of Java anchors
    ctypes.c_int,                    # tolerance in chunks
    ctypes.c_uint64,                 # start seed (inclusive, 64-bit)
    ctypes.c_uint64,                 # end seed (exclusive, 64-bit)
    ctypes.c_int,                    # max results
    ctypes.c_double,                 # time budget in seconds
    ctypes.c_int,                    # number of threads
]


def _bind(handle) -> None:
    """Attach argvtypes/restypes to a freshly loaded CDLL handle."""
    handle.seedfinder_scan.argtypes = _SCAN_ARGTYPES
    # c_void_p so Python keeps the raw pointer we must free afterwards.
    handle.seedfinder_scan.restype = ctypes.c_void_p
    handle.seedfinder_free_result.argtypes = [ctypes.c_void_p]
    handle.seedfinder_free_result.restype = None
    handle.seedfinder_status.argtypes = []
    handle.seedfinder_status.restype = ctypes.c_char_p
    # seedfinder_crack is absent from .so builds predating SeedCrackerX. A lib
    # without the symbol is a valid state - bind only when present or the whole
    # app dies at import.
    if hasattr(handle, "seedfinder_crack"):
        handle.seedfinder_crack.argtypes = _CRACK_ARGTYPES
        # c_void_p keeps the raw malloc'd pointer; freed via seedfinder_free_result.
        handle.seedfinder_crack.restype = ctypes.c_void_p
    # Same degrade rule for the 64-bit engine: a lib predating it is valid.
    if hasattr(handle, "seedfinder_crack64_shim"):
        handle.seedfinder_crack64_shim.argtypes = _CRACK64_ARGTYPES
        handle.seedfinder_crack64_shim.restype = ctypes.c_void_p


def _candidates() -> list[str]:
    """Possible native library locations, most likely first."""
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    meipass = [sys._MEIPASS] if getattr(sys, "frozen", False) else []
    return [
        *[os.path.join(m, n) for m in meipass
          for n in ("seedfinder_lib.dll", "seedfinder_lib.so")],
        os.path.join(root, "build_server", "seedfinder_lib.dll"),
        os.path.join(root, "build_server", "seedfinder_lib.so"),
        os.path.join(root, "build_server", "libseedfinder_lib.so"),
    ]


def bootstrap_lib() -> None:
    """Best-effort load at import time. Never raises — routes degrade to 503."""
    global lib
    lib_path = next((c for c in _candidates() if os.path.isfile(c)), None)
    if lib_path is None:
        print("[seedfinder] shared library not found", file=sys.stderr)
        return
    try:
        lib = ctypes.CDLL(os.path.abspath(lib_path))
        _bind(lib)
        print(f"[seedfinder] shared library loaded: {lib_path}", file=sys.stderr)
    except OSError as e:
        print(f"[seedfinder] Failed to load shared library: {e}", file=sys.stderr)
        lib = None


def load_lib(lib_path) -> None:
    """Eager load for CLI runs (index.py main). Exits on failure."""
    global lib
    if not os.path.isfile(lib_path):
        print(f"ERROR: shared library not found at {lib_path}", file=sys.stderr)
        print("Build it first with: cmake .. && make", file=sys.stderr)
        sys.exit(1)
    try:
        lib = ctypes.CDLL(os.path.abspath(lib_path))
        _bind(lib)
    except OSError as e:
        print(f"ERROR: Failed to load shared library: {e}", file=sys.stderr)
        sys.exit(1)
    print(f"shared library loaded: {lib_path}")