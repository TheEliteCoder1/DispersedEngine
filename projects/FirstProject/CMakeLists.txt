# projects/FirstProject/CMakeLists.txt
# ─────────────────────────────────────────────────────────────────────────────
# Builds the FirstProject runtime executable.
#
# Entry point  : scripts/MainMenuScript.cpp
#   • #include "engine.h"  works because DispersedEngine adds src/ to the
#     include path via its INTERFACE.
#   • src/main.cpp is NOT compiled here — it belongs to the Editor only.
#
# Assets / scenes are kept inside this project folder and are copied next
# to the executable after build so relative paths like "scenes/mainmenu.json"
# work at runtime without any hardcoded absolute paths.
# ─────────────────────────────────────────────────────────────────────────────

cmake_minimum_required(VERSION 3.20)

# ── Collect all script sources in this project ────────────────────────────
# Add more scripts here as your project grows:
#   set(PROJECT_SOURCES scripts/MainMenuScript.cpp scripts/GameplayScript.cpp)
set(PROJECT_SOURCES
    scripts/MainMenuScript.cpp
)

add_executable(FirstProject ${PROJECT_SOURCES})

# Inherit engine headers (src/) + all SDL3/box2d links
target_link_libraries(FirstProject PRIVATE DispersedEngine)

# The project's own scripts/ and assets/ directories are also on the
# include path so scripts can do: #include "SomeHelper.h"
target_include_directories(FirstProject PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts
)

# ── Post-build: copy project assets + scenes next to the executable ────────
# This means at runtime the exe can open "assets/fredoka.ttf",
# "scenes/mainmenu.json", etc. using plain relative paths.
add_custom_command(TARGET FirstProject POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:FirstProject>/assets
    COMMENT "Copying FirstProject assets"
)

add_custom_command(TARGET FirstProject POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/scenes
        $<TARGET_FILE_DIR:FirstProject>/scenes
    COMMENT "Copying FirstProject scenes"
)

# ── Platform settings ──────────────────────────────────────────────────────
if(EMSCRIPTEN)
    set_target_properties(FirstProject PROPERTIES SUFFIX ".html")
    target_link_options(FirstProject PRIVATE
        "-sALLOW_MEMORY_GROWTH=1"
        "-sUSE_WEBGL2=1"
        "-sASYNCIFY"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/scenes@/scenes"
        "--shell-file=${CMAKE_SOURCE_DIR}/emsdk/minimal_shell.html"
    )
else()
    set_target_properties(FirstProject PROPERTIES
        SUFFIX ".exe"
        # Remove WIN32_EXECUTABLE / ENTRY if you want a console window
        # during development for SDL_Log output.
        # WIN32_EXECUTABLE TRUE
    )
    # Uncomment the two lines below when releasing (hides the console):
    # set_target_properties(FirstProject PROPERTIES WIN32_EXECUTABLE TRUE)
    # target_link_options(FirstProject PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
endif()