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

target_include_directories(DispersedEngine INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}          # exposes engine.h, json.hpp, etc.
)

target_link_libraries(DispersedEngine INTERFACE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
    SDL3_ttf::SDL3_ttf
    box2d::box2d
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