"""SeedFinder HTTP Bridge — WSGI entrypoint (Vercel + CLI).

`app` is created at import time, which is what Vercel's serverless
runtimes call. Running the file directly starts a local dev server.
"""

import argparse
import os

from app import create_app, console, native

app = create_app()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SeedFinder HTTP Bridge")
    parser.add_argument(
        "--lib-path",
        default=None,
        help="Path to seedfinder_lib.dll/.so (auto-detected if omitted)",
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

    def _resolve(lib_path):
        if lib_path:
            return lib_path
        # bootstrap_lib() already tried the same candidates at import time;
        # default to the first one that actually exists.
        return next((c for c in native._candidates() if os.path.isfile(c)),
                    native._candidates()[0])

    native.load_lib(_resolve(args.lib_path))
    console.print_banner(args.host, args.port, _resolve(args.lib_path),
                         native.lib is not None)
    app.run(host=args.host, port=args.port, debug=False)