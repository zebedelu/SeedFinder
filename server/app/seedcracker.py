"""SeedCracker API blueprint - /seedcracker route.

Given at least 4 structures with coordinates, cracks the Bedrock world seed
by exhaustively validating candidate seeds (32-bit space, multi-threaded in
the native library). Disabled on Vercel: the compute is too expensive to run
on a serverless platform.

Response is always a JSON *list*. Item 0 is the header
{"status": "ok"|"partial"|"error"|"unavailable", ...}; the rest are
{"seed", "score", "matches"} results.
"""

import ctypes
import json
import os
import time

from flask import Blueprint, jsonify, render_template, request

from . import native

seedcracker_bp = Blueprint("seedcracker", __name__)

MIN_STRUCTURES = 4
MAX_STRUCTURES = 24
MAX_TOLERANCE = 8
COORD_LIMIT = 1_000_000_000
MAX_RESULTS_CAP = 2000
DEFAULT_MAX_SECONDS = 30.0
MAX_MAX_SECONDS = 120.0

GITHUB_URL = "https://github.com/zebedelu/SeedFinder"
DOWNLOAD_URL = f"{GITHUB_URL}/releases"

# Type ids supported by the cracker (periodic region placement). Mineshaft (15)
# has regionSize 1 and uses a per-chunk RNG - not crackable by region search.
STRUCTURE_NAMES = {
    1: "Desert Pyramid", 2: "Jungle Temple", 3: "Swamp Hut", 4: "Igloo",
    5: "Village", 6: "Ocean Ruin", 7: "Shipwreck", 8: "Ocean Monument",
    9: "Woodland Mansion", 10: "Pillager Outpost", 11: "Ruined Portal",
    13: "Ancient City", 14: "Buried Treasure", 23: "Trail Ruins",
    24: "Trial Chambers",
}

UNAVAILABLE = [{
    "status": "unavailable",
    "message": ("SeedCracker is not available on Vercel at the moment. This "
                "service is too expensive to keep running for free - please "
                "donate or use the offline/local version."),
    "download": DOWNLOAD_URL,
    "docs": "/seedcracker/documentation",
}]


def is_vercel() -> bool:
    return (
        os.environ.get("VERCEL") == "1"
        or "vercel.app" in os.environ.get("VERCEL_URL", "")
        or "vercel.app" in (request.host or "")
    )


def _parse(post_data, args):
    """Returns (tolerance, units, structures, opts). Raises ValueError."""
    structures = []
    opts = {}
    tolerance = 6
    units = "blocks"

    raw = post_data  # JSON body or None
    if raw is None:
        # GET query form: ?tolerance=6&units=blocks&structures=5,-280,152;8,696,360
        tolerance = int(args.get("tolerance", 6) or 6)
        units = args.get("units", "blocks") or "blocks"
        for k, cast in (("start", int), ("end", int), ("max", int),
                        ("max_seconds", float)):
            if args.get(k):
                opts[k] = cast(args[k])
        q = (args.get("structures") or "").strip()
        if q:
            for group in q.split(";"):
                parts = [p.strip() for p in group.split(",") if p.strip()]
                if len(parts) != 3:
                    raise ValueError(f"bad structure group: {group!r}")
                structures.append({"type": int(parts[0]),
                                   "x": int(parts[1]), "z": int(parts[2])})
    elif isinstance(raw, list):
        # standard list form: [ {options}, structure, structure, ... ]
        if not raw or not isinstance(raw[0], dict):
            raise ValueError("expected an options object as the first list item")
        head, tail = raw[0], raw[1:]
        tolerance = int(head.get("tolerance", 6))
        units = head.get("units", "blocks")
        for k in ("start", "end", "max", "max_seconds"):
            if k in head:
                opts[k] = head[k]
        if any(not isinstance(s, dict) for s in tail):
            raise ValueError("every list item after the first must be a structure object")
        structures = list(tail)
    elif isinstance(raw, dict):
        tolerance = int(raw.get("tolerance", 6))
        units = raw.get("units", "blocks")
        for k in ("start", "end", "max", "max_seconds"):
            if k in raw:
                opts[k] = raw[k]
        structures = raw.get("structures", [])
    else:
        raise ValueError("payload must be a JSON list or object")

    if not (0 <= tolerance <= MAX_TOLERANCE):
        raise ValueError(f"tolerance must be between 0 and {MAX_TOLERANCE} chunks")
    if units not in ("blocks", "chunks"):
        raise ValueError("units must be 'blocks' or 'chunks'")

    if not isinstance(structures, list) or not structures:
        raise ValueError("no structures provided")
    if len(structures) < MIN_STRUCTURES:
        raise ValueError(f"at least {MIN_STRUCTURES} structures are required")

    out = []
    for i, s in enumerate(structures):
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
        if t == 15:
            raise ValueError("Mineshaft (type 15) is not supported by SeedCracker")
        if t not in STRUCTURE_NAMES:
            raise ValueError(f"structure #{i + 1}: unsupported or unknown type {t}")
        if abs(x) > COORD_LIMIT or abs(z) > COORD_LIMIT:
            raise ValueError(f"structure #{i + 1}: coordinates out of range")
        out.append({"type": t, "x": x, "z": z})
    if len(out) > MAX_STRUCTURES:
        raise ValueError(f"too many structures (max {MAX_STRUCTURES})")
    return tolerance, units, out, opts


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


@seedcracker_bp.route("/seedcracker", methods=["GET", "POST"])
def seedcracker():
    # The same path serves the HTML console (plain GET) and the API (any
    # request that carries structures). A GET with the structures param is
    # the API's query-string form; a bare GET renders the page.
    if request.method == "GET" and not request.args.get("structures"):
        return render_template("seedcracker.html", active="seedcracker",
                               is_vercel=is_vercel(), download_url=DOWNLOAD_URL)
    if is_vercel():
        return jsonify(UNAVAILABLE)
    try:
        post_data = request.get_json(silent=True) if request.method == "POST" else None
        tolerance, units, structures, opts = _parse(post_data, request.args)
    except ValueError as e:
        return jsonify([{"status": "error", "message": str(e)}]), 400
    except Exception as e:  # noqa: BLE001 - keep the envelope stable
        return jsonify([{"status": "error", "message": f"internal error: {e}"}]), 500
    if native.lib is None:
        return jsonify([{"status": "error",
                         "message": "SeedCracker native library (.dll/.so) "
                                    "not loaded on this server."}]), 503
    try:
        return jsonify(_run(structures, tolerance, units, opts))
    except Exception as e:  # noqa: BLE001
        return jsonify([{"status": "error", "message": f"crack failed: {e}"}]), 500