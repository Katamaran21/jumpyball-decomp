cmake_minimum_required(VERSION 3.16)
project(jumpyball C)

# The port is ANSI C (C89).  Extensions stay ON because snprintf is C99 and is
# only declared by the system headers in gnu89 mode.
set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(JUMPYBALL_WARNINGS "Enable the full warning set" ON)
option(JUMPYBALL_BUILD_TOOLS "Build tools/modtest" OFF)
option(JUMPYBALL_COPY_ASSETS "Copy BITMAP/Sounds/Musics next to the binary" ON)
option(JUMPYBALL_EMBED_ASSETS "Compile BITMAP/Sounds/Musics into the executable" OFF)

# sdl2 is the portable backend (jb_platform_sdl2.c).  win32 is the native
# Win32 / Windows CE one (jb_platform_win32.c): GDI for the screen, waveOut
# for sound, no SDL2 anywhere.
set(JUMPYBALL_BACKEND "sdl2" CACHE STRING "Host backend: sdl2 or win32")
set_property(CACHE JUMPYBALL_BACKEND PROPERTY STRINGS sdl2 win32)

set(JUMPYBALL_SOURCES
    jb_appassets.c
    jb_assets.c
    jb_audio.c
    jb_ball.c
    jb_bmp.c
    jb_gfx.c
    jb_gfx_tile.c
    jb_keyconfig.c
    jb_level.c
    jb_main.c
    jb_menu.c
    jb_mod.c
    jb_mod_effects.c
    jb_player.c
    jb_stage.c
    jb_text.c
    jb_track.c
    jb_trackrow_forest.c
    jb_trackrow_grass.c
    jb_trackrow_ice_alt.c
    jb_trackrow_sky.c
    jb_trackrow_tiled.c)

if(JUMPYBALL_BACKEND STREQUAL "win32")
    if(NOT WIN32)
        message(FATAL_ERROR
                "JUMPYBALL_BACKEND=win32 needs a Windows target.  Cross-compile "
                "with -DCMAKE_TOOLCHAIN_FILE=tools/toolchain-mingw-w64.cmake, or "
                "build the Windows CE binary with tools/build_wince.sh.")
    endif()
    list(APPEND JUMPYBALL_SOURCES jb_platform_win32.c jb_audio_win32.c)
elseif(JUMPYBALL_BACKEND STREQUAL "sdl2")
    list(APPEND JUMPYBALL_SOURCES jb_platform_sdl2.c jb_audio_sdl2.c
                                  jb_touch_sdl2.c)

    find_package(SDL2 CONFIG QUIET)
    if(NOT SDL2_FOUND)
        find_package(SDL2 QUIET)
    endif()
    if(NOT SDL2_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
        endif()
    endif()
    if(NOT SDL2_FOUND)
        message(FATAL_ERROR
                "SDL2 not found.  Install libsdl2-dev / sdl2 / vcpkg's sdl2, pass "
                "-DCMAKE_PREFIX_PATH=<sdl2 prefix>, or build the native Windows "
                "backend with -DJUMPYBALL_BACKEND=win32.")
    endif()
else()
    message(FATAL_ERROR "JUMPYBALL_BACKEND must be sdl2 or win32")
endif()

if(JUMPYBALL_EMBED_ASSETS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(JB_EMBED_DATA ${CMAKE_CURRENT_SOURCE_DIR}/jb_embed_data.c)
    file(GLOB JB_EMBED_INPUTS
         ${CMAKE_CURRENT_SOURCE_DIR}/BITMAP/*.bmp
         ${CMAKE_CURRENT_SOURCE_DIR}/Sounds/*
         ${CMAKE_CURRENT_SOURCE_DIR}/Musics/*)
    add_custom_command(
        OUTPUT ${JB_EMBED_DATA}
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_embed.py
                --root ${CMAKE_CURRENT_SOURCE_DIR} --out ${JB_EMBED_DATA}
        DEPENDS ${JB_EMBED_INPUTS}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_embed.py
        COMMENT "Embedding assets into jb_embed_data.c")
    list(APPEND JUMPYBALL_SOURCES jb_embed.c ${JB_EMBED_DATA})
    set(JUMPYBALL_COPY_ASSETS OFF)
endif()

add_executable(jumpyball WIN32 ${JUMPYBALL_SOURCES})

if(JUMPYBALL_EMBED_ASSETS)
    target_compile_definitions(jumpyball PRIVATE JB_EMBED)
endif()

if(JUMPYBALL_BACKEND STREQUAL "win32")
    target_compile_definitions(jumpyball PRIVATE JB_BACKEND_WIN32)
    target_link_libraries(jumpyball PRIVATE gdi32 user32 winmm)
    # The WIN32 target property asks for /SUBSYSTEM:WINDOWS, and MSVC then
    # looks for WinMain.  There is no SDL2main to supply one here, so point the
    # linker back at the CRT entry that calls main().
    if(MSVC)
        target_link_options(jumpyball PRIVATE /ENTRY:mainCRTStartup)
    endif()
else()
    if(TARGET SDL2::SDL2)
        target_link_libraries(jumpyball PRIVATE SDL2::SDL2)
        if(TARGET SDL2::SDL2main)
            target_link_libraries(jumpyball PRIVATE SDL2::SDL2main)
        endif()
    elseif(TARGET PkgConfig::SDL2)
        target_link_libraries(jumpyball PRIVATE PkgConfig::SDL2)
    else()
        target_include_directories(jumpyball PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(jumpyball PRIVATE ${SDL2_LIBRARIES})
    endif()

    if(WIN32)
        target_link_libraries(jumpyball PRIVATE shell32)
    endif()
endif()

if(MSVC)
    target_compile_definitions(jumpyball PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(NOT WIN32)
    include(CheckLibraryExists)
    check_library_exists(m sin "" JUMPYBALL_NEED_LIBM)
    if(JUMPYBALL_NEED_LIBM)
        target_link_libraries(jumpyball PRIVATE m)
    endif()
endif()

if(JUMPYBALL_WARNINGS)
    if(MSVC)
        target_compile_options(jumpyball PRIVATE /W4)
    else()
        target_compile_options(jumpyball PRIVATE -Wall -Wextra)
    endif()
endif()

if(JUMPYBALL_COPY_ASSETS)
    foreach(dir BITMAP Sounds Musics)
        add_custom_command(TARGET jumpyball POST_BUILD
                           COMMAND ${CMAKE_COMMAND} -E copy_directory
                                   ${CMAKE_CURRENT_SOURCE_DIR}/${dir}
                                   $<TARGET_FILE_DIR:jumpyball>/${dir})
    endforeach()
endif()

if(JUMPYBALL_BUILD_TOOLS)
    add_executable(modtest tools/modtest.c jb_mod.c jb_mod_effects.c)
    target_include_directories(modtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(modtest PROPERTIES C_STANDARD 90 C_EXTENSIONS ON)
endif()

install(TARGETS jumpyball RUNTIME DESTINATION bin)
install(DIRECTORY BITMAP Sounds Musics DESTINATION bin)
