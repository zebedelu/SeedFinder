# seedfinder.wasm — Guia de Integração

Documento autossuficiente para integrar o **SeedFinder (scan de estruturas) e o
SeedCracker (busca de seeds por layout de estruturas)** no navegador (Web Workers + deploy
estático). **Não é preciso ler o código C** nem nenhum plano: tudo aqui foi
validado contra o comportamento real do build (`wasm/test_wasm.mjs` é a
referência executável — ela cobre scan Bedrock, scan Java, crack 32/64-bit e
disciplina de memória).

> **Rename (2026-09-25)**: o par de saída era `seedcracker.{js,wasm}` e agora é
> **`seedfinder.{js,wasm}`** (o módulo ganhou o scan). Consumidores existentes
> precisam trocar o import e apagar os arquivos antigos — ver §10.2.

---

## 1. O que é

- `seedfinder.wasm` — o motor compilado com Emscripten com **duas** superfícies:
  - **Scan** (`seedfinder_scan` / `seedfinder_scan_java`): dado um seed e uma
    posição, devolve as estruturas próximas (vilas, templos, monumentos, …)
    ordenadas por distância — o mesmo que a rota HTTP `GET|POST /scan` faz
    server-side.
  - **SeedCracker** (`seedfinder_crack` / `seedfinder_crack64_shim`): busca
    de seeds Bedrock por layout de estruturas a partir de ≥4 coordenadas alvo.
- `seedfinder.js` — glue ES6 gerado pelo Emscripten (`-sMODULARIZE=1
  -sEXPORT_ES6=1`): exporta um **factory default** que retorna uma Promise com
  a instância do módulo.

```js
import createSeedfinderModule from "./seedfinder.js";
const m = await createSeedfinderModule();
```

Ambos vivem em `wasm/out/` após o build. Com eles, o backend fica só com
arquivos estáticos — scan e quebra de seeds acontecem no cliente.

## 2. Build

Prerequisito (Windows/MSYS2 UCRT64):

```bash
pacman -S mingw-w64-ucrt-x86_64-emscripten
```

Build — **um clique, zero configuração**: os scripts acham o MSYS2 sozinhos e
põem `emcc`/`emcmake` no PATH. O único requisito é o pacote acima.

```bat
REM Windows: duplo-clique no Explorer, ou na linha de comando:
wasm\build_wasm.bat            REM build incremental (padrão)
wasm\build_wasm.bat clean      REM apaga build\ e reconstrói do zero
wasm\build_wasm.bat test       REM roda "node test_wasm.mjs" no fim
wasm\build_wasm.bat nopause    REM não pausa no fim (automação/CI)
```

```bash
# bash do MSYS2
bash wasm/build_wasm.sh [clean] [test]
```

- **Incremental por padrão** (segundos quando nada mudou); `clean` força
  reconfiguração total.
- `MSYS2_ROOT` sobrescreve a raiz do MSYS2 (padrão `C:\msys64`).
- No MSYS2 o Emscripten fica em `<MSYS2>\ucrt64\lib\emscripten` (é lá que
  estão `emcc.bat`/`emcmake.bat` — as variantes `.exe` são `.broken`), e as
  ferramentas em `<MSYS2>\ucrt64\bin`. Os scripts põem **as duas** no PATH.
- Mantenedor: no `.bat` o `emcmake` **precisa** de `call` (ele é um `.bat`);
  sem o `call` o script termina ali e o `cmake --build` nunca roda — era o
  "no-op silencioso" que existia antes.
- Internamente (de dentro de `wasm/`):
  `emcmake cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G "MinGW Makefiles"`
  e `cmake --build build -j <nproc>`.
- **Plano B** sem MSYS2: emsdk oficial (`emsdk_env.bat`) — exporte o PATH e os
  comandos `emcmake`/`cmake` são idênticos.
- Produto: **`wasm/out/seedfinder.js` + `wasm/out/seedfinder.wasm`** (mais a
  árvore intermediária `wasm/build/`, que pode ser apagada). Os scripts removem
  um eventual par antigo `seedcracker.*` em `out/` para não confundir a cópia.
- Flags de link relevantes (já no `wasm/CMakeLists.txt`): `WASM_BIGINT`
  (default do emcc moderno), `EXPORTED_FUNCTIONS=_seedfinder_crack,
  _seedfinder_crack64_shim,_seedfinder_scan,_seedfinder_scan_java,
  _seedfinder_free_result,_malloc,_free`,
  `EXPORTED_RUNTIME_METHODS=ccall,
  cwrap,UTF8ToString,HEAP32,HEAPF64`, `ALLOW_MEMORY_GROWTH=1`,
  `ENVIRONMENT=web,worker,node`. **Sem `-pthread`** (ver seção 9).

## 3. ABI — assinatura e tabela de parâmetros

### 3.0 Scan — `_seedfinder_scan` (Bedrock) e `_seedfinder_scan_java` (Java)

```c
char *seedfinder_scan(uint64_t seed,
                      double playerX, double playerZ,
                      int radius, int maxResults,
                      const int *types, int numTypes);

char *seedfinder_scan_java(/* mesmos 7 args */, const char *mcLabel);
```

| Param | Tipo JS | Notas |
|---|---|---|
| `seed` | **`BigInt`** (`6666n`, `BigInt(seedStr) & 0xFFFFFFFFFFFFFFFFn`) | `WASM_BIGINT`; o `& mask` espelha o `int & 0xFFFFFFFFFFFFFFFF` do Python e converte seeds negativos |
| `playerX` / `playerZ` | `number` | posição do jogador em **BLOCOS**; finitos, ±1e9 (o chamador valida) |
| `radius` | `number` | raio em **chunks**; o chamador faz o clamp (o C não valida — valores negativos entram no loop de regiões!) |
| `maxResults` | `number` | teto de resultados; idem, clamp é responsabilidade do chamador |
| `types` | `Int32Array` escrito em `HEAP32` após `_malloc(n*4)` | IDs Bedrock: **1–15, 23, 24** (16/17 não têm config → são pulados silenciosamente) |
| `numTypes` | `number` | ≥1 (o chamador valida; 0 devolve `{"results":[]}`) |
| `mcLabel` | só `_java`: ponteiro (`0` = NULL) ou `Int32Array` após `stringToUTF8` | `0` ⇒ `MC_NEWEST`; hoje o servidor sempre manda NULL |

