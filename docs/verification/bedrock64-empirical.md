# Validação empírica Bedrock 64-bit

Data: 2026-09-13 · Feature: SeedCrackerX 64-bit · Tarefa: Gate empírico (Task 1)

Fonte dos dados: **Chunkbase 26.0 (previsões web)** com seed `4294972605` (> 2³²,
= 5309 mod 2³²). A confirmação **in-game ainda está pendente** — coluna "jogo (1.21+)"
deve ser preenchida criando o mundo no jogo e anotando as coordenadas reais.

Coluna "/scan bate?" preenchida executando o `build_server/seedfinder_lib.dll` já
compilado via Flask test client:
`GET /scan?seed=4294972605&x=0&z=0&radius=1000&max=1000&types=1,2,3,4,5,24`
(+ um segundo scan centrado em -9128,-13352, raio 150, para a vila do deserto,
que fica a ~1011 chunks do spawn e não entra no raio máximo de 1000).
Critério de "bater": ponto de placement do /scan a ≤ 64 blocos da coordenada Chunkbase.

Trail Ruins / Outpost / Mansion / Shipwreck **excluídos** do gate: falsos positivos
conhecidos no /scan para esses tipos.

## Tabela de pontos

Nota de IDs: as etiquetas "Tipo" abaixo são os IDs numéricos da UI do Chunkbase/usuário.
O `/scan` e o crack usam os enums do cubiomes (village=5, desert_pyramid=1,
jungle_pyramid=2, swamp_hut=3, igloo=4, trial_chambers=24).

| Seed (64-bit) | Estrutura | Tipo (Chunkbase) | Chunkbase (x,z) blocos | jogo (1.21+) | /scan bate? |
|---|---|---|---|---|---|
| 4294972605 | village (Snowy) | 2 | 296, 232 | pendente | SIM (d=0) |
| 4294972605 | village (Taiga) | 4 | 760, 216 | pendente | NAO* |
| 4294972605 | village (Plains) | 1 | 1368, -392 | pendente | SIM (d=0) |
| 4294972605 | village (Savanna) | 3 | 1256, -5816 | pendente | SIM (d=0) |
| 4294972605 | village (Desert) | 5 | -9128, -13352 | pendente | SIM (d=0, exige scan re-centrado) |
| 4294972605 | trial chamber | 24 | -505, -281 | pendente | NAO** (d=266 ao ponto do /scan; placement 48-bit bate d≈13) |
| 4294972605 | trial chamber | 24 | 263, -311 | pendente | NAO** (d=96; placement 48-bit bate d≈13) |
| 4294972605 | trial chamber | 24 | -359, 199 | pendente | NAO** (d=139; placement 48-bit bate d≈13) |
| 4294972605 | trial chamber | 24 | 231, 169 | pendente | NAO** (d=126; placement 48-bit bate d≈13) |
| 4294972605 | desert temple | 8 | -3048, -296 | pendente | SIM (d=0) |
| 4294972605 | igloo | 6 | -280, 104 | pendente | SIM (d=0) |
| 4294972605 | jungle temple | 7 | 2584, -1288 | pendente | SIM (d=0) |
| 4294972605 | witch hut | 9 | 2136, -280 | pendente | SIM (d=0) |

\* **Taiga**: o placement do motor **bate exato** com o Chunkbase — probe direto
`getBedrockStructurePos(Village, MC_NEWEST, 4294972605, reg=(1,0))` → (760,216).
O /scan não reporta o ponto porque o filtro de viabilidade de bioma
(`isViableBedrockStructurePos`) rejeita a posição. É divergência do **filtro de
bioma do /scan** vs Chunkbase, não do placement MT — fora do escopo deste gate,
mas registrar para investigação futura.

\** **Trial Chambers**: divergência **esperada e esclarecedora** — o
`seedfinder_scan` atual trunca o seed para 32 bits (`seed & 0xFFFFFFFF` em
`core/seedfinder_wrapper.c:88`) antes de chamar o placement Java-style, então ele
calcula as posições com seed 5309, não com 4294972605. Alimentando o seed
48-bit completo direto em `getStructurePos(Trial_Chambers, MC_NEWEST, 4294972605, …)`
(probe `probe_tc.c`), os 4 pontos do Chunkbase aparecem todos a ≈13 blocos
(corner→centro de chunk) dos placement points: reg(-1,-1)→(-496,-272),
reg(0,-1)→(272,-320), reg(-1,0)→(-368,208), reg(0,0)→(240,160).
Isso é exatamente o gap que o SeedCrackerX 64-bit fecha.

## Invariantes automatizadas (core/test_invariants.c)

Compilar e rodar (a partir da raiz do repo; UCRT64 explícito, via Git bash):

```
& "C:\Program Files\Git\bin\bash.exe" -lc "cd build_server && /c/msys64/ucrt64/bin/gcc.exe -std=c11 -I.. -I../ChunkBiomesGUI -I../ChunkBiomesGUI/cubiomes ../core/test_invariants.c ../ChunkBiomesGUI/Bfinders.c ../ChunkBiomesGUI/cubiomes/biomes.c ../ChunkBiomesGUI/cubiomes/layers.c ../ChunkBiomesGUI/cubiomes/generator.c ../ChunkBiomesGUI/cubiomes/finders.c ../ChunkBiomesGUI/cubiomes/util.c ../ChunkBiomesGUI/cubiomes/noise.c ../ChunkBiomesGUI/cubiomes/biomenoise.c ../ChunkBiomesGUI/cubiomes/quadbase.c -lm -o test_invariants && ./test_invariants"
```

Resultado (2026-09-13, gcc UCRT64): **INVARIANTS_OK** (exit 0).

- **Invariante 1** — placement MT-based (tipos 1–14 via `getBedrockStructurePos`,
  que recebe `uint64_t` mas sementeia o MT com `seed & 0xFFFFFFFF` após
  `mix_seed`; aritmética inteira é congruente mod 2³²) é função apenas de
  seed mod 2³²: verificada para LOW=8675309 vs HIGH=LOW+2³² em regiões [-2,2]².
- **Invariante 2** — placement Java-style (`getStructurePos` →
  `getFeatureChunkInRegion`, que mascara com `(1<<48)-1`) depende apenas de
  seed mod 2⁴⁸: verificada para Trial Chambers com
  base=7777777777777777777 vs twin=base & 0xFFFFFFFFFFFF em regiões [-2,2]².

## Gate

Se /scan ou Chunkbase divergirem do **JOGO**, PARAR — o Brng/finders precisam de
reengenharia antes das Tasks 2+.

**Status: PROSSEGUE com ressalvas.** Placement do motor 100% alinhado com o
Chunkbase 26.0 (13/13 pontos batem no nível de placement; 8/13 aparecem via /scan —
as 5 divergências do /scan são explicadas: 4× truncamento 32-bit do scan para o
caminho Java-style (esperado — é exatamente o gap do feature) + 1× filtro de bioma
do scan na vila Taiga). Pendências antes de confiar no
gate empiricamente "vs jogo": (a) preencher a coluna "jogo (1.21+)" criando o
mundo com seed 4294972605; (b) confirmar se o jogo usa os bits 32–47 para as
trial chambers (tese do feature) ou se trunca como o /scan (tese oposta — nesse
caso o gate falha e as Tasks 2+ devem ser revistas).
