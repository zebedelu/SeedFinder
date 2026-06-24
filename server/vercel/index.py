"""SeedFinder HTTP Bridge — Flask API server (Linux variant).

Loads libseedfinder_bridge.so via ctypes and exposes structure scanning
as a REST API for the Flarial Lua script to consume via network.get().
"""


import argparse
import ctypes
import json
import os
import sys

from flask import Flask, request, jsonify
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

# Global ctypes library handle
_lib = None

# Try to load the .so at import time so all routes can use it.
# On Vercel this is the only chance we get — there's no main().
def _bootstrap_lib():
    global _lib
    base = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(base, 'seedfinder_lib.so'),
        os.path.join(base, '..', '..', '..', 'build_server', 'seedfinder_lib.so'),
        os.path.join(base, '..', '..', '..', 'build_server', 'libseedfinder_lib.so'),
    ]
    so_path = next((c for c in candidates if os.path.isfile(c)), candidates[0])
    if not os.path.isfile(so_path):
        print(f"[seedfinder] .so not found at {so_path}", file=sys.stderr)
        return
    try:
        _lib = ctypes.CDLL(os.path.abspath(so_path))
        _lib.seedfinder_scan.argtypes = [
            ctypes.c_uint64,
            ctypes.c_double, ctypes.c_double,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.POINTER(ctypes.c_int),
            ctypes.c_int,
        ]
        _lib.seedfinder_scan.restype = ctypes.c_void_p
        _lib.seedfinder_free_result.argtypes = [ctypes.c_void_p]
        _lib.seedfinder_free_result.restype = None
        _lib.seedfinder_status.argtypes = []
        _lib.seedfinder_status.restype = ctypes.c_char_p
        print(f"[seedfinder] .so loaded: {so_path}", file=sys.stderr)
    except OSError as e:
        print(f"[seedfinder] Failed to load .so: {e}", file=sys.stderr)
        _lib = None

_bootstrap_lib()


def _resolve_so_path(cli_so_path):
    """Resolve the .so path: CLI arg > sys._MEIPASS (frozen) > same-dir fallback.
    Tries both possible names produced by the CMake build."""
    if cli_so_path:
        return cli_so_path

    # Running inside a PyInstaller bundle
    if getattr(sys, 'frozen', False):
        return os.path.join(sys._MEIPASS, 'libseedfinder_bridge.so')

    # Dev mode: look next to the script (server/vercel/ when deployed to Vercel,
    # or server/base/linux/ when running locally inside the repo).
    base = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        # Vercel deploy: .so is colocated with index.py
        os.path.join(base, 'seedfinder_lib.so'),
        # Local dev: ../base/linux/index.py uses ../../../build_server/
        os.path.join(base, '..', '..', '..', 'build_server', 'seedfinder_lib.so'),
        os.path.join(base, '..', '..', '..', 'build_server', 'libseedfinder_lib.so'),
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return candidates[0]


def load_so(so_path):
    """Load the seedfinder shared library and set up ctypes signatures."""
    global _lib

    if not os.path.isfile(so_path):
        print(f"ERROR: shared library not found at {so_path}", file=sys.stderr)
        print("Build it first with: cmake -DSEEDFINDER_BRIDGE_SHARED=ON .. && make", file=sys.stderr)
        sys.exit(1)

    try:
        _lib = ctypes.CDLL(os.path.abspath(so_path))
    except OSError as e:
        print(f"ERROR: Failed to load .so: {e}", file=sys.stderr)
        sys.exit(1)

    # Use c_void_p for restype so Python doesn't auto-convert to bytes,
    # which would lose the original pointer needed for seedfinder_free_result.
    # seedfinder_scan(...) -> char* (caller must free)
    _lib.seedfinder_scan.argtypes = [
        ctypes.c_uint64,
        ctypes.c_double, ctypes.c_double,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
        ctypes.c_int,
    ]
    _lib.seedfinder_scan.restype = ctypes.c_void_p

    # seedfinder_free_result(void* result) -> void
    _lib.seedfinder_free_result.argtypes = [ctypes.c_void_p]
    _lib.seedfinder_free_result.restype = None

    # seedfinder_status() -> const char* (static string, no free needed)
    _lib.seedfinder_status.argtypes = []
    _lib.seedfinder_status.restype = ctypes.c_char_p

    print(f".so loaded: {so_path}")

