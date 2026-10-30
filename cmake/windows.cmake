# Plan 33b Tasks 2-3: Windows resources (included when WIN32 is set).
enable_language(RC)
set(RR_APP_MANIFEST "${CMAKE_SOURCE_DIR}/windows/rr-app.manifest")
set(RR_APP_ICON "${CMAKE_SOURCE_DIR}/assets/app-icon/roadrash.ico")
configure_file("${CMAKE_SOURCE_DIR}/windows/rr-app.rc.in" "${CMAKE_BINARY_DIR}/windows/rr-app.rc" @ONLY)
configure_file("${CMAKE_SOURCE_DIR}/windows/rr-game.rc.in" "${CMAKE_BINARY_DIR}/windows/rr-game.rc" @ONLY)

# Every executable of this directory (game, tools, tests) embeds the UTF-8 /
# DPI / long-path manifest, so tests run with the game's code page. The
# linker's own default manifest is turned off to avoid a duplicate resource.
function(rr_windows_embed_manifests)
    get_property(targets DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(type STREQUAL "EXECUTABLE")
            target_sources(${target} PRIVATE "${CMAKE_BINARY_DIR}/windows/rr-app.rc")
            if(MSVC)
                target_link_options(${target} PRIVATE /MANIFEST:NO)
            endif()
        endif()
    endforeach()
endfunction()
