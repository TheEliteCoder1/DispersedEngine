# projects/PingPong/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

set(PROJECT_SOURCES
    scripts/game.cpp
)

add_executable(PingPong ${PROJECT_SOURCES})

# Tell engine.h we are building a project script
target_compile_definitions(PingPong PRIVATE PROJECT_SCRIPT_BUILD=1)

target_link_libraries(PingPong PRIVATE DispersedEngine)

target_include_directories(PingPong PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts
)

# ── Post-build: copy the ENTIRE project folder next to the executable ────────
# This creates Release/PingPong/ containing assets/, scenes/, scripts/, etc.
# Now the exe can just use "./PingPong/..." to find everything!
add_custom_command(TARGET PingPong POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}
        $<TARGET_FILE_DIR:PingPong>/PingPong
    COMMENT "Copying entire PingPong folder next to executable"
)

# ── Platform settings ──────────────────────────────────────────────────────
if(EMSCRIPTEN)
    set_target_properties(PingPong PROPERTIES SUFFIX ".html")
    target_link_options(PingPong PRIVATE
        "-sALLOW_MEMORY_GROWTH=1"
        "-sUSE_WEBGL2=1"
        "-sASYNCIFY"
        "-sFORCE_FILESYSTEM"
        # Mount the entire project folder to /PingPong in the virtual FS
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}@/PingPong"
        "--shell-file=${CMAKE_SOURCE_DIR}/minimal_shell.html"
    )
else()
    set_target_properties(PingPong PROPERTIES
        SUFFIX ".exe"
    )
endif()