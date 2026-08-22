"""API blueprint — /status health check and /scan structure search."""

import ctypes
import json

from flask import Blueprint, jsonify, request

from . import native

api_bp = Blueprint("api", __name__)


@api_bp.route("/status")
def status():
    """Health check endpoint."""
    try:
        result = native.lib.seedfinder_status()
        return jsonify(json.loads(result))
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500


@api_bp.route("/scan")
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
    # Parse params with typed defaults when missing or empty.
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
        seed = _arg("seed", 0, int) & 0xFFFFFFFFFFFFFFFF
        player_x = _arg("x", 0.0, float)
        player_z = _arg("z", 0.0, float)
        radius = min(_arg("radius", 100, int), 1000)
        max_results = min(_arg("max", 20, int), 1000)
        types_str = request.args.get("types", "5").strip() or "5"
    except ValueError as e:
        return jsonify({"error": f"Invalid parameter: {e}",
                        "missing_or_invalid": missing}), 400

    # Surface missing args but still serve a result.
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

    # Bail out early if the native lib failed to load at startup.
    if native.lib is None:
        return jsonify({
            "error": "SeedFinder native library (.so) not loaded on this server.",
            "hint": "Check Vercel build logs for messages starting with [seedfinder].",
            "missing_or_invalid": missing,
            "results": [],
        }), 503

    # Call C function — returns void pointer to malloc'd JSON string
    try:
        raw_ptr = native.lib.seedfinder_scan(
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
        native.lib.seedfinder_free_result(raw_ptr)

    try:
        result = json.loads(json_str)
    except json.JSONDecodeError as e:
        return jsonify({"error": f"Invalid JSON from C: {e}"}), 500

    return jsonify(result)