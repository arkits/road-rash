# Plan 34 Task 5: rr-netd. The room/sealer core is pure (rr-net only) and is
# always built for the tests; the daemon needs libdatachannel. This file is
# also a standalone project (the Docker image builds only the server):
#   cmake -S server -B build-netd -DCMAKE_BUILD_TYPE=Release
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.22)
    project(rr-netd LANGUAGES C CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(RR_NETD_STANDALONE ON)
    set(RR_NET_TRANSPORT ON CACHE BOOL "" FORCE)
    find_package(Threads REQUIRED)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/net.cmake)
endif()
add_library(rr-netd-core STATIC ${CMAKE_CURRENT_LIST_DIR}/room.cpp ${CMAKE_CURRENT_LIST_DIR}/sealer.cpp)
target_include_directories(rr-netd-core PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(rr-netd-core PUBLIC rr-net)
# Plan 33b Task 1: the daemon uses POSIX sockets/signals and is deployed on
# Linux only; the room/sealer core and its tests build everywhere.
if(RR_NET_TRANSPORT AND NOT WIN32)
    if(NOT RR_NETD_REVISION)
        execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                        OUTPUT_VARIABLE RR_NETD_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    if(NOT RR_NETD_REVISION)
        set(RR_NETD_REVISION unknown)
    endif()
    add_executable(rr-netd ${CMAKE_CURRENT_LIST_DIR}/main.cpp)
    target_link_libraries(rr-netd PRIVATE rr-netd-core rr-net-transport Threads::Threads)
    target_compile_definitions(rr-netd PRIVATE RR_GIT_REVISION="${RR_NETD_REVISION}")
endif()
