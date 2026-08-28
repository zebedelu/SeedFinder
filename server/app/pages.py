"""Page blueprint — public HTML routes + SEO files.

/                          demo landing (SeedFinder + SeedCracker)
/seedfinder                SeedFinder API console
/seedfinder/documentation  SeedFinder API documentation
/robots.txt                search-engine crawl rules
/sitemap.xml               URL list for search engines
/llms.txt                  project description for LLM crawlers (llms.txt)
"""

import os

from flask import Blueprint, Response, render_template, request, send_from_directory

from .seedcracker import DOWNLOAD_URL, is_vercel

pages_bp = Blueprint("pages", __name__)

# Public routes listed in /sitemap.xml; served under the request's host so
# the files work on any domain (Vercel, custom, local dev) without hardcoding.
_PUBLIC_PATHS = (
    "/", "/seedfinder", "/seedfinder/documentation",
    "/seedcracker", "/seedcracker/documentation",
)

_BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # server/


@pages_bp.route("/")
def index():
    return render_template("index.html", active="home")


@pages_bp.route("/seedfinder")
def seedfinder():
    return render_template("seedfinder.html", active="seedfinder")


@pages_bp.route("/seedfinder/documentation")
def documentation():
    return render_template("seedfinder_documentation.html", active="docs")


@pages_bp.route("/seedcracker/documentation")
def seedcracker_documentation():
    return render_template("seedcracker_documentation.html",
                           active="seedcracker_docs", is_vercel=is_vercel(),
                           download_url=DOWNLOAD_URL)


@pages_bp.route("/robots.txt")
def robots():
    txt = (
        "User-agent: *\n"
        "Allow: /\n"
        "Disallow: /scan\n"
        "Disallow: /status\n"
        "Disallow: /seedcracker\n"
        "\n"
        f"Sitemap: {request.url_root}sitemap.xml\n"
    )
    return Response(txt, mimetype="text/plain")


@pages_bp.route("/sitemap.xml")
def sitemap():
    urls = "".join(
        f"  <url><loc>{request.url_root}{path.lstrip('/')}</loc></url>\n"
        for path in _PUBLIC_PATHS
    )
    xml = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
        f"{urls}</urlset>\n"
    )
    return Response(xml, mimetype="application/xml")


@pages_bp.route("/llms.txt")
def llms():
    base = request.url_root.rstrip("/")
    txt = f"""# SeedFinder

> Free REST API and Flarial Client module that finds Minecraft Bedrock structures (villages, ancient cities, ocean monuments, buried treasure, woodland mansions and more) from the world seed and player position. SeedCrackerX is the reverse tool: recover the most probable Bedrock world seed from a list of at least 4 structure coordinates.

## Sections

- [Home]({base}/): landing page with links to both consoles and their documentation.
- [SeedFinder console]({base}/seedfinder): run a structure scan in the browser.
- [SeedFinder API documentation]({base}/seedfinder/documentation): /status and /scan endpoints, parameters, structure IDs, client examples.
- [SeedCrackerX console]({base}/seedcracker): recover the most probable seed from structure coordinates (local API only).
- [SeedCrackerX documentation]({base}/seedcracker/documentation): /seedcracker payload, parameters, response format, live examples.

## SeedFinder API

- `GET {base}/status` - health check; returns `{{"status": "ok"}}` or a 500 error when the native library failed to load.
- `GET {base}/scan?seed=SEED&x=X&z=Z&radius=R&max=M&types=IDS` - lists structures near a position, sorted by distance. `radius` and `max` are capped at 1000; `types` is a comma-separated list of structure IDs. Any missing or invalid parameter is replaced by its default and reported in `missing_or_invalid`.
- Response: `{{"results": [{{"name": "village", "x": 696, "z": 376, "distance": 48.8}}], "missing_or_invalid": []}}` - `distance` in chunks, one decimal.

## SeedCrackerX API (local API only)

- `POST {base}/seedcracker` (also `GET`) - given at least 4 structures with `type`/`x`/`z`, returns the most probable Bedrock seeds. Sweeps the 32-bit seed space in parallel (native, multi-threaded), bounded by a time budget.
- POST body (canonical list form): `[{{"tolerance": 0, "max_seconds": 120}}, {{"type": 5, "x": -280, "z": 152}}, ...]`; a JSON object `{{"structures": [...]}}` is also accepted, and GET uses `?structures=5,-280,152;8,696,360`.
- Options: `tolerance` (0-8 chunks, default 6; lower = stronger match), `units` (`blocks` default or `chunks`), `start`/`end` (seed range, default full 32-bit space), `max` (result cap, default 500, max 2000), `max_seconds` (time budget, default 30, 1-120; partial results are returned when it expires).
- Response is always a JSON list: a header item `{{"status": "ok"|"partial"|"error"|"unavailable", "message", "structures", "tolerance", "units", "checked", "elapsed_ms", "timed_out"}}` followed by `{{"seed": 8675309, "score": 0, "matches": [[x, z], ...]}}` per probable seed.
- Disabled on the hosted deployment: computing the full sweep is too expensive to run for free, so the route answers with an `"unavailable"` list and a GitHub download link. Run the local server (`server\\start.bat` / `server/start.sh`) to use it.

## Structure IDs

`types` for /scan accepts: 1 Desert Pyramid, 2 Jungle Temple, 3 Swamp Hut, 4 Igloo, 5 Village, 6 Ocean Ruin, 7 Shipwreck, 8 Ocean Monument, 9 Woodland Mansion, 10 Pillager Outpost, 11 Ruined Portal, 12 Ruined Portal (Nether), 13 Ancient City, 14 Buried Treasure, 15 Mineshaft, 23 Trail Ruins, 24 Trial Chambers. (16 Desert Well and 17 Amethyst Geode are not supported.)

SeedCrackerX accepts types 1-11, 13, 14, 23, 24 (Mineshaft 15 is rejected - it uses a per-chunk RNG and cannot be searched by region).

## Deployment

- Hosted API: https://mineseedfinder.vercel.app (SeedCrackerX disabled).
- Local server: run `server\\start.bat` (Windows) or `server/start.sh` (Linux); answers on http://127.0.0.1:7890.
- Source, releases and the offline executable: https://github.com/zebedelu/SeedFinder.
"""
    return Response(txt, mimetype="text/plain")


@pages_bp.route("/favicon.ico")
def favicon():
    return send_from_directory(
        os.path.join(_BASE_DIR, "logo"), "logo.ico", mimetype="image/x-icon"
    )