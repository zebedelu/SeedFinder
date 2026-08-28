"""SeedFinder web app — application factory.

Keeps app creation, native library bootstrap and blueprint wiring in one
place so the Vercel entrypoint stays a thin WSGI import.
"""

import os

from flask import Flask, request
from flask_cors import CORS

from . import native
from .api import api_bp
from .pages import pages_bp
from .seedcracker import seedcracker_bp

# The app package lives in server/app, but templates/static sit one level
# up alongside index.py. Point flask at them explicitly.
_BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def create_app() -> Flask:
    app = Flask(
        __name__,
        template_folder=os.path.join(_BASE, "templates"),
        static_folder=os.path.join(_BASE, "static"),
    )
    CORS(app)

    @app.context_processor
    def inject_local_mode():
        # True when served from a local host (exe / start.bat / start.sh);
        # lets templates show a network warning that Vercel pages skip.
        return {"local_mode": request.host.startswith(("127.0.0.1", "localhost"))}

    # On Vercel there is no main() — the lib must be ready by import time.
    native.bootstrap_lib()

    app.register_blueprint(api_bp)
    app.register_blueprint(pages_bp)
    app.register_blueprint(seedcracker_bp)

    @app.errorhandler(404)
    def not_found(_):
        return "404 - page not found", 404

    return app