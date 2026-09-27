cmake_minimum_required(VERSION 3.10)

# Standalone WASM target — does not touch core/CMakeLists.txt.
set(SEEDFINDER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

project(seedfinder_wasm C)
set(CMAKE_C_STANDARD 11)

set(SEEDFINDER_SOURCES
  ${SEEDFINDER_ROOT}/core/seedfinder_wrapper.c
  ${SEEDFINDER_ROOT}/core/crack64.c
  ${SEEDFINDER_ROOT}/core/crack_mt.c
  ${SEEDFINDER_ROOT}/core/crack_auto.c
  ${SEEDFINDER_ROOT}/core/viability.c
  ${SEEDFINDER_ROOT}/ChunkBiomesGUI/Bfinders.c
  ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/biomes.c
  ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/layers.c
  ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/generator.c
  ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/finders.c
  ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/util.c
  ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/noise.c
  ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/biomenoise.c
  ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/quadbase.c
)

add_executable(seedfinder ${SEEDFINDER_SOURCES})

target_include_directories(seedfinder PRIVATE
  ${SEEDFINDER_ROOT}
  ${SEEDFINDER_ROOT}/ChunkBiomesGUI
  ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes
)

# WASM SIMD128: habilita o caminho crackScoreSimd (4 lanes). Sem isto,
# __wasm_simd128__ nao existe e o motor cai no escalar.
target_compile_options(seedfinder PRIVATE -msimd128 -flto -mbulk-memory)

# AVX2 stays off automatically: crack_simd.h only takes the x86 path on __x86_64__.
# Threads stay off: no -pthread; JS parallelizes via Web Workers per slice.
target_link_options(seedfinder PRIVATE
  # ponytail: link at -O1 so emcc skips the binaryen wasm-opt pass — the
  # MSYS2 binaryen build wedges at 0 CPU when spawned by emcc on Windows.
  # LLVM -O3 (from CMAKE_BUILD_TYPE=Release) does the heavy lifting anyway.
  -O1
  -msimd128
  -flto
  -mbulk-memory
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORTED_FUNCTIONS=_seedfinder_crack,_seedfinder_crack64_shim,_seedfinder_scan,_seedfinder_scan_java,_seedfinder_crack_auto_sweep,_seedfinder_crack_auto_lift48,_seedfinder_crack_auto_lift63,_seedfinder_free_result,_malloc,_free
  # HEAP64: os arrays uint64_t* do auto (lo32Seeds/s48Seeds) se leem via
  # BigInt64Array fresco por chamada (ver INTEGRATION.md §3.3-3.5).
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString,HEAP32,HEAPF64,HEAP64
  -sALLOW_MEMORY_GROWTH=1
  # CRACK_GROUPS>1 keeps several 401-word MT vectors per crack_mt_block call;
  # the default stack (64 KB) overflows at GROUPS=4.
  -sSTACK_SIZE=1048576
  -sENVIRONMENT=web,worker,node
)

set_target_properties(seedfinder PROPERTIES
  OUTPUT_NAME "seedfinder"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out"
)
