// wasm/test_wasm.mjs — assert-based smoke test for seedfinder.wasm.
// Run: node wasm/test_wasm.mjs   (Node >= 18, BigInt native)
import assert from "node:assert/strict";
import { existsSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import createModule from "./out/seedfinder.js";

const HERE = dirname(fileURLToPath(import.meta.url));
const SEED = 8675309; // same fixture as server/tests/test_native_crack.py
const FIXTURE = [
  { type: 5, x: -280, z: 152 }, { type: 5, x: -280, z: -360 },
  { type: 8, x: 696, z: 360 }, { type: 8, x: 712, z: 760 },
];

// Writes structures into the WASM heap, calls seedfinder_crack, parses the
// JSON and ALWAYS frees the returned pointer (error strings are strdup'd too).
function callCrack(m, structures, tolerance, startSeed, endSeed,
                   maxResults = 500, budget = 30.0) {
  const n = structures.length;
  const pT = m._malloc(n * 4), pX = m._malloc(n * 8), pZ = m._malloc(n * 8);
  for (let i = 0; i < n; i++) {
    m.HEAP32[(pT >> 2) + i] = structures[i].type;
    m.HEAPF64[(pX >> 3) + i] = structures[i].x;
    m.HEAPF64[(pZ >> 3) + i] = structures[i].z;
  }
  let ptr = 0;
  try {
    // uint64 params arrive as BigInt (WASM_BIGINT default); numThreads=1.
    ptr = m._seedfinder_crack(pT, n, pX, pZ, tolerance,
      BigInt(startSeed), BigInt(endSeed), maxResults, budget, 1);
    return JSON.parse(m.UTF8ToString(ptr));
  } finally {
    if (ptr) m._seedfinder_free_result(ptr);
    m._free(pT); m._free(pX); m._free(pZ);
  }
}

// Parity probe: run the same crack in the native DLL via a dependency-free
// ctypes one-shot (Node cannot load the DLL without an FFI package).
function nativeParity(structures, tolerance, startSeed, endSeed) {
  const dll = ["build_server/seedfinder_lib.dll",
               "build_server/seedfinder_lib.so"].map(p => join(HERE, "..", p))
                                              .find(existsSync);
  if (!dll) return null;
  const py = `
import ctypes, json, sys
p = json.loads(sys.argv[1])
lib = ctypes.CDLL(r"""${dll.replace(/"/g, "")}""")
lib.seedfinder_crack.restype = ctypes.c_void_p
lib.seedfinder_crack.argtypes = [
    ctypes.POINTER(ctypes.c_int), ctypes.c_int,
    ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double),
    ctypes.c_int, ctypes.c_uint64, ctypes.c_uint64,
    ctypes.c_int, ctypes.c_double, ctypes.c_int]
lib.seedfinder_free_result.argtypes = [ctypes.c_void_p]
n = len(p["structures"])
T = (ctypes.c_int * n)(*[s["type"] for s in p["structures"]])
X = (ctypes.c_double * n)(*[s["x"] for s in p["structures"]])
Z = (ctypes.c_double * n)(*[s["z"] for s in p["structures"]])
ptr = lib.seedfinder_crack(T, n, X, Z, p["tolerance"],
                           p["start"], p["end"], 500, 30.0, 1)
try:
    print(ctypes.string_at(ptr).decode("utf-8"))
finally:
    lib.seedfinder_free_result(ptr)
`;
  const out = execFileSync("python", ["-c", py, JSON.stringify({
    structures, tolerance, start: startSeed, end: endSeed,
  })], { encoding: "utf8", cwd: join(HERE, "..") });
  return JSON.parse(out);
}

// Same discipline for the 64-bit shim: two anchor arrays in, 14 args,
// BigInt seeds, EVERY pointer freed (crack64 emits matches in BLOCKS).
function callCrack64(m, mt, java, tolerance, startSeed, endSeed,
                     maxResults = 500, budget = 120.0) {
  const n = mt.length, j = java.length;
  const pT = m._malloc(n * 4), pX = m._malloc(n * 8), pZ = m._malloc(n * 8);
  const pjT = m._malloc(j * 4), pjX = m._malloc(j * 8), pjZ = m._malloc(j * 8);
  for (let i = 0; i < n; i++) {
    m.HEAP32[(pT >> 2) + i] = mt[i].type;
    m.HEAPF64[(pX >> 3) + i] = mt[i].x;
    m.HEAPF64[(pZ >> 3) + i] = mt[i].z;
  }
  for (let i = 0; i < j; i++) {
    m.HEAP32[(pjT >> 2) + i] = java[i].type;
    m.HEAPF64[(pjX >> 3) + i] = java[i].x;
    m.HEAPF64[(pjZ >> 3) + i] = java[i].z;
  }
  let ptr = 0;
  try {
    ptr = m._seedfinder_crack64_shim(pT, pX, pZ, n, pjT, pjX, pjZ, j,
      tolerance, startSeed, endSeed, maxResults, budget, 1);
    return JSON.parse(m.UTF8ToString(ptr));
  } finally {
    if (ptr) m._seedfinder_free_result(ptr);
    m._free(pT); m._free(pX); m._free(pZ);
    m._free(pjT); m._free(pjX); m._free(pjZ);
  }
}

// Scan discipline: one Int32Array of type ids on the heap, uint64 seed as
// BigInt (WASM_BIGINT), JSON pointer ALWAYS freed (success and error alike).
function callScan(m, seed, x, z, radius, maxResults, types, java = false) {
  const n = types.length;
  const pT = m._malloc(n * 4);
  for (let i = 0; i < n; i++) m.HEAP32[(pT >> 2) + i] = types[i];
  let ptr = 0;
  try {
    ptr = java
      ? m._seedfinder_scan_java(BigInt(seed), x, z, radius, maxResults, pT, n, 0)
      : m._seedfinder_scan(BigInt(seed), x, z, radius, maxResults, pT, n);
    return JSON.parse(m.UTF8ToString(ptr));
  } finally {
    if (ptr) m._seedfinder_free_result(ptr);
    m._free(pT);
  }
}

// Parity probe for scan (Bedrock or Java): same params through the native
// DLL, dependency-free ctypes one-shot (same trick as nativeParity above).
function nativeScanParity(seed, x, z, radius, maxResults, types, java = false) {
  const dll = ["build_server/seedfinder_lib.dll",
               "build_server/seedfinder_lib.so"].map(p => join(HERE, "..", p))
                                               .find(existsSync);
  if (!dll) return null;
  const fn = java ? "seedfinder_scan_java" : "seedfinder_scan";
  const py = `
import ctypes, json, sys
p = json.loads(sys.argv[1])
lib = ctypes.CDLL(r"""${dll.replace(/"/g, "")}""")
lib.seedfinder_free_result.argtypes = [ctypes.c_void_p]
f = lib.${fn}
f.restype = ctypes.c_void_p
args = [ctypes.c_uint64(p["seed"]), ctypes.c_double(p["x"]),
        ctypes.c_double(p["z"]), ctypes.c_int(p["radius"]),
        ctypes.c_int(p["max"])]
T = (ctypes.c_int * len(p["types"]))(*p["types"])
args += [T, ctypes.c_int(len(p["types"]))]
if p["java"]:
    args.append(ctypes.c_char_p(0))
ptr = f(*args)
try:
    print(ctypes.string_at(ptr).decode("utf-8"))
finally:
    lib.seedfinder_free_result(ptr)
`;
  const out = execFileSync("python", ["-c", py, JSON.stringify({
    seed, x, z, radius, max: maxResults, types, java,
  })], { encoding: "utf8", cwd: join(HERE, "..") });
  return JSON.parse(out);
}

const m = await createModule();

// 1) Recover the fixture seed in a narrow slice.
const ok = callCrack(m, FIXTURE, 0, SEED - 1000, SEED + 1000);
assert.equal(ok.error, undefined, `unexpected error: ${ok.error}`);
assert.equal(ok.threads, 1, "wasm must always report threads=1");
assert.equal(ok.timed_out, false);
assert.ok(ok.results.some(r => r.seed === SEED),
  `seed ${SEED} not recovered: ${JSON.stringify(ok.results)}`);

// 2) Validation errors surface as {"error"} and still free cleanly.
assert.ok("error" in callCrack(m, FIXTURE.slice(0, 3), 0, 0, 1000),
  "3 structures must error");
assert.ok("error" in callCrack(m, [...FIXTURE, { type: 15, x: 0, z: 0 }],
  0, 0, 1000), "Mineshaft must error");
assert.ok("error" in callCrack(m, FIXTURE, 9, 0, 1000),
  "tolerance 9 must error");
assert.ok("error" in callCrack(m, FIXTURE, 0, 5000, 5000),
  "empty range must error");

// 3) Parity with the native library (when built), on a mid-size slice.
const native = nativeParity(FIXTURE, 0, 0, 2_000_000);
if (native && !native.error) {
  const wasm = callCrack(m, FIXTURE, 0, 0, 2_000_000);
  assert.deepEqual(wasm.results, native.results,
    "wasm and native results diverge");
  console.log(`parity: OK (${wasm.results.length} results identical)`);
} else {
  console.log("parity: SKIPPED (no native lib at build_server/)");
}

// 4) False-positive regression: fictitious structures for which the cracker
// used to return seed 254568 with matches that never generate in-game (the
// biome filter is now applied to results). The seed must no longer appear.
const USER_STRUCTURES = [
  { type: 4, x: 136, z: 136 }, { type: 14, x: 264, z: -104 },
  { type: 1, x: -408, z: -184 }, { type: 11, x: -424, z: 152 },
  { type: 13, x: -248, z: -664 },
];
const fp = callCrack(m, USER_STRUCTURES, 6, 0, 16_000_000, 500, 30.0);
assert.equal(fp.error, undefined, `unexpected error: ${fp.error}`);
assert.ok(!fp.results.some(r => r.seed === 254568),
  `false-positive seed 254568 still returned: ${JSON.stringify(fp.results)}`);

// 5) Benchmark — checked/s, the number quoted in INTEGRATION.md.
// 3 runs: SIMD grouping gains are small enough that single runs are noisy.
const BENCH_END = 16_000_000;
const rates = [];
for (let run = 0; run < 3; run++) {
  const t0 = performance.now();
  const bench = callCrack(m, FIXTURE, 6, 0, BENCH_END, 500, 30.0);
  const dt = (performance.now() - t0) / 1000;
  assert.ok(bench.checked > 0, "benchmark checked nothing");
  rates.push(bench.checked / dt);
  console.log(`benchmark[${run}]: ${bench.checked.toLocaleString()} seeds in ` +
    `${dt.toFixed(2)}s = ${Math.round(bench.checked / dt).toLocaleString()} ` +
    `checked/s`);
}
const best = Math.max(...rates);
console.log(`benchmark: ${Math.round(best).toLocaleString()} checked/s (best of 3)`);

// 6) 64-bit shim smoke: _seedfinder_crack64_shim recovers a full 63-bit seed
// in a narrow (2^25-wide) window. Anchors are the same self-consistent
// fixture provenance as core/test_crack64.c testCrack64 — everything derived
// under FULL = 7777777777777777777 (constants verified against the native
// engine by build_server/probe64t9.c: unique candidate == FULL, tol 0):
//   Java anchor: getStructurePos(Trial_Chambers, MC_NEWEST, FULL, 0, 0)
//     -> pos (272,288) -> ((p-8)>>4)*16 = (256,272)   [test_crack64.c:167]
//   MT anchors: getBedrockStructurePos(Igloo, MC_NEWEST, FULL & 0xFFFFFFFF,
//     cells (-4,-4) -> (-1816,-1912) and (3,-3) -> (1832,-1272)), both
//     structureIsViable under FULL (probe64t9b.c).
// NOTE: the brief's "reuse FIXTURE[0..1] as MT anchors" was measured NOT to
// recover FULL — those blocks come from seed 8675309's MT placements and the
// biome lift under FULL rejects them — so the anchors are derived from FULL
// instead, same recipe as testCrack64. seed values exceed Number.MAX_SAFE_
// INTEGER, so match on seed_str.
const FULL64 = 7777777777777777777n;
if (typeof m._seedfinder_crack64_shim === "function") {
  const MT64 = [{ type: 4, x: -1816, z: -1912 }, { type: 4, x: 1832, z: -1272 }];
  const J64 = [{ type: 24, x: 256, z: 272 }];
  const c64 = callCrack64(m, MT64, J64, 0, FULL64 - (1n << 24n),
                          FULL64 + (1n << 24n), 500, 120.0);
  assert.equal(c64.error, undefined, `unexpected crack64 error: ${c64.error}`);
  assert.equal(c64.bits, 64);
  assert.equal(c64.threads, 1, "wasm must always report threads=1");
  assert.equal(c64.timed_out, false);
  assert.ok(c64.results.some(r => r.seed_str === "7777777777777777777"),
    `seed FULL not recovered: ${JSON.stringify(c64.results.slice(0, 5))}`);
  console.log("crack64: OK (seed 7777777777777777777 recovered in ±2^24)");
} else {
  console.log("CRACK64 WASM SKIPPED (stale module - rebuild via build_wasm)");
}

// 7) Scan (Bedrock) — ground truth in-game de server/tests/
// test_scan_village_groundtruth.py: seed=6666, jogador (0,0), radius=100,
// types=[5]. As 8 vilas confirmadas devem aparecer; as 3 celulas de rio/pantano
// (falsos positivos do gate antigo) NAO podem aparecer.
const TRUE_VILLAGES = [
  [-168, 56], [744, -344], [-312, -1000], [744, -872],
  [248, 1144], [-312, 1208], [1256, -328], [56, -1480],
];
const FALSE_POSITIVES = [[264, -296], [-776, -872], [776, -1368]];
const scanGT = callScan(m, 6666, 0, 0, 100, 500, [5]);
assert.equal(scanGT.error, undefined, `unexpected scan error: ${scanGT.error}`);
const got = new Set(scanGT.results.map(r => `${r.x},${r.z}`));
for (const [x, z] of TRUE_VILLAGES) {
  assert.ok(got.has(`${x},${z}`),
    `missing true village (${x},${z}): ${JSON.stringify(scanGT.results)}`);
}
for (const [x, z] of FALSE_POSITIVES) {
  assert.ok(!got.has(`${x},${z}`),
    `false positive village (${x},${z}) reported by wasm scan`);
}
assert.deepEqual(scanGT.results.map(r => r.distance),
  [...scanGT.results.map(r => r.distance)].sort((a, b) => a - b),
  "scan results must be sorted by distance");
console.log(`scan bedrock: OK (${scanGT.results.length} villages, ground truth)`);

// 8) Scan free discipline — repeated calls must not grow the heap unbounded
// (each result is malloc'd; missing frees show up as unbounded growth).
// HEAP32 is in EXPORTED_RUNTIME_METHODS; its length is the heap size / 4.
const heapBefore = m.HEAP32.length;
for (let i = 0; i < 20; i++) callScan(m, 6666, 0, 0, 100, 500, [5, 8]);
const heapAfter = m.HEAP32.length;
assert.ok(heapAfter <= heapBefore * 4,
  `heap grew from ${heapBefore * 4} to ${heapAfter * 4} bytes over 20 scans — leak?`);

// 9) Parity with the native library (when built): identical results for the
// same params, Bedrock AND Java.
const nativeBedrock = nativeScanParity(6666, 0, 0, 100, 500, [5, 8]);
if (nativeBedrock && !nativeBedrock.error) {
  const wasmBedrock = callScan(m, 6666, 0, 0, 100, 500, [5, 8]);
  assert.deepEqual(wasmBedrock.results, nativeBedrock.results,
    "wasm and native scan results diverge (bedrock)");
  console.log(`scan parity bedrock: OK (${wasmBedrock.results.length} results)`);
} else {
  console.log("scan parity bedrock: SKIPPED (no native lib at build_server/)");
}

// 10) Scan Java smoke — mcLabel=0 => MC_NEWEST (the only mode the server
// exposes today). Must return a well-formed results list, sorted by distance,
// and match the native lib when available.
const scanJava = callScan(m, 6666, 0, 0, 100, 500, [5, 8], true);
assert.equal(scanJava.error, undefined, `unexpected java scan error: ${scanJava.error}`);
assert.ok(Array.isArray(scanJava.results), "java scan must return results[]");
assert.deepEqual(scanJava.results.map(r => r.distance),
  [...scanJava.results.map(r => r.distance)].sort((a, b) => a - b),
  "java scan results must be sorted by distance");
const nativeJava = nativeScanParity(6666, 0, 0, 100, 500, [5, 8], true);
if (nativeJava && !nativeJava.error) {
  assert.deepEqual(scanJava.results, nativeJava.results,
    "wasm and native scan results diverge (java)");
  console.log(`scan parity java: OK (${scanJava.results.length} results)`);
} else {
  console.log(`scan java: OK (${scanJava.results.length} results, no native parity)`);
}

// 11) Auto-mode pipeline (sweep -> lift48 -> lift63), os tres exports novos.
// Mesma disciplina de callCrack: malloc -> views frescas -> BigInt ->
// UTF8ToString -> JSON.parse -> free de TODO ponteiro no finally.
const MT_AUTO = [
  { type: 4, x: -280, z: 104 }, { type: 2, x: 2584, z: -1288 },
  { type: 1, x: -3048, z: -296 }, { type: 3, x: 2136, z: -280 },
];
const TC_AUTO = [
  { type: 24, x: -505, z: -281 }, { type: 24, x: 263, z: -311 },
  { type: 24, x: -359, z: 199 }, { type: 24, x: 231, z: 169 },
];
const LO32_FIXTURE = 5309n;             // 4294972605 & 0xFFFFFFFF
const FULL_FIXTURE = 4294972605n;
// Tol <= 3: o guard do lift48 (nS48 > 1024) rejeita os yields maiores com
// 4 TCs (medido: tol 4 -> 1134, tol 5 -> 2626, tol 6 -> 3391); tol 2 -> 68.
const AUTO_TOL = 2;

// uint64_t* SEMPRE via BigInt64Array sobre HEAP64 (nunca Number/double) —
// e o view e' criado apos o malloc, para pegar o buffer fresco.
function allocU64(m, values) {
  const p = m._malloc(values.length * 8);
  const view = new BigInt64Array(m.HEAP64.buffer, p, values.length);
  for (let i = 0; i < values.length; i++) view[i] = BigInt(values[i]);
  return p;
}

function writeStructs(m, structures, pT, pX, pZ) {
  for (let i = 0; i < structures.length; i++) {
    m.HEAP32[(pT >> 2) + i] = structures[i].type;
    m.HEAPF64[(pX >> 3) + i] = structures[i].x;
    m.HEAPF64[(pZ >> 3) + i] = structures[i].z;
  }
}

function callAutoSweep(m, structures, tolerance, startSeed, endSeed,
                       maxResults = 2000, budget = 0.0, threads = 1) {
  const n = structures.length;
  const pT = m._malloc(n * 4), pX = m._malloc(n * 8), pZ = m._malloc(n * 8);
  writeStructs(m, structures, pT, pX, pZ);
  let ptr = 0;
  try {
    ptr = m._seedfinder_crack_auto_sweep(pT, n, pX, pZ, tolerance,
      BigInt(startSeed), BigInt(endSeed), maxResults, budget, threads);
    return JSON.parse(m.UTF8ToString(ptr));
  } finally {
    if (ptr) m._seedfinder_free_result(ptr);
    m._free(pT); m._free(pX); m._free(pZ);
  }
}

function callAutoLift48(m, mt, lo32s, java = [], tolerance = AUTO_TOL,
                        maxResults = 2000) {
  const nMt = mt.length, nL = lo32s.length, nJ = java.length;
  const pT = m._malloc(nMt * 4), pX = m._malloc(nMt * 8), pZ = m._malloc(nMt * 8);
  const pL = allocU64(m, lo32s.length ? lo32s : [0n]);
  const pjT = m._malloc((nJ || 1) * 4), pjX = m._malloc((nJ || 1) * 8),
        pjZ = m._malloc((nJ || 1) * 8);
  writeStructs(m, mt, pT, pX, pZ);
  writeStructs(m, java, pjT, pjX, pjZ);
  let ptr = 0;
  try {
    ptr = m._seedfinder_crack_auto_lift48(pT, pX, pZ, nMt, pL, nL,
      pjT, pjX, pjZ, nJ, tolerance, maxResults);
    return JSON.parse(m.UTF8ToString(ptr));
  } finally {
    if (ptr) m._seedfinder_free_result(ptr);
    m._free(pT); m._free(pX); m._free(pZ); m._free(pL);
    m._free(pjT); m._free(pjX); m._free(pjZ);
  }
}

function callAutoLift63(m, s48s, mt, tolerance = AUTO_TOL, maxResults = 2000,
                        budget = 0.0) {
  const nS = s48s.length, nMt = mt.length;
  const pS = allocU64(m, s48s);
  const pT = m._malloc(nMt * 4), pX = m._malloc(nMt * 8), pZ = m._malloc(nMt * 8);
  writeStructs(m, mt, pT, pX, pZ);
  let ptr = 0;
  try {
    ptr = m._seedfinder_crack_auto_lift63(pS, nS, pT, pX, pZ, nMt,
      tolerance, maxResults, budget);
    return JSON.parse(m.UTF8ToString(ptr));
  } finally {
    if (ptr) m._seedfinder_free_result(ptr);
    m._free(pS); m._free(pT); m._free(pX); m._free(pZ);
  }
}

// 11a) sweep placement-only: o crack de 32 bits de HOJE nao acha o lo32 5309
// nessa janela (o gate de bioma o descarta - defeito 2); o auto_sweep acha.
const autoSweep = callAutoSweep(m, MT_AUTO, 6, 0n, 16_000_000n);
assert.equal(autoSweep.error, undefined, `auto sweep error: ${autoSweep.error}`);
assert.ok(autoSweep.results.some(r => r.seed === 5309),
  `auto sweep missed lo32 5309: ${JSON.stringify(autoSweep.results.slice(0, 5))}`);
assert.ok(!("matches" in autoSweep.results[0]),
  "auto sweep must stay placement-only (no matches, no biome gate)");
console.log(`auto sweep: OK (lo32 5309 in ${autoSweep.checked} seeds)`);

// 11b) lift48: deteccao 32-bit (sem ancoras) + lift pelas ancoras Java.
const liftDirect = callAutoLift48(m, FIXTURE, [SEED], [], AUTO_TOL);
assert.equal(liftDirect.error, undefined, `lift48 direct error: ${liftDirect.error}`);
assert.ok(liftDirect.direct32.some(r => r.seed === SEED),
  `lift48 direct32 missed ${SEED}: ${JSON.stringify(liftDirect.direct32)}`);
const liftAnchored = callAutoLift48(m, MT_AUTO, [LO32_FIXTURE], TC_AUTO, AUTO_TOL);
assert.equal(liftAnchored.error, undefined, `lift48 anchored error: ${liftAnchored.error}`);
assert.ok(liftAnchored.s48.some(s => BigInt(s) === FULL_FIXTURE),
  `lift48 did not lift 4294972605 (${liftAnchored.s48.length} s48)`);
assert.deepEqual(liftAnchored.direct32, [],
  "64-bit world must not produce direct32 hits");
console.log(`auto lift48: OK (direct32=${liftDirect.direct32.length}, ` +
  `s48=${liftAnchored.s48.length})`);

// 11c) lift63: recupera a seed completa a partir do s48 do lift48.
const lift63 = callAutoLift63(m, [FULL_FIXTURE], MT_AUTO, AUTO_TOL);
assert.equal(lift63.error, undefined, `lift63 error: ${lift63.error}`);
assert.ok(lift63.results.some(r => r.seed_str === "4294972605"),
  `lift63 missed 4294972605: ${JSON.stringify(lift63.results.slice(0, 5))}`);
console.log(`auto lift63: OK (${lift63.results.length} results, ` +
  `checked=${lift63.checked})`);

// 11d) Disciplina de memoria (Review Focus #4): 20 chamadas com listas de
// 1024 elementos nao podem crescer o heap sem limite. O lift63 roda com
// budget curto de proposito — senao 1024 s48 x 32768 hi levaria horas.
const heap64Before = m.HEAP64.length;
const bigLo32 = Array.from({ length: 1024 }, (_, i) => BigInt(i * 4093 + 7));
const bigS48 = Array.from({ length: 1024 }, (_, i) => BigInt(i * 65537 + 1));
for (let i = 0; i < 20; i++) {
  callAutoLift48(m, MT_AUTO, bigLo32, TC_AUTO, AUTO_TOL);
  callAutoLift63(m, bigS48, MT_AUTO, AUTO_TOL, 2000, 0.15);
}
const heap64After = m.HEAP64.length;
assert.ok(heap64After <= heap64Before * 4,
  `HEAP64 grew from ${heap64Before * 8} to ${heap64After * 8} bytes over 40 auto calls — leak?`);
console.log(`auto free discipline: OK (HEAP64 ${heap64Before * 8} -> ${heap64After * 8} bytes)`);

// 11e) Paridade com a lib nativa (quando build_server/ existe): os tres
// exports com os mesmos parametros devem devolver JSON identico.
function nativeAutoParity(kind, payload) {
  const dll = ["build_server/seedfinder_lib.dll",
               "build_server/seedfinder_lib.so"].map(p => join(HERE, "..", p))
                                               .find(existsSync);
  if (!dll) return null;
  const py = `
import ctypes, json, sys
p = json.loads(sys.argv[1])
lib = ctypes.CDLL(r"""${dll.replace(/"/g, "")}""")
lib.seedfinder_free_result.argtypes = [ctypes.c_void_p]
I = ctypes.POINTER(ctypes.c_int); D = ctypes.POINTER(ctypes.c_double)
U = ctypes.POINTER(ctypes.c_uint64)
def structs(n): return ((ctypes.c_int * n)(*(s["type"] for s in p["s"][:n])),
                         (ctypes.c_double * n)(*(float(s["x"]) for s in p["s"][:n])),
                         (ctypes.c_double * n)(*(float(s["z"]) for s in p["s"][:n])))
def jstructs():
    n = len(p["j"]); return ((ctypes.c_int * n)(*(s["type"] for s in p["j"])),
                             (ctypes.c_double * n)(*(float(s["x"]) for s in p["j"])),
                             (ctypes.c_double * n)(*(float(s["z"]) for s in p["j"])))
def run(f, args):
    f.restype = ctypes.c_void_p
    ptr = f(*args)
    try: return ctypes.string_at(ptr).decode("utf-8")
    finally: lib.seedfinder_free_result(ptr)
if p["kind"] == "sweep":
    f = lib.seedfinder_crack_auto_sweep
    f.argtypes = [I, ctypes.c_int, D, D, ctypes.c_int, ctypes.c_uint64,
                  ctypes.c_uint64, ctypes.c_int, ctypes.c_double, ctypes.c_int]
    T, X, Z = structs(len(p["s"]))
    print(run(f, (T, len(p["s"]), X, Z, p["tolerance"], p["start"], p["end"],
                  p["max"], 0.0, 1)))
elif p["kind"] == "lift48":
    f = lib.seedfinder_crack_auto_lift48
    f.argtypes = [I, D, D, ctypes.c_int, U, ctypes.c_int, I, D, D,
                  ctypes.c_int, ctypes.c_int, ctypes.c_int]
    T, X, Z = structs(len(p["s"])); jT, jX, jZ = jstructs()
    L = (ctypes.c_uint64 * max(len(p["lo32"]), 1))(*(p["lo32"] or [0]))
    print(run(f, (T, X, Z, len(p["s"]), L, len(p["lo32"]), jT, jX, jZ,
                  len(p["j"]), p["tolerance"], p["max"])))
else:
    f = lib.seedfinder_crack_auto_lift63
    f.argtypes = [U, ctypes.c_int, I, D, D, ctypes.c_int, ctypes.c_int,
                  ctypes.c_int, ctypes.c_double]
    S = (ctypes.c_uint64 * len(p["s48"]))(*p["s48"])
    T, X, Z = structs(len(p["s"]))
    print(run(f, (S, len(p["s48"]), T, X, Z, len(p["s"]), p["tolerance"],
                  p["max"], 0.0)))
`;
  const out = execFileSync("python", ["-c", py, JSON.stringify({ kind, ...payload })],
                           { encoding: "utf8", cwd: join(HERE, "..") });
  return JSON.parse(out);
}

const sweepParity = nativeAutoParity("sweep", {
  s: MT_AUTO, tolerance: 6, start: 0, end: 16_000_000, max: 2000,
});
if (sweepParity) {
  assert.deepEqual(autoSweep, sweepParity, "auto sweep wasm/native diverge");
  const liftParity = nativeAutoParity("lift48", {
    s: MT_AUTO, j: TC_AUTO, lo32: [5309], tolerance: AUTO_TOL, max: 2000,
  });
  assert.deepEqual(liftAnchored, liftParity, "auto lift48 wasm/native diverge");
  const l63Parity = nativeAutoParity("lift63", {
    s: MT_AUTO, s48: [4294972605], tolerance: AUTO_TOL, max: 2000,
  });
  assert.deepEqual(lift63, l63Parity, "auto lift63 wasm/native diverge");
  console.log("auto parity native: OK (sweep + lift48 + lift63 identical)");
} else {
  console.log("auto parity native: SKIPPED (no native lib at build_server/)");
}

console.log("ALL WASM TESTS PASSED");
