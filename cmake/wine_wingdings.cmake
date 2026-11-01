# Wine's own Wingdings (fonts/wingding.ttf, LGPL-2.1; third_party/wine-fonts/README),
# compiled into rr-resource as the fallback for hosts without a system Wingdings.
set(RR_WINGDINGS_TTF "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wine-fonts/wingding.ttf")
set(RR_WINGDINGS_CPP "${CMAKE_BINARY_DIR}/generated/wine_wingdings.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${RR_WINGDINGS_TTF}")
file(READ "${RR_WINGDINGS_TTF}" rr_wingdings_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," rr_wingdings_bytes "${rr_wingdings_hex}")
string(REGEX REPLACE "((0x..,){24})" "\\1\n" rr_wingdings_bytes "${rr_wingdings_bytes}")
file(CONFIGURE OUTPUT "${RR_WINGDINGS_CPP}" CONTENT "// Generated from third_party/wine-fonts/wingding.ttf by cmake/wine_wingdings.cmake.
#include <cstdint>
#include <vector>
namespace rr {
namespace {
const uint8_t wingding_ttf[] = {
@rr_wingdings_bytes@
};
} // namespace
std::vector<uint8_t> builtin_wine_wingdings() {
    return {wingding_ttf, wingding_ttf + sizeof wingding_ttf};
}
} // namespace rr
" @ONLY)
