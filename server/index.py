"""SeedFinder HTTP Bridge — API server entrypoint.

Running the file directly starts a local dev server.
"""

import argparse
import os
import sys

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
    parser.add_argument(
        "--check-update",
        action="store_true",
        help="Run the interactive update check (like the frozen exe) and exit",
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
    frozen = getattr(sys, "frozen", False)
    if frozen or args.check_update:
        console.check_for_update(allow_swap=frozen)
        if args.check_update:
            sys.exit(0)
    app.run(host=args.host, port=args.port, debug=False)