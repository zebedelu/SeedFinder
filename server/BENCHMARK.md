# Benchmark nativo × WASM (motor SeedCracker)

F0 do app SeedCracker: quanto o motor custa em cada runner (`.dll` nativo vs
build WASM) para decidir qual promessa pública os números sustentam.

- **Data:** 2026-10-18 · **Branch:** `feat/f0-bench` · **Commit medido:** `ff7f85b`.
- **Procedência do artefato nativo (auditoria):** a
  `build_server/seedfinder_lib.dll` medida foi construída de um **working tree com
  alteração NÃO commitada** em `core/crack_mitm.c` (formatação do `seed_str`,
  signed). Por isso **este registro não é reproduzível apenas a partir dos fontes
  commitados** — um build do HEAD puro sai byte-diferente. A alteração é **neutra
  em taxa** nos caminhos medidos: só muda o texto de `seed_str` de um hit do MITM,
  e a janela de timing medida é isenta de acerto (nenhum hit é serializado).
- **Máquina:** Windows, **6 núcleos**, Python 3.13.12, Node v26.7.0.
- **Artefato nativo:** `build_server/seedfinder_lib.dll` (824021 B, reconstruído
  para esta medição). **Artefato WASM (produção):** `wasm/out/seedfinder.wasm`
  (566780 B).
- **Convenção numérica:** `.` = separador decimal e inteiros **sem** separador de
  milhar (os dígitos ficam todos juntos). Nenhuma vírgula aparece dentro de um
  número, em nenhum lugar deste documento — assim `~1696 s` só pode ser lido como
  mil seiscentos e noventa e seis segundos, nunca como "1.7 s". **É o documento
  que normaliza, não a ferramenta:** `python server/bench.py` continua imprimindo
  a taxa com separador de milhar (vírgula, via `{rate:,.0f}` em `bench.py`); as
  tabelas abaixo são essa saída real com os separadores de milhar removidos à mão.
  Os valores são os medidos.

## Metodologia

- Fixtures reais com seed conhecida (`server/bench_fixtures.py`); nenhuma foi
  gerada pelo próprio cracker. Fonte de cada uma: Chunkbase / fixtures de teste
  do repo (`bedrock32` = seed 8675309; `auto64` = 4294972605; `java_mitm` =
  4294967300).
- `--repeat N` + **mediana por taxa**: cada fase roda N vezes e a linha reportada
  é a de taxa mediana (empate → menor tempo). A tabela principal usa `--repeat 3`.
- **Janela de medição:** `2^26` candidatos (67108864) para `bedrock32` e
  `auto64`. O `java_mitm` **não** usa `2^26`: mede a janela de timing
  `U ∈ [2^16, 2^17)` (largura `2^16`, isenta de acerto), porque a varredura real
  do MITM é sobre `U = seed >> 24`, não sobre a seed. Uma linha com `timed_out`
  é marcada como **parcial** e **não** entra em extrapolação.
- Threads registradas por linha: nativo = `cpu_count - 1` = **5**; WASM de
  produção = **1**. Comparar sem isso é desonesto.
  - **A coluna `threads` registra o valor PEDIDO, não o efetivo.** O build de
    produção do WASM não tem `-pthread`: o motor **clampa para 1 thread** em
    silêncio (o `#if defined(__EMSCRIPTEN__)` de `core/*.c` força
    `numThreads = 1`). Pedir 5 threads ao build de produção produz uma linha que
    **diz** `threads=5` e roda a 1 thread (sonda do clamp abaixo).
  - As linhas com `threads > 1` no lado WASM vêm do build **experimental**
    `SEEDFINDER_WASM_PTHREAD=ON` (ver *Reprodução*); **não** é o build que o site
    serve.
- `dll_bytes`/`dll_mtime` no JSON detectam lib velha ou build `-O0`; o
  `wasm_mtime` é conferido contra o `core/` mais novo — nativo novo contra WASM
  velho invalida a comparação (o bench avisa). Nesta medição **não houve aviso**.
- **Semântica de `space`** (ambíguo no JSON): em `bedrock32` e `auto64` o
  `space` = `2^32` é o espaço da **varredura de seeds de 32 bits**, não os 64
  bits do modo auto; em `java_mitm` o `space` = `2^24` é o espaço de `U =
  seed >> 24`. As extrapolações da tabela usam o `space` da própria fixture.

### Reprodução

