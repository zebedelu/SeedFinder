"""SeedCracker API blueprint - /seedcracker route.

Given at least 4 structures with coordinates, cracks the Bedrock world seed
by exhaustively validating candidate seeds (32-bit space, multi-threaded in
the native library). With "mode": "64" (POST JSON only, or an "end" above
2^32) the 64-bit engine runs instead: Bedrock-MT structures plus at least
one Java-style anchor (Trail Ruins 23 / Trial Chambers 24) recover the full
64-bit seed.
"""

import ctypes
import json
import os
import time

from flask import Blueprint, jsonify, request

from . import native

seedcracker_bp = Blueprint("seedcracker", __name__)

MIN_STRUCTURES = 4
MAX_STRUCTURES = 24
MAX_TOLERANCE = 8
COORD_LIMIT = 1_000_000_000
MAX_RESULTS_CAP = 2000
DEFAULT_MAX_SECONDS = 30.0
MAX_MAX_SECONDS = 120.0

STRUCTURE_NAMES = {
    1: "Desert Pyramid", 2: "Jungle Temple", 3: "Swamp Hut", 4: "Igloo",
    5: "Village", 6: "Ocean Ruin", 7: "Shipwreck", 8: "Ocean Monument",
    9: "Woodland Mansion", 10: "Pillager Outpost", 11: "Ruined Portal",
    13: "Ancient City", 14: "Buried Treasure", 23: "Trail Ruins",
    24: "Trial Chambers",
}

JAVA_TYPES = {23, 24}
MT_TYPES = {t for t in STRUCTURE_NAMES if t not in JAVA_TYPES}

MINESHAFT_MSG = "Mineshaft (type 15) is not supported by SeedCracker"
JAVA_ANCHOR_MSG = (
    "Trail Ruins and Trial Chambers must be passed as 'java_structures' "
    "in 64-bit mode")


def _validate_structures(items, min_count, max_count, allowed, overrides,
                         units):
    """Shared per-list validation used by both modes. Raises ValueError."""
    if not isinstance(items, list) or not items:
        raise ValueError("no structures provided")
    if len(items) < min_count:
        raise ValueError(f"at least {min_count} structures are required")
    out = []
    for i, s in enumerate(items):
        if not isinstance(s, dict):
            raise ValueError(f"structure #{i + 1} is not an object")
        try:
            t = int(s["type"])
            x = int(s["x"])
            z = int(s["z"])
        except (KeyError, TypeError, ValueError):
            raise ValueError(f"structure #{i + 1} needs integer 'type', 'x' and 'z'")
        if units == "chunks":
            x = x * 16
            z = z * 16
        if t in overrides:
            raise ValueError(overrides[t])
        if t not in allowed:
            raise ValueError(f"structure #{i + 1}: unsupported or unknown type {t}")
        if abs(x) > COORD_LIMIT or abs(z) > COORD_LIMIT:
            raise ValueError(f"structure #{i + 1}: coordinates out of range")
        out.append({"type": t, "x": x, "z": z})
    if len(out) > max_count:
        raise ValueError(f"too many structures (max {max_count})")
    return out


def _is_mode64(head):
    """Explicit mode key, or an 'end' beyond the 32-bit space (JSON only)."""
    mode = head.get("mode", "32")
    if str(mode) == "64":
        return True
    if str(mode) not in ("32", "None"):
        raise ValueError("mode must be '32' or '64'")
    end = head.get("end")
    if isinstance(end, (int, float)) and int(end) > (1 << 32):
        return True
    return False


