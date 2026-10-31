#include "rri.hpp"
#include "assets.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <memory>
extern "C" {
#include <jpeglib.h>
}

namespace rr {
namespace {
constexpr int max_rri_dimension = 4096;
}

uint32_t quantise565(uint32_t c) {
    auto q5 = [](uint32_t v) {
        v >>= 3;
        return (v << 3) | (v >> 2);
    };
    auto q6 = [](uint32_t v) {
        v >>= 2;
        return (v << 2) | (v >> 4);
    };
    return q5(c >> 16 & 0xff) << 16 | q6(c >> 8 & 0xff) << 8 | q5(c & 0xff);
}

uint32_t darken555(uint32_t c) {
    auto d = [](uint32_t v) {
        v = (v >> 3) >> 1;
        return (v << 3) | (v >> 2);
    };
    // The halved 555 value goes through the same display table as the
    // picture: 6-bit green with a zero low bit (golden fe-restroom: 56, not 57).
    const uint32_t g = ((c >> 8 & 0xff) >> 3 >> 1) << 1;
    return d(c >> 16 & 0xff) << 16 | ((g << 2) | (g >> 4)) << 8 | d(c & 0xff);
}

namespace {
struct JpegError {
    jpeg_error_mgr base;
    std::jmp_buf jump;
    char message[JMSG_LENGTH_MAX]{};
};
void jpeg_error(j_common_ptr info) {
    auto* error = reinterpret_cast<JpegError*>(info->err);
    (*info->err->format_message)(info, error->message);
    std::longjmp(error->jump, 1);
}
} // namespace

RgbImage decode_rri(const std::vector<uint8_t>& bytes) {
    // Heap state survives libjpeg's C error jump; no C++ object is constructed
    // between setjmp and a library call that may invoke error_exit.
    auto info = std::make_unique<jpeg_decompress_struct>();
    auto error = std::make_unique<JpegError>();
    info->err = jpeg_std_error(&error->base);
    error->base.error_exit = jpeg_error;
    error->base.output_message = [](j_common_ptr) {};
    if (setjmp(error->jump)) {
        jpeg_destroy_decompress(info.get());
        throw std::runtime_error(std::string("Invalid RRI image: ") + error->message);
    }
    jpeg_create_decompress(info.get());
    jpeg_mem_src(info.get(), bytes.data(), bytes.size());
    jpeg_read_header(info.get(), TRUE);
    if (!info->image_width || !info->image_height || info->image_width > max_rri_dimension ||
        info->image_height > max_rri_dimension || info->data_precision != 8) {
        jpeg_destroy_decompress(info.get());
        throw std::runtime_error("Invalid RRI image dimensions/precision");
    }
    info->out_color_space = JCS_RGB;
    info->dct_method = JDCT_IFAST; // original 0x0041e496 (cinfo + 0x44 = 1)
    jpeg_start_decompress(info.get());
    // Original 0x004293a0 builds the 1408-byte IJG clamp table with &0xf8,
    // including its saturated and wrapping segments. This also quantises
    // Y/Cb/Cr before the colour converter, not just the final RGB result.
    for (int i = -256; i < 1152; ++i) info->sample_range_limit[i] &= 0xf8;
    const int w = int(info->output_width), h = int(info->output_height);
    const size_t row_bytes = size_t(w) * 3;
    // libjpeg owns this buffer; a decoding error releases it with the pool.
    JSAMPARRAY rows = (*info->mem->alloc_sarray)(reinterpret_cast<j_common_ptr>(info.get()), JPOOL_IMAGE,
                                                 JDIMENSION(row_bytes), h);
    while (info->output_scanline < info->output_height) {
        const auto y = info->output_scanline;
        jpeg_read_scanlines(info.get(), rows + y, 1);
    }
    // No libjpeg calls that can error after constructing the return image.
    RgbImage img(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto* p = rows[y] + 3 * x;
            // The original stores RGB555 then indexes the display-mask table:
            // RGB565 green has a zero low bit (0x0041e090 / 0x0041e6b0).
            const uint32_t r = p[0] >> 3, g = (p[1] >> 3) << 1, b = p[2] >> 3;
            img.at(x, y) = ((r << 3) | (r >> 2)) << 16 | ((g << 2) | (g >> 4)) << 8 | ((b << 3) | (b >> 2));
        }
    jpeg_destroy_decompress(info.get());
    return img;
}

static std::string image_path(const std::string& name) {
    std::string p = "IMAGES/";
    for (char c : name) p.push_back(c == '\\' ? '/' : char(std::toupper(static_cast<unsigned char>(c))));
    return p + ".RRI";
}

bool menu_image_exists(const Assets& a, const std::string& name) {
    return a.exists(image_path(name));
}

RgbImage load_menu_image(const Assets& a, const std::string& name) {
    return decode_rri(a.read(image_path(name)));
}

RgbImage compose_menu_picture(const Assets& a, const std::string& name) {
    const RgbImage img = load_menu_image(a, name);
    RgbImage out(640, 480, 0);
    // 0x0041e240: a "Postcard/" or "Street/" picture starts from a copy of its
    // base picture and is decoded over it. The insets are partial: 397x480
    // postcards sit right of the 243-wide "Postcard" strip, 258x250 street
    // views at (146,230). Pixel-exact against the original's decoded image
    // cache (Street/Klub: 64500/64500 inset, the rest equals Street).
    int x = 0, y = 0;
    const bool postcard = name.rfind("Postcard/", 0) == 0, street = name.rfind("Street/", 0) == 0;
    if ((postcard || street) && menu_image_exists(a, postcard ? "Postcard" : "Street")) {
        const RgbImage base = load_menu_image(a, postcard ? "Postcard" : "Street");
        for (int row = 0; row < std::min(base.height, 480); ++row)
            for (int col = 0; col < std::min(base.width, 640); ++col) out.at(col, row) = base.at(col, row);
        if (postcard)
            x = 640 - img.width;
        else {
            x = 146;
            y = 230;
        }
    }
    for (int row = 0; row < img.height; ++row)
        for (int col = 0; col < img.width; ++col)
            if (out.contains(x + col, y + row)) out.at(x + col, y + row) = img.at(col, row);
    return out;
}
} // namespace rr