```bash
# Tabela principal (nativo 5T + WASM de produção 1T):
python server/bench.py --repeat 3 --wasm
# Referência 1-thread do nativo (para o fator por thread):
python server/bench.py --fixtures bedrock32 --threads 1 --repeat 3
# Saída bruta (JSON + markdown) em build_bench/ (gitignored); as conclusões
# permanentes são este documento.

# Experimento threaded do WASM (build experimental):
# wasm/CMakeLists.txt é untracked por design, então a flag NÃO está no git.
# Toolchain: MSYS2 UCRT64 emscripten (C:\msys64\ucrt64\lib\emscripten).
cmake -S wasm -B wasm/build -DCMAKE_BUILD_TYPE=Release -DSEEDFINDER_WASM_PTHREAD=ON -G "MinGW Makefiles"
cmake --build wasm/build -j 6
# Linhas threaded (o --wasm-threads vira --threads N no runner node; sem o flag o
# argv fica igual ao de sempre, ou seja 1 thread):
python server/bench.py --fixtures bedrock32 --wasm --wasm-threads 5 --repeat 3
python server/bench.py --fixtures bedrock32 --wasm --wasm-threads 8 --repeat 3
python server/bench.py --fixtures auto64   --wasm --wasm-threads 5 --repeat 3
# O RUNTIME_OUTPUT_DIRECTORY é fixo em wasm/out: o build experimental SOBRESCREVE
# o de produção (não há wasm/out-pthread/). Restaurar depois:
cmake -S wasm -B wasm/build -DCMAKE_BUILD_TYPE=Release -DSEEDFINDER_WASM_PTHREAD=OFF -G "MinGW Makefiles"
cmake --build wasm/build -j 6

# Sonda do clamp (build de PRODUÇÃO): pede 5 threads e mostra a linha com
# threads=5 rodando a 1 thread:
python server/bench.py --fixtures bedrock32 --wasm --wasm-threads 5 --repeat 1 --window 0x1000000
```

O `-pthread` liga e roda, com **um** warning (não erro):
`-pthread + ALLOW_MEMORY_GROWTH may run non-wasm code slowly`. Artefatos:
`seedfinder.js` 38252 → 71363 B, `seedfinder.wasm` 566780 → 587253 B. No
browser, threads exigem `SharedArrayBuffer`, que exige os headers **COOP/COEP**
(`Cross-Origin-Opener-Policy: same-origin` + `Cross-Origin-Embedder-Policy:
require-corp`) no site. O Node do benchmark não precisa deles.

## Resultados

Tabela principal — saída real de `python server/bench.py --repeat 3 --wasm`
(produção: nativo 5T, WASM 1T). O CLI ainda imprime a taxa com separador de
milhar (vírgula); aqui esses separadores foram removidos à mão para respeitar a
convenção numérica (ver acima) — os valores são literais, a pontuação não. Coluna
final = tempo estimado para o espaço completo, **extrapolado** da taxa medida
(`--full` **não** foi executado):

| runner | fixture | fase | unidade | threads | checked | tempo (s) | taxa/s | espaço completo (extrapolado) |
|---|---|---|---|---|---|---|---|---|
| native | bedrock32 | crack32 | seeds | 5 | 67108864 | 26.50 | 2532416 | ~1696 s (28.3 min) |
| native | auto64 | sweep | seeds | 5 | 67108864 | 21.30 | 3151239 | ~1363 s (22.7 min) |
| native | auto64 | lift48 | candidates | 5 | 2000 | 0.19 | 10639 | n/a (não é varredura) |
| native | auto64 | lift48_rank1 | candidates | 5 | 0 | 0.00 | **skipped: engine error: too many 48-bit candidates (3229) - add more Trial Chambers/Trail Ruins or enter their corner coordinates more precisely** | n/a |
| native | auto64 | lift63 | hi | 5 | 0 | 0.00 | **skipped: sem s48 da fase 3 - lift63 nao roda** | n/a |
| native | java_mitm | mitm | u | 1 | 65536 | 10.71 | 6120 | ~2741 s (45.7 min)¹ |
| wasm | bedrock32 | crack32 | seeds | 1 | 67108864 | 67.07 | 1000519 | ~4292 s (71.5 min) |
| wasm | auto64 | sweep | seeds | 1 | 67108864 | 52.13 | 1287416 | ~3335 s (55.6 min) |
| wasm | auto64 | lift48 | candidates | 1 | 2000 | 0.25 | 8124 | n/a (não é varredura) |
| wasm | auto64 | lift48_rank1 | candidates | 1 | 0 | 0.00 | **skipped: too many 48-bit candidates (3229) - add more Trial Chambers/Trail Ruins or enter their corner coordinates more precisely** | n/a |
| wasm | auto64 | lift63 | hi | 1 | 0 | 0.00 | **skipped: sem s48 da fase 3 - lift63 nao roda** | n/a |
| wasm | java_mitm | mitm | u | 1 | 65536 | 39.93 | 1641 | ~10222 s (170 min)¹ |

