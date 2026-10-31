# The original modifies the IJG sample-range table to truncate to five bits.
# SIMD paths bypass that table. Always build the pinned scalar decoder, for
# identical output on macOS, Android, WebAssembly and future desktop targets.
include(ExternalProject)
set(RR_JPEG_ROOT "${CMAKE_BINARY_DIR}/jpeg-scalar")
set(RR_JPEG_SOURCE "${RR_JPEG_ROOT}/src")
set(RR_JPEG_BINARY "${RR_JPEG_ROOT}/build")
set(RR_JPEG_PLATFORM_ARGS)
foreach(var CMAKE_TOOLCHAIN_FILE CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET
        CMAKE_OSX_SYSROOT ANDROID_ABI ANDROID_PLATFORM ANDROID_STL CMAKE_C_COMPILER
        CMAKE_C_COMPILER_TARGET CMAKE_SYSROOT CMAKE_C_FLAGS CMAKE_EXE_LINKER_FLAGS)
    if(DEFINED ${var} AND NOT "${${var}}" STREQUAL "")
        list(APPEND RR_JPEG_PLATFORM_ARGS "-D${var}=${${var}}")
    endif()
endforeach()
if(MSVC)
    set(RR_JPEG_LIBRARY "${RR_JPEG_BINARY}/$<CONFIG>/jpeg-static.lib")
else()
    set(RR_JPEG_LIBRARY "${RR_JPEG_BINARY}/libjpeg.a")
endif()
file(MAKE_DIRECTORY "${RR_JPEG_SOURCE}/src" "${RR_JPEG_BINARY}")
ExternalProject_Add(rr-jpeg-build
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.1.4.1.tar.gz
    URL_HASH SHA256=a7da42b640377c2a9a9665e2c4b0ea60cd5599afb48c2521e6df0c9dc9d15a25
    SOURCE_DIR "${RR_JPEG_SOURCE}" BINARY_DIR "${RR_JPEG_BINARY}"
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_SIMD=OFF -DWITH_TURBOJPEG=OFF
        ${RR_JPEG_PLATFORM_ARGS}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target jpeg-static --config $<CONFIG>
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${RR_JPEG_LIBRARY}")
add_library(rr-jpeg STATIC IMPORTED GLOBAL)
set_target_properties(rr-jpeg PROPERTIES IMPORTED_NO_SYSTEM TRUE IMPORTED_LOCATION "${RR_JPEG_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${RR_JPEG_SOURCE}/src;${RR_JPEG_BINARY}")
add_dependencies(rr-jpeg rr-jpeg-build)