- **Parâmetros de validação NÃO são feitos pelo C** (diferente do crack, que
  devolve `{"error": ...}`): o scan assume entrada limpa. **Toda a validação
  (defaults, clamps, `missing_or_invalid`) vive no chamador JS** e deve
  reproduzir fielmente a rota `/scan` — ver §10.4.
- Retorno: ponteiro para `{"results": [{"name","x","z","distance"}, ...]}` —
  **idêntico em formato ao `jsonify(result)` do servidor** (distância em chunks,
  ordenado por distância crescente, cortado em `maxResults`). A rota Python
  também pode acrescentar `missing_or_invalid`; ver §10.4 para o espelho.
- Uso: mesmo padrão do `callScan` do `wasm/test_wasm.mjs` (smoke #7–10).
  `_seedfinder_scan_java` é idêntico com o 8º arg `0`.

```js
function callScan(m, { seed, x, z, radius, max, types }, java = false) {
  const n = types.length;
  const pT = m._malloc(n * 4);
  let ptr = 0;
  try {
    const heap32 = m.HEAP32;               // view fresca: growth invalida a antiga
    for (let i = 0; i < n; i++) heap32[(pT >> 2) + i] = types[i];
    ptr = java
      ? m._seedfinder_scan_java(BigInt(seed), x, z, radius, max, pT, n, 0)
      : m._seedfinder_scan(BigInt(seed), x, z, radius, max, pT, n);
    return JSON.parse(m.UTF8ToString(ptr));
  } finally {
    if (ptr) m._seedfinder_free_result(ptr);  // SEMPRE
    m._free(pT);
  }
}
```

**Custo**: o scan é síncrono e não tem `timeBudgetSec` (varre todas as regiões
do raio). Pior caso medido (radius=200 clampado, 11 tipos, Node 24, este repo):
**~370 ms** por chamada — curto, mas suficiente para travar um frame na main
thread; rode no worker (§5/§6).

### 3.1 Crack 32-bit — `_seedfinder_crack`

```c
char *seedfinder_crack(const int *types, int numTypes,
                       const double *xBlocks, const double *zBlocks,
                       int tolerance, uint64_t startSeed, uint64_t endSeed,
                       int maxResults, double timeBudgetSec, int numThreads);
```

| Param | Tipo JS | Notas |
|---|---|---|
| `types` | `Int32Array` escrito em `HEAP32` após `_malloc(n*4)` | IDs válidos: **1–11, 13, 14, 23, 24** |
| `numTypes` | `number` | 4–24 |
| `xBlocks` / `zBlocks` | `Float64Array` em `HEAPF64` após `_malloc(n*8)` | **Coordenadas em BLOCOS**, não chunks (chunk × 16) |
| `tolerance` | `number` | 0–8 chunks |
| `startSeed` / `endSeed` | **`BigInt`** (`0n`, `0x100000000n`) | `WASM_BIGINT`; range é `[start, end)` |
| `maxResults` | `number` | 1–2000 |
| `timeBudgetSec` | `number` | segundos (budget de tempo) |
| `numThreads` | `number` | **sempre 1** — o C força 1 no WASM |

Retorno: ponteiro para uma string JSON (ver seção 4).

### 3.2 Variante 64-bit — `_seedfinder_crack64_shim` (14 args)

```c
char *seedfinder_crack64_shim(const int *mtTypes, const double *mtX,
                              const double *mtZ, int nMt,
                              const int *jTypes, const double *jX,
                              const double *jZ, int nJava,
                              int tolerance, uint64_t startSeed, uint64_t endSeed,
                              int maxResults, double budgetSec, int numThreads);
```

Mesmas convenções de heap/BigInt do §3.1, com dois arrays de entrada (MT +
Java-style) e diferenças de semântica:

| Param | Tipo JS | Notas |
|---|---|---|
| `mtTypes` + `nMt` | `Int32Array`, `number` | Estruturas MT (mod 2^32): **1–24**, IDs 1–11, 13, 14; Mineshaft (15), Trail Ruins (23) e Trial Chambers (24) são **rejeitados** aqui — os dois últimos vão no array Java |
| `jTypes` + `nJava` | `Int32Array`, `number` | Âncoras Java-style: **≥1** (recomendado 4+), só tipos **23/24** |
| coords (`mtX/mtZ`, `jX/jZ`) | `Float64Array` | **BLOCOS**, como no crack de 32 bits |
| `startSeed` / `endSeed` | **`BigInt`** | Espaço de seed completa (0–2^63); span < 2^48 = varredura bounded rápida, span ≥ 2^48 = varredura 2^48 completa (≈65k× o espaço 32-bit — milhares de core-hours sem paralelismo; use janela estreita + 4+ Trial Chambers) |
| demais | | `tolerance`, `maxResults`, `budgetSec`, `numThreads` idem §3.1 |

Retorno: JSON com `"bits":64` no envelope e **dois campos de seed** por item:
`seed` (number — **PERDE PRECISÃO**: seeds > 2^53 não são representáveis em
double; compare sempre pelo `seed_str`) e `seed_str` (string verbatim).
**Matches saem em BLOCOS** no crack64 (o crack de 32 bits devolve chunks); a
rota Python `/seedcracker` normaliza para chunks, mas o consumidor WASM recebe
blocos crus — divida por 16 com floor se quiser chunks.
Uso idêntico ao do callCrack do `wasm/test_wasm.mjs` (helper `callCrack64`,
smoke #6), **mas cuidado com a ordem**: cada grupo é `(types, x, z, n)` — os
counts vêm DEPOIS dos arrays de coordenadas, NÃO no padrão do crack de 32 bits
(`types, n, x, z`). Fluxo: malloc dos 6 buffers → escrever views → BigInt nos
seeds → `UTF8ToString` → `JSON.parse` → `_seedfinder_free_result` +
`_free`×6 no `finally`.

## 4. Retorno e disciplina de memória

Fluxo obrigatório, **todo caminho de execução** (vale para scan, crack e
crack64 — todos devolvem um ponteiro malloc'd):

```js
// crack:
const ptr = m._seedfinder_crack(/* ... */, BigInt(start), BigInt(end), max, budget, 1);
// scan (mesma disciplina, ver §3.0):
const ptr = m._seedfinder_scan(BigInt(seed), x, z, radius, max, pT, n);
const json = JSON.parse(m.UTF8ToString(ptr));
m._seedfinder_free_result(ptr);   // SEMPRE — inclusive se vier {"error": ...}
```

- `_seedfinder_free_result(ptr)` é **obrigatório em TODO retorno**, sucesso ou
  erro — as strings de erro também são alocadas (`strdup`). Vazar o ptr é
  vazar memória WASM (que cresce com `ALLOW_MEMORY_GROWTH=1`).
- Chame `UTF8ToString(ptr)` **antes** do free, obviamente.
- Libere também os buffers de entrada: `_free(pTypes)`, `_free(pX)`,
  `_free(pZ)`. Padrão recomendado: `try { ... } finally { free de tudo }`
  (é o que `wasm/test_wasm.mjs` faz).
- JSON de **sucesso** do **crack** — idêntico ao do endpoint `/seedcracker` do
  servidor:

```json
{"results":[{"seed": 8675309, "score": 2, "matches": [[-280, 152], ...]}],
 "checked": 16000000, "timed_out": false, "threads": 1}
```

- JSON de **sucesso do scan** — idêntico ao do endpoint `/scan`
  (`{"results": [{"name": "Village", "x": -168, "z": 56, "distance": 10.5}, ...]}`);
  sem campos extras. Erros de **validação** do scan não existem no C — são
  produzidos pelo chamador (§10.4).

- JSON de **erro do crack**: `{"error": "<mensagem>"}`. As 9 mensagens, verbatim:

| Mensagem | Causa |
|---|---|
| `at least 4 structures are required` | `numTypes < 4` |
| `too many structures (max 24)` | `numTypes > 24` |
| `tolerance must be between 0 and 8 chunks` | fora de 0–8 |
| `maxResults must be between 1 and 2000` | fora de 1–2000 |
| `empty seed range` | `startSeed >= endSeed` |
| `unknown structure type` | ID fora da lista válida |
| `Mineshaft is not supported by SeedCracker` | tipo 15 (não é region-crackable) |
| `coordinates out of range` | coordenada além de ±1e9 |
| `invalid coordinates` | NaN/infinito nas coordenadas |

## 5. A chamada é bloqueante

Tanto `_seedfinder_crack` (até completar a fatia ou estourar `timeBudgetSec`)
quanto `_seedfinder_scan` (até terminar a varredura do raio) rodam
**sincronamente**, sem ceder o event loop. Consequências:

- **Nunca chame na main thread** — congela a UI.
- Sempre rode dentro de um **Web Worker**.
- O crack tem o budget (`timeBudgetSec`) como mecanismo de cooperação: com ele
  pequeno (ex.: 0.5–2 s por chamada), o worker responde/encerra rápido e o
  scheduler na main thread decide continuar, trocar de fatia ou cancelar.
- O scan **não tem budget** (é uma varredura única e determinística): pior caso
  medido ~370 ms (§3.0). É uma chamada só — sem fatiamento; basta rodá-la no
  worker e esperar a resposta.

## 6. Padrão de referência: workers-por-fatia

Contrato recomendado: a main thread divide `[start, end)` em fatias e enfileira
para N workers; cada worker mantém **uma** instância do módulo e atende fatias
em loop. Pseudo-código (verbatim do spec):

```js
// main thread: fila de fatias [start,end) -> N workers
const SLICE = 1n << 26n; // 67M seeds por fatia (ajustável)
const workers = Array.from({length: navigator.hardwareConcurrency - 1}, () => {
  const w = new Worker(new URL("./crack.worker.js", import.meta.url),
                       { type: "module" });
  w.postMessage({ cmd: "init", structures, tolerance, maxResults, budget });
  return w;
});
// nextSlice() devolve a próxima fatia ou null; progresso = soma dos
// "checked" de cada resposta; cancelamento = parar de enfileirar +
// worker.terminate() (descartar resultados pendentes das fatias em voo).
// Agregação final: ordenar por score asc; fatias são disjuntas -> sem dedupe.
```

```js
// crack.worker.js: instancia UMA vez, atende fatias em loop
import createSeedfinderModule from "./seedfinder.js";
const m = await createSeedfinderModule();
self.onmessage = (e) => {
  // malloc -> HEAP32/HEAPF64 -> _seedfinder_crack(bigint, bigint, ..., 1)
  // -> UTF8ToString -> JSON.parse -> _seedfinder_free_result -> _free
  // -> postMessage({checked, results, timed_out})
  // (o mesmo worker pode atender cmd:"scan" — ver §10.3)
};
```

Notas:

- Fatias disjuntas ⇒ resultados não se sobrepõem, não há dedupe a fazer;
  apenas concatene e ordene por `score` (asc) — e por `seed` como desempate.
- Para fatias **parciais** (cancelamento/timeout): o JSON de uma fatia
  interrompida por `timeBudgetSec` vem com `"timed_out": true` e
  `checked` reflete o quanto andou — some mesmo assim.
- `SLICE = 1n << 26n` (~67M seeds) é um bom default: com ~2M seeds/s por
  core (seção 8, SIMD128), cada fatia leva ~33 s; reduza se quiser
  progresso/retorno mais fino.

## 7. Limites e validação do motor

| Entrada | Limite | Erro se violar |
|---|---|---|
| `numTypes` | 4–24 | `at least 4 structures are required` / `too many structures (max 24)` |
| IDs de estrutura | 1–11, 13, 14, 23, 24 | `unknown structure type` |
| Tipo 15 (Mineshaft) | **rejeitado sempre** (RNG por-chunk, não é region-crackable) | `Mineshaft is not supported by SeedCracker` |
| `tolerance` | 0–8 chunks | `tolerance must be between 0 and 8 chunks` |
| `maxResults` | 1–2000 | `maxResults must be between 1 and 2000` |
| `startSeed`/`endSeed` | `start < end` (32 bits) | `empty seed range` |
| Coordenadas (blocos) | **±1e9** | `coordinates out of range` / `invalid coordinates` (NaN/inf) |

**Full-range 64-bit (span ≥ 2³²) tem um guard próprio.** O stage 4 (resolução dos
16 bits altos) custa `#s48 × 2^16 × ~150 µs` (`applySeed` + gate de bioma), então
âncoras Java aproximadas são inviáveis: medido, `tolerance` 2 → ~1 h; 3 → ~9 h;
6 → anos. O motor mede o yield numa amostra e devolve **na hora**:

```
{"error":"unbounded 64-bit crack is infeasible with approximate Java anchors:
 ~N 48-bit candidates (X.XX/lo32) x 65536 high-bit lifts (est. Ns > Ns left) -
 pass a start/end window (high bits fixed) or exact Trial Chambers/Trail Ruins
 coordinates"}
```

Com âncoras exatas (`tolerance: 0`) o yield cai para ~1 candidato e o full-range
completa (stage A ~5–7 min em 6 threads nativas; no WASM SIMD (4-lane),
multiplique pelo nº de fatias/workers da seção 6). Um pedido **bounded** (span < 2³²) segue
barato (~1 s com 4 Trial Chambers) e é o caminho recomendado.

## 8. Performance

- Caminho WASM vetorizado com **WASM SIMD128**: `core/crack_simd.h` mapeia o
  mesmo núcleo `crackScoreSimd` para 4 lanes (WASM SIMD128) ou 8 lanes (AVX2),
  e `CRACK_GROUPS=4` roda 4 grupos independentes para esconder a latência do
  `i32x4.mul`. O build usa `-msimd128` (compile + link).
- Benchmark real medido neste repo (`node wasm/test_wasm.mjs`, 16M seeds,
  `CRACK_GROUPS=4`): **≈2,06M seeds/s** (2.063.946 checked/s best-of-3, mediana
  ~2.056M), Node 24.19, Windows, single-thread. Baseline escalar (sem
  `-msimd128`): ~1.046M checked/s — ganho **≈2×**.
- Ainda assim, **~2–4× mais lento por core** que o AVX2 nativo (que também roda
  `CRACK_GROUPS=4`) — a varredura completa de 32 bits (2³² ≈ 4,29 bi seeds) leva
  ~35 min em 1 core com SIMD, **mitigada por N cores** (seção 6): 8 workers
  ⇒ ~5 min.
- Estratégia recomendada no cliente: budget por chamada (seção 5) + fatias
  (seção 6). O `tolerance` baixo e estruturas com poucas regiões candidatas
  filtram muito mais rápido — a ordem das estruturas importa pouco para o
  chamador (o motor já ordena internamente as targets).

## 9. Deploy Vercel

- Copie **`wasm/out/*` como estão** (arquivos estáticos, ex.: `public/wasm/`).
  Nada de build especial.
- **Zero headers especiais**: não há `-pthread` nem `SharedArrayBuffer`, então
  **não** precisa de COOP/COEP nem de `crossOriginIsolated`.
- `Content-Type` de `.wasm` (`application/wasm`) já está correto na Vercel —
  nada a configurar.
- O glue é ES module (`-sEXPORT_ES6=1`): o import funciona nativamente no
  navegador e em workers com `{ type: "module" }`.
- Para aplicar isso no repositório do site (com código e checklist), siga a
  **seção 10**.

## 10. Integrar no repositório do site (`mineseedfinder` / mineseedfinder.vercel.app)

Esta seção documenta como o WASM está integrado no repo do site
(mineseedfinder), que é um projeto separado e apenas *consome* estes arquivos.
Estado atual:

- A rota `/seedcracker` hospedada devolve `"unavailable"` (rodar o crack no
  servidor é caro demais na Vercel); o WASM resolve isso levando a quebra para
  o navegador — **já implementado no site** (`static/wasm/crack.worker.js`).
- O **scan** também roda no navegador: o console `/seedfinder` chama o WASM
  (worker, §10.3) com a validação JS da §10.4 e usa `fetch("/scan")` apenas
  como fallback; a rota HTTP permanece para consumidores de API e o demo da
  home.
- A rota `/scan` **permanece no servidor** para consumidores de API (curl, docs,
  o módulo Lua) e para o demo da home — **não a remova**. Só o console
  `/seedfinder` muda de transporte.

### 10.1 Descobrir o framework e o diretório público

Antes de copiar, identifique o framework do site:

| Sinal no repo | Framework | Onde colocar |
|---|---|---|
| `app/` + `templates/` + `index.py` | **Flask** (**é o caso do mineseedfinder**) | `static/wasm/` (servido em `/static/wasm/`) |
| `next.config.js/ts/mjs` | Next.js | `public/wasm/` |
| `vite.config.*` | Vite | `public/wasm/` |
| `astro.config.*` | Astro | `public/wasm/` |
| só `index.html` + `package.json` | estático | `public/wasm/` (crie) |

No Flask, tudo em `static/` é servido em `/static/`:
`static/wasm/seedfinder.js` → `/static/wasm/seedfinder.js`. **Não** importe o
glue pelo bundler; carregue por URL em runtime (ver 10.3) — é o que evita ter
de mexer em config.

### 10.2 Copiar os arquivos (migração do rename)

Copie, do repo do motor (este), os **dois** arquivos para o site:

```
wasm/out/seedfinder.js    ->  <site>/static/wasm/seedfinder.js    (Flask; public/wasm/ em SPA)
wasm/out/seedfinder.wasm  ->  <site>/static/wasm/seedfinder.wasm
```

**Migração de um site que já tem o par antigo** (o caso do mineseedfinder):

1. Copiar `seedfinder.js` + `seedfinder.wasm` para `static/wasm/`.
2. **Apagar** os antigos `static/wasm/seedcracker.js` e
   `static/wasm/seedcracker.wasm` (o glue novo procura `seedfinder.wasm` ao
   lado de si; sobrar o par velho só confunde a cópia manual).
3. Atualizar o import no worker existente
   (`static/wasm/crack.worker.js:18`):
   `/static/wasm/seedcracker.js` → `/static/wasm/seedfinder.js`.
4. Atualizar as cópias locas de `INTEGRATION.md` e `test_wasm.mjs` em
   `static/wasm/` (são espelhos deste documento/referência executável).

Regras do par:

- Os dois andam **juntos e na mesma pasta**: o `.js` resolve `seedfinder.wasm`
  relativo à própria URL (`/static/wasm/seedfinder.js` →
  `/static/wasm/seedfinder.wasm`).
- Não renomeie nenhum dos dois.
- Sugestão de automação: um script `sync-wasm` que copia de um diretório
  vendor (ou baixa de um release) para `static/wasm/`, para o par nunca ficar
  dessincronizado.

Exports usados pelo cliente (o glue já expõe): `_seedfinder_crack`,
`_seedfinder_crack64_shim`, **`_seedfinder_scan`**, **`_seedfinder_scan_java`**,
`_seedfinder_free_result`, `_malloc`, `_free`.

### 10.3 Worker (`static/wasm/crack.worker.js`)

Colocar o worker em `static/`/`public/` evita configurar bundler para workers.
Ele guarda **uma** instância do módulo e atende `init`, fatias de crack e
**`scan`** (o scan é uma chamada única, sem fatiamento).

```js
// static/wasm/crack.worker.js  — ES module worker
let m = null, cfg = null;

self.onmessage = async (e) => {
  const msg = e.data;
  try {
    if (msg.cmd === "init") {
      if (!m) {
        // import dinâmico de URL absoluta: o comentário faz webpack/vite ignorarem
        const mod = await import(/* webpackIgnore: true */ /* @vite-ignore */ "/static/wasm/seedfinder.js");
        m = await mod.default({ locateFile: (f) => new URL(f, "/static/wasm/").href });
      }
      cfg = msg;
      self.postMessage({ type: "ready" });
      return;
    }
    if (msg.cmd === "scan") {
      // msg: { seed, x, z, radius, max, types } — já validados na main thread
      // (§10.4). O C não valida: só envie o que passou pela validação.
      const out = callScan(m, msg);
      self.postMessage({ type: "scan-done", out });
      return;
    }
    if (msg.cmd === "slice") {
      const out = cfg.mode === "64"
        ? callCrack64(m, cfg.mt, cfg.java, cfg.tolerance, msg.start, msg.end, cfg.maxResults, cfg.budget)
        : callCrack(m, cfg.structures, cfg.tolerance, msg.start, msg.end, cfg.maxResults, cfg.budget);
      self.postMessage({ type: "done", out });
      return;
    }
  } catch (err) {
    self.postMessage({ type: "fatal", error: String((err && err.message) || err) });
  }
};

// Scan (§3.0): BigInt no seed, free obrigatório, HEAP32 fresco.
function callScan(m, p) {
  const n = p.types.length;
  const pT = m._malloc(n * 4);
  let ptr = 0;
  try {
    const heap32 = m.HEAP32;
    for (let i = 0; i < n; i++) heap32[(pT >> 2) + i] = p.types[i];
    ptr = m._seedfinder_scan(BigInt(p.seed), p.x, p.z, p.radius, p.max, pT, n);
    return JSON.parse(m.UTF8ToString(ptr));
  } finally {
    if (ptr) m._seedfinder_free_result(ptr);
    m._free(pT);
  }
}

function callCrack(m, structures, tolerance, startSeed, endSeed, maxResults, budget) {
  const n = structures.length;
  const pT = m._malloc(n * 4), pX = m._malloc(n * 8), pZ = m._malloc(n * 8);
  let ptr = 0;
  try {
    for (let i = 0; i < n; i++) {
      m.HEAP32[(pT >> 2) + i] = structures[i].type;
      m.HEAPF64[(pX >> 3) + i] = structures[i].x;
      m.HEAPF64[(pZ >> 3) + i] = structures[i].z;
    }
    ptr = m._seedfinder_crack(pT, n, pX, pZ, tolerance,
      BigInt(startSeed), BigInt(endSeed), maxResults, budget, 1);
    return JSON.parse(m.UTF8ToString(ptr));
  } finally {
    if (ptr) m._seedfinder_free_result(ptr);
    m._free(pT); m._free(pX); m._free(pZ);
  }
}

function callCrack64(m, mt, java, tolerance, startSeed, endSeed, maxResults, budget) {
  const n = mt.length, j = java.length;
  const pT = m._malloc(n * 4), pX = m._malloc(n * 8), pZ = m._malloc(n * 8);
  const pjT = m._malloc(j * 4), pjX = m._malloc(j * 8), pjZ = m._malloc(j * 8);
  let ptr = 0;
  try {
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
    ptr = m._seedfinder_crack64_shim(pT, pX, pZ, n, pjT, pjX, pjZ, j,
      tolerance, BigInt(startSeed), BigInt(endSeed), maxResults, budget, 1);
    return JSON.parse(m.UTF8ToString(ptr));
  } finally {
    if (ptr) m._seedfinder_free_result(ptr);
    m._free(pT); m._free(pX); m._free(pZ);
    m._free(pjT); m._free(pjX); m._free(pjZ);
  }
}
```

### 10.4 Validação em JS — espelho fiel da rota `/scan` (obrigatório)

O C do scan **não valida nada** (§3.0): defaults, clamps, `missing_or_invalid`
e mensagens de erro existiam só no servidor
(`mineseedfinder/app/api.py::_scan`). Para o console responder **exatamente
como responde hoje**, toda essa lógica foi para o cliente. Este helper é um
espelho literal — mesma ordem de checagens, mesmas mensagens, mesmo shape:

```js
// static/wasm/scan_validate.js — espelho de app/api.py::_scan (site)
// Limites vêm do template (Jinja já injeta nos inputs):
//   radius: input.min/max = {{ min_radius }}/{{ max_radius }}
//   max:    input.min/max = {{ min_results }}/{{ max_results }}
// (defaults do .env: radius 0–200, max 0–100). Nunca hardcode.

export function validateScan(raw, limits) {
  // raw: {seed, x, z, radius, max, types} — strings vindas dos inputs
  //       (types = CSV das checkboxes; vazio/ausente = default "5")
  const missing = [];
  const strip = (v) => String(v ?? "").trim();

  // ---- espelho de _arg(): vazio => default + registro em missing ----
  const arg = (name, def, cast) => {
    const s = strip(raw[name]);
    if (!s) { missing.push(name); return def; }
    const v = cast(s);
    if (v === null) throw { py: `invalid value for '${name}': '${s}'` };
    return v;
  };

  // int() do Python: sinal, dígitos com underscores ("1_000"), sem 0x/0b/0o.
  // BigInt para não perder precisão (o C recebe uint64).
  const pyInt = (s) => {
    if (/^[+-]?0[xXbBoO]/.test(s)) return null;
    const t = s.replace(/_/g, "");
    if (!/^[+-]?\d+$/.test(t)) return null;
    return BigInt(t);
  };
  // float() do Python: dígitos/underscores/exponente + tokens nan/inf.
  const pyFloat = (s) => {
    const sign = /^[+-]/.test(s) ? s[0] : "";
    const body = sign ? s.slice(1) : s;
    if (/^nan$/i.test(body)) return NaN;
    if (/^(inf|infinity)$/i.test(body)) return sign === "-" ? -Infinity : Infinity;
    if (/^[+-]?0[xXbBoO]/.test(s)) return null;
    const t = s.replace(/_/g, "");
    if (!/^[+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?$/.test(t)) return null;
    return Number(t);
  };

  let seed, x, z, radius, max, typesStr;
  try {
    seed = arg("seed", 0n, pyInt) & 0xFFFFFFFFFFFFFFFFn; // int & mask (aceita negativo)
    x = arg("x", 0, pyFloat);
    z = arg("z", 0, pyFloat);
    radius = arg("radius", 100, pyInt);
    max = arg("max", 20, pyInt);
    typesStr = strip(raw.types) || "5";       // NÃO entra em missing (igual ao Python)
  } catch (e) {
    if (e.py) return { error: `Invalid parameter: ${e.py}`, missing_or_invalid: missing };
    throw e;
  }

  // ---- coordenadas finitas e dentro de ±1e9 ----
  const COORD_LIMIT = 1_000_000_000;
  if (!(Number.isFinite(x) && Number.isFinite(z)
        && Math.abs(x) <= COORD_LIMIT && Math.abs(z) <= COORD_LIMIT)) {
    return { error: `x and z must be finite coordinates within ±${COORD_LIMIT}`,
             missing_or_invalid: [...missing, "x", "z"] };
  }

  // ---- mínimos rejeitados, máximos truncados ----
  if (radius < BigInt(limits.minRadius)) {
    return { error: `radius must be at least ${limits.minRadius}`,
             missing_or_invalid: [...missing, "radius"] };
  }
  if (max < BigInt(limits.minResults)) {
    return { error: `max must be at least ${limits.minResults}`,
             missing_or_invalid: [...missing, "max"] };
  }
  if (radius > BigInt(limits.maxRadius)) radius = BigInt(limits.maxRadius);
  if (max > BigInt(limits.maxResults)) max = BigInt(limits.maxResults);

  // ---- tipos ----
  const types = [];
  for (const t of typesStr.split(",")) {
    const s = t.trim();
    if (!s) continue;
    const v = pyInt(s);
    if (v === null) {
      return { error: "types must be comma-separated integers",
               missing_or_invalid: [...missing, "types"] };
    }
    types.push(Number(v));
  }
  if (!types.length) return { results: [] };            // 200 com lista vazia
  if (types.length > 64) {
    return { error: "too many types (max 64)",
             missing_or_invalid: [...missing, "types"] };
  }

  // OK => parâmetros prontos para o worker (cmd: "scan")
  return { seed: seed.toString(), x, z,
           radius: Number(radius), max: Number(max), types };
}
```

Detalhes de paridade (todos verificados contra `api.py`):

| Comportamento do servidor | No cliente |
|---|---|
| `missing_or_invalid` só nas **respostas de erro** (o 200 não traz) | idem: só nos objetos `{error}` |
| Ordem: parse → coordenadas → mínimos → clamps → tipos | mesma ordem (a primeira falha vence) |
| `types` **nunca** entra em `missing` por omissão (default `"5"`) | idem — checkbox desmarcada ⇒ `["5"]` ⇒ villages only |
| `types` vazio que sobrevive ao parse (ex. `","`) ⇒ `{"results": []}` | idem (200, sem `missing_or_invalid`) |
| seed negativo ⇒ `& 0xFFFFFFFFFFFFFFFF` | `BigInt & maskn` (dois-complemento, idem) |
| `radius`/`max` acima do cap ⇒ clamp (200/100) | idem, com os limites **lidos do template** |
| 503 (`.so` não carregado) | análogo: módulo WASM falhou ao carregar ⇒ `{error, hint, results: [], missing_or_invalid}` e **fallback** (abaixo) |

**Troca no console (`templates/seedfinder.html`)** — hoje:

```js
const res = await fetch(url);        // url = buildUrl() => "/scan?seed=..."
const data = await res.json();
if (res.status === 503 || !data.results) { /* render error */ }
```

**Depois:**

```js
const params = validateScan({ seed, x, z, radius, max, types: typesCsv }, LIMITS);
if (params.error) {                      // espelho do 400: mesmo texto de hoje
  return render(`<p class="results-error">${params.error}</p>`);
}
if (params.results) {                    // types parseado vazio => []
  return render("<p class='results-empty'>No structures found in this area.</p>");
}
// OK: delega ao worker (não trava a main thread — §5)
worker.postMessage({ cmd: "scan", ...params });
// em "scan-done": out tem {results:[...]} — o renderer atual já serve, pois
// ele só lê data.results / data.error.
```

- **Fallback (recomendado)**: se o worker responder `fatal` (`.wasm` 404/offline)
  ou o módulo não inicializar em ~5 s, volte para `fetch(buildUrl())` — a rota
  `/scan` continua viva no servidor, então a UI nunca fica morta (e na prática
  o custo Vercel só ocorre nesse fallback).
- **`buildUrl()` continua existindo**: o console exibe a URL da API para copiar
  (campo `/scan?...`) e a página de docs não muda — a API pública permanece.
- **`LIMITS` sem hardcode**: a rota `/seedfinder` já injeta os caps do `.env`
  nos atributos `min`/`max` dos inputs (`pages.py`). Basta ler do DOM:

```js
const num = (el, attr, def) => {
  const v = Number(document.getElementById(el)?.getAttribute(attr));
  return Number.isFinite(v) ? v : def;
};
const LIMITS = {
  minRadius:  num("radius", "min", 0),  maxRadius:  num("radius", "max", 200),
  minResults: num("max", "min", 0),     maxResults: num("max", "max", 100),
};
```

  Assim um deploy que mude `SEEDFINDER_MAX_RADIUS`/`…_MAX_RESULTS` no `.env`
  continua valendo no cliente sem editar JS.
- **Texto da UI**: o FAQ do console diz "calls the hosted API directly";
  atualize para "runs entirely in your browser" (e o demo da home **permanece**
  com `fetch("/scan")` — decisão de manter a API pública testável).
- **Teste de paridade** (ver §10.7): para ~10 combinações de inputs (válidos e
  inválidos), compare objeto a objeto a resposta do `validateScan` com a do
  servidor `GET /scan` mesmos params — devem ser idênticas.

### 10.5 Scheduler (main thread)

Crie em qualquer lugar do app (não precisa ir para `public/`):

```js
// A main thread fatia [start,end) e distribui para N workers.
export function startCrack(opts, { onProgress, onResult, onError }) {
  const SLICE = 1n << 26n;                 // ~67M seeds por fatia
  const n = Math.max(1, (navigator.hardwareConcurrency || 4) - 1);
  const workers = [], results = [];
  let next = opts.start, checked = 0n, cancelled = false;

  const nextSlice = () => {
    if (cancelled || next >= opts.end) return null;
    const s = next;
    const e = (s + SLICE < opts.end) ? s + SLICE : opts.end;
    next = e;
    return [s, e];
  };

  const finishIfDone = () => {
    if (!cancelled && next >= opts.end && workers.every(w => !w.busy)) {
      workers.forEach(w => w.worker.terminate());
      onResult(results);
    }
  };

  const dispatch = (w) => {
    const s = nextSlice();
    if (!s) { w.busy = false; finishIfDone(); return; }
    w.busy = true;
    w.worker.postMessage({ cmd: "slice", start: s[0], end: s[1] });
  };

  for (let i = 0; i < n; i++) {
    const worker = new Worker("/static/wasm/crack.worker.js", { type: "module" });
    const w = { worker, busy: false };
    worker.onmessage = (e) => {
      const msg = e.data;
      if (msg.type === "ready") return dispatch(w);
      if (msg.type === "fatal") {
        cancelled = true; workers.forEach(x => x.worker.terminate()); return onError(msg.error);
      }
      const out = msg.out;                       // "done"
      if (out.error) {
        cancelled = true; workers.forEach(x => x.worker.terminate()); return onError(out.error);
      }
      checked += BigInt(out.checked || 0);
      if (out.results) results.push(...out.results);
      onProgress && onProgress({ checked, timedOut: !!out.timed_out });
      dispatch(w);
    };
    worker.postMessage({ cmd: "init", ...opts });
    workers.push(w);
  }

  return { cancel() { cancelled = true; workers.forEach(w => w.worker.terminate()); } };
}
```

Regras do scheduler:

- **Fatias disjuntas** ⇒ concatene e ordene por `score` asc (desempate por `seed`
  ou `seed_str`). Não há dedupe a fazer.
- Fatias com `timed_out: true` são parciais: **some o `checked` mesmo assim**.
- `SLICE = 1n << 26n` (~33 s/core na velocidade SIMD128) equilibra progresso e
  retorno; reduza para progresso mais fino.
- **Ordem final**: só ordene no `onResult` final; durante o progresso, apenas
  exiba os melhores parciais.

### 10.6 Modo 64-bit no site

- O modo é do **64-bit shim** (`_seedfinder_crack64_shim`): arrays `mt` +
  `java`, e o retorno tem `"bits":64` e **`seed_str`**. Sempre identifique o
  candidato por `seed_str` (o `seed` numérico perde precisão acima de 2^53).
- **Armadilha do fatiamento**: o guard de full-range (§7) só dispara quando uma
  *chamada* tem `span ≥ 2^32`. Se a UI fatiar um range enorme em fatias de
  `2^26`, **cada fatia é bounded e o guard nunca dispara** — a busca rodaria para
  sempre. Portanto, **limite a janela total do 64-bit na UI** (ex.: ≤ `2^32`) e
  mostre uma estimativa de tempo; não deixe o usuário disparar `[0, 2^63)`.
- Se `out.error` vier com `"infeasible with approximate Java anchors"`, mostre a
  mensagem como orientação: "use coordenadas exatas de Trial Chambers/Trail
  Ruins ou uma janela `start/end`" (não é bug).
- Faixa recomendada para o 64-bit: `span < 2^32` com 4+ Trial Chambers
  (`tolerance: 0` se as coordenadas forem exatas; senão o guard orienta).

### 10.7 Checklist de verificação (executado na migração; reexecute após qualquer rebuild do par WASM)

**Migração do par (rename):**

1. **Referência executável** (no repo do motor): `node wasm/test_wasm.mjs` →
   `ALL WASM TESTS PASSED`. Isso valida o par `.js/.wasm` (scan + crack) antes
   de copiar.
2. **Cópia**: confirmar que `static/wasm/seedfinder.js` **e**
   `static/wasm/seedfinder.wasm` existem, que os antigos `seedcracker.*` foram
   **apagados**, que o import do worker aponta para `/static/wasm/seedfinder.js`
   e que o `.wasm` é servido com `Content-Type: application/wasm`
   (Network tab, ou `curl -I https://<site>/static/wasm/seedfinder.wasm`).

**Crack (já existente, não pode regredir):**

3. **Smoke no navegador**: com o fixture de 4 estruturas
   `[{5,-280,152},{5,-280,-360},{8,696,360},{8,712,760}]`, `tolerance:0`,
   `start=8675309-1000n`, `end=8675309+1000n` → deve conter `seed: 8675309`.
4. **UI não trava**: a chamada roda no worker (main thread livre). Confirme
   girando um contador/animação durante a busca.
5. **Sem vazamento**: rode o mesmo smoke 20×; a memória do módulo
   (`ALLOW_MEMORY_GROWTH=1`) não deve crescer indefinidamente — se crescer, algum
   `_seedfinder_free_result`/`_free` está faltando.
6. **Erros felizes**: 3 estruturas, Mineshaft (15), `tolerance: 9` e range vazio
   devem retornar `{"error": ...}` (não lançar) e continuar rodando.

**Scan (novo):**

7. **Paridade da validação** (o item mais importante): para ~10 casos — válidos
   (defaults ausentes, radius acima do cap, seed negativo, types CSV) e
   inválidos (`x=abc`, `x=inf`, `radius=-1`, `max=-5`, `types=1,2,x`,
   `types=,,,,`, 65 types) — compare a resposta do `validateScan` com
   `GET /scan?<mesmos params>` no servidor: **mesmo `error`, mesma ordem de
   `missing_or_invalid`, mesmo clamp, mesmo JSON de sucesso**.
8. **Ground truth**: no console com seed `6666`, posição (0,0), radius 100,
   só Village → as 8 vilas reais aparecem e as 3 células de rio
   (`264,-296`, `-776,-872`, `776,-1368`) **não** aparecem (mesma regressão de
   `server/tests/test_scan_village_groundtruth.py`).
9. **Console sem rede**: com o DevTools Network aberto, um Scan do console não
   gera request para `/scan` (só o wasm `.js`/`.wasm` na carga). Se o WASM
   falhar, o **fallback** `fetch("/scan")` dispara (testa bloqueando
   `seedfinder.wasm` no Network → o scan ainda funciona via servidor).
10. **UI não trava no scan**: pior caso (radius 200, todos os tipos, ~370 ms)
    roda no worker — contador/animação segue liso.
11. **Sem vazamento no scan**: 20 scans repetidos no console (ou pelo worker
    direto) sem crescimento de heap — coberto pelo smoke #8 do
    `test_wasm.mjs`, repetir no navegador se mexer no free.
12. **Demo e API intactos**: o demo da home ainda busca `/scan` (server-side) e
    `curl "…/scan?seed=…"` continua respondendo — a rota não foi removida.

### 10.8 O que NÃO fazer

- **Não** rode crack **nem scan** na main thread (congela a UI) — sempre worker.
- **Não** adicione `COOP/COEP`, `SharedArrayBuffer` ou `crossOriginIsolated`:
  não são necessários (§9) e podem quebrar o resto do site.
- **Não** cacheie `m.HEAP32` / `m.HEAPF64` entre chamadas — memory growth
  invalida as views; pegue-as frescas a cada uso.
- **Não** passe `number` para `startSeed`/`endSeed` (crack) nem para `seed`
  (scan) — sempre `BigInt`.
- **Não** esqueça `_seedfinder_free_result(ptr)` (inclusive em erro) nem os
  `_free` dos buffers de entrada.
- **Não** pule a validação do scan: o C **não valida** — enviar radius negativo
  ou types malformado direto ao `_seedfinder_scan` é como ter erro 400/hang no
  servidor, mas **sem** resposta de erro (§10.4 é obrigatória).
- **Não** reative a busca server-side em `/seedcracker` nem remova a rota
  `/scan`: o objetivo do scan-WASM é tirar o **custo por Scan** da Vercel, não
  derrubar a API pública (docs, curl, Lua e demo continuam batendo nela).
- **Não** renomeie/mova só um dos dois arquivos do par nem deixe os antigos
  `seedcracker.*` junto dos novos.

## 11. Pegadinhas

1. **`startSeed`/`endSeed` (crack) e `seed` (scan) são BigInt obrigatórios**
   (`WASM_BIGINT`). Passar `number` quebra silenciosamente o ABI — sempre
   `BigInt(start)`, `BigInt(end)` / literais `0n`, `0x100000000n`, e no scan
   `BigInt(seedStr) & 0xFFFFFFFFFFFFFFFFn` (a máscara também converte seed
   negativo, igual ao Python).
2. **`UTF8ToString(ptr)` antes do `_seedfinder_free_result(ptr)`** — depois do
   free o ponteiro é inválido. E o free é obrigatório também para erros.
3. **Views `HEAP32`/`HEAPF64` podem ser invalidadas por memory growth**
   (`ALLOW_MEMORY_GROWTH=1` está ligado). Pegue `m.HEAPF64` / `m.HEAP32`
   **fresco a cada chamada**, nunca cacheie a view. `_malloc` durante o setup
   também pode disparar growth — escreva na view *depois* do malloc, usando a
   referência atual.
4. **O scan não valida entrada** — diferente do crack (que devolve
   `{"error": ...}`), `_seedfinder_scan` com radius negativo/types malformados
   só obedece ao que recebe (o Python rejeitava isso com 400). A validação do
   §10.4 é parte do contrato, não opcional.
5. **`mcLabel` no `_seedfinder_scan_java`**: passe `0` (NULL) — é o que o
   servidor faz hoje; qualquer string inválida cai em `MC_NEWEST`
   silenciosamente.
6. **Variante UMD**: se o consumidor não usa ES modules, reconstrua com
   `-sEXPORT_ES6=0` (o resto das flags idêntico) e o glue vira UMD
   (`require`/global).
7. `numThreads` **sempre 1** — o paralelismo é JS (workers-por-fatia), não do
   motor.
