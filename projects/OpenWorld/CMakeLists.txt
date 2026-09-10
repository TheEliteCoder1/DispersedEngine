cmake_minimum_required(VERSION 3.20)

set(PROJECT_SOURCES
    scripts/game.cpp
    scripts/losing_screen.cpp
    scripts/main_menu.cpp
    scripts/battle.cpp
)

add_executable(OpenWorld ${PROJECT_SOURCES})

target_compile_definitions(OpenWorld PRIVATE PROJECT_SCRIPT_BUILD=1 LOSING_SCREEN_STANDALONE=1 MAIN_MENU_STANDALONE=1)

target_link_libraries(OpenWorld PRIVATE DispersedEngine)

target_include_directories(OpenWorld PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts
)

add_custom_command(TARGET OpenWorld POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}
        $<TARGET_FILE_DIR:OpenWorld>/OpenWorld
    COMMENT "Copying entire OpenWorld folder next to executable"
)

if(EMSCRIPTEN)
    set_target_properties(OpenWorld PROPERTIES SUFFIX ".html")
    target_compile_options(OpenWorld PRIVATE -O3 -fexceptions)
    target_link_options(OpenWorld PRIVATE -O3)
    target_link_options(OpenWorld PRIVATE
        "-sALLOW_MEMORY_GROWTH=1"
        "-sUSE_WEBGL2=1"
        "-sFORCE_FILESYSTEM"
        "-sNO_DISABLE_EXCEPTION_CATCHING"
        "-sEXCEPTION_CATCHING_ALLOWED=all"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}@/OpenWorld"
        "--shell-file=${CMAKE_SOURCE_DIR}/minimal_shell.html"
    )
else()
    set_target_properties(OpenWorld PROPERTIES SUFFIX ".exe")
endif()