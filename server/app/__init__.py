"""SeedFinder web app — application factory."""
import os

from flask import Flask
from flask_cors import CORS

from . import native
from .api import api_bp
from .pages import pages_bp
from .seedcracker import seedcracker_bp

_BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def create_app() -> Flask:
    app = Flask(__name__)
    CORS(app)

    native.bootstrap_lib()

    app.register_blueprint(api_bp)
    app.register_blueprint(pages_bp)
    app.register_blueprint(seedcracker_bp)

    @app.errorhandler(404)
    def not_found(_):
        return "404 - page not found", 404

    return app