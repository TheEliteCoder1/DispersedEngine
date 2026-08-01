# projects/OpenWorld/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

set(PROJECT_SOURCES
    scripts/game.cpp
)

add_executable(OpenWorld ${PROJECT_SOURCES})

# Tell engine.h we are building a project script
target_compile_definitions(OpenWorld PRIVATE PROJECT_SCRIPT_BUILD=1)

target_link_libraries(OpenWorld PRIVATE DispersedEngine)

target_include_directories(OpenWorld PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts
)

# ── Post-build: copy the ENTIRE project folder next to the executable ────────
# This creates Release/OpenWorld/ containing assets/, scenes/, scripts/, etc.
# Now the exe can just use "./OpenWorld/..." to find everything!
add_custom_command(TARGET OpenWorld POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}
        $<TARGET_FILE_DIR:OpenWorld>/OpenWorld
    COMMENT "Copying entire OpenWorld folder next to executable"
)

# ── Platform settings ──────────────────────────────────────────────────────
if(EMSCRIPTEN)
    set_target_properties(OpenWorld PROPERTIES SUFFIX ".html")

    # Web (default emcc) build was defaulting to -O0, which is far slower
    # than the optimized native Windows build — that gap looks like
    # "web vs native" choppiness but is really "unoptimized vs optimized".
    target_compile_options(OpenWorld PRIVATE -O3)
    target_compile_options(OpenWorld PRIVATE -fexceptions)
    target_link_options(OpenWorld PRIVATE -O3)


    target_link_options(OpenWorld PRIVATE
        "-sALLOW_MEMORY_GROWTH=1"
        "-sUSE_WEBGL2=1"
        "-sFORCE_FILESYSTEM"
        # Something in the engine (scene/JSON loading, etc.) throws a real
        # C++ exception during startup, so exception support is required.
        # Native Wasm exceptions (-fwasm-exceptions + -sSUPPORT_LONGJMP=wasm)
        # were tried, but the prebuilt vcpkg static libs (FreeType, libpng)
        # were built with the old JS-emulated SjLj/exception runtime and
        # aren't compatible with the new model at link time — the linker
        # keeps pulling in "-legacysjlj"/"-legacyexcept" runtime pieces that
        # don't provide emscripten_longjmp under the new model. Falling back
        # to the legacy JS-based catching here is what's actually compatible
        # with these prebuilt libraries without recompiling them.
        "-sNO_DISABLE_EXCEPTION_CATCHING"
        "-sEXCEPTION_CATCHING_ALLOWED=all"
        # Mount the entire project folder to /OpenWorld in the virtual FS.
        # (Previously trimmed this to just assets/+scenes/ to cut startup
        # stalls, but the scene loader also reads a resources/ folder
        # — resources/pad.physicsres, resources/ball.physicsres — at
        # runtime, resolved relative to the project root. Narrowing the
        # mount silently dropped that folder and broke physics-resource
        # loading. Mounting everything is what's actually correct here;
        # revisit trimming later only with a full list of what the engine
        # reads at runtime.)
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}@/OpenWorld"
        "--shell-file=${CMAKE_SOURCE_DIR}/minimal_shell.html"
    )
    # ASYNCIFY intentionally still omitted — only add it back if something
    # needs to yield mid-call (emscripten_sleep, blocking synchronous fetch).
else()
    set_target_properties(OpenWorld PROPERTIES
        SUFFIX ".exe"
    )
endif()