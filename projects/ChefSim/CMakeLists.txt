# projects/ChefSim/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

set(PROJECT_SOURCES
    scripts/MainMenuScript.cpp
)

add_executable(ChefSim ${PROJECT_SOURCES})

# Tell engine.h we are building a project script
target_compile_definitions(ChefSim PRIVATE PROJECT_SCRIPT_BUILD=1)

target_link_libraries(ChefSim PRIVATE DispersedEngine)

target_include_directories(ChefSim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts
)

# ── Post-build: copy the ENTIRE project folder next to the executable ────────
# This creates Release/ChefSim/ containing assets/, scenes/, scripts/, etc.
# Now the exe can just use "./ChefSim/..." to find everything!
add_custom_command(TARGET ChefSim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}
        $<TARGET_FILE_DIR:ChefSim>/ChefSim
    COMMENT "Copying entire ChefSim folder next to executable"
)

# ── Platform settings ──────────────────────────────────────────────────────────────
if(EMSCRIPTEN)
    set_target_properties(ChefSim PROPERTIES SUFFIX ".html")
    target_link_options(ChefSim PRIVATE
        "-sALLOW_MEMORY_GROWTH=1"
        "-sUSE_WEBGL2=1"
        "-sASYNCIFY"
        "-sFORCE_FILESYSTEM"
        # Mount the entire project folder to /ChefSim in the virtual FS
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}@/ChefSim"
        "--shell-file=${CMAKE_SOURCE_DIR}/minimal_shell.html"
    )
else()
    set_target_properties(ChefSim PROPERTIES
        SUFFIX ".exe"
    )
endif()
