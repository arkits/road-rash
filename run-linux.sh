#!/bin/sh
# Build and launch the playable game on Linux (the run-macos.sh counterpart).
#   ./run-linux.sh [--debug] [ASSETS_DIR] [rr-game args...]
# Default is an optimized Release build in build-play/; --debug builds the
# unoptimized Debug tree in build/ (slow: the CPU renderer runs ~7x slower).
# Needs CMake 3.22+, GCC 13+ or Clang 17+, Python 3, OpenSSL headers and the
# X11/Wayland/audio headers the pinned SDL is built against (a system SDL 3.4+
# is used instead when present). Missing tools print the install command for
# Debian/Ubuntu, Fedora or Arch. On a Steam Deck use the AppImage instead
# (docs/USER_GUIDE.md): SteamOS has no compiler.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TYPE=Release
BUILD="$ROOT/build-play"
if [ "${1:-}" = "--debug" ]; then
    TYPE=Debug
    BUILD="$ROOT/build"
    shift
fi

deps_hint() {
    . /etc/os-release 2>/dev/null || true
    case " ${ID:-} ${ID_LIKE:-} " in
    *" debian "*|*" ubuntu "*)
        echo "sudo apt install build-essential cmake ninja-build python3 pkg-config git libssl-dev" \
            "libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev" \
            "libxfixes-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev" \
            "libegl-dev libgl-dev libgles-dev libdrm-dev libgbm-dev libpulse-dev" \
            "libpipewire-0.3-dev libasound2-dev libudev-dev libdbus-1-dev libibus-1.0-dev" ;;
    *" fedora "*|*" rhel "*)
        echo "sudo dnf install gcc-c++ cmake ninja-build python3 pkgconf-pkg-config git openssl-devel" \
            "libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel" \
            "libXScrnSaver-devel libXtst-devel libXfixes-devel libxkbcommon-devel wayland-devel" \
            "wayland-protocols-devel libdecor-devel mesa-libEGL-devel mesa-libGL-devel" \
            "mesa-libGLES-devel libdrm-devel mesa-libgbm-devel pulseaudio-libs-devel" \
            "pipewire-devel alsa-lib-devel systemd-devel dbus-devel ibus-devel" ;;
    *" arch "*)
        echo "sudo pacman -S --needed base-devel cmake ninja python pkgconf git openssl libx11" \
            "libxext libxrandr libxcursor libxi libxss libxtst libxfixes libxkbcommon wayland" \
            "wayland-protocols libdecor mesa libdrm libpulse pipewire alsa-lib systemd-libs dbus ibus" ;;
    *)
        echo "install CMake, a C++20 compiler, Python 3, pkg-config, OpenSSL and the X11, Wayland," \
            "EGL/GL, PulseAudio/PipeWire/ALSA, udev and D-Bus development packages" ;;
    esac
}
missing() {
    echo "run-linux.sh: $1 not found. To install the build dependencies:" >&2
    echo "  $(deps_hint)" >&2
    exit 1
}

command -v cmake >/dev/null 2>&1 || missing cmake
command -v python3 >/dev/null 2>&1 || missing python3
# GCC 13+ or Clang 17+ for C++20; prefer a newer GCC when the default is older.
if [ -z "${CXX:-}" ]; then
    if command -v g++ >/dev/null 2>&1; then
        major=$(g++ -dumpversion | cut -d. -f1)
        if [ "$major" -lt 13 ]; then
            for v in 14 13; do
                if command -v "g++-$v" >/dev/null 2>&1; then
                    export CC="gcc-$v" CXX="g++-$v"
                    break
                fi
            done
        fi
    elif command -v clang++ >/dev/null 2>&1; then
        export CC=clang CXX=clang++
    else
        missing "a C++ compiler (g++ or clang++)"
    fi
fi
cxx=${CXX:-g++}
cxx_major=$("$cxx" -dumpversion 2>/dev/null | cut -d. -f1)
case $("$cxx" --version 2>/dev/null | head -n 1) in
*clang*) cxx_min=17 ;;
*) cxx_min=13 ;;
esac
if [ "${cxx_major:-0}" -lt "$cxx_min" ]; then
    echo "run-linux.sh: $cxx $cxx_major is too old; the port needs GCC 13+ or Clang 17+." >&2
    echo "  Install a newer compiler and set CXX/CC (for example CXX=g++-13 CC=gcc-13)," >&2
    echo "  or use the prebuilt tarball or AppImage, which run on glibc 2.31 and later." >&2
    exit 1
fi
if command -v pkg-config >/dev/null 2>&1; then
    pkg-config --exists openssl || missing "OpenSSL development files"
    # The pinned SDL builds without a video backend when neither is present.
    if ! pkg-config --exists sdl3 && ! pkg-config --exists x11 && ! pkg-config --exists wayland-client; then
        missing "X11 or Wayland development files"
    fi
fi

GENERATOR=
if [ ! -f "$BUILD/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then GENERATOR="-GNinja"; fi
# shellcheck disable=SC2086 # GENERATOR is empty or one word
cmake -S "$ROOT" -B "$BUILD" $GENERATOR -DCMAKE_BUILD_TYPE="$TYPE"
cmake --build "$BUILD" --parallel --target rr-game
ASSETS="$ROOT/../cdrom/ROADRASH"
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then ASSETS=$1; shift; fi
if [ -f "$ASSETS/ROADRASH.EXE" ]; then
    exec "$BUILD/rr-game" --assets "$ASSETS" "$@"
else
    # No imported/mounted disc: run the first-run import flow (or the disc
    # imported earlier).
    exec "$BUILD/rr-game" "$@"
fi
