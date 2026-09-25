"""API blueprint — /status health check and /scan structure search.

Scan routes (GET; POST lands in the next task):
  /scan          edition from the `version` param (default bedrock,
                 first-letter rule: j… -> java, b… -> bedrock)
  /scan/java     Java fixed (path wins; `version` silently ignored)
  /scan/bedrock  Bedrock fixed
"""

import ctypes
import json

from flask import Blueprint, jsonify, request

from . import native

api_bp = Blueprint("api", __name__)


@api_bp.route("/status")
@api_bp.route("/health")
def status():
    """Health check endpoint."""
    try:
        result = native.lib.seedfinder_status()
        return jsonify(json.loads(result))
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500


def _param_source():
    """Params for the current verb. GET reads the query string only."""
    return request.args


def _scan(fixed_edition):
    """Shared scan handler. fixed_edition: None | "java" | "bedrock"."""
    # Parse params with typed defaults when missing or empty.
    missing = []

    try:
        source = _param_source()
    except ValueError as e:
        return jsonify({"error": f"Invalid parameter: {e}",
                        "missing_or_invalid": missing}), 400

    def _arg(name, default, cast):
        raw = source.get(name, "").strip()
        if not raw:
            missing.append(name)
            return default
        try:
            return cast(raw)
        except ValueError:
            raise ValueError(f"invalid value for {name!r}: {raw!r}")

    # Edition resolution: fixed path wins and ignores `version`; on /scan
    # the first letter decides (j -> java, b -> bedrock), default bedrock.
    edition = fixed_edition
    if edition is None:
        raw_version = source.get("version", "")
        if raw_version is None or raw_version == "":
            edition = "bedrock"
        else:
            first = str(raw_version)[0].lower()
            if first == "j":
                edition = "java"
            elif first == "b":
                edition = "bedrock"
            else:
                return jsonify({
                    "error": ("Invalid parameter: invalid value for "
                              f"'version': {raw_version!r} "
                              "(must start with 'j' or 'b')"),
                    "missing_or_invalid": missing + ["version"],
                }), 400

    try:
        seed = _arg("seed", 0, int) & 0xFFFFFFFFFFFFFFFF
        player_x = _arg("x", 0.0, float)
        player_z = _arg("z", 0.0, float)
        radius = min(_arg("radius", 100, int), 1000)
        max_results = min(_arg("max", 20, int), 1000)
        types_str = source.get("types", "5").strip() or "5"
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
            "hint": "Check the server startup logs for [seedfinder] messages.",
            "missing_or_invalid": missing,
            "results": [],
        }), 503

    # Call C function — returns void pointer to malloc'd JSON string
    try:
        if edition == "java":
            fn = getattr(native.lib, "seedfinder_scan_java", None)
            if fn is None:
                return jsonify({
                    "error": "SeedFinder native library has no Java scan support.",
                    "hint": "rebuild the native library — Java scan support missing",
                    "missing_or_invalid": missing,
                    "results": [],
                }), 503
            raw_ptr = fn(
                ctypes.c_uint64(seed & 0xFFFFFFFFFFFFFFFF),
                ctypes.c_double(player_x),
                ctypes.c_double(player_z),
                ctypes.c_int(radius),
                ctypes.c_int(max_results),
                c_types,
                ctypes.c_int(num_types),
                None,  # mcLabel -> MC_NEWEST (future version param)
            )
        else:
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


@api_bp.route("/scan")
def scan():
    return _scan(None)


@api_bp.route("/scan/java")
def scan_java():
    return _scan("java")


@api_bp.route("/scan/bedrock")
def scan_bedrock():
    return _scan("bedrock")
