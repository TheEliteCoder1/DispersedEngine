# root CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
# LANGUAGES CXX C: C is required alongside C++ because src/ufbx.c (the FBX
# importer, see src/CMakeLists.txt) is a plain C source file. Without C
# enabled here, CMake has no C compiler configured at all, and the ufbx
# target fails with "CMake can not determine linker language for target: ufbx".
project(DispersedEngine VERSION 1.0.0 LANGUAGES CXX C)
 
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
# ── Options ────────────────────────────────────────────────────────────────
option(BUILD_ENGINE  "Build the Editor executable (src/main.cpp)" ON)
 
# ── Dependencies (resolved by vcpkg) ──────────────────────────────────────
find_package(ZLIB        REQUIRED)
find_package(SDL3        REQUIRED CONFIG)
find_package(SDL3_image  REQUIRED CONFIG)
find_package(SDL3_mixer  REQUIRED CONFIG)
find_package(SDL3_ttf    REQUIRED CONFIG)
find_package(box2d       REQUIRED CONFIG)
find_package(Jolt        REQUIRED CONFIG)
find_package(glm         REQUIRED CONFIG)
find_package(tinyobjloader     REQUIRED CONFIG)
# ── ADDED: 3D model importers (engine3d.h's model_loaders_3d.h) ───────────
# fastgltf is an official vcpkg port (pulls in simdjson transitively,
# handled automatically -- no separate find_package needed for it).
find_package(fastgltf    REQUIRED CONFIG)
 
# ── Engine interface library + optional Editor ─────────────────────────────
add_subdirectory(src)
 
# ── Projects ───────────────────────────────────────────────────────────────
# Each project under projects/ has its own CMakeLists.txt that decides
# which script(s) to compile into its executable.
add_subdirectory(projects)
