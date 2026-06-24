"""SeedFinder HTTP Bridge — Flask API server.

Loads seedfinder_lib.dll via ctypes and exposes structure scanning
as a REST API for the Flarial Lua script to consume via network.get().
"""

HOME_PAGE = """<!doctype html>
<html lang="en">
	<head>
		<meta charset="UTF-8" />
		<meta name="viewport" content="width=device-width, initial-scale=1.0" />
		<title>SeedFinder</title>
		<style>
			body {
				font-family: Arial, sans-serif;
				max-width: 800px;
				margin: 30px auto;
				padding: 0 15px;
			}
			h1 {
				text-align: center;
			}
			.field {
				margin: 10px 0;
			}
			input[type="text"],
			input[type="number"] {
				padding: 5px;
				width: 220px;
			}
			.types {
				display: grid;
				grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
				gap: 5px;
				margin-top: 10px;
			}
			button {
				margin-top: 15px;
				padding: 8px 16px;
				cursor: pointer;
			}
			#results {
				margin-top: 25px;
			}
			li {
				margin-bottom: 6px;
			}
		</style>
	</head>
	<body>
		<h1>SeedFinder</h1>
		<div class="field">
			<label>Seed</label><br />
			<input id="seed" type="text" value="1" />
		</div>
		<div class="field">
			<label>X</label><br />
			<input id="x" type="number" value="0" />
		</div>
		<div class="field">
			<label>Z</label><br />
			<input id="z" type="number" value="0" />
		</div>
		<div class="field">
			<label>Radius (chunks)</label><br />
			<input id="radius" type="number" value="100" />
		</div>
		<div class="field">
			<label>Maximum Results</label><br />
			<input id="max" type="number" value="50" />
		</div>
		<h3>Structures</h3>
		<div class="types">
			<label><input type="checkbox" value="1" /> Desert Pyramid</label>
			<label><input type="checkbox" value="2" /> Jungle Temple</label>
			<label><input type="checkbox" value="3" /> Swamp Hut</label>
			<label><input type="checkbox" value="4" /> Igloo</label>
			<label><input type="checkbox" value="5" /> Village</label>
			<label><input type="checkbox" value="6" /> Ocean Ruin</label>
			<label><input type="checkbox" value="7" /> Shipwreck</label>
			<label><input type="checkbox" value="8" /> Monument</label>
			<label><input type="checkbox" value="9" /> Mansion</label>
			<label><input type="checkbox" value="10" /> Outpost</label>
			<label><input type="checkbox" value="11" /> Ruined Portal</label>
			<label><input type="checkbox" value="12" /> Ruined Portal (Nether)</label>
			<label><input type="checkbox" value="13" /> Ancient City</label>
			<label><input type="checkbox" value="14" /> Buried Treasure</label>
			<label><input type="checkbox" value="15" /> Mineshaft</label>
			<label><input type="checkbox" value="16" /> Desert Well</label>
			<label><input type="checkbox" value="17" /> Geode</label>
			<label><input type="checkbox" value="23" /> Trail Ruins</label>
			<label><input type="checkbox" value="24" /> Trial Chambers</label>
		</div>
		<button onclick="scan()">Scan</button>
		<div id="results"></div>
		<script>
			async function scan() {
				const seed = document.getElementById("seed").value;
				const x = document.getElementById("x").value;
				const z = document.getElementById("z").value;
				const radius = document.getElementById("radius").value;
				const max = document.getElementById("max").value;
				const types = [...document.querySelectorAll(".types input:checked")]
					.map((x) => x.value)
					.join(",");
				let url = `http://127.0.0.1:7890/scan?seed=${seed}&x=${x}&z=${z}&radius=${radius}&max=${max}`;
				if (types) url += `&types=${types}`;
				const results = document.getElementById("results");
				results.innerHTML = "Loading...";
				try {
					const response = await fetch(url);
					const data = await response.json();
					if (!data.results || !data.results.length) {
						results.innerHTML = "<p>No results found.</p>";
						return;
					}
					let html = "<h3>Results</h3><ul>";
					for (const item of data.results) {
						html += `
                <li>
                    <b>${item.name}</b>
                    | X: ${item.x}
                    | Z: ${item.z}
                    | Distance: ${item.distance}
                </li>
            `;
					}
					html += "</ul>";
					results.innerHTML = html;
				} catch {
					results.innerHTML = "<p>Failed to connect to API.</p>";
				}
			}
		</script>
	</body>
</html>
"""

import argparse
import ctypes
import json
import os
import sys

from flask import Flask, request, jsonify
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

# Global ctypes library handle
_lib = None


