<p align="center">
  <img src="server/logo/logo.ico" height="140" alt="SeedFinder logo">
</p>

<h1 align="center">SeedFinder</h1>

<p align="center">
  <b>Find Minecraft Bedrock structures - as a REST API, and as an overlay inside Flarial Client.</b>
</p>

<p align="center">
  <img alt="License" src="https://img.shields.io/badge/license-Apache--2.0-blue">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Minecraft%20Bedrock-4c1">
</p>

SeedFinder looks up nearby Minecraft **Bedrock Edition** structures - villages, buried treasure, ancient cities, ocean monuments, and so on - given a world seed and a player position. Under the hood it's a small C engine built on [cubiomes](https://github.com/Cubitect/cubiomes) plus Bedrock-specific structure math, exposed over HTTP, and hooked into an overlay module for [Flarial Client](https://flarial.xyz).

You can call the hosted API directly from any language, or drop the Lua module into Flarial and get structures listed in-game, sorted by distance, without touching a browser-based seed map.

![SeedFinder overlay showing nearby villages sorted by distance](screenshots/village1.png)

---

## Table of contents

- [Tech stack](#tech-stack)
- [Features](#features)
- [Live example](#live-example)
- [Two ways to use SeedFinder](#two-ways-to-use-seedfinder)
- [How it works](#how-it-works)
- [SeedCrackerX (reverse seed search)](#seedcrackerx-reverse-seed-search)
- [API reference](#api-reference)
- [Supported structures](#supported-structures)
- [Building from source](#building-from-source)
- [Benchmarks](#benchmarks)
- [Why SeedFinder?](#why-seedfinder)
- [Roadmap](#roadmap)
- [Contributing](#contributing)
- [Credits](#credits)
- [License](#license)
- [Authors](#authors)

---

## Tech stack

| Component | Language | Role |
|---|---|---|
| `core/` | C (+ an experimental C++ bridge) | Wraps cubiomes and Bedrock structure math into a shared library (`seedfinder_lib.dll` / `.so`) |
| `server/` | Python (Flask) | Loads that library with `ctypes`, exposes it over HTTP |
| `script/` | Lua | Runs inside Flarial Client, calls the API, draws the ImGui overlay |

Target platform is Minecraft Bedrock Edition, 1.18 through the latest release. Most players just want the packaged Windows workflow (`SeedFinder.exe` + Lua script); the API underneath is plain HTTP and doesn't care what OS or language is calling it.

## Features

- **Native scan engine.** The actual search runs in compiled C against cubiomes, not Lua or Python, so it stays fast even at large radii - see [Benchmarks](#benchmarks) for real numbers.
- **Plain REST API.** One `GET /scan` endpoint, query-string parameters in, JSON out. No SDK, no auth.
- **Official site.** The app is live at `https://mineseedfinder.vercel.app` - a free hosted instance you can call without building or running anything yourself.
- **In-game overlay for Flarial Client**, results sorted by distance, rendered with ImGui.
- **15 working structure types** - villages, temples, ocean structures, ancient cities, mansions, portals, and more. Full list [below](#supported-structures).
- **Multi-type queries** - ask for several structures in one request (`types=5,8,9`) instead of one call per type.
- **SeedCrackerX (reverse seed search)** - give at least 4 structures with coordinates and get the most probable Bedrock seed back. Sweeps the 32-bit seed space in parallel in native C inside a time budget. Runs on the local API - see [SeedCrackerX](#seedcrackerx-reverse-seed-search).
- **No dependencies for players.** The packaged `.exe` bundles the server itself; no Python or pip needed on the player's machine.
- Apache-2.0, source available, and nothing in the code sends data anywhere except the request you made.

## Live example

No install, no local server - this hits the hosted API directly:

```bash
curl "https://mineseedfinder.vercel.app/scan?seed=1234567890&x=0&z=0&radius=100&max=20&types=5"
```

```json
{
  "results": [
    { "name": "village", "x": 696, "z": 376, "distance": 48.8 }
  ]
}
```

`types=5` is Village. Try `14` for Buried Treasure, `13` for Ancient City, or a few at once with `types=5,8,9`. Full ID table is [below](#supported-structures).

## Two ways to use SeedFinder

**A. In Minecraft, through Flarial Client.** This is the packaged path most players want:

1. Download `SeedFinder.exe` and `SeedFinder.lua` from [Releases](../../releases).
2. Run `SeedFinder.exe` - it starts a local server on `http://127.0.0.1:7890`.
3. Copy `SeedFinder.lua` into `%localappdata%\Flarial\Client\Scripts\Modules`.
4. Open Minecraft with Flarial Client. SeedFinder shows up in your module list and talks to the local server automatically.

```bat
REM Optional: custom port/host
SeedFinder.exe --port 8080 --host 0.0.0.0
```

![SeedFinder overlay showing nearby Ancient Cities in the Deep Dark](screenshots/ancient_city1.png)

**B. As a plain HTTP API**, if you're building something of your own - a bot, a tool, a site. Point it at the hosted instance, or [run your own](#building-from-source):

```python
import requests

BASE = "https://mineseedfinder.vercel.app"

def scan(seed, x=0, z=0, radius=100, max_=20, types="5"):
    params = {"seed": seed, "x": x, "z": z, "radius": radius, "max": max_, "types": types}
    r = requests.get(f"{BASE}/scan", params=params, timeout=15)
    r.raise_for_status()
    return r.json()

for s in scan(seed=31415, radius=100, max_=50, types="5,8,9")["results"]:
    print(f"{s['name']:<14} (x={s['x']}, z={s['z']}) dist={s['distance']}")
```

```javascript
// Node 18+, no dependencies
const BASE = "https://mineseedfinder.vercel.app";

async function scan(seed, x = 0, z = 0, radius = 100, max = 20, types = "5") {
  const url = new URL("/scan", BASE);
  Object.entries({ seed, x, z, radius, max, types }).forEach(([k, v]) => url.searchParams.set(k, v));
  const res = await fetch(url, { signal: AbortSignal.timeout(15000) });
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
}
```

```lua
-- Inside a Flarial Client module; `network` is a Flarial global
local BASE = "http://127.0.0.1:7890" -- or https://mineseedfinder.vercel.app
local function scan(seed, x, z, radius, maxResults, types)
  local url = string.format("%s/scan?seed=%d&x=%d&z=%d&radius=%d&max=%d&types=%s",
    BASE, seed, x, z, radius, maxResults, types)
  local resp = network.get(url)
  if not resp or resp.code ~= 200 then return nil end
  return resp.body -- JSON string
end
```

## How it works

```
Flarial Lua Script  --HTTP-->  Flask server  --ctypes-->  seedfinder_lib (.so / .dll)  -->  cubiomes
   (in Minecraft)               (port 7890)                  (compiled from core/)
```

1. **C core** (`core/`) - `seedfinder_wrapper.c` links cubiomes statically and implements `seedfinder_scan()`. For each requested structure type it walks the relevant grid regions around the player within `radius`, checks biome viability, computes distance, sorts, caps at `max`, and returns a hand-built JSON string across the ABI boundary (freed afterward with `seedfinder_free_result`).
2. **HTTP server** (`server/`) - a single Flask app package (`server/app/`) loads that shared library and exposes `/status` and `/scan`. One codebase serves every deployment: `server/index.py` is the WSGI entry, the same file runs locally on Windows and Linux via `server/start.bat` / `server/start.sh`, and it's what gets frozen into `SeedFinder.exe` by `server/build_exe.py`.
3. **Flarial script** (`script/SeedFinder.lua`) - polls `/status`, calls `/scan` with the player's live coordinates, and draws the results in an ImGui panel with a name-to-icon lookup. A second, structurally identical module, `script/WHSeedFinder.lua`, points at the hosted API (https://mineseedfinder.vercel.app) instead of the local server.

There's also an experimental fourth path in `core/SeedFinderBridge.cpp` / `.h`: a direct Lua↔C bridge meant to be compiled straight into the Flarial Client DLL, cutting out the HTTP hop entirely. It's set up in `CMakeLists.txt` but the shipped Lua script doesn't use it yet - see [Roadmap](#roadmap).

## SeedCrackerX (reverse seed search)

SeedFinder walks seed → structures. [SeedCrackerX](https://mineseedfinder.vercel.app/seedcracker) goes the other way: give it at least 4 structures with their coordinates and it returns the most probable Bedrock world seeds that generate them.

It sweeps the 32-bit Bedrock seed space in parallel inside the native library (one thread per CPU core, capped at 64), bounded by a time budget — so an answer always comes back, even if the full sweep would take minutes. The more structures you list and the tighter the `tolerance`, the fewer seeds pass.

```bash
curl -X POST http://127.0.0.1:7890/seedcracker \
  -H "Content-Type: application/json" \
  -d '[{"tolerance":0,"max_seconds":120},{"type":5,"x":-280,"z":152},{"type":5,"x":-280,"z":-360},{"type":8,"x":696,"z":360},{"type":8,"x":712,"z":760}]'
```

```json
[
  { "status": "ok", "message": "crack completed", "structures": 4, "tolerance": 0, "units": "blocks", "checked": 16000000, "elapsed_ms": 2800, "timed_out": false },
  { "seed": 8675309, "score": 0, "matches": [[-18, 9], [-18, -23], [43, 22], [44, 47]] }
]
```

The response is always a JSON list: a header item with `status` (`ok`, `partial` when the time budget ran out, `error`, or `unavailable`), then one item per probable seed with `seed`, `score` (sum of squared chunk deviations across structures — lower is better) and the matched structure chunk coordinates.

SeedCracker runs on the **local API only** (`http://127.0.0.1:7890`, started with `server\start.bat` or `server/start.sh`). The hosted instance disables the route because the computation is too expensive to keep running for free; it answers with an `"unavailable"` list and a download link. Payload options: `tolerance` (0–8 chunks, default 6), `units` (`blocks` or `chunks`), `start`/`end` (seed range), `max` (default 500, cap 2000), `max_seconds` (default 30, 1–120). Full docs: [SeedCrackerX documentation](https://mineseedfinder.vercel.app/seedcracker/documentation).

## API reference

### `GET /status`

```json
{"status": "ok"}
```

### `GET /scan`

| Param | Type | Default | Notes |
|---|---|---|---|
| `seed` | integer | `0` | World seed. Negative values are accepted and folded into an unsigned 64-bit seed, the way Minecraft represents seeds internally. |
| `x` | float | `0` | Player X block position. |
| `z` | float | `0` | Player Z block position. |
| `radius` | integer | `100` | Search radius in **chunks**. |
| `max` | integer | `20` | Max results returned, sorted by distance ascending. |
| `types` | string | `"5"` | Comma-separated [structure IDs](#supported-structures), e.g. `"5,8,9"`. |

```json
{
  "results": [
    { "name": "village", "x": 696, "z": 376, "distance": 48.8 }
  ]
}
```

`distance` is in chunks, rounded to one decimal.

Error responses:

```jsonc
// 400 - bad value, e.g. seed=abc
{ "error": "Invalid parameter: invalid literal for int() with base 10: 'abc'" }

// 503 - native library failed to load (hosted only, if the build didn't ship the .so)
{ "error": "SeedFinder native library (.so) not loaded on this server.", "results": [] }
```

All deployments run the same app package, so behavior is identical everywhere: `radius` and `max` are clamped to `1000`, and a `missing_or_invalid` array lists any params that had to be defaulted (e.g. a stray `radius=50000` is capped rather than let through).

The local server also serves a tiny HTML form at `/` if you'd rather click through a request than type a `curl` command.

### `POST /seedcracker`

Every SeedFinder deployment can also crack a seed back from structures. Parameters:

| Param | Type | Default | Notes |
|---|---|---|---|
| `structures` | list | — | 4–24 objects `{"type": int, "x": int, "z": int}`. Mineshaft (15) not supported. |
| `tolerance` | int | `6` | Match radius in chunks, `0`–`8`. Lower = stronger match. |
| `units` | string | `"blocks"` | `"blocks"` or `"chunks"` (multiplied by 16). |
| `start` / `end` | int | `0` / `2^32` | Inclusive start / exclusive end of the seed sweep. |
| `max` | int | `500` | Max results kept (best by score). |
| `max_seconds` | float | `30` | Time budget; partial results when it expires. |

Response is a JSON list — header then `{"seed", "score", "matches"}` per candidate. Only the local API serves it; the hosted site returns `"unavailable"`. See [SeedCrackerX (reverse seed search)](#seedcrackerx-reverse-seed-search) and [SeedCrackerX documentation](https://mineseedfinder.vercel.app/seedcracker/documentation).

## Supported structures

Confirmed by calling `/scan` for every ID against a real seed at a large radius:

| ID | Structure | Status | ID | Structure | Status |
|----|-----------|:---:|----|-----------|:---:|
| 1 | Desert Pyramid | ✅ | 13 | Ancient City | ✅ |
| 2 | Jungle Temple | ✅ | 14 | Buried Treasure | ✅ |
| 3 | Swamp Hut | ✅ | 15 | Mineshaft | ✅ |
| 4 | Igloo | ✅ | 16 | Desert Well | ❌ not supported |
| 5 | Village | ✅ | 17 | Amethyst Geode | ❌ not supported |
| 6 | Ocean Ruin | ✅ | 23 | Trail Ruins | ✅ |
| 7 | Shipwreck | ✅ | 24 | Trial Chambers | ✅ |
| 8 | Ocean Monument | ✅ | | | |
| 9 | Woodland Mansion | ✅ | | | |
| 10 | Pillager Outpost | ✅ | | | |
| 11 | Ruined Portal | ✅ | | | |
| 12 | Ruined Portal (Nether) | ✅ | | | |

17 of the 19 listed IDs are supported. Desert Well (`16`) and Amethyst Geode (`17`) are **not supported**: those are per-chunk placement features, not region-based structures, and the engine has no Bedrock prediction for them - see [Roadmap](#roadmap).

Bastion Remnant, Nether Fortress, and End City aren't exposed under any ID yet, though their structure configs already exist in the engine - see [Roadmap](#roadmap).

## Building from source

**Prerequisites**

- Windows: [MSYS2](https://www.msys2.org/) with the MinGW64 toolchain (`gcc`, `g++`, `mingw32-make`)
- Linux: `cmake`, `make`, `gcc` (e.g. `sudo apt install cmake build-essential` on Debian/Ubuntu)
- Python 3.11+ with `pip install flask flask-cors`
- [Flarial Client](https://flarial.xyz), only needed to actually load the Lua module in-game

**Build + run**

```bash
# Windows
server\start.bat

# Linux
server/start.sh
```

Both scripts build `seedfinder_lib` via CMake into `build_server/`, then start the Flask server on port `7890`.

**Package into a Windows `.exe`**:

```bat
pip install pyinstaller
python server\build_exe.py
REM Output: server\dist\SeedFinder.exe
```

**Install the Lua module** - see [`script/INSTALL.txt`](script/INSTALL.txt).

## Benchmarks

I ran these myself against the real compiled engine, not estimated:

- Timed with `curl -w "%{time_total}"` against `localhost`, so this is engine + Flask overhead, network latency excluded.
- 5 requests per scenario, average shown. There's no caching in the code, so every request does a full scan.

### v1.0.0 (measured July 4, 2026)

- Binary tested: `build_server/seedfinder_lib.so`, built from this repo's own source, served through `server/index.py` (Flask's built-in dev server, same as the project ships - no custom harness).
- Environment: single-vCPU Intel Xeon @ 2.10 GHz container, Ubuntu 24.04.4, Python 3.12.3, Flask 3.1.3. That's a modest single core - expect quicker results on a real desktop. The server is also single-threaded by default (Flask's dev server), so it won't spread a scan across multiple cores regardless of what's available.

| Scenario | Radius | Types | Results | Avg. time |
|---|---:|---|---:|---:|
| Village | 100 | `5` | 1 | ~4.6 ms |
| Buried Treasure | 200 | `14` | 20 (capped) | ~152.6 ms |
| Ancient City | 500 | `13` | 20 (capped) | ~42.9 ms |
| Village + Monument + Mansion | 300 | `5,8,9` | 50 (capped) | ~132.6 ms |
| 5 types combined | 1000 | `5,14,13,9,10` | 1000 (capped) | ~3.72 s |

### v1.2.0 (measured August 29, 2026)

Taken after the `faster seedcracker & seedfinder` engine optimization. This is a different machine than the v1.0.0 table above (Intel Core i3-1305U laptop, 6 threads, Windows 11, Python 3.14.7, Flask 3.1.3), so absolute times are **not** directly comparable between the two tables - which is exactly why the pre-optimization code (commit `7513824`) was rebuilt and re-measured on this same machine, under identical conditions (seed `8675309` from player position `0, 0`, one warm-up request per scenario before timing). That gives an apples-to-apples before/after for the engine change itself:

| Scenario | Radius | Types | Results | Pre-optimization | v1.2.0 | Faster by |
|---|---:|---|---:|---:|---:|---:|
| Village | 100 | `5` | 1 | 15.9 ms | 10.6 ms | 33% |
| Buried Treasure | 200 | `14` | 20 (capped) | 286.5 ms | 220.8 ms | 23% |
| Ancient City | 500 | `13` | 20 (capped) | 84.7 ms | 68.9 ms | 19% |
| Village + Monument + Mansion | 300 | `5,8,9` | 50 (capped) | 213.4 ms | 196.7 ms | 8% |
| 5 types combined | 1000 | `5,14,13,9,10` | 1000 (capped) | 6.56 s | 4.86 s | 26% |

Net: on identical hardware, the optimization shaves 8-33% off scan time, with the biggest absolute win on the heaviest scenario (the 5-type radius-1000 scan drops ~1.7 s).

The interesting part: cost tracks how often a structure's grid repeats, not just the radius. Buried Treasure has a small region spacing and gets checked almost every chunk pair, so radius 200 actually costs more than Ancient City at radius 500, since Ancient City's grid is much sparser - both tables above show the same ~3x cost ratio. If you're polling this from something latency-sensitive - an in-game overlay, say - smaller radii and fewer combined types will feel a lot snappier than cranking both up.

## Why SeedFinder?

Compared to opening a browser-based seed map like Chunkbase mid-game: same underlying structure math (it credits and builds on Chunkbase's ported algorithms), but the results land inside Flarial instead of needing you to alt-tab out.

Compared to a Lua- or Python-only structure finder: the search itself runs in compiled C against cubiomes, which is the whole reason a radius-1000, 5-type scan finishes in a few seconds instead of a lot longer.

Compared to needing your own backend: the hosted API is free and public, so a small tool or bot can integrate without anyone standing up infrastructure for it.

And the honest tradeoff: this is a Bedrock-specific, Flarial-specific project with a Windows-first packaged path. If you want Java Edition seed finding, or a fully cross-platform native client rather than an HTTP API, [ChunkBiomesGUI](https://github.com/Nel-S/ChunkBiomes) or [cubiomes](https://github.com/Cubitect/cubiomes) directly might suit you better - SeedFinder is really this project's packaging of that same engine for Flarial users and API consumers.

## Roadmap

- **Direct Lua↔C bridge** (`core/SeedFinderBridge.cpp`) - compile the engine straight into the Flarial Client DLL and expose `seedfinder_bridge.scanStructures(...)` to Lua, removing the HTTP hop for the in-game path.
- **SeedCrackerX polish** - the reverse seed search ([section above](#seedcrackerx-reverse-seed-search)) works and is documented; future work: hosted availability, more structure types, and a tighter scoring model.
- **Amethyst Geode and Desert Well support** - IDs `17` and `16` are per-chunk placement features (not region structures) and are not available. Geode prediction would additionally require simulating cave air volume, which the engine doesn't model.
- **Nether/End structures** - Bastion, Fortress, and End City configs already exist internally but aren't wired into the public `types` list yet.

## Contributing

Bug reports, feature ideas, pull requests, and doc fixes are all welcome. For anything bigger, open an issue first so we can talk through the approach before you sink time into it.

## Credits

- Structure-finding algorithms: [Chunkbase](https://chunkbase.com) by Alexander Gundermann
- Biome generation: [Cubiomes](https://github.com/Cubitect/cubiomes) (Cubitect, MIT License)
- Bedrock GUI reference: [ChunkBiomesGUI](https://github.com/Nel-S/ChunkBiomes)

## License

[Apache License 2.0](LICENSE)

## Authors

[@zebedelu](https://github.com/zebedelu)

Contributors whose code is included in this project:

[@Nel-S](https://github.com/Nel-S)

[@Cubitect](https://github.com/Cubitect)