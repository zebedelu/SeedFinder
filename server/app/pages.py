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
    return render_template("documentation.html", active="docs")


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

SeedFinder is a free REST API that locates Minecraft Bedrock structures from the world seed and the player position: villages, ancient cities, ocean monuments, buried treasure, mansions and more. No sign-up and no auth.

## Pages

- [Home]({base}/): landing page with links to the console and the documentation.
- [SeedFinder console]({base}/seedfinder): interactive console to run a structure scan in the browser.
- [API documentation]({base}/seedfinder/documentation): full documentation of the /status and /scan endpoints, parameters, structure IDs and client examples.

## API

- `GET {base}/status` - API health check.
- `GET {base}/scan?seed=SEED&x=X&z=Z&radius=R&max=M&types=IDS` - finds structures around a position. `radius` and `max` are capped at 1000. `types` is a comma-separated list of IDs.

### Structure IDs

1=Desert Pyramid, 2=Jungle Temple, 3=Swamp Hut, 4=Igloo, 5=Village, 6=Ocean Ruin, 7=Shipwreck, 8=Ocean Monument, 9=Woodland Mansion, 10=Pillager Outpost, 11/12=Ruined Portal, 13=Ancient City, 14=Buried Treasure, 15=Mineshaft, 23=Trail Ruins, 24=Trial Chambers

## SeedCracker (local API only)

- `GET/POST {base}/seedcracker` - given a list of at least 4 structures with coordinates, returns the most probable Bedrock seeds. Runs on the local API; disabled on the hosted (Vercel) deployment because the computation is too expensive to run for free.
- [SeedCracker documentation]({base}/seedcracker/documentation): payload format, parameters, structure IDs and real examples.

## Other

- [GitHub repository](https://github.com/zebedelu/SeedFinder): source code, builds and usage instructions.
"""
    return Response(txt, mimetype="text/plain")


@pages_bp.route("/favicon.ico")
def favicon():
    return send_from_directory(
        os.path.join(_BASE_DIR, "logo"), "logo.ico", mimetype="image/x-icon"
    )