"""API blueprint — /status health check and /scan structure search.

Scan routes (GET query string, or POST with a JSON object body):
  /scan          edition from the `mc` param (default bedrock,
                 first-letter rule: j… -> java, b… -> bedrock)
  /scan/java     Java fixed (path wins; `mc` silently ignored)
  /scan/bedrock  Bedrock fixed

`mc` picks the edition, never the game version. The name is deliberately
short so a future game-version param (`version`, e.g. "1.21") can land
without colliding with it.
"""

import ctypes
import json

from flask import Blueprint, jsonify, request

from . import native

api_bp = Blueprint("api", __name__)

# Scan responses are deterministic (same params -> same bytes), so the CDN
# may reuse them: 1h shared cache + stale-while-revalidate serves an expired
# copy while regenerating in the background. Success paths only — errors
# stay uncached so the Lua client can detect failures.
_SCAN_CACHE = "public, s-maxage=3600, stale-while-revalidate=86400"


def _ok(payload):
    resp = jsonify(payload)
    resp.headers["Cache-Control"] = _SCAN_CACHE
    return resp


@api_bp.route("/status")
@api_bp.route("/health")
def status():
    """Health check endpoint."""
    try:
        result = native.lib.seedfinder_status()
        resp = jsonify(json.loads(result))
        # CDN serves this for 60s without invoking the function again; the
        # Lua client polls every 30s, so ~half the polls hit the edge cache.
        resp.headers["Cache-Control"] = "public, max-age=60"
        return resp
    except Exception as e:
        # No cache header on the error path — the Lua client uses /status
        # failures to detect that the server is down.
        return jsonify({"status": "error", "message": str(e)}), 500


def _param_source():
    """GET reads the query string; POST reads only the JSON body.

    Same keys either way. A non-empty body that is not a JSON object
    raises ValueError -> 400. Empty/absent body -> all defaults.
    """
    if request.method != "POST":
        return request.args
    raw = request.get_data(cache=True)
    if not raw:
        return {}
    body = request.get_json(silent=True, force=True)
    if not isinstance(body, dict):
        raise ValueError("POST body must be a JSON object with the same "
                         "keys as the query string")
    return body


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
        if name not in source:
            missing.append(name)
            return default
        raw = source[name]
        if isinstance(raw, bool) or raw is None:
            raise ValueError(f"invalid value for {name!r}: {raw!r}")
        if isinstance(raw, str):
            raw = raw.strip()
            if not raw:
                missing.append(name)
                return default
            try:
                return cast(raw)
            except ValueError:
                raise ValueError(f"invalid value for {name!r}: {raw!r}")
        # Native JSON types (POST body): int for int casts, int|float for
        # float casts; anything else is a type error.
        if cast is int and isinstance(raw, int):
            return raw
        if cast is float and isinstance(raw, (int, float)):
            return float(raw)
        raise ValueError(f"invalid value for {name!r}: {raw!r}")

    # Edition resolution: fixed path wins and ignores `mc`; on /scan
    # the first letter decides (j -> java, b -> bedrock), default bedrock.
    edition = fixed_edition
    if edition is None:
        raw_mc = source.get("mc", "")
        if raw_mc is None or raw_mc == "":
            edition = "bedrock"
        else:
            first = str(raw_mc)[0].lower()
            if first == "j":
                edition = "java"
            elif first == "b":
                edition = "bedrock"
            else:
                return jsonify({
                    "error": ("Invalid parameter: invalid value for "
                              f"'mc': {raw_mc!r} "
                              "(must start with 'j' or 'b')"),
                    "missing_or_invalid": missing + ["mc"],
                }), 400

    try:
        seed = _arg("seed", 0, int) & 0xFFFFFFFFFFFFFFFF
        player_x = _arg("x", 0.0, float)
        player_z = _arg("z", 0.0, float)
        radius = min(_arg("radius", 100, int), 1000)
        max_results = min(_arg("max", 20, int), 1000)
    except ValueError as e:
        return jsonify({"error": f"Invalid parameter: {e}",
                        "missing_or_invalid": missing}), 400

    # Surface missing args but still serve a result.
    types_raw = source.get("types", "5")
    try:
        if isinstance(types_raw, bool) or not isinstance(types_raw, str):
            raise ValueError("not a string")
        types_str = types_raw.strip() or "5"
        types_list = [int(t.strip()) for t in types_str.split(",") if t.strip()]
    except ValueError:
        return jsonify({
            "error": "types must be comma-separated integers",
            "missing_or_invalid": missing + ["types"],
        }), 400

    if not types_list:
        return _ok({"results": []})

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
    args = (
        ctypes.c_uint64(seed & 0xFFFFFFFFFFFFFFFF),
        ctypes.c_double(player_x),
        ctypes.c_double(player_z),
        ctypes.c_int(radius),
        ctypes.c_int(max_results),
        c_types,
        ctypes.c_int(num_types),
    )
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
            raw_ptr = fn(*args, None)  # mcLabel -> MC_NEWEST (future game-version param)
        else:
            raw_ptr = native.lib.seedfinder_scan(*args)
    except Exception as e:
        return jsonify({"error": f"Scan failed: {e}",
                        "missing_or_invalid": missing}), 500

    if not raw_ptr:
        return _ok({"results": [], "missing_or_invalid": missing})

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

    return _ok(result)


@api_bp.route("/scan", methods=["GET", "POST"])
def scan():
    return _scan(None)


@api_bp.route("/scan/java", methods=["GET", "POST"])
def scan_java():
    return _scan("java")


@api_bp.route("/scan/bedrock", methods=["GET", "POST"])
def scan_bedrock():
    return _scan("bedrock")
