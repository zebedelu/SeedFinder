# Benchmark nativo × WASM (motor SeedCracker)

F0 do app SeedCracker: quanto o motor custa em cada runner (`.dll` nativo vs
build WASM) para decidir qual promessa pública os números sustentam.

- **Data:** 2026-10-18 · **Branch:** `feat/f0-bench` · **Commit medido:** `ff7f85b`
  (+ a correção de formatação `seed_str` em `core/crack_mitm.c` do working tree,
  que não afeta taxa).
- **Máquina:** Windows, **6 núcleos**, Python 3.13.12, Node v26.7.0.
- **Artefato nativo:** `build_server/seedfinder_lib.dll` (824.021 B, reconstruído
  para esta medição). **Artefato WASM (produção):** `wasm/out/seedfinder.wasm`
  (566.780 B).

## Metodologia

- Fixtures reais com seed conhecida (`server/bench_fixtures.py`); nenhuma foi
  gerada pelo próprio cracker. Fonte de cada uma: Chunkbase / fixtures de teste
  do repo (`bedrock32` = seed 8675309; `auto64` = 4294972605; `java_mitm` =
  4294967300).
- `--repeat N` + **mediana por taxa**: cada fase roda N vezes e a linha reportada
  é a de taxa mediana (empate → menor tempo). A tabela principal usa `--repeat 3`.
- Janela padrão de **`2^26` candidatos** (67.108.864) para medir taxa e
  extrapolar o espaço completo. Uma linha com `timed_out` é marcada como
  **parcial** e **não** entra em extrapolação.
- Threads registradas por linha: nativo = `cpu_count - 1` = **5**; WASM de
  produção = **1**. Comparar sem isso é desonesto.
  - **A coluna `threads` registra o valor PEDIDO, não o efetivo.** O build de
    produção do WASM não tem `-pthread`: o motor **clampa para 1 thread** em
    silêncio (o `#if defined(__EMSCRIPTEN__)` de `core/*.c` força
    `numThreads = 1`). Rodar o runner de produção com `--threads 5` produz uma
    linha que **diz** `threads=5` e roda a 1 thread (verificado: janela `2^24`
    em 15,94 s = 1.052.771/s, velocidade de 1 thread).
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
# Saída bruta (JSON + markdown) em build_bench/ (gitignored); as conclusões
# permanentes são este documento.

