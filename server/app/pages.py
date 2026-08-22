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

pages_bp = Blueprint("pages", __name__)

# Public routes listed in /sitemap.xml; served under the request's host so
# the files work on any domain (Vercel, custom, local dev) without hardcoding.
_PUBLIC_PATHS = ("/", "/seedfinder", "/seedfinder/documentation")

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


@pages_bp.route("/robots.txt")
def robots():
    txt = (
        "User-agent: *\n"
        "Allow: /\n"
        "Disallow: /scan\n"
        "Disallow: /status\n"
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

SeedFinder é uma API REST gratuita que localiza estruturas do Minecraft Bedrock a partir da seed do mundo e da posição do jogador: vilas, cidades antigas, monumentos oceânicos, tesouros enterrados, mansões e muito mais. Sem cadastro e sem auth.

## Páginas

- [Início]({base}/): landing page com os links para o console e a documentação.
- [Console SeedFinder]({base}/seedfinder): console interativo para executar um scan de estruturas no navegador.
- [Documentação da API]({base}/seedfinder/documentation): documentação completa dos endpoints /status e /scan, parâmetros, IDs de estruturas e exemplos de clientes.

## API

- `GET {base}/status` — health check da API.
- `GET {base}/scan?seed=SEED&x=X&z=Z&radius=R&max=M&types=IDS` — busca estruturas ao redor de uma posição. `radius` e `max` são limitados a 1000. `types` é uma lista de IDs separada por vírgulas.

### IDs de estruturas

1=Desert Pyramid, 2=Jungle Temple, 3=Swamp Hut, 4=Igloo, 5=Village, 7=Shipwreck, 8=Ocean Monument, 9=Woodland Mansion, 11/12=Ruined Portal, 13=Ancient City, 14=Buried Treasure, 15=Mineshaft, 23=Trail Ruins, 24=Trial Chambers

## Outros

- [Repositório no GitHub](https://github.com/zebedelu/SeedFinder): código-fonte, builds e instruções de uso.
"""
    return Response(txt, mimetype="text/plain")


@pages_bp.route("/favicon.ico")
def favicon():
    return send_from_directory(
        os.path.join(_BASE_DIR, "logo"), "logo.ico", mimetype="image/x-icon"
    )