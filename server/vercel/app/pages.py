"""Page blueprint — public HTML routes.

/                          demo landing (SeedFinder + SeedCracker)
/seedfinder                SeedFinder API console
/seedfinder/documentation  SeedFinder API documentation
"""

from flask import Blueprint, render_template

pages_bp = Blueprint("pages", __name__)


@pages_bp.route("/")
def index():
    return render_template("index.html", active="home")


@pages_bp.route("/seedfinder")
def seedfinder():
    return render_template("seedfinder.html", active="seedfinder")


@pages_bp.route("/seedfinder/documentation")
def documentation():
    return render_template("documentation.html", active="docs")