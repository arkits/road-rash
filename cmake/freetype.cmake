# Menu text: Wine-GDI renders the original's TrueType faces with its bundled
# FreeType 2.13.3 (v40 interpreter, mono target). Building that exact release
# reproduces its hinted bitmaps; a system FreeType of another version does not
# (2.14.3: 6986/8448 glyphs). Static, no optional dependencies.
# Offline builds (Flatpak) pass the unpacked 2.13.3 tree as RR_FREETYPE_SOURCE_DIR.
include(ExternalProject)
set(RR_FREETYPE_SOURCE_DIR "" CACHE PATH "Unpacked FreeType 2.13.3 source (skips the download)")
set(RR_FREETYPE_ROOT "${CMAKE_BINARY_DIR}/freetype")
if(RR_FREETYPE_SOURCE_DIR)
    set(RR_FREETYPE_SOURCE "${RR_FREETYPE_SOURCE_DIR}")
    set(RR_FREETYPE_FETCH)
else()
    set(RR_FREETYPE_SOURCE "${RR_FREETYPE_ROOT}/src")
    set(RR_FREETYPE_FETCH
        URL https://download.savannah.gnu.org/releases/freetype/freetype-2.13.3.tar.xz
            https://downloads.sourceforge.net/project/freetype/freetype2/2.13.3/freetype-2.13.3.tar.xz
        URL_HASH SHA256=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289)
endif()
set(RR_FREETYPE_BINARY "${RR_FREETYPE_ROOT}/build")
set(RR_FREETYPE_PLATFORM_ARGS)
foreach(var CMAKE_TOOLCHAIN_FILE CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET
        CMAKE_OSX_SYSROOT ANDROID_ABI ANDROID_PLATFORM ANDROID_STL CMAKE_C_COMPILER
        CMAKE_C_COMPILER_TARGET CMAKE_SYSROOT CMAKE_C_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_MAKE_PROGRAM)
    if(DEFINED ${var} AND NOT "${${var}}" STREQUAL "")
        list(APPEND RR_FREETYPE_PLATFORM_ARGS "-D${var}=${${var}}")
    endif()
endforeach()
# Always Release: FreeType's Debug build adds a "d" postfix and enables its tracing.
# The sub-build inherits the generator; multi-config ones (the Windows presets'
# Ninja Multi-Config) put the library in Release/.
if(MSVC)
    set(RR_FREETYPE_NAME freetype.lib)
else()
    set(RR_FREETYPE_NAME libfreetype.a)
endif()
get_property(RR_FREETYPE_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(RR_FREETYPE_MULTI_CONFIG)
    set(RR_FREETYPE_LIBRARY "${RR_FREETYPE_BINARY}/Release/${RR_FREETYPE_NAME}")
else()
    set(RR_FREETYPE_LIBRARY "${RR_FREETYPE_BINARY}/${RR_FREETYPE_NAME}")
endif()
# The port links the static CRT (cmake/msvc.cmake); FreeType's 3.5 policy cap
# would ignore CMAKE_MSVC_RUNTIME_LIBRARY and leave it on the DLL CRT. The parent
# evaluates the expression, so a Debug port gets /MTd under FreeType's Release.
if(MSVC)
    list(APPEND RR_FREETYPE_PLATFORM_ARGS -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
        "-DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif()
file(MAKE_DIRECTORY "${RR_FREETYPE_SOURCE}/include" "${RR_FREETYPE_BINARY}/include")
ExternalProject_Add(rr-freetype-build
    ${RR_FREETYPE_FETCH}
    SOURCE_DIR "${RR_FREETYPE_SOURCE}" BINARY_DIR "${RR_FREETYPE_BINARY}"
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON
        -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON -DSKIP_INSTALL_ALL=ON
        ${RR_FREETYPE_PLATFORM_ARGS}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target freetype --config Release
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${RR_FREETYPE_LIBRARY}")
add_library(rr-freetype STATIC IMPORTED GLOBAL)
set_target_properties(rr-freetype PROPERTIES IMPORTED_NO_SYSTEM TRUE IMPORTED_LOCATION "${RR_FREETYPE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${RR_FREETYPE_BINARY}/include;${RR_FREETYPE_SOURCE}/include")
add_dependencies(rr-freetype rr-freetype-build)
