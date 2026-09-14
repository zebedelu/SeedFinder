# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

SeedFinder is a Minecraft Bedrock Edition structure finder that runs as a module inside [Flarial Client](https://flarial.xyz). Given a world seed and the player's position, it locates nearby villages, temples, monuments, ancient cities, etc. — without relying on ChunkBase — by driving [cubiomes](https://github.com/Cubitect/cubiomes) directly.

`README.md` is the user-facing reference: full API docs (params, response shapes), the supported-structures list, and real benchmark tables (v1.0.0 and v1.2.0). Point users there for API questions instead of re-deriving from code.

This repository is responsible for compiling the native libraries (`.dll` and `.so`). The `.so` is consumed by an external repository called **`mineseedfinder`**, deployed on Vercel (`https://mineseedfinder.vercel.app`) to distribute the product for free. It exposes a `/scan` endpoint so the seedfinder can be tested; SeedCrackerX is available **only via WASM**, since running it as a server API would be too expensive on the Vercel runtime.

## Architecture

```
Flarial Lua Script  ──HTTP──>  Flask API (127.0.0.1:7890)  ──ctypes──>  seedfinder_lib.{dll,so}
   script/SeedFinder.lua         server/index.py                          core/seedfinder_wrapper.c
   (ImGui overlay)               (loads DLL/.so, JSON I/O)                (links cubiomes + Bfinders)
```

There are **two Flarial Lua modules** in `script/` — both are structurally identical (the file diff is only server URLs and log strings):

- `script/SeedFinder.lua` — **HTTP Bridge Edition**: points at the local `http://127.0.0.1:7890` server, the packaged Windows path (with `SeedFinder.exe`).
- `script/WHSeedFinder.lua` — **Hosted API Edition**: points at `https://mineseedfinder.vercel.app`.

If you edit the scan/UI logic or `STRUCTURE_TYPES` in one, mirror it in the other — keeping them out of sync is a real footgun.

1. **C core** (`core/`) — Structure-finding engine. Two parallel implementations:
   - `core/seedfinder_wrapper.c` — plain C entry point exported as `seedfinder_scan`, `seedfinder_free_result`, `seedfinder_status`. This is what the Python server loads. It also holds the entire **SeedCrackerX** engine (`seedfinder_crack`) — see "SeedCrackerX" below.
   - `core/SeedFinderBridge.cpp` / `.h` — alternative C++ entry point that registers a `seedfinder_bridge` Lua global inside a Lua state (for an in-process Flarial DLL injection path that is **not** built by the current `start.bat` target — see "Two build trees" below).
   Both share the same algorithm: iterate regions = `floor(playerChunk ± radius) / regionSize`, call `getBedrockStructurePos`, run `isViableBedrockStructurePos` for the biome filter, sort by distance. Floor-div helper is duplicated in both files. `seedfinder_wrapper.c` additionally special-cases **Outpost**: it re-samples the biome at the outpost's own placement cell (offset from the structure position) because cubiomes' upstream Java-style gate lets positions sitting on swamp/river biomes through.

2. **HTTP server** (`server/`) — Flask + flask-cors, loads the native library via `ctypes.CDLL`. Blueprints in `server/app/`: `api.py` (`GET /status` aliased as `GET /health`, `GET /scan?seed=…&x=…&z=…&radius=…&max=…&types=1,5,10`), `seedcracker.py` (the `/seedcracker` reverse-seed-search API), `pages.py` (a **single** `/` landing page with copy-paste API examples — the SEO/doc routes and HTML templates were deleted with the Vercel deployment). Reads the malloc'd JSON in the C side, calls `seedfinder_free_result` to release it.

3. **Flarial Lua module** (`script/SeedFinder.lua`) — Runs inside Flarial Client. Pings `http://127.0.0.1:7890/status` (5 s throttle), then calls `/scan`. Renders an ImGui overlay with structure type toggles and results sorted by distance. Reads the player's current seed/position from the Flarial runtime. Drop into `%LOCALAPPDATA%\Flarial\Client\Scripts\Modules\SeedFinder.lua` to load (see `script/INSTALL.txt`).

## SeedCrackerX (reverse seed search)

The reverse tool: given ≥4 structure coordinates, recover the most probable Bedrock seed(s). Sweeps the 32-bit seed space in native C, multi-threaded, inside a time budget. Lives in `server/app/seedcracker.py` (blueprint) + the crack engine at the bottom of `core/seedfinder_wrapper.c` (`seedfinder_crack`).

- **Route** `/seedcracker` is **API-only** now: POST JSON (object or list-of-items form) or GET with `?structures=5,-280,152;8,696,360`. A bare GET without structures returns a 400 error list — the HTML console and `/seedcracker/documentation` were deleted with the templates.
- **Response is always a JSON *list***, unlike `/scan`'s object: item 0 is the header `{"status": "ok"|"partial"|"error", "checked", "elapsed_ms", "timed_out", ...}`, then `{"seed", "score", "matches": [[x, z], ...]}` per candidate. `partial` means the `max_seconds` budget (default 30, 1–120) expired before the sweep finished.
- **Constraints**: 4–24 structures; `tolerance` 0–8 chunks (default 6, lower = stricter); Mineshaft (type 15) rejected (per-chunk RNG, not region-crackable); `max` default 500 (cap 2000); `start`/`end` bound the seed range (default the full 32-bit space). Only types 1–11, 13, 14, 23, 24 are crackable.
- **Local-only**: this route exists only when running the local server (`server/start.bat` or `server/start.sh`). On the hosted Vercel deployment it is **not** exposed as an API — the reverse seed search ships **only** via the WASM build (see "SeedCrackerX WASM"), because running it server-side would be too expensive on the Vercel runtime.
- **Engine notes**: Win32 threads on Windows (DLL stays self-contained under MinGW, no pthread link), pthreads on Linux (CMake `find_package(Threads)`). An AVX2 path (`crack_mt4_block`) scores 4 seeds per call, runtime-dispatched via `__builtin_cpu_supports("avx2")` with a scalar fallback. Targets are sorted fewest-candidate-regions-first (strongest filter early). Validation errors are returned as `{"error": ...}` JSON.
- **64-bit mode** (`core/crack64.c`, exported via `seedfinder_crack64_shim` in `seedfinder_wrapper.c`): cracks the full Bedrock seed (< 2^63) in a 4-stage pipeline — (1) Java-style anchors (Trail Ruins 23 / Trial Chambers 24) via a fast-path LCG whose parity with cubiomes `getStructurePos` is pinned by `core/test_crack64.c` (test = truth source), (2) a **bounded** 2^48 residual sweep (`sweep48MT`; span ≥ 2^48 = full sweep, ~hours/core), (3) MT cross against the low-32-bit placements of regular Bedrock structures, (4) 2^16 high-bit **biome lift** through the shared `structureIsViable` gate. Thread helpers (`crackThreadCreate/Join`, `nowms_s`) live in `core/platform_threads.h`; the Emscripten branch of `sweep48MT` forces 1 inline thread like the 32-bit engine. crack64 emits matches in **blocks** (the 32-bit crack emits chunks); each candidate also carries `seed_str` because 64-bit seeds exceed JS safe integers. Empirical grounding: `docs/verification/bedrock64-empirical.md`.
- **Breaking change (clients)**: `/seedcracker` **auto-detects 64-bit mode when `end` > 2^32** (or `"mode": "64"` explicit, POST JSON only; GET stays 32-bit). 32-bit clients that passed an upper bound beyond 2^32 now hit the 64-bit engine (or a 503 on a stale lib) instead of a wrapped 32-bit sweep.

## SeedCrackerX WASM (browser build)

`wasm/CMakeLists.txt` compiles `core/seedfinder_wrapper.c` + `core/crack64.c` + `ChunkBiomesGUI/Bfinders.c` + cubiomes sources **directly** (no DLL link) with Emscripten into `wasm/out/seedcracker.{wasm,js}` (ES6 modularized glue). Design docs: `docs/superpowers/{specs,plans}/2026-09-05-wasm-seedcracker*.md`; **`wasm/INTEGRATION.md` is the self-contained reference** for consuming the module (ABI table, worker scheduler, memory discipline) — read it before touching the wasm side. Both `wasm/` and `docs/` are gitignored.

- **Exports only** `_seedfinder_crack`, `_seedfinder_crack64_shim`, `_seedfinder_free_result`, `_malloc`, `_free` — no `seedfinder_scan`. The WASM target is SeedCrackerX-only (32- and 64-bit).
- **Emscripten-safety constraint on committed code**: `seedfinder_crack` has an `#if defined(__EMSCRIPTEN__)` path (line ~619 of `seedfinder_wrapper.c`) that forces `numThreads = 1` and runs the worker **inline** — the same branch exists in `sweep48MT` (`core/crack64.c`); the WASM build has no `-pthread`; JS parallelizes via Web Workers per seed-range slice (reference pattern in `wasm/INTEGRATION.md` §6). Keep this path compiling when refactoring the engine.
- **ABI gotchas**: `WASM_BIGINT` → `startSeed`/`endSeed` are **BigInt** in JS; input coordinates are **blocks**, not chunks; `_seedfinder_free_result(ptr)` is mandatory on **every** return (error strings are `strdup`'d too); the call is synchronous/blocking — never on the main thread, use the `timeBudgetSec` budget for cooperation.
- **AVX2 is off automatically** (`SEEDFINDER_SIMD` requires `__x86_64__`); scalar WASM ≈ 900k seeds/s per core (2–4× slower than native AVX2 per core).
- **Known toolchain gotchas** (see comments in `wasm/CMakeLists.txt`): links at `-O1` because the MSYS2 `wasm-opt`/binaryen pass wedges at 0 CPU on Windows when spawned by emcc (LLVM `-O3` does the real optimization); and `ChunkBiomesGUI/Brng.h` RNG functions (`_mNext`, `mNextInt`, …) must **not** carry `PURE_FUNC` — they mutate the MersenneTwister, and clang CSEs repeated same-pointer calls under optimization, making the RNG repeat values (the "wasm divergence" bug fixed in commit `4165bcb`). Don't re-add the attribute.

## Server entrypoint

Flask server code lives in **one** tree — `server/app/` (package) + `server/index.py` (WSGI shim). The same code runs in every environment:

| Entry | When used | Native lib |
|---|---|---|
| `server/index.py` + `server/app/` | Local dev via `server/start.bat` / `server/start.sh`, and `SeedFinder.exe` via `server/build_exe.py` | Auto-detected by `app/native.py` (`_candidates()`), first existing wins: `sys._MEIPASS` (frozen exe), then `server/build_server/seedfinder_lib.{dll,so}` and `server/build_server/libseedfinder_lib.so`. **Gotcha:** the auto-detect paths point at `server/build_server/`, which `start.bat` never fills — it builds the repo-root `build_server/` and `index.py` loads that via the explicit `--lib-path` flag. So `bootstrap_lib()` at import typically finds nothing on a dev machine; the dev flow relies on `index.py`'s explicit load. The test scripts also force the DLL path explicitly. |

The app package bootstraps ctypes at **import time** (`app/__init__.py` → `native.bootstrap_lib()`) — this matters for the frozen `SeedFinder.exe`, where the lib must be discoverable via `_MEIPASS`.

## Three build trees

There are **three separate CMake projects** — they do not share build output:

| Tree | Root | Output | Purpose |
|---|---|---|---|
| `core/CMakeLists.txt` | invokable via `server/start.bat` (Windows) or `server/start.sh` (Linux) | `build_server/seedfinder_lib.dll` (Win) or `seedfinder_lib.so` (Linux) | The native lib the Python server loads. Note **Linux filename is `seedfinder_lib.so` without `lib` prefix** — `core/CMakeLists.txt` sets `PREFIX ""`. Links `seedfinder_wrapper.c`. |
| `CMakeLists.txt` (root) | standalone, experimental | `seedfinder_bridge.lib` / `seedfinder_bridge.dll` | For linking `SeedFinderBridge.cpp` into a Flarial DLL. **Not** used by the current Lua networking path. |
| `wasm/CMakeLists.txt` | `wasm/build_wasm.bat` / `.sh` (Emscripten, **gitignored** — local only) | `wasm/out/seedcracker.wasm` + `seedcracker.js` | Browser build of the SeedCrackerX engine — see "SeedCrackerX WASM" below. |

When editing the engine, you usually only need `core/seedfinder_wrapper.c`. The root `CMakeLists.txt`, `core/SeedFinderBridge.cpp/.h`, and `core/test_bridge.c` are an alternate in-process path that isn't wired to anything in `script/`.

## Common commands

### Build & run (Windows — primary dev env)

```bat
:: Build DLL + start server (dev). Expects MSYS2 UCRT64 on PATH (start.bat prepends it).
server\start.bat

:: Freeze server + DLL into SeedFinder.exe for distribution
python server\build_exe.py
:: Then: server\dist\SeedFinder.exe [--port 8080 --host 0.0.0.0]
```

`server/start.bat` does, in order: prepends MSYS2 **UCRT64** (`C:\msys64\ucrt64\bin`) to PATH — the toolchain **must** be UCRT64, not the legacy 32-bit MinGW.org gcc, whose DLL a 64-bit Python refuses with WinError 193. It then runs `cmake -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -S ../core -B .`, `mingw32-make`, then launches `python server\index.py --lib-path build_server\seedfinder_lib.dll`.

`start.bat` wipes `build_server/` and rebuilds from scratch if the cached CMake build was produced by a different compiler (it greps `CMakeCache.txt` for `ucrt64`/`mingw64`); otherwise it keeps the incremental build.

### Build & run (Linux — dev / alternate local server)

```bash
# user builds inside VirtualBox Kali VM (no WSL): toolchain is gcc 14.2 / cmake 4.3.3
sudo apt install -y build-essential cmake python3 python3-pip
pip install --user flask flask-cors

cd ~/SeedFinder
mkdir -p build_server && cd build_server
cmake -S ../core -B .
make -j$(nproc)
# Output: build_server/seedfinder_lib.so  (note: NO 'lib' prefix)
```

`server/start.sh` automates the above then launches `python server/index.py --lib-path build_server/seedfinder_lib.so`. **CRLF gotcha:** if `start.sh` is rewritten via the Windows-side Edit tool, run `sed -i 's/\r$//' server/start.sh` before executing — otherwise bash hits `^M` and fails on the shebang line. `start.sh` was first shipped with CRLF and broke on first run; user worked around manually.

### Smoke-test the running server

```bash
curl "http://127.0.0.1:7890/status"
curl "http://127.0.0.1:7890/scan?seed=8675309&x=0&z=0&radius=100&max=20&types=5,1,10"
```

### Tests

Two assert-based test scripts live in `server/tests/` (no framework, run directly):
- `python server/tests/test_native_crack.py` — ctypes smoke test against `build_server\seedfinder_lib.dll`; recovers fixture seeds 8675309 and 31415.
- `python server/tests/test_seedcracker.py` — Flask test client covering 400 validation, Mineshaft rejection, and the happy path (seed 8675309) across the object/list/GET/chunks payload forms. It forces the Windows DLL via `native.load_lib(...)` *after* `create_app()`, because the factory re-runs `bootstrap_lib()` which won't auto-find the repo-root build.

`core/test_bridge.c` is a one-shot harness that links the **root** tree's `SeedFinderBridge.cpp` — it is **not** exercised by `start.bat`. To run it manually you need to build the root `CMakeLists.txt` with `-DSEEDFINDER_BRIDGE_SHARED=OFF`, link against a Lua (`C:/msys64/mingw64/include/lua.hpp`), then invoke `./test_bridge.exe`.

Third assert-based test: `node wasm/test_wasm.mjs` (Node ≥ 18) — drives `wasm/out/seedcracker.js` via the real ABI (BigInt, HEAP32/HEAPF64, mandatory frees) and recovers fixture seed 8675309; also includes a native-DLL parity probe. Requires building the WASM first (`wasm/build_wasm.bat` / `.sh`).

### Stale: `.github/workflows/build-lib.yml`

The `build-lib` GitHub Actions workflow (manual trigger) is **out of date**. It copies `build_server/seedfinder_lib.so` to `server/seedfinder_lib.so` ("where Vercel will pick it up") and smoke-tests routes that no longer exist (`/seedfinder`, `/seedfinder/documentation`, `/seedcracker/documentation`, `/robots.txt`, `/sitemap.xml`, `/llms.txt`) plus a bare-GET `/seedcracker` (which now returns 400). Running it would fail. Either delete it or update it to the current route set and stop copying to the Vercel path — the Vercel deployment is an external repository (`mineseedfinder`) that consumes the `.so`, not this repo.

## Prerequisites

- **Windows dev**: MSYS2 **UCRT64** toolchain (`gcc`, `g++`, `mingw32-make`) — the 64-bit gcc that `server/start.bat` prepends; Python 3.11+ (64-bit, to load the 64-bit DLL) with `pip install -r server/requirements.txt` (plus `-r server/requirements-build.txt` only to build the exe); Flarial Client (only to test the Lua module).
- **Linux build**: `gcc`, `make`, `cmake`, Python 3 with `flask flask-cors`. Run inside a Debian-family VM — no WSL installed.
- **For the in-process `SeedFinderBridge` path** (root `CMakeLists.txt`): a Lua development install at `C:/msys64/mingw64` and optionally `FLARIAL_SDK_PATH` set. Not used by the main Lua networking path.
- **For the WASM build**: `pacman -S mingw-w64-ucrt-x86_64-emscripten` inside MSYS2 UCRT64 (or the official emsdk — commands are identical); Node ≥ 18 to run `wasm/test_wasm.mjs`.

## Key files

- `core/seedfinder_wrapper.c` — the export surface the server actually calls (`seedfinder_scan`, `seedfinder_free_result`, `seedfinder_status`, `seedfinder_crack`)
- `server/app/native.py` — ctypes signature table + `_candidates()` auto-discovery of the native lib (`.dll`/`.so`); `seedfinder_crack` is bound **only if the symbol exists** (`hasattr`), so a stale lib (pre-SeedCrackerX) degrades instead of killing the app at import
- `server/app/__init__.py` — `create_app()` factory; `native.bootstrap_lib()` at import
- `server/app/api.py` — `/status` (alias `/health`), `/scan` (`_arg()` helper applies defaults and surfaces `missing_or_invalid`)
- `server/app/seedcracker.py` — `/seedcracker` blueprint (API only, `_parse()` for the payload forms); see "SeedCrackerX"
- `server/app/pages.py` — a single `/` landing page with inline HTML: interactive forms for both `/scan` and `/seedcracker` plus copy-paste curl/Python examples; the previous SEO/doc routes were removed with the Vercel deployment
- `server/app/console.py` — ASCII startup banner + ANSI/UTF-8 console setup for `SeedFinder.exe` / `start.bat`
- `server/index.py` — WSGI shim (`app` for the frozen exe + `--lib-path/--port/--host` CLI for local dev; port also via `SEEDFINDER_PORT` env); calls `console.print_banner` on local runs
- `server/build_exe.py` — PyInstaller builder; bundles the DLL plus the `logo/` icon into `server/dist/SeedFinder.exe` (single-file, `--console`). There are no `templates/` or `static/` dirs anymore — deleted with Vercel.
- `script/SeedFinder.lua` — `STRUCTURE_TYPES` map (string ↔ int ID), `STRUCTURE_ICONS`, ImGui rendering (local HTTP Bridge Edition; see "Architecture")
- `script/WHSeedFinder.lua` — same module, Hosted API Edition (points at `https://mineseedfinder.vercel.app`, the external `mineseedfinder` repo)
- `ChunkBiomesGUI/cubiomes/` — vendored cubiomes source; `ChunkBiomesGUI/Bfinders.c` — Bedrock-specific structure overrides, compiled in statically; `ChunkBiomesGUI/Brng.h` — Bedrock RNG (see the PURE_FUNC gotcha under "SeedCrackerX WASM")
- `wasm/{CMakeLists.txt,build_wasm.bat,.sh,test_wasm.mjs,INTEGRATION.md}` — the browser SeedCrackerX build (gitignored tree); `wasm/INTEGRATION.md` is its reference doc
- `build_server/seedfinder_lib.dll` — the build artifact `/scan` loads, produced by `server/start.bat` into `build_server/` (gitignored)

## Conventions worth knowing

- **Structure type IDs** are integers defined by cubiomes; the Lua modules keep the canonical human-readable list in `STRUCTURE_TYPES`. If you add a structure type, update **all three** places — `STRUCTURE_TYPES` in `SeedFinder.lua` **and** `WHSeedFinder.lua`, and verify cubiomes recognises the ID via `getBedrockStructureConfig`. The `/scan` endpoint's `types=` param is a comma-separated list of these IDs. SeedCrackerX keeps its own crackable-type list (`STRUCTURE_NAMES` in `server/app/seedcracker.py`) — a scan-only type does not automatically become crackable.
- Native-lib loading is best-effort and never raises during import. CLI runs (`index.py main` → `native.load_lib`) `sys.exit(1)` when the lib is missing or fails to load, so `server/start.bat` exits early. If the lib never loads, `/scan` returns 503 and `/status` returns 500. Always rebuild with `server/start.bat` rather than running `index.py` alone after a clean.
- `seedfinder_scan` returns a pointer that the caller **must free via `seedfinder_free_result`**. Servers do this in a `finally` block — keep that ordering if refactoring.
- **Input clamps on `/scan`**: `radius = min(radius, 1000)` and `max_results = min(max_results, 1000)` to prevent DoS via large iter regions. Keep these limits in place across all entrypoints.
- **CLAUDE.md itself is listed in `.gitignore`**. If you intend to commit it, that line needs to go.
