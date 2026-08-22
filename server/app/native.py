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


def _bind(handle) -> None:
    """Attach argvtypes/restypes to a freshly loaded CDLL handle."""
    handle.seedfinder_scan.argtypes = _SCAN_ARGTYPES
    # c_void_p so Python keeps the raw pointer we must free afterwards.
    handle.seedfinder_scan.restype = ctypes.c_void_p
    handle.seedfinder_free_result.argtypes = [ctypes.c_void_p]
    handle.seedfinder_free_result.restype = None
    handle.seedfinder_status.argtypes = []
    handle.seedfinder_status.restype = ctypes.c_char_p


def _candidates() -> list[str]:
    """Possible native library locations, most likely first."""
    base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # server/
    root = os.path.dirname(base)  # project root
    meipass = [sys._MEIPASS] if getattr(sys, "frozen", False) else []
    return [
        *[os.path.join(m, n) for m in meipass
          for n in ("seedfinder_lib.dll", "seedfinder_lib.so")],
        os.path.join(base, "seedfinder_lib.so"),
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