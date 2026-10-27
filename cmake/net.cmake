# Plan 34: online mano-a-mano. `rr-net` holds the pure codecs, rollback and
# the deterministic loopback network (no SDL, no sockets) and always builds.
# RR_NET_TRANSPORT adds libdatachannel (WebSocket + WebRTC DataChannel) for
# the native clients, the bots and the server rr-netd.
if(EMSCRIPTEN OR ANDROID OR RR_FUZZ)
    set(RR_NET_TRANSPORT_DEFAULT OFF) # browser uses JS glue; Android wiring is plan 34 Task 10
else()
    set(RR_NET_TRANSPORT_DEFAULT ON)
endif()
option(RR_NET_TRANSPORT "Build libdatachannel network transports, rr-netd and net bots" ${RR_NET_TRANSPORT_DEFAULT})

set(RR_NET_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
add_library(rr-net STATIC ${RR_NET_ROOT}/src/net/protocol.cpp ${RR_NET_ROOT}/src/net/loopback_channel.cpp)
target_include_directories(rr-net PUBLIC ${RR_NET_ROOT}/src)
set_target_properties(rr-net PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Plan 34 Task 7: the compatibility gate hashes the simulation sources at
# build time. Every client built from the same source computes the same
# RR_SIM_SOURCE_HASH; any simulation change (Task 2/3 files, codecs, room
# logic) changes the handshake fingerprint, so mixed rooms are rejected.
file(GLOB_RECURSE RR_SIM_SOURCES CONFIGURE_DEPENDS "${RR_NET_ROOT}/src/*.cpp" "${RR_NET_ROOT}/src/*.hpp")
set(RR_SIM_HASHES "")
foreach(source IN LISTS RR_SIM_SOURCES)
    file(SHA256 "${source}" source_hash)
    string(APPEND RR_SIM_HASHES "${source_hash}")
endforeach()
string(SHA256 RR_SIM_SOURCE_HASH "${RR_SIM_HASHES}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RR_SIM_SOURCES})
if(NOT RR_NETD_STANDALONE)
    set_property(SOURCE "${RR_NET_ROOT}/src/net/compat.cpp" APPEND PROPERTY COMPILE_DEFINITIONS RR_SIM_SOURCE_HASH="${RR_SIM_SOURCE_HASH}")
endif()

if(RR_NET_TRANSPORT)
    include(FetchContent)
    # libdatachannel v0.24.6 (MPL-2.0), pinned by commit. Only the submodules
    # a media-free build needs are fetched: libjuice (MPL-2.0), usrsctp
    # (BSD-3-Clause) and plog (MIT). TLS is OpenSSL (Apache-2.0), linked
    # statically on macOS so packages carry no Homebrew dylib.
    set(NO_MEDIA ON CACHE BOOL "" FORCE)
    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(USE_GNUTLS OFF CACHE BOOL "" FORCE)
    set(USE_MBEDTLS OFF CACHE BOOL "" FORCE)
    set(USE_NICE OFF CACHE BOOL "" FORCE)
    if(APPLE)
        set(OPENSSL_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG 6b1e2e620f1e37f0eafeee702eaea0043cb305fd # v0.24.6
        GIT_SUBMODULES deps/plog deps/usrsctp deps/libjuice
        GIT_SHALLOW FALSE
        EXCLUDE_FROM_ALL)
    set(RR_SAVED_BUILD_SHARED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    FetchContent_MakeAvailable(libdatachannel)
    set(BUILD_SHARED_LIBS ${RR_SAVED_BUILD_SHARED})
    if(RR_FUZZ)
        # Pinned libjuice picohash casts C hash callbacks through void pointers.
        # Keep ASan/other UBSan checks; exclude only this third-party function-type check.
        target_compile_options(juice-static PRIVATE -fno-sanitize=function)
    endif()

    add_library(rr-net-transport STATIC ${RR_NET_ROOT}/src/net/ws_channel.cpp ${RR_NET_ROOT}/src/net/rtc_channel.cpp ${RR_NET_ROOT}/src/net/rtc_transport.cpp)
    target_link_libraries(rr-net-transport PUBLIC rr-net LibDataChannel::LibDataChannelStatic)
    target_compile_definitions(rr-net-transport PUBLIC RR_NET_TRANSPORT=1)
endif()

if(RR_FUZZ AND NOT RR_NETD_STANDALONE)
    # fuzz_net_protocol and the seed writer link rr-net in addition to
    # rr-resource (glob in CMakeLists.txt).
    target_link_libraries(fuzz_net_protocol PRIVATE rr-net)
    target_link_libraries(fuzz-seeds PRIVATE rr-net)
endif()

if(NOT RR_NETD_STANDALONE)
    add_subdirectory(${RR_NET_ROOT}/server ${CMAKE_BINARY_DIR}/server)
endif()

if(NOT RR_NETD_STANDALONE)
    target_sources(rr-resource PRIVATE ${RR_NET_ROOT}/src/net/client.cpp ${RR_NET_ROOT}/src/net/compat.cpp ${RR_NET_ROOT}/src/net/mano.cpp ${RR_NET_ROOT}/src/frontend_net.cpp)
    target_link_libraries(rr-resource PUBLIC rr-net)
endif()

if(RR_NET_TRANSPORT AND NOT RR_NETD_STANDALONE)
    add_executable(net-bots tools/net_bots.cpp)
    target_link_libraries(net-bots PRIVATE rr-resource rr-net-transport)
endif()

if(EMSCRIPTEN AND NOT RR_NET_TRANSPORT)
    add_library(rr-net-transport STATIC ${RR_NET_ROOT}/src/net/ws_channel_web.cpp ${RR_NET_ROOT}/src/net/rtc_channel_web.cpp ${RR_NET_ROOT}/src/net/rtc_transport.cpp)
    target_link_libraries(rr-net-transport PUBLIC rr-net)
    target_compile_definitions(rr-net-transport PUBLIC RR_NET_TRANSPORT=1)
endif()

if(EMSCRIPTEN AND RR_WEB_BROWSER)
    add_executable(net_rtc-web tests/net_rtc_test.cpp)
    target_link_libraries(net_rtc-web PRIVATE rr-net-transport)
    target_include_directories(net_rtc-web PRIVATE tests)
    target_link_options(net_rtc-web PRIVATE "-sASYNCIFY=1" "-sENVIRONMENT=web" "-sEXIT_RUNTIME=1")
endif()
if(EMSCRIPTEN AND RR_WEB_BROWSER)
    add_executable(net_transport-web tests/net_transport_test.cpp)
    target_link_libraries(net_transport-web PRIVATE rr-net-transport)
    target_link_options(net_transport-web PRIVATE "-sASYNCIFY=1" "-sENVIRONMENT=web" "-sEXIT_RUNTIME=1")
endif()
