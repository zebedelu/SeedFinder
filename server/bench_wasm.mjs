// server/bench_wasm.mjs — runner WASM do benchmark F0.
// Uso: node server/bench_wasm.mjs --fixtures build_bench/fixtures.json
// Imprime UM array JSON em stdout (logs vao para stderr).
//
// ABI (wasm/INTEGRATION.md §3.3-3.5, wasm/test_mitm.mjs): uint64_t chega como
// BigInt; arrays uint64_t* vao por HEAP64; _seedfinder_free_result(ptr) e'
// obrigatorio em TODO retorno (inclusive erro) e as entradas vao por _malloc.
import { readFileSync } from "node:fs";
import createModule from "../wasm/out/seedfinder.js";

const args = process.argv.slice(2);
const argOf = (name, def) => {
  const i = args.indexOf(name);
  return i >= 0 && i + 1 < args.length ? args[i + 1] : def;
};
const fixturesPath = argOf("--fixtures");
if (!fixturesPath) {
  console.error("--fixtures <path> obrigatorio");
  process.exit(2);
}
const payload = JSON.parse(readFileSync(fixturesPath, "utf8"));
// `full` vem do bench.py: sem ele o java_mitm mediria u_timing enquanto o lado
// nativo mede [0, 2^24) - janelas diferentes na mesma linha da tabela.
const FULL = Boolean(payload.full);

// Mesmo marcador de guard do bench.py (_ENGINE_GUARD_MARKERS): pedido de mais
// ancoras e' "nada a medir" (skipped), nao falha de medicao.
const ENGINE_GUARD = "too many 48-bit candidates";

const m = await createModule();

const now = () => Number(process.hrtime.bigint()) / 1e6;

function intArray(values) {
  const p = m._malloc(values.length * 4);
  m.HEAP32.set(values, p >> 2);
  return p;
}
function f64Array(values) {
  const p = m._malloc(values.length * 8);
  m.HEAPF64.set(values, p >> 3);
  return p;
}
function u64Array(values) {
  const p = m._malloc(values.length * 8);
  m.HEAP64.set(values.map((v) => BigInt(v)), p >> 3);
  return p;
}
function freeAll(ptrs) {
  for (const p of ptrs) if (p) m._free(p);
}
// `ptrs` = buffers desta chamada e de mais nenhuma (os reusados entre fases sao
// liberados pelo chamador). Devolve sempre {data, ms}: um NULL ou um {"error"}
// do motor vira `data.error` e a fase vira linha invalida - nunca uma taxa
// fabricada, e nunca aborta as outras fases/linhas.
function call(fn, ptrs, ...rest) {
  let ptr = 0;
  const t0 = now();
  try {
    ptr = fn(...rest);
    if (!ptr) {
      return { data: { error: `${fn.name || "engine"} returned NULL` },
               ms: now() - t0 };
    }
    const data = JSON.parse(m.UTF8ToString(ptr));
    return { data, ms: now() - t0 };
  } finally {
    if (ptr) m._seedfinder_free_result(ptr);
    freeAll(ptrs);
  }
}

// Uma linha do benchmark. `data` e' o JSON cru do motor: se ele traz {"error"},
// nao houve medicao nenhuma e a linha sai invalida com a mensagem (mesmo
// formato dos _error_row do bench.py) - no mitm, por exemplo, o checked seria o
// tamanho da janela e o tempo, real, daria uma taxa plausivel e falsa.
function row(fixture, phase, unit, threads, checked, ms, data) {
  if (data && data.error) {
    return {
      runner: "wasm", fixture, phase, unit, threads, checked: 0,
      elapsed_ms: 0, timed_out: false, valid: false, error: String(data.error),
    };
  }
  return {
    runner: "wasm", fixture, phase, unit, threads,
    checked: Number(checked), elapsed_ms: Math.round(ms * 1000) / 1000,
    timed_out: Boolean(data.timed_out), valid: !data.timed_out,
  };
}

// Fase sem o que medir (ausencia por contrato), nao falha: espelha o _skip_row
// do bench.py em vez de chamar o motor com entrada que ele recusa.
function skipRow(fixture, phase, unit, threads, note) {
  return {
    runner: "wasm", fixture, phase, unit, threads, checked: 0, elapsed_ms: 0,
    timed_out: false, valid: false, skipped: true, note: String(note),
  };
}