def _parse(post_data, args):
    """Returns (mode64, tolerance, units, structures, java_structures, opts).

    Raises ValueError. 64-bit mode is POST-JSON only; GET stays 32-bit.
    """
    java_structures = []
    opts = {}
    tolerance = 6
    units = "blocks"

    raw = post_data  # JSON body or None
    if raw is None:
        mode64 = False
        tolerance = int(args.get("tolerance", 6) or 6)
        units = args.get("units", "blocks") or "blocks"
        for k, cast in (("start", int), ("end", int), ("max", int),
                        ("max_seconds", float)):
            if args.get(k):
                opts[k] = cast(args[k])
        structures = []
        q = (args.get("structures") or "").strip()
        if q:
            for group in q.split(";"):
                parts = [p.strip() for p in group.split(",") if p.strip()]
                if len(parts) != 3:
                    raise ValueError(f"bad structure group: {group!r}")
                structures.append({"type": int(parts[0]),
                                   "x": int(parts[1]), "z": int(parts[2])})
    elif isinstance(raw, list):
        if not raw or not isinstance(raw[0], dict):
            raise ValueError("expected an options object as the first list item")
        head, tail = raw[0], raw[1:]
        mode64 = _is_mode64(head)
        tolerance = int(head.get("tolerance", 6))
        units = head.get("units", "blocks")
        for k in ("start", "end", "max", "max_seconds"):
            if k in head:
                opts[k] = head[k]
        if any(not isinstance(s, dict) for s in tail):
            raise ValueError("every list item after the first must be a structure object")
        if mode64:
            structures = head.get("mt_structures", tail)
            java_structures = head.get("java_structures", [])
        else:
            structures = list(tail)
    elif isinstance(raw, dict):
        mode64 = _is_mode64(raw)
        tolerance = int(raw.get("tolerance", 6))
        units = raw.get("units", "blocks")
        for k in ("start", "end", "max", "max_seconds"):
            if k in raw:
                opts[k] = raw[k]
        if mode64:
            structures = raw.get("mt_structures", raw.get("structures", []))
            java_structures = raw.get("java_structures", [])
        else:
            structures = raw.get("structures", [])
    else:
        raise ValueError("payload must be a JSON list or object")

    if not (0 <= tolerance <= MAX_TOLERANCE):
        raise ValueError(f"tolerance must be between 0 and {MAX_TOLERANCE} chunks")
    if units not in ("blocks", "chunks"):
        raise ValueError("units must be 'blocks' or 'chunks'")

    if not mode64:
        structures = _validate_structures(
            structures, MIN_STRUCTURES, MAX_STRUCTURES,
            set(STRUCTURE_NAMES), {15: MINESHAFT_MSG}, units)
        return mode64, tolerance, units, structures, [], opts

    if not java_structures:
        raise ValueError("64-bit mode requires at least one Java-style anchor "
                         "(Trail Ruins 23 / Trial Chambers 24) in "
                         "'java_structures'")
    structures = _validate_structures(
        structures, 1, MAX_STRUCTURES, MT_TYPES,
        {15: MINESHAFT_MSG, 23: JAVA_ANCHOR_MSG, 24: JAVA_ANCHOR_MSG}, units)
    java_structures = _validate_structures(
        java_structures, 1, MAX_STRUCTURES, JAVA_TYPES, {}, units)
    return mode64, tolerance, units, structures, java_structures, opts


def _run(structures, tolerance, units, opts):
    t0 = time.monotonic()
    n = len(structures)
    types = (ctypes.c_int * n)(*(s["type"] for s in structures))
    xs = (ctypes.c_double * n)(*(float(s["x"]) for s in structures))
    zs = (ctypes.c_double * n)(*(float(s["z"]) for s in structures))
    start = int(opts.get("start", 0)) & 0xFFFFFFFF
    end = min(int(opts.get("end", 0x100000000)), 0x100000000)
    max_results = min(int(opts.get("max", 500)), MAX_RESULTS_CAP)
    budget = float(opts.get("max_seconds", DEFAULT_MAX_SECONDS))
    budget = max(1.0, min(budget, MAX_MAX_SECONDS))
    threads = max(1, min(os.cpu_count() or 4, 64))
    ptr = native.lib.seedfinder_crack(
        types, n, xs, zs, tolerance, start, end, max_results, budget, threads)
    if not ptr:
        raise RuntimeError("seedfinder_crack returned NULL")
    try:
        data = json.loads(ctypes.string_at(ptr).decode("utf-8"))
    finally:
        native.lib.seedfinder_free_result(ptr)
    if "error" in data:
        return [{"status": "error", "message": data["error"]}]
    timed_out = bool(data.get("timed_out"))
    elapsed = int((time.monotonic() - t0) * 1000)
    head = {
        "status": "partial" if timed_out else "ok",
        "message": ("search stopped by time budget - results are partial"
                    if timed_out else "crack completed"),
        "structures": n, "tolerance": tolerance, "units": units,
        "checked": data.get("checked", 0), "elapsed_ms": elapsed,
        "timed_out": timed_out,
    }
    return [head] + data.get("results", [])