def _resolve_dll_path(cli_dll_path):
    """Resolve the DLL path: CLI arg > sys._MEIPASS (frozen) > same-dir fallback."""
    if cli_dll_path:
        return cli_dll_path

    # Running inside a PyInstaller bundle
    if getattr(sys, 'frozen', False):
        return os.path.join(sys._MEIPASS, 'seedfinder_lib.dll')

    # Dev mode: look next to the script (server/base/win/ -> project root -> build_server/)
    return os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        '..', '..', '..', 'build_server', 'seedfinder_lib.dll')


def load_dll(dll_path):
    """Load the seedfinder_lib.dll and set up ctypes signatures."""
    global _lib

    if not os.path.isfile(dll_path):
        print(f"ERROR: DLL not found at {dll_path}", file=sys.stderr)
        print("Build it first with: start.bat or cmake + mingw32-make", file=sys.stderr)
        sys.exit(1)

    try:
        _lib = ctypes.CDLL(os.path.abspath(dll_path))
    except OSError as e:
        print(f"ERROR: Failed to load DLL: {e}", file=sys.stderr)
        sys.exit(1)

    # Use c_void_p for restype so Python doesn't auto-convert to bytes,
    # which would lose the original pointer needed for seedfinder_free_result.
    # seedfinder_scan(...) -> char* (caller must free)
    _lib.seedfinder_scan.argtypes = [
        ctypes.c_uint64,
        ctypes.c_double, ctypes.c_double,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.POINTER(ctypes.c_int),
        ctypes.c_int,
    ]
    _lib.seedfinder_scan.restype = ctypes.c_void_p

    # seedfinder_free_result(void* result) -> void
    _lib.seedfinder_free_result.argtypes = [ctypes.c_void_p]
    _lib.seedfinder_free_result.restype = None

    # seedfinder_status() -> const char* (static string, no free needed)
    _lib.seedfinder_status.argtypes = []
    _lib.seedfinder_status.restype = ctypes.c_char_p

    print(f"DLL loaded: {dll_path}")

@app.route("/")
def index():
    """If you want to test the API"""
    return HOME_PAGE


@app.route("/status")
def status():
    """Health check endpoint."""
    try:
        result = _lib.seedfinder_status()
        return jsonify(json.loads(result))
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500


@app.route("/scan")
def scan():
    """Scan for structures near the given position.

    Query parameters:
    seed (int) — World seed
    x (float) — Player X block position
    z (float) — Player Z block position
    radius (int) — Search radius in chunks
    max (int) — Maximum results to return (default 20)
    types (string) — Comma-separated structure type IDs (e.g. "5,1,10")
    """
    try:
        seed = int(request.args.get("seed", "0"))
        player_x = float(request.args.get("x", "0"))
        player_z = float(request.args.get("z", "0"))
        radius = int(request.args.get("radius", "100"))
        max_results = int(request.args.get("max", "20"))
        types_str = request.args.get("types", "5")
    except (ValueError, TypeError) as e:
        return jsonify({"error": f"Invalid parameter: {e}"}), 400

    # Parse types
    try:
        types_list = [int(t.strip()) for t in types_str.split(",") if t.strip()]
    except ValueError:
        return jsonify({"error": "types must be comma-separated integers"}), 400

    if not types_list:
        return jsonify({"results": []})

    # Build ctypes array
    num_types = len(types_list)
    c_types = (ctypes.c_int * num_types)(*types_list)

    # Call C function — returns void pointer to malloc'd JSON string
    try:
        raw_ptr = _lib.seedfinder_scan(
            ctypes.c_uint64(seed & 0xFFFFFFFFFFFFFFFF),
            ctypes.c_double(player_x),
            ctypes.c_double(player_z),
            ctypes.c_int(radius),
            ctypes.c_int(max_results),
            c_types,
            ctypes.c_int(num_types),
        )
    except Exception as e:
        return jsonify({"error": f"Scan failed: {e}"}), 500

    if not raw_ptr:
        return jsonify({"results": []})

    # Read the C string from the pointer, then free it
    try:
        raw_bytes = ctypes.string_at(raw_ptr)
        json_str = raw_bytes.decode("utf-8")
    except Exception as e:
        return jsonify({"error": f"Failed to read result: {e}"}), 500
    finally:
        _lib.seedfinder_free_result(raw_ptr)

    # Parse JSON
    try:
        result = json.loads(json_str)
    except json.JSONDecodeError as e:
        return jsonify({"error": f"Invalid JSON from C: {e}"}), 500

    return jsonify(result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="SeedFinder HTTP Bridge")
    parser.add_argument(
        "--dll-path",
        default=None,
        help="Path to seedfinder_lib.dll (auto-detected if omitted)",
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
        help="Host to bind to (default: 127.0.0.1)",
    )
    args = parser.parse_args()

    dll_path = _resolve_dll_path(args.dll_path)
    load_dll(dll_path)
    print(f"SeedFinder server starting on http://{args.host}:{args.port}")
    app.run(host=args.host, port=args.port, debug=False)