# Experimento threaded do WASM (build experimental):
# wasm/CMakeLists.txt é untracked por design, então a flag NÃO está no git.
# Toolchain: MSYS2 UCRT64 emscripten (C:\msys64\ucrt64\lib\emscripten).
cmake -S wasm -B wasm/build -DCMAKE_BUILD_TYPE=Release -DSEEDFINDER_WASM_PTHREAD=ON -G "MinGW Makefiles"
cmake --build wasm/build -j 6
# O RUNTIME_OUTPUT_DIRECTORY é fixo em wasm/out: o build experimental SOBRESCREVE
# o de produção (não há wasm/out-pthread/). Restaurar depois:
cmake -S wasm -B wasm/build -DCMAKE_BUILD_TYPE=Release -DSEEDFINDER_WASM_PTHREAD=OFF -G "MinGW Makefiles"
cmake --build wasm/build -j 6
```

O `-pthread` liga e roda, com **um** warning (não erro):
`-pthread + ALLOW_MEMORY_GROWTH may run non-wasm code slowly`. Artefatos:
`seedfinder.js` 38.252 → 71.363 B, `seedfinder.wasm` 566.780 → 587.253 B. No
browser, threads exigem `SharedArrayBuffer`, que exige os headers **COOP/COEP**
(`Cross-Origin-Opener-Policy: same-origin` + `Cross-Origin-Embedder-Policy:
require-corp`) no site. O Node do benchmark não precisa deles.

## Resultados

Tabela principal — saída real de `python server/bench.py --repeat 3 --wasm`
(produção: nativo 5T, WASM 1T). Coluna final = tempo estimado para o espaço
completo, **extrapolado** da taxa medida (`--full` **não** foi executado):

| runner | fixture | fase | unidade | threads | checked | tempo (s) | taxa/s | espaço completo (extrapolado) |
|---|---|---|---|---|---|---|---|---|
| native | bedrock32 | crack32 | seeds | 5 | 67108864 | 26.50 | 2,532,416 | ~1.696 s (28,3 min) |
| native | auto64 | sweep | seeds | 5 | 67108864 | 21.30 | 3,151,239 | ~1.363 s (22,7 min) |
| native | auto64 | lift48 | candidates | 5 | 2000 | 0.19 | 10,639 | n/a (não é varredura) |
| native | auto64 | lift48_rank1 | candidates | 5 | 0 | 0.00 | **skipped: engine error: too many 48-bit candidates (3229) - add more Trial Chambers/Trail Ruins or enter their corner coordinates more precisely** | n/a |
| native | auto64 | lift63 | hi | 5 | 0 | 0.00 | **skipped: sem s48 da fase 3 - lift63 nao roda** | n/a |
| native | java_mitm | mitm | u | 1 | 65536 | 10.71 | 6,120 | ~2.741 s (45,7 min)¹ |
| wasm | bedrock32 | crack32 | seeds | 1 | 67108864 | 67.07 | 1,000,519 | ~4.292 s (71,5 min) |
| wasm | auto64 | sweep | seeds | 1 | 67108864 | 52.13 | 1,287,416 | ~3.335 s (55,6 min) |
| wasm | auto64 | lift48 | candidates | 1 | 2000 | 0.25 | 8,124 | n/a (não é varredura) |
| wasm | auto64 | lift48_rank1 | candidates | 1 | 0 | 0.00 | **skipped: too many 48-bit candidates (3229) - add more Trial Chambers/Trail Ruins or enter their corner coordinates more precisely** | n/a |
| wasm | auto64 | lift63 | hi | 1 | 0 | 0.00 | **skipped: sem s48 da fase 3 - lift63 nao roda** | n/a |
| wasm | java_mitm | mitm | u | 1 | 65536 | 39.93 | 1,641 | ~10.222 s (170 min)¹ |

¹ `java_mitm` mede a janela de timing `U ∈ [2^16, 2^17)`, isenta de acerto. Em
`--full` a seed verdadeira (U = 256) causa early-exit, então a extrapolação
**superestima** o tempo end-to-end real.

Tabela do experimento threaded (build **`SEEDFINDER_WASM_PTHREAD=ON`**, mediana
de 3 execuções; **não** é o build de produção):

| runner | fixture | fase | threads (efetivas) | checked | tempo (s) | taxa/s |
|---|---|---|---|---|---|---|
| wasm (pthread) | bedrock32 | crack32 | 5 | 67108864 | 17.82 | 3,765,873 |
| wasm (pthread) | bedrock32 | crack32 | 8 | 67108864 | 16.93 | 3,964,428 |
| wasm (pthread) | auto64 | sweep | 5 | 67108864 | 13.15 | 5,104,508 |
| wasm (pthread) | auto64 | lift48 | 1 | 2000 | 0.25 | 8,124 |

Paridade de resultado (não só velocidade): `_seedfinder_crack` com `threads=1` e
`threads=8` devolve **mesmo `checked` e mesmos `results`** na janela `[0, 16e6)`
(que contém a seed 8675309) — o particionamento por threads está correto.

## Leitura

Fatores derivados (produção, o que o usuário sente hoje):

- **Fator total `bedrock32`** (WASM 1T ÷ nativo 5T): `67.07 / 26.50 = 2,53×` — o
  WASM de produção é ~2,5× mais lento end-to-end.
- **Fator total `java_mitm`** (ambos 1T): `39.93 / 10.71 = 3,73×` — o WASM é
  ~3,7× mais lento (o MITM não é paralelizado em nenhum dos lados).
- **Fator por thread** (isola SIMD/codegen): `(2.532.416 / 5) / 1.000.519 =
  0,506` → **o WASM é ~1,98× mais rápido POR THREAD**. A vantagem nativa vem
  inteiramente do paralelismo, não do motor.
  - Caveat: aqui "nativo por thread" = taxa de 5T ÷ 5 (inclui overhead de
    threading). Uma medição direta de nativo 1T (Task 4) deu 961.935/s → fator
    por thread ~1,24×.
- **Teste `-pthread`:** **passou** (liga e roda; warning de memory-growth é
  benigno). Com o build pthread o WASM **supera** o nativo na mesma máquina:
  `5T 3,77M/s` e `8T 3,96M/s` vs `nativo 5T 2,53M/s` (1,49× / 1,57×). No sweep do
  auto64: `wasm 5T 5,10M/s` vs `nativo 5T 3,15M/s` (1,62×).

**Conclusão.** A promessa pública *"baixe porque é mais rápido"* **não é
sustentada pelos dados**. O WASM já é mais rápido **por thread** (SIMD128 vs
AVX2/codegen nativo), e o nativo só ganha end-to-end **porque usa 5 threads** —
com o build `-pthread` o WASM iguala e passa o nativo na mesma máquina. A
vantagem nativa é **threading, não o motor**.

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
  **extrapolados** da taxa da janela `2^26` e estão marcados como tal; o
  `java_mitm` ainda tem o viés de early-exit da nota ¹.
- **Timings de máquina única** (6 núcleos) e **variam com o número de núcleos**;
  o nativo usa `cpu_count - 1`, o WASM de produção usa 1.
- As fases do `auto` **não são apples-to-apples** por thread: o nativo roda
  `lift48`/`lift63` com N threads, enquanto o runner WASM mantém **1 thread**
  nessas fases (só `crack32` e `sweep` recebem o argumento `threads`).
- **Follow-up (não é falha do F0):** `wasm/test_wasm.mjs` (untracked, 02/10) usa
  a ABI **antiga** de tolerância escalar e quebra com o commit `980b406`
  (tolerância por estrutura). Falha idêntica com o core atual reconstruído →
  pré-existente; atualizar/espelhar é trabalho separado.