def _run64(structures, java_structures, tolerance, units, opts):
    t0 = time.monotonic()
    n_mt = len(structures)
    n_j = len(java_structures)
    mt_types = (ctypes.c_int * n_mt)(*(s["type"] for s in structures))
    mt_xs = (ctypes.c_double * n_mt)(*(float(s["x"]) for s in structures))
    mt_zs = (ctypes.c_double * n_mt)(*(float(s["z"]) for s in structures))
    j_types = (ctypes.c_int * n_j)(*(s["type"] for s in java_structures))
    j_xs = (ctypes.c_double * n_j)(*(float(s["x"]) for s in java_structures))
    j_zs = (ctypes.c_double * n_j)(*(float(s["z"]) for s in java_structures))
    start = int(opts.get("start", 0)) % (1 << 64)
    end = int(opts.get("end", 1 << 63)) % (1 << 64)
    max_results = min(int(opts.get("max", 500)), MAX_RESULTS_CAP)
    budget = float(opts.get("max_seconds", DEFAULT_MAX_SECONDS))
    budget = max(1.0, min(budget, MAX_MAX_SECONDS))
    threads = max(1, min(os.cpu_count() or 4, 64))
    ptr = native.lib.seedfinder_crack64_shim(
        mt_types, mt_xs, mt_zs, n_mt, j_types, j_xs, j_zs, n_j,
        tolerance, start, end, max_results, budget, threads)
    if not ptr:
        raise RuntimeError("seedfinder_crack64_shim returned NULL")
    try:
        data = json.loads(ctypes.string_at(ptr).decode("utf-8"))
    finally:
        native.lib.seedfinder_free_result(ptr)
    if "error" in data:
        return [{"status": "error", "message": data["error"]}]
    timed_out = bool(data.get("timed_out"))
    elapsed = int((time.monotonic() - t0) * 1000)
    head = {
        "status": "partial" if timed_out else "ok",
        "message": ("search stopped by time budget - results are partial"
                    if timed_out else "crack completed"),
        "structures": n_mt + n_j, "tolerance": tolerance, "units": units,
        "bits": 64,
        "checked": data.get("checked", 0), "elapsed_ms": elapsed,
        "timed_out": timed_out, "threads": data.get("threads", threads),
    }
    # C emits matches in blocks; convert to chunks with the same
    # (block - 8) >> 4 center convention as the 32-bit crack: MT centers sit
    # at 8 mod 16 so the offset is a no-op for them, while Java-style anchors
    # return exact corner blocks (16k) and would land 1 chunk high otherwise.
    results = []
    for cand in data.get("results", []):
        cand = dict(cand)
        cand["matches"] = [[(x - 8) // 16, (z - 8) // 16] for x, z in cand["matches"]]
        results.append(cand)
    return [head] + results


@seedcracker_bp.route("/seedcracker", methods=["GET", "POST"])
def seedcracker():
    try:
        post_data = request.get_json(silent=True) if request.method == "POST" else None
        (mode64, tolerance, units, structures, java_structures,
         opts) = _parse(post_data, request.args)
    except ValueError as e:
        return jsonify([{"status": "error", "message": str(e)}]), 400
    except Exception as e:  # noqa: BLE001 - keep the envelope stable
        return jsonify([{"status": "error", "message": f"internal error: {e}"}]), 500
    if native.lib is None:
        return jsonify([{"status": "error",
                         "message": "SeedCracker native library (.dll/.so) "
                                    "not loaded on this server."}]), 503
    if mode64 and not hasattr(native.lib, "seedfinder_crack64_shim"):
        return jsonify([{"status": "error",
                         "message": "64-bit crack requires a rebuilt native "
                                    "library (seedfinder_crack64 missing)."}]), 503
    try:
        if mode64:
            return jsonify(_run64(structures, java_structures, tolerance,
                                  units, opts))
        return jsonify(_run(structures, tolerance, units, opts))
    except Exception as e:  # noqa: BLE001
        return jsonify([{"status": "error", "message": f"crack failed: {e}"}]), 500
