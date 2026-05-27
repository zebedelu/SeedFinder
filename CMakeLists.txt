cmake_minimum_required(VERSION 3.10)
project(SeedFinderBridge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option: build as static lib (for linking into Flarial DLL) or shared lib
option(SEEDFINDER_BRIDGE_SHARED "Build as shared library (DLL)" OFF)

# Include cubiomes as subdirectory (reuses ChunkBiomesGUI's cubiomes)
add_subdirectory(ChunkBiomesGUI/cubiomes)

# Bfinders library (from ChunkBiomesGUI)
add_library(bfinders STATIC
	"${CMAKE_CURRENT_SOURCE_DIR}/ChunkBiomesGUI/Bfinders.c"
)
target_include_directories(bfinders PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/ChunkBiomesGUI"
	"${CMAKE_CURRENT_SOURCE_DIR}/ChunkBiomesGUI/cubiomes"
)
target_link_libraries(bfinders PUBLIC cubiomes)

# The bridge library
set(bridge_sources
	"${CMAKE_CURRENT_SOURCE_DIR}/core/SeedFinderBridge.cpp"
)

if(SEEDFINDER_BRIDGE_SHARED)
	add_library(seedfinder_bridge SHARED ${bridge_sources})
else()
	add_library(seedfinder_bridge STATIC ${bridge_sources})
endif()

target_include_directories(seedfinder_bridge PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/core"
	"${CMAKE_CURRENT_SOURCE_DIR}/ChunkBiomesGUI"
	"${CMAKE_CURRENT_SOURCE_DIR}/ChunkBiomesGUI/cubiomes"
	# Flarial SDK Lua include path — adjust to your Flarial SDK location
	"$ENV{FLARIAL_SDK_PATH}/include"
	# MSYS2 MinGW64 Lua include path (for local development/testing)
	"C:/msys64/mingw64/include"
)

target_link_libraries(seedfinder_bridge PUBLIC bfinders cubiomes)

if(WIN32 AND SEEDFINDER_BRIDGE_SHARED)
	target_link_libraries(seedfinder_bridge PRIVATE -static-libgcc -static-libstdc++)
endif()