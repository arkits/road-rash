# Plan 33b Task 1: MSVC and clang-cl options (included when MSVC is set).
# Static CRT, so packages need no VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# UTF-8 sources and execution charset, standard conformance, C++ exceptions,
# large translation units (race.cpp, frontend.cpp).
add_compile_options(/utf-8 /bigobj "$<$<COMPILE_LANGUAGE:CXX>:/EHsc;/permissive-;/Zc:__cplusplus>")
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>")
endif()
if(RR_TSAN OR RR_FUZZ)
    message(FATAL_ERROR "RR_TSAN and RR_FUZZ need GCC/Clang sanitizer runtimes; build them on macOS or Linux")
endif()
