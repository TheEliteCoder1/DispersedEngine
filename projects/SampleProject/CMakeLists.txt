# projects/SampleProject/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

set(PROJECT_SOURCES
    scripts/MainMenuScript.cpp
)

add_executable(SampleProject ${PROJECT_SOURCES})

# Tell engine.h we are building a project script
target_compile_definitions(SampleProject PRIVATE PROJECT_SCRIPT_BUILD=1)

target_link_libraries(SampleProject PRIVATE DispersedEngine)

target_include_directories(SampleProject PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts
)

# ── Post-build: copy the ENTIRE project folder next to the executable ────────
# This creates Release/SampleProject/ containing assets/, scenes/, scripts/, etc.
# Now the exe can just use "./SampleProject/..." to find everything!
add_custom_command(TARGET SampleProject POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}
        $<TARGET_FILE_DIR:SampleProject>/SampleProject
    COMMENT "Copying entire SampleProject folder next to executable"
)

# ── Platform settings ──────────────────────────────────────────────────────
if(EMSCRIPTEN)
    set_target_properties(SampleProject PROPERTIES SUFFIX ".html")
    target_link_options(SampleProject PRIVATE
        "-sALLOW_MEMORY_GROWTH=1"
        "-sUSE_WEBGL2=1"
        "-sASYNCIFY"
        "-sFORCE_FILESYSTEM"
        # Mount the entire project folder to /SampleProject in the virtual FS
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}@/SampleProject"
        "--shell-file=${CMAKE_SOURCE_DIR}/minimal_shell.html"
    )
else()
    set_target_properties(SampleProject PROPERTIES
        SUFFIX ".exe"
    )
endif()