const rows = [];
for (const fx of payload.fixtures) {
  const space = Math.min(payload.space, fx.space);
  if (fx.mode === "bedrock32") {
    const n = fx.structures.length;
    const pT = intArray(fx.structures.map((s) => s.type));
    const pX = f64Array(fx.structures.map((s) => s.x));
    const pZ = f64Array(fx.structures.map((s) => s.z));
    const { data, ms } = call(m._seedfinder_crack, [pT, pX, pZ], pT, n, pX, pZ,
      fx.tolerance, BigInt(0), BigInt(space), 2000, 3600.0, 1);
    rows.push(row(fx.name, "crack32", "seeds", 1, data.checked || 0, ms, data));
    continue;
  }
  if (fx.mode === "auto") {
    const mt = fx.mt_structures, jv = fx.java_structures;
    // Estes buffers alimentam as TRES fases (sweep -> lift48 -> lift63), entao
    // sao liberados UMA vez no finally. Liberar no fim de cada `call` deixaria
    // ponteiros pendurados: o malloc da fase seguinte reusa a memoria e o lift48
    // recebia tolerancias corrompidas ("structure #1: tolerance must be between
    // 0 and 8 chunks"), abortando o runner inteiro.
    const pT = intArray(mt.map((s) => s.type));
    const pX = f64Array(mt.map((s) => s.x));
    const pZ = f64Array(mt.map((s) => s.z));
    const pTol = intArray(mt.map(() => fx.tolerance));
    const jT = intArray(jv.map((s) => s.type));
    const jX = f64Array(jv.map((s) => s.x));
    const jZ = f64Array(jv.map((s) => s.z));
    const jTol = intArray(jv.map(() => fx.tolerance));
    try {
      const sweep = call(m._seedfinder_crack_auto_sweep, [],
        pT, mt.length, pX, pZ, pTol, BigInt(0), BigInt(space), 2000, 3600.0, 1);
      rows.push(row(fx.name, "sweep", "seeds", 1, sweep.data.checked || 0,
        sweep.ms, sweep.data));
      if (sweep.data.error) continue;  // sem sweep nao ha fase 2/3 a medir

      const lo32 = (sweep.data.results || []).slice(0, 2000).map((r) => r.seed);
      if (!lo32.length) {
        // Sem candidatos nao ha o que liftar: skipped (ausencia), nunca chamar
        // o motor com array vazio (ele recusa e isso seria erro de quem chama).
        rows.push(skipRow(fx.name, "lift48", "candidates", 1,
          "sweep sem candidatos lo32 - fase 2 nao roda"));
        rows.push(skipRow(fx.name, "lift48_rank1", "candidates", 1,
          "sweep sem candidatos lo32 - sem rank-1"));
        rows.push(skipRow(fx.name, "lift63", "hi", 1,
          "sem s48 da fase 3 - lift63 nao roda"));
        continue;
      }
      // Fase 2: a LISTA inteira (deteccao). Com nLo32 > 1 o s48 sai vazio POR
      // CONTRATO (core/crack_auto.h): serve para direct32, nao alimenta lift63.
      const pLo = u64Array(lo32);
      const l48 = call(m._seedfinder_crack_auto_lift48, [pLo],
        pT, pX, pZ, pTol, mt.length, pLo, lo32.length,
        jT, jX, jZ, jTol, jv.length, 2000);
      const l48Row = row(fx.name, "lift48", "candidates", 1, lo32.length,
        l48.ms, l48.data);
      if (!l48.data.error && lo32.length > 1) {
        l48Row.note = "s48 vazio por contrato (nLo32 > 1): a fase 2 so' produz direct32";
      }
      rows.push(l48Row);

      // Fase 3: rank-1 - e' aqui que o lift Java roda. O guard de 1024 candidatos
      // e' um PEDIDO de mais ancoras (skipped); qualquer outro erro e' falha.
      const pRank1 = u64Array([lo32[0]]);
      const rank1 = call(m._seedfinder_crack_auto_lift48, [pRank1],
        pT, pX, pZ, pTol, mt.length, pRank1, 1,
        jT, jX, jZ, jTol, jv.length, 2000);
      const guarded = Boolean(rank1.data.error) &&
        String(rank1.data.error).includes(ENGINE_GUARD);
      if (guarded) {
        rows.push(skipRow(fx.name, "lift48_rank1", "candidates", 1,
          rank1.data.error));
      } else {
        rows.push(row(fx.name, "lift48_rank1", "candidates", 1, 1,
          rank1.ms, rank1.data));
      }

      // Fase 4: lift63 so' com o s48 da fase 3 - nunca com array vazio.
      const s48 = guarded ? [] : (rank1.data.s48 || []);
      if (!s48.length) {
        rows.push(skipRow(fx.name, "lift63", "hi", 1,
          "sem s48 da fase 3 - lift63 nao roda"));
        continue;
      }
      const pS48 = u64Array(s48);
      const l63 = call(m._seedfinder_crack_auto_lift63, [pS48],
        pS48, s48.length, pT, pX, pZ, pTol, mt.length,
        jT, jX, jZ, jTol, jv.length, 2000, 3600.0);
      rows.push(row(fx.name, "lift63", "hi", 1, l63.data.checked || 0,
        l63.ms, l63.data));
    } finally {
      freeAll([pT, pX, pZ, pTol, jT, jX, jZ, jTol]);
    }
    continue;
  }
  if (fx.mode === "java_mitm") {
    const n = fx.structures.length;
    const pT = intArray(fx.structures.map((s) => s.type));
    const pX = f64Array(fx.structures.map((s) => s.x));
    const pZ = f64Array(fx.structures.map((s) => s.z));
    const [a, b] = FULL ? [0, fx.space] : fx.u_timing;
    const { data, ms } = call(m._seedfinder_crack_mitm_window, [pT, pX, pZ],
      pT, n, pX, pZ, 50, 3600.0, 1, BigInt(a), BigInt(b));
    rows.push(row(fx.name, "mitm", "u", 1, b - a, ms, data));
    continue;
  }
  console.error(`modo nao suportado no runner wasm: ${fx.mode}`);
  process.exit(3);
}

process.stdout.write(JSON.stringify(rows));