¹ `java_mitm` mede a janela de timing `U ∈ [2^16, 2^17)`, isenta de acerto. Em
`--full` a seed verdadeira (U = 256) causa early-exit, então a extrapolação
**superestima** o tempo end-to-end real. Além disso, **extrapolar a coluna do
MITM é um descasamento de modelo, não uma medição:** o `checked` que o motor
reporta nessa fase é `N + nU`, com `N = 2^24` (construção da LUT, custo fixo,
`core/crack_mitm.c`, linhas 307 e 438) e `nU = 65536` (a janela) — ~257× maior que a largura
de janela que o bench registra (`checked = end_u - start_u = 65536`). A taxa do
bench já carrega esse custo fixo no denominador, e a extrapolação o multiplica por
256, como se a LUT fosse reconstruída 256 vezes; o `--full` real constrói a LUT
**uma** vez. Trate as células `~2741 s` e `~10222 s` como ordem de grandeza, não
como medição.

Referência **1-thread** (para isolar SIMD/codegen do paralelismo). Nativo de
`python server/bench.py --fixtures bedrock32 --threads 1 --repeat 3`; o WASM 1T
é a linha de produção da tabela acima:

| runner | fixture | fase | threads | checked | tempo (s) | taxa/s |
|---|---|---|---|---|---|---|
| native | bedrock32 | crack32 | 1 | 67108864 | 91.57 | 732859 |
| wasm | bedrock32 | crack32 | 1 | 67108864 | 67.07 | 1000519 |

Tabela do experimento threaded — saída real de
`python server/bench.py --fixtures … --wasm --wasm-threads N --repeat 3`, no
build **`SEEDFINDER_WASM_PTHREAD=ON`** (**não** é o build de produção). A coluna
`threads` é o valor pedido **e** efetivo neste build:

| runner | fixture | fase | threads | checked | tempo (s) | taxa/s |
|---|---|---|---|---|---|---|
| wasm (pthread) | bedrock32 | crack32 | 5 | 67108864 | 13.84 | 4849320 |
| wasm (pthread) | bedrock32 | crack32 | 8 | 67108864 | 16.38 | 4096021 |
| wasm (pthread) | auto64 | sweep | 5 | 67108864 | 13.47 | 4980286 |
| wasm (pthread) | auto64 | lift48 | 1 | 2000 | 0.19 | 10762 |

Par **nativo** dos mesmos comandos (o lado nativo roda junto, no mesmo
`--repeat 3`; é a origem de `2826388/s` e `4041523/s` usados na *Leitura*):

| runner | fixture | fase | threads | checked | tempo (s) | taxa/s |
|---|---|---|---|---|---|---|
| native | bedrock32 | crack32 | 5 | 67108864 | 23.74 | 2826388 |
| native | bedrock32 | crack32 | 5 | 67108864 | 23.97 | 2799513 |
| native | auto64 | sweep | 5 | 67108864 | 16.60 | 4041523 |

As duas linhas de `bedrock32` são os lados nativos do comando de 5 threads
(`2826388/s`, pareado com o WASM 5T) e do de 8 threads (`2799513/s`, pareado com o
WASM 8T); só o par de 5T entra nos fatores.

O `8T` **não** é melhor que o `5T` aqui: o pool pede 8 threads em 6 núcleos
(oversubscribe) e o resultado varia entre execuções — o ganho relevante é a 5T.

Sonda do **clamp** — build de produção, mesmo comando pedindo 5 threads:

| runner | fixture | fase | threads | checked | tempo (s) | taxa/s |
|---|---|---|---|---|---|---|
| wasm (produção) | bedrock32 | crack32 | 5 | 16777216 | 12.47 | 1345260 |

A linha **diz** `threads=5` e entrega ~1.3M/s (classe de 1 thread), contra
4849320/s do build pthread com o mesmo `--wasm-threads 5`. Confirma que o
valor pedido não é o efetivo.

**Paridade de resultado** (não só velocidade) — `build_bench/thread_parity.mjs`
no build pthread, `_seedfinder_crack` na janela `[0, 16e6)` (contém a seed
8675309), `threads=1` vs `threads=8`:

```
t1: threads=1 checked=16000000 results=[{"seed":8675309, "score":0, "matches":[[-18, 9], [-18, -23], [43, 22], [44, 47]]}]
t8: threads=8 checked=16000000 results=[{"seed":8675309, "score":0, "matches":[[-18, 9], [-18, -23], [43, 22], [44, 47]]}]
identical(results+checked): true
```

(Evidência com um espaço após cada vírgula — JSON equivalente — para respeitar a
convenção numérica; os valores são os da execução real.)

Mesmo `checked`, mesmos `results` → o particionamento por threads está correto.

## Leitura

Fatores derivados (produção, o que o usuário sente hoje). Todos saem de números
impressos acima (tabela principal, referência 1-thread, tabela threaded e o par
nativo dela):

