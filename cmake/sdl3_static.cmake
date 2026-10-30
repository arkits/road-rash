include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
# Official release metadata and the downloaded archive have this digest.
# SDL is zlib-licensed; copy its licence into the application resources.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
# Plan 33 Task A7: 3.4.18 (October 2, 2026) is >= 3.4.12, which adds
# SDL_HINT_ENABLE_STEAM_SCREEN_KEYBOARD. Android still links the 3.4.8 AAR.
# Linux keeps SDL's run-time loading of X11/Wayland/PipeWire/PulseAudio/ALSA
# (SDL's default *_SHARED options), so one binary runs on every desktop.
# Linux desktop builds need SDL's build dependencies (X11 + extensions,
# Wayland, libdecor, PulseAudio/PipeWire/ALSA, udev, D-Bus; SDL's
# README-linux lists them) and SDL fails configure when one is missing, so a
# package never silently loses a backend. RR_SDL_HEADLESS is for containers
# that only run tests/tools with the dummy drivers (CI, determinism, bots).
option(RR_SDL_HEADLESS "Linux: build the pinned SDL without X11/Wayland (CI and tool containers)" OFF)
if(RR_SDL_HEADLESS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
    set(SDL_X11 OFF CACHE BOOL "" FORCE)
    set(SDL_WAYLAND OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3)
FetchContent_MakeAvailable(SDL3)
