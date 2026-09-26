# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

SeedFinder is a Minecraft Bedrock Edition **and Java Edition (latest)** structure finder that runs as a module inside [Flarial Client](https://flarial.xyz). Given a world seed and the player's position, it locates nearby villages, temples, monuments, ancient cities, etc. — without relying on ChunkBase — by driving [cubiomes](https://github.com/Cubitect/cubiomes) directly (Bedrock via `Bfinders` overrides, Java via cubiomes' own `getStructurePos`).

`README.md` is the user-facing reference: full API docs (params, response shapes), the supported-structures list, and real benchmark tables (v1.0.0 and v1.2.0). Point users there for API questions instead of re-deriving from code.

This repository is responsible for compiling the native libraries (`.dll` and `.so`). The `.so` is consumed by an external repository called **`mineseedfinder`**, deployed on Vercel (`https://mineseedfinder.vercel.app`) to distribute the product for free. It exposes a `/scan` endpoint so the seedfinder can be tested; SeedCrackerX is available **only via WASM**, since running it as a server API would be too expensive on the Vercel runtime. The site's `/scan` console is being migrated to the **same WASM module** (scan export, added 2026-09-25) so each Scan stops costing a serverless invocation — the HTTP route stays for API consumers (see "WASM (browser build)").

## Architecture

```
Flarial Lua Script  ──HTTP──>  Flask API (127.0.0.1:7890)  ──ctypes──>  seedfinder_lib.{dll,so}
   script/SeedFinder.lua         server/index.py                          core/seedfinder_wrapper.c
   (ImGui overlay)               (loads DLL/.so, JSON I/O)                (links cubiomes + Bfinders)
```

There is a **single Flarial Lua module** in `script/`:

- `script/SeedFinder.lua` — points at `https://mineseedfinder.vercel.app` (hosted API) when the **Server URL** field is left empty, or at whatever URL the user types (e.g. the local `http://127.0.0.1:7890` server, the packaged Windows path with `SeedFinder.exe`).

1. **C core** (`core/`) — Structure-finding engine. Two parallel implementations:
   - `core/seedfinder_wrapper.c` — plain C entry point. Exports `seedfinder_scan` (Bedrock, unchanged ABI), `seedfinder_scan_java` (Java; extra `const char *mcLabel` arg, always `NULL` today — the future hook for a game-version param, resolved via cubiomes' `str2mc`), plus `seedfinder_free_result`, `seedfinder_status`. Both scans share one internal loop, `scan_impl(..., edition, mcLabel)` (in `core/seedfinder_wrapper.c`), with 4 edition dispatch points: `setupGenerator` mc, structure config, placer (`getBedrockStructurePos` vs `getStructurePos`), biome gate (`structureIsViable` vs `isViableStructurePos`). This is what the Python server loads. It also holds the entire **SeedCrackerX** engine (`seedfinder_crack`) — see "SeedCrackerX" below.
   - `core/SeedFinderBridge.cpp` / `.h` — alternative C++ entry point that registers a `seedfinder_bridge` Lua global inside a Lua state (for an in-process Flarial DLL injection path that is **not** built by the current `start.bat` target — see "Two build trees" below).
   Both share the same algorithm: iterate regions = `floor(playerChunk ± radius) / regionSize`, call the edition's placer (`getBedrockStructurePos` on Bedrock, `getStructurePos` on Java), run the edition's biome gate (`structureIsViable` on Bedrock, `isViableStructurePos` on Java), sort by distance. Floor-div helper is duplicated in both files. `seedfinder_wrapper.c` additionally special-cases **Outpost**: it re-samples the biome at the outpost's own placement cell (offset from the structure position) because cubiomes' upstream Java-style gate lets positions sitting on swamp/river biomes through.

2. **HTTP server** (`server/`) — Flask + flask-cors, loads the native library via `ctypes.CDLL`. Blueprints in `server/app/`: `api.py` (`GET /status` aliased as `GET /health`; scan routes `GET|POST /scan`, `GET|POST /scan/java`, `GET|POST /scan/bedrock` — params `seed/x/z/radius/max/types` plus `mc` on `/scan` only), `seedcracker.py` (the `/seedcracker` reverse-seed-search API), `pages.py` (a **single** `/` landing page with copy-paste API examples — the SEO/doc routes and HTML templates were deleted with the Vercel deployment). Reads the malloc'd JSON in the C side, calls `seedfinder_free_result` to release it.

3. **Flarial Lua module** (`script/SeedFinder.lua`) — Runs inside Flarial Client. Pings `http://127.0.0.1:7890/status` (5 s throttle), then calls `/scan`. Renders an ImGui overlay with structure type toggles and results sorted by distance. Reads the player's current seed/position from the Flarial runtime. Drop into `%LOCALAPPDATA%\Flarial\Client\Scripts\Modules\SeedFinder.lua` to load (see `script/INSTALL.txt`).

## SeedCrackerX (reverse seed search)

The reverse tool: given ≥4 structure coordinates, recover the most probable Bedrock seed(s). Sweeps the 32-bit seed space in native C, multi-threaded, inside a time budget. Lives in `server/app/seedcracker.py` (blueprint) + the crack engine at the bottom of `core/seedfinder_wrapper.c` (`seedfinder_crack`).

- **Route** `/seedcracker` is **API-only** now: POST JSON (object or list-of-items form) or GET with `?structures=5,-280,152;8,696,360`. A bare GET without structures returns a 400 error list — the HTML console and `/seedcracker/documentation` were deleted with the templates.
- **Response is always a JSON *list***, unlike `/scan`'s object: item 0 is the header `{"status": "ok"|"partial"|"error", "checked", "elapsed_ms", "timed_out", ...}`, then `{"seed", "score", "matches": [[x, z], ...]}` per candidate. `partial` means the `max_seconds` budget (default 30, 1–120) expired before the sweep finished.
- **Constraints**: 4–24 structures; `tolerance` 0–8 chunks (default 6, lower = stricter); Mineshaft (type 15) rejected (per-chunk RNG, not region-crackable); `max` default 500 (cap 2000); `start`/`end` bound the seed range (default the full 32-bit space). Only types 1–11, 13, 14, 23, 24 are crackable.
- **Local-only**: this route exists only when running the local server (`server/start.bat` or `server/start.sh`). On the hosted Vercel deployment it is **not** exposed as an API — the reverse seed search ships **only** via the WASM build (see "WASM (browser build)"), because running it server-side would be too expensive on the Vercel runtime.
- **Engine notes**: Win32 threads on Windows (DLL stays self-contained under MinGW, no pthread link), pthreads on Linux (CMake `find_package(Threads)`). A lane-generic SIMD path (`crack_mt_block`, `core/crack_simd.h`) scores **`CRACK_WIDTH` seeds per call** — 32 on AVX2 (8 lanes × `CRACK_GROUPS=4`), 16 on WASM SIMD128 (4 lanes × 4) — runtime-dispatched via `crackSimdDetect()` (`__builtin_cpu_supports("avx2")` natively, compile-time on WASM) with a scalar fallback. Targets are sorted fewest-candidate-regions-first (strongest filter early — 1.9× on the sweep once the AVX2 lanes were saturated). Validation errors are returned as `{"error": ...}` JSON. `core/CMakeLists.txt` forces `CMAKE_BUILD_TYPE=Release` when unset: an empty build type left the DLL at `-O0`, ~6× slower on the crack (2.8M vs 17M seeds/s).
- **64-bit mode** (`core/crack64.c`, exported via `seedfinder_crack64_shim` in `seedfinder_wrapper.c`): cracks the full Bedrock seed (< 2^63) in a 4-stage pipeline — (1) Java-style anchors (Trail Ruins 23 / Trial Chambers 24) via a fast-path LCG whose parity with cubiomes `getStructurePos` is pinned by `core/test_crack64.c` (test = truth source), (2) a **bounded** 2^48 residual sweep (`sweep48MT`; span ≥ 2^48 = full sweep — ≈65k× the 32-bit space, thousands of core-hours per core unparallelized, so callers must pass a narrow window), (3) MT cross against the low-32-bit placements of regular Bedrock structures, (4) 2^16 high-bit **biome lift** through the shared `structureIsViable` gate. Thread helpers (`crackThreadCreate/Join`, `nowms_s`) live in `core/platform_threads.h`; the Emscripten branch of `sweep48MT`/`liftJavaHi` forces 1 inline thread like the 32-bit engine. crack64 emits matches in **blocks** (the 32-bit crack emits chunks); each candidate also carries `seed_str` because 64-bit seeds exceed JS safe integers. Empirical grounding: `docs/verification/bedrock64-empirical.md`.
- **64-bit dispatch by span** (`seedfinder_crack64`): `span = end - start < 2^32` → the bounded path above; `span ≥ 2^32` → **full-range**: sample pre-check on 2^22 seeds + full lo32 sweep (`sweepMtSurvivors`, 2^32 × ~150 µs/seed-8 targets on 6 threads ≈ 5–7 min) + Java lift 32→48 (`liftJavaHi`, parallel over survivors). `types` dispatch, `max_seconds` cap for 64-bit is `MAX_MAX_SECONDS_64 = 2400` (the 32-bit cap stays 120).
- **Full-range is only feasible with NEAR-EXACT Java anchors** — this is the key empirical finding (`build_server/probe_tol.c`, 6 threads, i5-class laptop). Stage 4 pays `#s48 × 2^16 × ~150 µs` (`applySeed` + biome gate) to resolve the top 16 bits, so the Java lift must yield very few 48-bit candidates: tol 0 → ~1 s48 (seconds); tol 2 → 2 176 s48 → ~1 h; tol 3 → ~9 h; tol 6 → ~95 M s48 → **years**. Two guards (sampled Java yield before the full lift, plus a post-lift estimate) turn that into an instant actionable `{"error": "... pass a start/end window ... or exact Trial Chambers/Trail Ruins coordinates"}` instead of a hang. Approximate Chunkbase coordinates therefore require a window; full-range recovery with exact anchors is proven by `core/test_crack64.c` (`SEEDFINDER_FULL_RANGE_TEST=1` → `FULL_RANGE_OK`), and the guard itself by `server/tests/test_crack64_api.py::test_full_range_guard`.
- **`mtFilterBits` was deleted**: the analytical bits estimate (sum of `2·log2(regionSize)`) over-predicted the filter (said 40 bits for a 4-MT set that really left ~2.7M lo32 survivors) and has been replaced by a **sampled** pre-check (sweep 2^22 seeds ≈ 1 s, extrapolate survivors ×1024, reject above the exact `2^20` post-check gate).
- **Breaking change (clients)**: `/seedcracker` **auto-detects 64-bit mode when `end` > 2^32** (or `"mode": "64"` explicit, POST JSON only; GET stays 32-bit). 32-bit clients that passed an upper bound beyond 2^32 now hit the 64-bit engine (or a 503 on a stale lib) instead of a wrapped 32-bit sweep.

## WASM (browser build: scan + SeedCrackerX)

`wasm/CMakeLists.txt` compiles `core/seedfinder_wrapper.c` + `core/crack64.c` + `core/crack_mt.c` + `ChunkBiomesGUI/Bfinders.c` + cubiomes sources **directly** (no DLL link) with Emscripten into `wasm/out/seedfinder.{wasm,js}` (ES6 modularized glue). The `core/crack_mt.c` entry is **mandatory** (added 2026-09-17): without it the link fails on `sweepMtSurvivors`/`crackScoreSimd`. Design docs: `docs/superpowers/{specs,plans}/2026-09-05-wasm-seedcracker*.md`; **`wasm/INTEGRATION.md` is the self-contained reference** for consuming the module (ABI table incl. the scan, JS validation mirroring the site's `/scan` route, worker scheduler, memory discipline, site migration checklist) — read it before touching the wasm side. `wasm/` and `docs/` are gitignored, **but the five wasm source files (`CMakeLists.txt`, `build_wasm.bat/.sh`, `test_wasm.mjs`, `INTEGRATION.md`) are force-tracked exceptions** (added 2026-09-25, same trick as `docs/verification/bedrock64-empirical.md`) — `wasm/build/` and `wasm/out/` stay ignored; a new file under `wasm/` still needs `git add -f`. Build with one click: `wasm/build_wasm.bat` (Windows, double-click) / `wasm/build_wasm.sh` (MSYS2 bash) - both auto-detect the MSYS2 UCRT64 Emscripten (`C:\msys64\ucrt64\lib\emscripten`, which is where `emcc.bat`/`emcmake.bat` live; the `.exe` variants are `.broken`; plus `ucrt64\bin`), build incrementally by default, and accept `clean`/`test`/`nopause` flags. Editor heads-up: the `.bat` must use `call emcmake` (emcmake is itself a `.bat`) - without `call` the script ends there and `cmake --build` never runs, which was the old "silent no-op".

- **Exports** `_seedfinder_crack`, `_seedfinder_crack64_shim`, **`_seedfinder_scan`**, **`_seedfinder_scan_java`** (added 2026-09-25 — the module now serves the site's `/scan` console in-browser too, so each Scan costs the Vercel serverless nothing), `_seedfinder_free_result`, `_malloc`, `_free`. Output was renamed `seedcracker.*` → **`seedfinder.*`** (both build scripts delete a stale `out/seedcracker.*` pair); the site migration (copy + worker import + `/scan` fallback) is INTEGRATION.md §10.2.
- **Scan ABI gotchas**: the C `scan_impl` **does not validate input** (unlike crack, which returns `{"error":...}`) — defaults/clamps/`missing_or_invalid` live in the caller and must mirror the site's `app/api.py::_scan` (INTEGRATION.md §10.4); `seed` is **BigInt** (`& 0xFFFFFFFFFFFFFFFFn`, accepting negative seeds like Python); no `timeBudgetSec` — worst case measured ~370 ms (radius 200, 11 types) and it still must run in a worker; `mcLabel` is passed as `0` (NULL) today.
- **Emscripten-safety constraint on committed code**: `seedfinder_crack` has an `#if defined(__EMSCRIPTEN__)` path (line ~619 of `seedfinder_wrapper.c`) that forces `numThreads = 1` and runs the worker **inline** — the same branch exists in `sweep48MT` (`core/crack64.c`); the WASM build has no `-pthread`; JS parallelizes via Web Workers per seed-range slice (reference pattern in `wasm/INTEGRATION.md` §6). Keep this path compiling when refactoring the engine.
- **ABI gotchas**: `WASM_BIGINT` → `startSeed`/`endSeed` are **BigInt** in JS; input coordinates are **blocks**, not chunks; `_seedfinder_free_result(ptr)` is mandatory on **every** return (error strings are `strdup`'d too); the call is synchronous/blocking — never on the main thread, use the `timeBudgetSec` budget for cooperation.
- **WASM SIMD128**: the WASM build uses `-msimd128` (compile + link) to enable the vectorized `crackScoreSimd` path. `core/crack_simd.h` is the single source of truth: it maps a small `CV_*` macro shim to **AVX2 8-lane** on x86 and **WASM SIMD128 4-lane** on wasm, and defines `CRACK_SIMD`/`CRACK_LANES`/`CRACK_GROUPS`/`CRACK_WIDTH`. Without the flag `__wasm_simd128__` doesn't exist and the engine falls back to scalar (~2× slower: measured 1.046M → 2.06M checked/s, `node wasm/test_wasm.mjs`). **`wasm/CMakeLists.txt` is tracked** (force-added despite the `wasm/` ignore) — `-msimd128` (and `-sSTACK_SIZE=1048576`, required by `CRACK_GROUPS=4`) ship with it, so a fresh clone builds WASM without re-adding flags; the mandatory `core/crack_mt.c` source line is in the same file. The link stays at `-O1` (binaryen workaround).
- **`CRACK_GROUPS=4`** (default in `core/crack_simd.h`): `crack_mt_block` runs 4 independent vector groups in the same MT-init loop, hiding `i32x4.mul` latency. `CRACK_WIDTH = CRACK_LANES × CRACK_GROUPS` (32 on native AVX2, 16 on WASM). Measured ~5% over `2` on WASM and ~1.6× on the native 16M-seed smoke (0.5s → 0.3s); raising it grows the per-call stack (`v[CRACK_GROUPS][401]`), hence the 1 MB WASM stack.
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
| `wasm/CMakeLists.txt` | `wasm/build_wasm.bat` / `.sh` (Emscripten; sources tracked, `wasm/` otherwise **gitignored**) | `wasm/out/seedfinder.wasm` + `seedfinder.js` | Browser build of the scan + SeedCrackerX engines — see "WASM (browser build)" below. |

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

Third assert-based test: `node wasm/test_wasm.mjs` (Node ≥ 18) — drives `wasm/out/seedfinder.js` via the real ABI (BigInt, HEAP32/HEAPF64, mandatory frees) and recovers fixture seed 8675309; includes a native-DLL parity probe, the scan ground-truth regression (seed 6666 / 8 villages, mirroring `server/tests/test_scan_village_groundtruth.py`), Bedrock+Java scan parity probes, and the crack64 smoke. Requires building the WASM first (`wasm/build_wasm.bat` / `.sh`).

### Stale: `.github/workflows/build-lib.yml`

The `build-lib` GitHub Actions workflow (manual trigger) is **out of date**. It copies `build_server/seedfinder_lib.so` to `server/seedfinder_lib.so` ("where Vercel will pick it up") and smoke-tests routes that no longer exist (`/seedfinder`, `/seedfinder/documentation`, `/seedcracker/documentation`, `/robots.txt`, `/sitemap.xml`, `/llms.txt`) plus a bare-GET `/seedcracker` (which now returns 400). Running it would fail. Either delete it or update it to the current route set and stop copying to the Vercel path — the Vercel deployment is an external repository (`mineseedfinder`) that consumes the `.so`, not this repo.

## Prerequisites

- **Windows dev**: MSYS2 **UCRT64** toolchain (`gcc`, `g++`, `mingw32-make`) — the 64-bit gcc that `server/start.bat` prepends; Python 3.11+ (64-bit, to load the 64-bit DLL) with `pip install -r server/requirements.txt` (plus `-r server/requirements-build.txt` only to build the exe); Flarial Client (only to test the Lua module).
- **Linux build**: `gcc`, `make`, `cmake`, Python 3 with `flask flask-cors`. Run inside a Debian-family VM — no WSL installed.
- **For the in-process `SeedFinderBridge` path** (root `CMakeLists.txt`): a Lua development install at `C:/msys64/mingw64` and optionally `FLARIAL_SDK_PATH` set. Not used by the main Lua networking path.
- **For the WASM build**: `pacman -S mingw-w64-ucrt-x86_64-emscripten` inside MSYS2 UCRT64 (or the official emsdk — commands are identical); Node ≥ 18 to run `wasm/test_wasm.mjs`.

## Key files

- `core/seedfinder_wrapper.c` — the export surface the server actually calls (`seedfinder_scan`, `seedfinder_scan_java`, `seedfinder_free_result`, `seedfinder_status`, `seedfinder_crack`)
- `server/app/native.py` — ctypes signature table + `_candidates()` auto-discovery of the native lib (`.dll`/`.so`); `seedfinder_crack` is bound **only if the symbol exists** (`hasattr`), so a stale lib (pre-SeedCrackerX) degrades instead of killing the app at import
- `server/app/__init__.py` — `create_app()` factory; `native.bootstrap_lib()` at import
- `server/app/api.py` — `/status` (alias `/health`), the three scan routes (`/scan`, `/scan/java`, `/scan/bedrock`, GET+POST); `_scan(fixed_edition)` is the shared handler
- `server/app/seedcracker.py` — `/seedcracker` blueprint (API only, `_parse()` for the payload forms); see "SeedCrackerX"
- `server/app/pages.py` — a single `/` landing page with inline HTML: interactive forms for both `/scan` and `/seedcracker` plus copy-paste curl/Python examples; the previous SEO/doc routes were removed with the Vercel deployment
- `server/app/console.py` — ASCII startup banner + ANSI/UTF-8 console setup for `SeedFinder.exe` / `start.bat`
- `server/index.py` — WSGI shim (`app` for the frozen exe + `--lib-path/--port/--host` CLI for local dev; port also via `SEEDFINDER_PORT` env); calls `console.print_banner` on local runs
- `server/build_exe.py` — PyInstaller builder; bundles the DLL plus the `logo/` icon into `server/dist/SeedFinder.exe` (single-file, `--console`). There are no `templates/` or `static/` dirs anymore — deleted with Vercel.
- `script/SeedFinder.lua` — `STRUCTURE_TYPES` map (string ↔ int ID), `STRUCTURE_ICONS`, ImGui rendering; empty Server URL = hosted API, otherwise the typed URL (see "Architecture")
- `ChunkBiomesGUI/cubiomes/` — vendored cubiomes source; `ChunkBiomesGUI/Bfinders.c` — Bedrock-specific structure overrides, compiled in statically; `ChunkBiomesGUI/Brng.h` — Bedrock RNG (see the PURE_FUNC gotcha under "WASM (browser build)")
- `wasm/{CMakeLists.txt,build_wasm.bat,.sh,test_wasm.mjs,INTEGRATION.md}` — the browser build of scan + SeedCrackerX (sources force-tracked; `build/`+`out/` ignored); `wasm/INTEGRATION.md` is its reference doc (incl. the site-side integration checklist)
- `build_server/seedfinder_lib.dll` — the build artifact `/scan` loads, produced by `server/start.bat` into `build_server/` (gitignored)

## Conventions worth knowing

- **Edition dispatch**: `/scan/java` and `/scan/bedrock` fix the edition and **silently ignore** `mc`; `/scan` resolves `mc` by its **first letter** (`j…` → java, `b…` → bedrock, case-insensitive; anything else → 400; absent → `bedrock`). GET reads the query string, POST reads only the JSON body (same keys; non-object body → 400). The response shape is identical across editions. `native.py` binds `seedfinder_scan_java` only `if hasattr` so a stale lib degrades to a 503 **only** when Java is requested. The param is named `mc` (edition) and **not** `version`, so a future game-version param can use `version` without a breaking rename; the `mcLabel` C argument is always `NULL` from the server — that future param wires into it (`str2mc`), so do not add it without a new task.
- **Structure type IDs** are integers defined by cubiomes; the Lua module keeps the canonical human-readable list in `STRUCTURE_TYPES`. If you add a structure type, update `STRUCTURE_TYPES` in `SeedFinder.lua` and verify cubiomes recognises the ID via `getBedrockStructureConfig`. The `/scan` endpoint's `types=` param is a comma-separated list of these IDs. SeedCrackerX keeps its own crackable-type list (`STRUCTURE_NAMES` in `server/app/seedcracker.py`) — a scan-only type does not automatically become crackable.
- Native-lib loading is best-effort and never raises during import. CLI runs (`index.py main` → `native.load_lib`) `sys.exit(1)` when the lib is missing or fails to load, so `server/start.bat` exits early. If the lib never loads, `/scan` returns 503 and `/status` returns 500. Always rebuild with `server/start.bat` rather than running `index.py` alone after a clean.
- `seedfinder_scan` returns a pointer that the caller **must free via `seedfinder_free_result`**. Servers do this in a `finally` block — keep that ordering if refactoring.
- **Input clamps on `/scan`**: `radius = min(radius, 1000)` and `max_results = min(max_results, 1000)` to prevent DoS via large iter regions. Keep these limits in place across all entrypoints.
- **Gate de bioma Bedrock** (`structureIsViable` em `core/seedfinder_wrapper.c` + `bedrockViableBiome` em `core/viability.c`): a viabilidade é checada na célula da própria estrutura (`getBiomeAt(g,0,(x>>4)*4+2,319>>2,(z>>4)*4+2)`), com tabela por tipo. NÃO use o gate `isViableStructurePos` do cubiomes como filtro único — ele é Java-style (amostra canto de bounding box via LCG Java) e deixa passar rio/pântano/oceano. O antigo hack só-Outpost foi generalizado. Regressão: `seed=6666`, radius 100, `types=5` (`server/tests/test_scan_village_groundtruth.py`).
- **CLAUDE.md itself is listed in `.gitignore`**. If you intend to commit it, that line needs to go.
