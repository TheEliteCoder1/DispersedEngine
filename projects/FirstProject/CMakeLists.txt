# projects/FirstProject/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

set(PROJECT_SOURCES
    scripts/MainMenuScript.cpp
)

add_executable(FirstProject ${PROJECT_SOURCES})

# Tell engine.h we are building a project script
target_compile_definitions(FirstProject PRIVATE PROJECT_SCRIPT_BUILD=1)

target_link_libraries(FirstProject PRIVATE DispersedEngine)

target_include_directories(FirstProject PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts
)

# ── Post-build: copy the ENTIRE project folder next to the executable ────────
# This creates Release/FirstProject/ containing assets/, scenes/, scripts/, etc.
# Now the exe can just use "./FirstProject/..." to find everything!
add_custom_command(TARGET FirstProject POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}
        $<TARGET_FILE_DIR:FirstProject>/FirstProject
    COMMENT "Copying entire FirstProject folder next to executable"
)

# ── Platform settings ──────────────────────────────────────────────────────
if(EMSCRIPTEN)
    set_target_properties(FirstProject PROPERTIES SUFFIX ".html")
    target_link_options(FirstProject PRIVATE
        "-sALLOW_MEMORY_GROWTH=1"
        "-sUSE_WEBGL2=1"
        "-sASYNCIFY"
        "-sFORCE_FILESYSTEM"
        # Mount the entire project folder to /FirstProject in the virtual FS
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}@/FirstProject"
        "--shell-file=${CMAKE_SOURCE_DIR}/minimal_shell.html"
    )
else()
    set_target_properties(FirstProject PROPERTIES
        SUFFIX ".exe"
    )
endif()