HOME_PAGE = "No index.html found."
try:
    home_page_file = open("index.html","r+")
    HOME_PAGE = home_page_file.read()
except:
    print(HOME_PAGE)
    
@app.route("/")
def index():
    """If you want to test the API"""
    return HOME_PAGE


@app.route("/status")
def status():
    """Health check endpoint."""
    try:
        result = _lib.seedfinder_status()
        return jsonify(json.loads(result))
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500


@app.route("/scan")
def scan():
    """Scan for structures near the given position.

    Query parameters:
    seed (int) — World seed
    x (float) — Player X block position
    z (float) — Player Z block position
    radius (int) — Search radius in chunks
    max (int) — Maximum results to return (default 20)
    types (string) — Comma-separated structure type IDs (e.g. "5,1,10")
    """
    # Parse params with explicit defaults when missing or empty.
    missing = []
    def _arg(name, default, cast):
        raw = request.args.get(name, "").strip()
        if not raw:
            missing.append(name)
            return default
        try:
            return cast(raw)
        except ValueError:
            raise ValueError(f"invalid value for {name!r}: {raw!r}")

    try:
        seed = _arg("seed", "0", int) & 0xFFFFFFFFFFFFFFFF
        player_x = _arg("x", "0", float)
        player_z = _arg("z", "0", float)
        radius = min(_arg("radius", "100", int), 1000)
        max_results = min(_arg("max", "20", int), 1000)
        types_str = request.args.get("types", "5").strip() or "5"
    except ValueError as e:
        return jsonify({"error": f"Invalid parameter: {e}",
                        "missing_or_invalid": missing}), 400

    # Surface missing args but still serve a result.
    # Parse types
    try:
        types_list = [int(t.strip()) for t in types_str.split(",") if t.strip()]
    except ValueError:
        return jsonify({
            "error": "types must be comma-separated integers",
            "missing_or_invalid": missing + ["types"],
        }), 400

    if not types_list:
        return jsonify({"results": []})

    # Build ctypes array
    num_types = len(types_list)
    c_types = (ctypes.c_int * num_types)(*types_list)

    # Bail out early if the native lib failed to load at startup —
    # otherwise we'd get a confusing 'NoneType has no attribute ...'
    if _lib is None:
        return jsonify({
            "error": "SeedFinder native library (.so) not loaded on this server.",
            "hint": "Check Vercel build logs for messages starting with [seedfinder].",
            "missing_or_invalid": missing,
            "results": [],
        }), 503

    # Call C function — returns void pointer to malloc'd JSON string
    try:
        raw_ptr = _lib.seedfinder_scan(
            ctypes.c_uint64(seed & 0xFFFFFFFFFFFFFFFF),
            ctypes.c_double(player_x),
            ctypes.c_double(player_z),
            ctypes.c_int(radius),
            ctypes.c_int(max_results),
            c_types,
            ctypes.c_int(num_types),
        )
    except Exception as e:
        return jsonify({"error": f"Scan failed: {e}",
                        "missing_or_invalid": missing}), 500

    if not raw_ptr:
        return jsonify({"results": [], "missing_or_invalid": missing})

    # Read the C string from the pointer, then free it
    try:
        raw_bytes = ctypes.string_at(raw_ptr)
        json_str = raw_bytes.decode("utf-8")
    except Exception as e:
        return jsonify({"error": f"Failed to read result: {e}",
                        "missing_or_invalid": missing}), 500
    finally:
        _lib.seedfinder_free_result(raw_ptr)

    # Parse JSON
    try:
        result = json.loads(json_str)
    except json.JSONDecodeError as e:
        return jsonify({"error": f"Invalid JSON from C: {e}"}), 500

    return jsonify(result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SeedFinder HTTP Bridge (Linux)")
    parser.add_argument(
        "--so-path",
        default=None,
        help="Path to libseedfinder_bridge.so (auto-detected if omitted)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=int(os.environ.get("SEEDFINDER_PORT", 7890)),
        help="Port to listen on (default: 7890, env: SEEDFINDER_PORT)",
    )
    parser.add_argument(
        "--host",
        default="127.0.0.1",
        help="Host to bind on (default: 127.0.0.1)",
    )
    args = parser.parse_args()

    so_path = _resolve_so_path(args.so_path)
    load_so(so_path)
    print(f"SeedFinder server starting on http://{args.host}:{args.port}")
    app.run(host=args.host, port=args.port, debug=False)
