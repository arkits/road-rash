#pragma once
#include "text.hpp"
#include <string>

namespace rr {
class Assets;
// Menu pictures (IMAGES/*.RRI, JPEG). The original decodes with IJG libjpeg
// (IFAST DCT, sample-range table masked to 5 bits) into RGB555, then converts
// through the 555->display table built by 0x0041e090 (0x0041e430 / 0x0041e6b0):
// RGB565 with a zero green low bit. Bit-exact against the original's decoded
// image cache (frontend_test raw-cache check).
RgbImage decode_rri(const std::vector<uint8_t>& bytes);
// "Postcard/City" -> IMAGES/POSTCARD/CITY.RRI. Throws when missing or undecodable.
RgbImage load_menu_image(const Assets&, const std::string& name);
bool menu_image_exists(const Assets&, const std::string& name);
// The 640x480 menu picture for `name`, with Postcard/Street insets composed
// over their base picture as 0x0041e240 does. Throws like load_menu_image.
RgbImage compose_menu_picture(const Assets&, const std::string& name);
// 0x0041e6e0: the second surface used behind text boxes, each 5-bit channel halved.
// Measured against the original: dark fills equal halved-555 of the decoded
// photo (button bar, description boxes), not the 0x7bde-masked 565 mapping;
// the picture surface itself is 565 (0x0041e090 table proof).
uint32_t darken555(uint32_t rgb);
uint32_t quantise565(uint32_t rgb);
} // namespace rr
