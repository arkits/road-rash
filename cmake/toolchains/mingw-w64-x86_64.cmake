# Plan 33b Task 1: cross-compile Windows x86-64 with MinGW-w64 GCC (Homebrew
# mingw-w64 on macOS, the mingw-w64 package on Linux). A quick portability
# check only: MSVC is the release toolchain and real Windows the acceptance.
#   cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake
# Tests can run under Wine with -DCMAKE_CROSSCOMPILING_EMULATOR=<wine>; that
# result is never Windows acceptance.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
# Search only the MinGW sysroot for libraries/packages, so a host SDL (for
# example Homebrew's) is never picked up; SDL then builds from the pin.
execute_process(COMMAND ${CMAKE_C_COMPILER} -print-sysroot OUTPUT_VARIABLE RR_MINGW_SYSROOT
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
set(CMAKE_FIND_ROOT_PATH "${RR_MINGW_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# Static libstdc++/libgcc/winpthread so the executables run without MinGW DLLs.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
