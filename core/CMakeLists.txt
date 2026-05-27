cmake_minimum_required(VERSION 3.10)

# Point to the real project root
set(SEEDFINDER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

project(seedfinder_lib C)
set(CMAKE_C_STANDARD 11)

if(MINGW)
 add_compile_options(-Wall -Wextra)
 add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX SEEDFINDER_BRIDGE_SHARED)
endif()

# cubiomes as static (linked into our DLL)
add_library(cubiomes_static STATIC
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/biomes.c
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/layers.c
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/generator.c
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/finders.c
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/util.c
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/noise.c
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/biomenoise.c
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes/quadbase.c
)
target_include_directories(cubiomes_static PUBLIC
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes
)
target_compile_definitions(cubiomes_static PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Bfinders as static (linked into our DLL)
add_library(bfinders_static STATIC
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI/Bfinders.c
)
target_include_directories(bfinders_static PUBLIC
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI
)
target_compile_definitions(bfinders_static PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(bfinders_static PUBLIC cubiomes_static)

# Our shared library (DLL)
add_library(seedfinder_lib SHARED
 ${CMAKE_CURRENT_SOURCE_DIR}/seedfinder_wrapper.c
)
target_include_directories(seedfinder_lib PRIVATE
 ${SEEDFINDER_ROOT}
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI
 ${SEEDFINDER_ROOT}/ChunkBiomesGUI/cubiomes
)
target_link_libraries(seedfinder_lib PRIVATE bfinders_static cubiomes_static)

set_target_properties(seedfinder_lib PROPERTIES
 WINDOWS_EXPORT_ALL_SYMBOLS OFF
 POSITION_INDEPENDENT_CODE ON
 PREFIX ""
)

if(MINGW)
 target_link_options(seedfinder_lib PRIVATE -static-libgcc -static-libstdc++)
endif()
