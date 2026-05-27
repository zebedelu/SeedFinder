"""SeedFinder HTTP Bridge — Flask API server.

Loads seedfinder_lib.dll via ctypes and exposes structure scanning
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


def load_dll(dll_path):
    """Load the seedfinder_lib.dll and set up ctypes signatures."""
    global _lib

    if not os.path.isfile(dll_path):
        print(f"ERROR: DLL not found at {dll_path}", file=sys.stderr)
        print("Build it first with: start.bat or cmake + mingw32-make", file=sys.stderr)
        sys.exit(1)

    try:
        _lib = ctypes.CDLL(os.path.abspath(dll_path))
    except OSError as e:
        print(f"ERROR: Failed to load DLL: {e}", file=sys.stderr)
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

    print(f"DLL loaded: {dll_path}")


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
    try:
        seed = int(request.args.get("seed", "0"))
        player_x = float(request.args.get("x", "0"))
        player_z = float(request.args.get("z", "0"))
        radius = int(request.args.get("radius", "100"))
        max_results = int(request.args.get("max", "20"))
        types_str = request.args.get("types", "5")
    except (ValueError, TypeError) as e:
        return jsonify({"error": f"Invalid parameter: {e}"}), 400

    # Parse types
    try:
        types_list = [int(t.strip()) for t in types_str.split(",") if t.strip()]
    except ValueError:
        return jsonify({"error": "types must be comma-separated integers"}), 400

    if not types_list:
        return jsonify({"results": []})

    # Build ctypes array
    num_types = len(types_list)
    c_types = (ctypes.c_int * num_types)(*types_list)

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
        return jsonify({"error": f"Scan failed: {e}"}), 500

    if not raw_ptr:
        return jsonify({"results": []})

    # Read the C string from the pointer, then free it
    try:
        raw_bytes = ctypes.string_at(raw_ptr)
        json_str = raw_bytes.decode("utf-8")
    except Exception as e:
        return jsonify({"error": f"Failed to read result: {e}"}), 500
    finally:
        _lib.seedfinder_free_result(raw_ptr)

    # Parse JSON
    try:
        result = json.loads(json_str)
    except json.JSONDecodeError as e:
        return jsonify({"error": f"Invalid JSON from C: {e}"}), 500

    return jsonify(result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SeedFinder HTTP Bridge")
    parser.add_argument(
        "--dll-path",
        default=os.path.join(os.path.dirname(__file__), "..", "build_server", "seedfinder_lib.dll"),
        help="Path to seedfinder_lib.dll",
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
        help="Host to bind to (default: 127.0.0.1)",
    )
    args = parser.parse_args()

    load_dll(args.dll_path)
    print(f"SeedFinder server starting on http://{args.host}:{args.port}")
    app.run(host=args.host, port=args.port, debug=False)
