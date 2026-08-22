"""SeedFinder HTTP Bridge — WSGI entrypoint (Vercel + CLI).

`app` is created at import time, which is what Vercel's serverless
runtimes call. Running the file directly starts a local dev server.
"""

import argparse
import os
import sys

from app import create_app, native

app = create_app()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SeedFinder HTTP Bridge (Linux)")
    parser.add_argument(
        "--so-path",
        default=None,
        help="Path to seedfinder_lib.so (auto-detected if omitted)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=int(os.environ.get("SEEDFINDER_PORT", 7890)),
        help="Port to listen on (default: 7890, env: SEEDFINDER_PORT)",
    )
    parser.add_argument(
        "--host",
        default="127.0.0.1",
        help="Host to bind on (default: 127.0.0.1)",
    )
    args = parser.parse_args()

    def _resolve(so_path):
        if so_path:
            return so_path
        if getattr(sys, "frozen", False):
            return os.path.join(sys._MEIPASS, "seedfinder_lib.so")
        # bootstrap_lib() already tried the same candidates at import time;
        # default to the colocated .so whenever it exists.
        return next((c for c in native._candidates() if os.path.isfile(c)),
                    native._candidates()[0])

    native.load_so(_resolve(args.so_path))
    print(f"SeedFinder server starting on http://{args.host}:{args.port}")
    app.run(host=args.host, port=args.port, debug=False)