# root CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(DispersedEngine VERSION 1.0.0 LANGUAGES CXX)
 
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
# ── Options ────────────────────────────────────────────────────────────────
option(BUILD_ENGINE  "Build the Editor executable (src/main.cpp)" ON)
 
# ── Dependencies (resolved by vcpkg) ──────────────────────────────────────
find_package(SDL3        REQUIRED CONFIG)
find_package(SDL3_image  REQUIRED CONFIG)
find_package(SDL3_mixer  REQUIRED CONFIG)
find_package(SDL3_ttf    REQUIRED CONFIG)
find_package(box2d       REQUIRED CONFIG)
 
# ── Engine interface library + optional Editor ─────────────────────────────
add_subdirectory(src)
 
# ── Projects ───────────────────────────────────────────────────────────────
# Each project under projects/ has its own CMakeLists.txt that decides
# which script(s) to compile into its executable.
add_subdirectory(projects)