- **Fator total `bedrock32`** (WASM 1T ÷ nativo 5T): `67.07 / 26.50 = 2.53×` — o
  WASM de produção é ~2.5× mais lento end-to-end.
- **Fator total `java_mitm`** (ambos 1T): `39.93 / 10.71 = 3.73×` — o WASM é
  ~3.7× mais lento (o MITM não é paralelizado em nenhum dos lados).
- **Fator por thread — número headline (medido direto, 1T vs 1T):**
  `1000519 / 732859 = 1.37×` — o WASM já ganha do nativo com **uma** thread
  (linhas da referência 1-thread acima). É o único número por-thread **medido**,
  sem modelo.
- **Número normalizado por threading** (fórmula do brief
  `(native/s) / threads / (wasm/s)`): `(2532416 / 5) / 1000519 = 0.506` → WASM
  ~1.98× "por thread". **Isto não é um speedup por thread medido:** pressupõe que
  o nativo 5T escala linearmente a partir do 1T (`5 × 732859 = 3664295/s`), e os
  dados **contradizem** essa premissa — o nativo 5T real é `2532416/s`, só ~3.5× o
  1T. Serve para separar threading de codegen, não como promessa de velocidade por
  thread. A vantagem nativa end-to-end vem do paralelismo, não do motor.
  - Os números de 1 thread variam entre execuções na mesma máquina: medições
    anteriores deste harness (mesma máquina) deram nativo 1T **961935/s** e WASM
    1T **~1186903/s** → `1.23×`; o menor par já visto é
    `1000519 / 961935 = 1.04×`. Ou seja: **WASM ≥ nativo por thread
    (1.04×–1.37× conforme a execução)**; a direção se manteve em todas as
    execuções, a magnitude não (ver *Limitações*).
- **Teste `-pthread`:** **passou** (liga e roda; warning de memory-growth é
  benigno). Com o build pthread o WASM **supera** o nativo na mesma máquina. No
  `bedrock32`, pareando o mesmo comando: WASM 5T `4849320/s` vs nativo 5T
  `2826388/s` → `1.72×`; no sweep do `auto64`: `4980286/s` vs `4041523/s` →
  `1.23×` (o par nativo está na tabela threaded acima). O `8T` fica em
  `4096021/s` (oversubscribe de 6 núcleos).

**Conclusão.** A promessa pública *"baixe porque é mais rápido"* **não é
sustentada pelos dados**. O WASM já é mais rápido **por thread** (SIMD128 vs
AVX2/codegen nativo: 1.04×–1.37× nas medições), e o nativo só ganha end-to-end
**porque usa 5 threads** — com o build `-pthread` o WASM iguala e passa o nativo
na mesma máquina. A vantagem nativa é **threading, não o motor**.

Os diferenciais reais do app (não velocidade):

- roda **offline** (sem depender do servidor);
- **sem limite de tempo** de requisição;
- **progresso** e **cancelar**;
- **sem teto de memória** por chamada;
- **sem throttling de aba** (browser em background congela timers/workers).

Se o site adotar o build pthread para fechar a diferença, precisa servir
**COOP/COEP** para `SharedArrayBuffer`.

## Limitações

- **Nenhuma medição real de `lift63`.** O motor recusa a fase com esta fixture:
  `too many 48-bit candidates (3229)` (4 Trial Chambers, tolerância 6, guard de
  1024). Isso **não** é falha do benchmark — é um **achado para a UI do app**:
  com poucas âncoras Java o modo 64-bit precisa pedir mais Trial Chambers/Trail
  Ruins (ou coords de canto mais precisas) em vez de parecer travado.
  `lift48_rank1` e `lift63` saem `skipped` dos dois lados.
- **`--full` não foi executado.** Os tempos de espaço completo são
  **extrapolados** da taxa da janela `2^26` (ou da janela de timing do MITM) e
  estão marcados como tal; o `java_mitm` ainda tem o viés de early-exit da nota ¹.
- **Timings de máquina única** (6 núcleos) e **variam entre execuções e com o
  número de núcleos** — observado: nativo 1T de 732859/s a 961935/s e WASM 1T de
  1000519/s a ~1186903/s. O nativo usa `cpu_count - 1`; o WASM de produção usa
  1.
- As fases do `auto` **não são apples-to-apples** por thread: o nativo roda
  `lift48`/`lift63` com N threads, enquanto o runner WASM mantém **1 thread**
  nessas fases (só `crack32` e `sweep` recebem o argumento `threads`).
- **Follow-up (não é falha do F0):** `wasm/test_wasm.mjs` (untracked, 02/10) usa
  a ABI **antiga** de tolerância escalar e quebra com o commit `980b406`
  (tolerância por estrutura). Falha idêntica com o core atual reconstruído →
  pré-existente; atualizar/espelhar é trabalho separado.
