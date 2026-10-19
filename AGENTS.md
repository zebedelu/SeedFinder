# AGENTS.md

This file guides AI coding agents in this repository.

## What this project is

SeedFinder finds Minecraft **Bedrock** and **Java** structures (villages, monuments, ancient cities, …) given a world seed + player position. It drives [cubiomes](https://github.com/Cubitect/cubiomes) directly — Bedrock via the `Bfinders` overrides, Java via cubiomes' own `getStructurePos`. It ships as (a) a REST API and (b) a Flarial Client overlay script.

This repo builds the native libraries (`.dll` / `.so`). The `.so` is consumed by the sibling **private** repo `mineseedfinder` (site `https://www.mineseedfinder.com`). `README.md` is the user-facing + API reference — point API questions there instead of re-deriving shapes from code.

## Architecture

```
script/SeedFinder.lua  --HTTP-->  server/app/ (Flask, :7890)  --ctypes-->  core/ -> seedfinder_lib.{dll,so}  -->  cubiomes + Bfinders
```

- `script/SeedFinder.lua` — the only Flarial Lua module. `STRUCTURE_TYPES` (string ↔ int ID) + `STRUCTURE_ICONS`. Empty **Server URL** field ⇒ hosted API (`https://www.mineseedfinder.com`); otherwise the typed URL (e.g. local `http://127.0.0.1:7890`). Install path in `script/INSTALL.txt`.
- `server/` — one Flask app package (`server/app/`) + `server/index.py` WSGI shim. The same code runs in every deployment.
- `core/` — the C engine (see *Engine*).

## Three build trees (don't mix their outputs)

| Tree | Invoked by | Output | Purpose |
|---|---|---|---|
| `core/CMakeLists.txt` | `server/start.bat` (Win) / `server/start.sh` (Linux) | `build_server/seedfinder_lib.dll` \| `.so` | The lib the Python server loads. **Linux name has no `lib` prefix** (`PREFIX ""`). |
| root `CMakeLists.txt` | standalone, experimental | `seedfinder_bridge.lib` \| `.dll` | In-process Flarial DLL bridge (`core/SeedFinderBridge.cpp`). **Not** used by the Lua networking path; not built by `start.bat`. |
| `wasm/CMakeLists.txt` | `wasm/build_wasm.bat` / `.sh` | `wasm/out/seedfinder.{wasm,js}` | Browser build (scan + SeedCracker). The whole `wasm/` folder is gitignored/untracked — never commit it. |

`core/CMakeLists.txt` forces `CMAKE_BUILD_TYPE=Release` when unset: an empty build type left the DLL at `-O0` (~6× slower crack). Keep it.

## Commands

### Build + run (Windows — primary dev env)

```bat
server\start.bat
```

`start.bat`: prepends MSYS2 **UCRT64** (`C:\msys64\ucrt64\bin`) — the toolchain **must** be UCRT64, not legacy MinGW.org gcc (its DLL makes 64-bit Python fail with WinError 193); runs `cmake -G "MinGW Makefiles" -S ../core -B .` + `mingw32-make` into the **repo-root** `build_server/`; then `python server\index.py --lib-path build_server\seedfinder_lib.dll`. It wipes `build_server/` only if the cached CMake compiler isn't ucrt64/mingw64; otherwise incremental.

### Build + run (Linux)

```bash
cd build_server && cmake -S ../core -B . && make -j$(nproc)   # -> build_server/seedfinder_lib.so
server/start.sh                                                # automates the above + runs index.py --lib-path
```

CRLF gotcha: if `server/start.sh` is ever rewritten via the Windows editor, run `sed -i 's/\r$//' server/start.sh` first or bash fails on `^M`.

### Package the Windows exe

```bat
pip install -r server/requirements.txt -r server/requirements-build.txt
python server\build_exe.py        REM -> server\dist\SeedFinder.exe
```

Optional Authenticode signing when `SEEDFINDER_SIGN_THUMBPRINT` or `SEEDFINDER_SIGN_PFX` (+ `SEEDFINDER_SIGN_PFX_PASSWORD`) is set (needs `signtool.exe`); unsigned builds skip, but a **configured-but-broken** signing run fails the build.

### Smoke-test a running server

```bash
curl "http://127.0.0.1:7890/status"
curl "http://127.0.0.1:7890/scan?seed=8675309&x=0&z=0&radius=100&max=20&types=5,1,10"
```

### WASM

`wasm/build_wasm.bat` (double-click) / `wasm/build_wasm.sh` (MSYS2 bash) auto-detect the UCRT64 emscripten; build incrementally by default; accept `clean`/`test`/`nopause`. Verify with `node wasm/test_wasm.mjs` (Node ≥ 18). `wasm/INTEGRATION.md` is the consuming reference.

### CI

`.github/workflows/build-lib.yml` — **manual-only** (`workflow_dispatch`, click "Run workflow"). Builds the Linux `.so` on `ubuntu-latest`, smoke-tests, uploads the `seedfinder-lib` artifact. Download it and commit the `.so` to the **root of `mineseedfinder`** (Vercel picks it up). `server/tests/` is Windows-only and is *not* run in CI.

## Engine (core/)

Files: `seedfinder_wrapper.c` (export surface + `scan_impl`), `viability.c` (Bedrock biome gate), `crack_mt.c` (shared Bedrock placement MT + SIMD), `crack64.c` (64-bit pipeline), `crack_java.c` (Java 48-bit + MITM dispatch), `crack_mitm.c` (Java MITM), `crack_auto.c` (auto-mode), `crack_simd.h`, `platform_threads.h`, `SeedFinderBridge.cpp/.h` (alternate C++ path).

ctypes-bound exports (`server/app/native.py`): `seedfinder_scan`, `seedfinder_scan_java`, `seedfinder_free_result`, `seedfinder_status`, `seedfinder_crack`, `seedfinder_crack64_shim`, `seedfinder_crack_java`, `seedfinder_biome_grid`, `seedfinder_biome_palette`. WASM additionally exports `_seedfinder_crack_mitm`, `_seedfinder_crack_mitm_window`, `_seedfinder_crack_auto_{sweep,lift48,lift63}`.

- `scan_impl(..., edition, mcLabel)` — one loop, **4 edition dispatch points**: `setupGenerator` mc, structure config (`getStructureConfig` vs `getBedrockStructureConfig`), placer (`getStructurePos` vs `getBedrockStructurePos`), biome gate (`isViableStructurePos` vs `structureIsViable`).
- **Bedrock gate**: `structureIsViable` (wrapper) → own-cell check → `bedrockViableBiome` (`core/viability.c`, per-type table). Do **not** use cubiomes' Java-style `isViableStructurePos` alone as the Bedrock filter — it lets river/swamp/ocean through. (The old Outpost-only special case is gone; the own-cell check is now generalized.) Regression: `server/tests/test_scan_village_groundtruth.py` (seed 6666, radius 100, `types=5`).
- **`scan_impl` does not validate input** — defaults/clamps/`missing_or_invalid` live in Python (`server/app/api.py::_scan`) and must be mirrored by any other caller. `seed` is folded into an unsigned 64-bit value; `radius`/`max` clamp to 1000.
- `seedfinder_free_result` is mandatory on **every** return (the server does it in a `finally`; error strings are `strdup`'d too).

### SeedCracker

> **Regra (vale nos dois repos): a seed COMPLETA é sempre a prioridade.** A saída de um cracker só tem valor se for a **seed completa** — o valor exato que o jogo aceita no campo de seed. Resultado **parcial não é aceitável**: "só os 48 bits baixos" não serve, porque sem os bits altos o usuário não reproduz biomas/terreno (só as estruturas). A **representação** também conta: uma seed negativa impressa em unsigned (`%llu`) é inutilizável — o jogo rejeita a string fora do range signed e a hasheia → outro mundo. Nunca marque um export/caminho de crack como pronto se ele entregar menos que a seed completa na forma que o jogo aceita.

- **32-bit Bedrock sweep** — `seedfinder_crack` (engine in `seedfinder_wrapper.c` + `crack_mt.c`), multi-threaded (Win32 threads on Windows, pthreads elsewhere), inside a time budget.
- **SIMD**: `crack_simd.h` is the single source of truth — AVX2 8-lane on x86, WASM SIMD128 4-lane, scalar fallback; runtime dispatch via `crackSimdDetect()`. `CRACK_GROUPS` is per-ISA (16 on x86 GCC/AVX2, 4 on WASM — higher spills in linear memory). `crack_mt_block` uses a rolling window (`array[0..4]` + `array[397..400]`, not the full 401-word array) so high `GROUPS` fits in cache.
- **64-bit** (`crack64.c`, export `seedfinder_crack64_shim`): 4-stage pipeline. `span < 2^32` → **bounded** (2^48 residual window); `span ≥ 2^32` → **full-range** (lo32 sweep → Java lift 32→48 → hi16 biome lift). Full-range needs *near-exact* Java anchors; sampled + post-lift guards return an actionable error (pass a `start`/`end` window or exact Trial Chambers/Trail Ruins coords) instead of hanging. `MAX_MAX_SECONDS_64 = 2400`. Empirical basis: `docs/verification/bedrock64-empirical.md`.
- **Negative seeds**: the auto-mode `lift63` (`crack_auto.c`) sweeps `hi ∈ [0, 2^16)` (not `2^15`), so it covers the signed half (bit 63) and returns the complete Bedrock seed `-N` — a negative-seed world used to come back empty/partial. The 32-bit HTTP sweep still prints unsigned (`%llu`); the native `crack64` still clamps `endSeed` at `1<<63` (the site never hits it).
- **Anchor block convention** (`anchor48Build`, `core/crack64.c`): the target chunk is `(block >> 4) - 1`, the same space as `javaChunk` (`(getStructurePos - 8) >> 4`). It accepts both the block a player reads (`/tp` = corner + 8) and the raw corner. The old `(block - 8) >> 4` was one chunk off for `/tp` blocks (true seed scored 4 instead of 0, tolerance 0 failed).
- **Java** (`crack_java.c`): 48-bit cracker + dispatch to **MITM** (`crack_mitm.c`, Matthew Bolan `mjtb49` clean-room reimplementation) for `tolerance 0` + uniform non-power-of-2 structures; final confirmation by `javaCheckSeed` (zero false positives). `MAX_MAX_SECONDS_JAVA = 2400`. The tolerant full-range lift was **removed as unsound** — full-range `tolerance > 0` now returns an actionable error.
- **Route** (`server/app/seedcracker.py`): `/seedcracker` + `/seedcracker/java` + `/seedcracker/bedrock`. Response is always a **JSON list** — item 0 is a header (`status` `ok`|`partial`|`error`, `checked`, `elapsed_ms`, `timed_out`, …), then `{seed, score, matches}` per candidate. Constraints: 4–24 structures, Mineshaft (15) rejected, tolerance 0–8 (default 6), `max` 500 (cap 2000), `max_seconds` default 30 (32-bit cap 120). 64-bit **auto-detects when `end > 2^32`** (**POST JSON only**; GET stays 32-bit). Each candidate carries `seed_str` (exact decimal — use it above 2^53). Local-only; the hosted site answers `"unavailable"`.

## Server notes (server/app/)

- `native.bootstrap_lib()` runs at **import time** (`__init__.py`) and is best-effort — never raises; a missing lib degrades (`/scan` → 503, `/status` → 500). `index.py main` → `native.load_lib` → `sys.exit(1)` on failure, so `start.bat` exits early. **Always rebuild with `start.bat`**, don't run `index.py` alone after a clean.
- **Path gotcha**: `_candidates()` auto-discovery looks under `server/build_server/` (and `_MEIPASS` for the frozen exe), but `start.bat` builds the **repo-root** `build_server/` — dev relies on the explicit `--lib-path`.
- `native.py` binds `seedfinder_scan_java` / `seedfinder_crack*` only `if hasattr` — a stale lib degrades (503 only when Java is requested) instead of killing the app at import.
- Routes: `GET|POST /status` (alias `/health`), `GET|POST /scan`, `/scan/java`, `/scan/bedrock`; `GET|POST /seedcracker`, `/seedcracker/java`, `/seedcracker/bedrock`; `GET /` landing page (`pages.py`, inline HTML + copy-paste examples).
- `mc` is the **edition**, not the game version: resolved by first letter (`j…`→java, `b…`→bedrock, absent→bedrock, anything else→400); the fixed `/scan/java` and `/scan/bedrock` paths ignore it. `version` is reserved for a future game-version param that wires into the C `mcLabel` (always `NULL` today) — don't add it without a task.
- `APP_VERSION` in `console.py` must be bumped per release; the release tag `vX.Y.Z` must match. `console.py` prints an update **notice only** — the old auto-updater was removed (AV false-positive signal); don't re-add it.
- The sibling `mineseedfinder` repo has an independent copy of the scan handler with env-configurable caps (`SEEDFINDER_MAX_RADIUS`/`SEEDFINDER_MAX_RESULTS`). The copies are independent — don't "sync" them without a task.

## WASM (browser build)

- `wasm/CMakeLists.txt` compiles the core `.c` files + cubiomes + `Bfinders.c` **directly** (no DLL link) with Emscripten into `wasm/out/seedfinder.{wasm,js}`. The `core/crack_mt.c` entry is mandatory or the link fails on `sweepMtSurvivors`/`crackScoreSimd`.
- Toolchain gotchas (see comments in `wasm/CMakeLists.txt`): the link stays at `-O1` because the MSYS2 `wasm-opt`/binaryen pass wedges on Windows; the `.bat` must use `call emcmake` (it is itself a `.bat`) or `cmake --build` never runs; `-msimd128` and `-sSTACK_SIZE=1048576` are required (`CRACK_GROUPS=4`).
- RNG gotcha: `ChunkBiomesGUI/Brng.h` RNG functions (`_mNext`, `mNextInt`, …) must **not** carry `PURE_FUNC` — they mutate the MersenneTwister and clang CSEs repeated same-pointer calls under optimization, making the RNG repeat values (commit `4165bcb`). Don't re-add the attribute.
- Emscripten path: `seedfinder_crack` and `sweep48MT` have an `#if defined(__EMSCRIPTEN__)` branch forcing `numThreads = 1` inline (the WASM build has no `-pthread`); JS parallelizes via Web Workers. Keep it compiling when refactoring.
- ABI: `WASM_BIGINT` (seeds are **BigInt**), input coords are **blocks** (not chunks), `_seedfinder_free_result(ptr)` is mandatory on every return, and the call is synchronous/blocking — run it in a worker and use `timeBudgetSec`. `wasm/INTEGRATION.md` is the self-contained consuming reference (ABI tables, worker scheduler, guards).

## Tests

### Python (`server/tests/`)

11 assert-based scripts, run **directly** (no pytest/framework), plus `fixtures_crack64.json` (real Chunkbase field data for seed 4294972605). Most hardcode the Windows `build_server\seedfinder_lib.dll`; Flask-client tests force it via `native.load_lib(...)` **after** `create_app()` because the factory re-runs `bootstrap_lib()`.

- `python server/tests/test_native_crack.py` — ctypes smoke of `seedfinder_crack`; recovers fixtures 8675309 / 31415.
- `python server/tests/test_seedcracker.py` — Flask client: 400 gates, Mineshaft rejection, seed 8675309, object/list/GET/chunks forms.
- `python server/tests/test_seedcracker_java.py` — Java routes, `mc` dispatch, mode64-on-java 400, tolerant-lift guard, MITM recovery.
- `test_scan_api.py`, `test_scan64.py`, `test_scan_java_native.py`, `test_scan_village_groundtruth.py` (seed-6666 regression), `test_crack64_api.py`, `test_crack_auto.py`, `test_mitm_window.py`, `test_biome_grid.py`.
- Opt-in (slow) paths: `SEEDFINDER_FULL_RANGE_TEST=1` (full-range 64-bit) and `SEEDFINDER_AUTO_E2E=1` (auto E2E).

### C harnesses (`core/test_*.c`)

**Not built by any CMake target** — each is standalone with a compile-command comment header. Run manually: `test_mitm.c`, `test_mitm_world.c`, `test_crack_java.c`, `test_crack64.c` (parity vs cubiomes; `SEEDFINDER_FULL_RANGE_TEST=1` → `FULL_RANGE_OK`), `test_viability.c`, `test_invariants.c`, `test_java_tol.c`, `test_bridge.c` (needs the root tree + MinGW64 Lua; not run by `start.bat`).

## Ground truth (tabela da verdade)

When the user provides a world seed **and** structure coordinates and confirms the data is real, save it to the ground-truth file — which lives **only in the sibling private repo `mineseedfinder`**, never in this (public) repo.

- **File:** `../mineseedfinder/static/wasm/ground_truth.json` (path always relative to this repo as `../mineseedfinder/...`).
- **Never store truth in this repo.** Do not create a `ground_truth.json` (or any truth table) under `server/tests/` or anywhere here. If the sibling repo isn't present locally, tell the user instead of creating the file here.
- **Confirm first:** ask the user whether the seed/structures are real and where they came from (in-game / ChunkBase / …). Fictitious or example data is **not** saved.
- Data produced by seedfinder/SeedCracker itself is **not** independent truth (not circular). Only save what the user read in-game or from an external source.
- **One entry per seed.** Structures from different seeds get separate entries — never merge across seeds or drop a seed. If the seed exists, append new structures (no duplicate `type,x,z`).
- Coordinates are **blocks**; `type` is the cubiomes structure ID; `edition` is `bedrock` | `java` | `bedrock64`.
- After adding, run the tests that consume the file (create one if none exists that loads `ground_truth.json` and compares it against `/scan`).

```json
{
  "entries": [
    {
      "seed": "8675309",
      "edition": "bedrock",
      "source": "in-game",
      "verified": true,
      "date": "2026-10-13",
      "notes": "",
      "structures": [
        { "type": 5, "name": "village", "x": -168, "z": 56 }
      ]
    }
  ]
}
```

## Conventions worth knowing

- **Structure type IDs** are cubiomes integers. If you add a type, update `STRUCTURE_TYPES` in `script/SeedFinder.lua` and verify cubiomes recognises the ID. SeedCracker keeps its own crackable list (`STRUCTURE_NAMES` in `server/app/seedcracker.py`) — a scan-only type is not automatically crackable.
- `CLAUDE.md` is listed in `.gitignore`; `AGENTS.md` is tracked. Keep the tracked/ignored boundary in mind before staging.
- Do **not** commit anything under `wasm/` (fully untracked) or `docs/` — the one tracked exception is `docs/verification/bedrock64-empirical.md`.
- `build_server/` artifacts are gitignored. The Windows packaged artifact is `server/dist/SeedFinder.exe`.
