# src/CMakeLists.txt
# ─────────────────────────────────────────────────────────────────────────────
# DispersedEngine  — INTERFACE (header-only) library.
#
# Any target that links against DispersedEngine automatically gets:
#   • src/ on its include path  →  #include "engine.h" / #include "json.hpp"
#   • All SDL3 / box2d link flags
#
# src/main.cpp is compiled ONLY into the Editor executable and is never
# part of the interface library, so project scripts never see it.
# ─────────────────────────────────────────────────────────────────────────────

add_library(DispersedEngine INTERFACE)

# ── ufbx: vendored single-file C library (src/ufbx.h + src/ufbx.c), not a
# vcpkg package -- there's no find_package() for it, so it has to be
# compiled directly from source. Without this, loadFbx() in engine3d.h
# compiles fine (it only needs the declarations in ufbx.h) but every
# ufbx_* symbol it calls is unresolved at link time, since ufbx.c -- the
# file that actually defines them -- was never part of any target.
add_library(ufbx STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ufbx.c
)
target_include_directories(ufbx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(DispersedEngine INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}          # exposes engine.h, json.hpp, etc.
)

target_link_libraries(DispersedEngine INTERFACE
    ZLIB::ZLIB
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
    SDL3_ttf::SDL3_ttf
    box2d::box2d  
    Jolt::Jolt 
    glm::glm 
    tinyobjloader::tinyobjloader
    # ── ADDED: 3D model importers, consumed by engine3d.h's
    # model_loaders_3d.h (glTF/GLB via fastgltf, FBX via ufbx). Both
    # are pulled through the INTERFACE library so every executable
    # that links DispersedEngine -- Editor and every project under
    # projects/ -- gets them automatically, same as glm/box2d/Jolt
    # above.
    fastgltf::fastgltf
    ufbx
)

# ── Editor executable (only when BUILD_ENGINE is ON) ──────────────────────
if(BUILD_ENGINE)
    add_executable(Editor
        ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp   # editor entry point only
    )

    target_link_libraries(Editor PRIVATE DispersedEngine)

    # Copy editor assets next to the Editor executable after build
    add_custom_command(TARGET Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:Editor>/assets
        COMMENT "Copying editor assets to output directory"
    )

    if(EMSCRIPTEN)
        set_target_properties(Editor PROPERTIES SUFFIX ".html")
        target_link_options(Editor PRIVATE
            "-sALLOW_MEMORY_GROWTH=1"
            "-sUSE_WEBGL2=1"
            "-sASYNCIFY"
            "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
            "--preload-file=${CMAKE_SOURCE_DIR}/projects@/projects"
            "--shell-file=${CMAKE_SOURCE_DIR}/minimal_shell.html"
            "-sFORCE_FILESYSTEM"
        )
        else()
        set_target_properties(Editor PROPERTIES
            SUFFIX          ".exe"
            WIN32_EXECUTABLE TRUE
        )
        target_link_options(Editor PRIVATE
            "/SUBSYSTEM:WINDOWS"
            "/ENTRY:mainCRTStartup"
        )
    endif()
endif()
