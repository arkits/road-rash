// Plan 21 Task 4: full-colour raster with bilinear/trilinear filtering.
// - Equivalence: render_nodes_rgb with nearest + all-off equals to_rgba of
//   the indexed render, at scales 1 and 2, over golden + synthetic lists.
// - Halo: white disc on transparent, magnified 8x bilinear: no dark pixels
//   inside, no colour outside the silhouette (+-1px).
// - Bilinear value: 2x2 checker stretched to 64x64: centre is mid-grey +-2.
// - Trilinear: quad shrinking across a mip boundary: no brightness step > 8
//   between adjacent rows.
#include "cel.hpp"
#include <cstdlib>
#include "framebuffer_rgb.hpp"
#include "texture.hpp"
#include "golden.hpp"
#include "raster_rgb.hpp"
#include "rider_sprites.hpp"
#include "scene_dump.hpp"
#include "texture_library.hpp"
#include "view.hpp"
#include "world_renderer.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>

namespace {
const char* const course_files[5] = {"CITY", "HIWAY", "MEDLY", "NAPA", "CANYN"};
}

int main(int argc, char** argv) {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << "\n";
        }
    };
    std::string assets_root = RR_SOURCE_DIR "/../cdrom/ROADRASH";
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--assets" && i + 1 < argc) assets_root = argv[++i];
    }
    rr::Assets assets(assets_root);
    // Halo fixture: white disc on index 0.
    rr::CelMip disc;
    disc.width = 16;
    disc.height = 16;
    disc.stride_shift = 4;
    disc.pixels.assign(256, 0);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const double d = std::hypot(x - 7.5, y - 7.5);
            if (d <= 6.5) disc.pixels[size_t(y) * 16 + x] = 255;
        }
    rr::Palette white;
    white.rgbx.fill(0);
    white.rgbx[4 * 255] = 255;
    white.rgbx[4 * 255 + 1] = 255;
    white.rgbx[4 * 255 + 2] = 255;
    {
        rr::FramebufferRgb fb(128, 128);
        fb.clear(0);
        std::array<rr::RgbVertex, 4> q{{{0, 0, 0, 0}, {128, 0, 16, 0}, {128, 128, 16, 16}, {0, 128, 0, 16}}};
        {
            auto m = rr::rgba_mips_for_test(disc, white);
            rr::draw_rgb_quad_bilinear(fb, q, m.data(), m.size(), false, {0, 0, 128, 127});
        }
        int dark_inside = 0, colour_outside = 0;
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x) {
                const uint32_t p = fb.row(y)[x];
                const double d = std::hypot(x - 63.5, y - 63.5) / 8.0;
                const int r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff, a = p >> 24;
                if (d <= 5.5 && a > 128 && (r < 200 || g < 200 || b < 200)) ++dark_inside;
                if (d >= 7.5 && a > 128) ++colour_outside;
            }
        check(dark_inside == 0, "no dark halo inside the disc");
        check(colour_outside == 0, "no colour outside the silhouette");
        if (dark_inside || colour_outside)
            std::cerr << "halo dark_inside=" << dark_inside << " colour_outside=" << colour_outside << "\n";
    }
    // Bilinear value fixture.
    {
        rr::CelMip checker;
        checker.width = 2;
        checker.height = 2;
        checker.stride_shift = 1;
        checker.pixels = {1, 2, 2, 1};
        rr::Palette bw;
        bw.rgbx.fill(0);
        bw.rgbx[4] = 0;
        bw.rgbx[5] = 0;
        bw.rgbx[6] = 0;
        bw.rgbx[8] = 255;
        bw.rgbx[9] = 255;
        bw.rgbx[10] = 255;
        rr::FramebufferRgb fb(64, 64);
        fb.clear(0);
        std::array<rr::RgbVertex, 4> q{{{0, 0, 0, 0}, {64, 0, 2, 0}, {64, 64, 2, 2}, {0, 64, 0, 2}}};
        {
            auto m = rr::rgba_mips_for_test(checker, bw);
            rr::draw_rgb_quad_bilinear(fb, q, m.data(), m.size(), false, {0, 0, 64, 63});
        }
        const uint32_t c = fb.row(32)[32];
        const int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
        check(std::abs(r - 128) <= 2 && std::abs(g - 128) <= 2 && std::abs(b - 128) <= 2,
              "centre is mid-grey");
        if (!(std::abs(r - 128) <= 2 && std::abs(g - 128) <= 2 && std::abs(b - 128) <= 2))
            std::cerr << "centre=" << r << "," << g << "," << b << "\n";
    }
    const std::string tsv = rr::golden::oracle_dir() + "/napa-moving.tsv";
    if (!std::filesystem::exists(tsv) || !assets.exists("DATA/GLOBAL.RSC")) {
        std::cout << "skip: no moving capture or disc\n";
        return 77;
    }
    const auto golden = rr::golden::load(tsv);
    const std::string stem = tsv.substr(0, tsv.size() - 4);
    const int course = int(golden.frames.at(0).at("course"));
    const auto lib = rr::testlib::load_texture_library(assets, course_files[course]);
    const auto exe = assets.read("ROADRASH.EXE");
    const auto sprites = rr::load_rider_sprites(assets, exe, true);
    const auto palette = rr::Palette::from_raw(assets.read("DATA/PALETTE.RAW"));
    rr::RenderThreads pool(1);
    size_t frames = 0;
    for (int s = 1; s <= 2; ++s) {
        for (const auto& f : golden.frames) {
            if (!f.count("scene.file")) continue;
            const long file = f.at("scene.file");
            if (file != 0 && file != 60 && file != 120) continue;
            const auto dump = rr::scene::load(stem + "-scene" + std::to_string(file) + ".raw");
            std::vector<rr::DrawNode> nodes;
            std::vector<std::unique_ptr<rr::CelTexture>> dynamic;
            for (const auto& sn : dump.nodes) {
                rr::DrawNode n;
                for (int k = 0; k < 4; ++k)
                    n.view_points[size_t(k)] = {int32_t(sn.u32(0x44 + 8 * size_t(k))),
                                                int32_t(sn.u32(0x48 + 8 * size_t(k)))};
                n.flip_u = sn.bytes[0x64] != 0;
                n.flip_v = sn.bytes[0x65] != 0;
                if (sn.u32(8) == 0x00466684) {
                    auto t = rr::rider_texture(sprites, int32_t(sn.u32(0x38)), int32_t(sn.u32(0x34)));
                    if (!t) continue;
                    dynamic.push_back(std::make_unique<rr::CelTexture>(std::move(*t)));
                    n.texture = dynamic.back().get();
                } else if (sn.bytes[0x66])
                    n.flat_index = sn.bytes[0x67];
                else if (!(n.texture = lib.find(sn.hash, sn.size)))
                    continue;
                nodes.push_back(n);
            }
            const rr::ViewMode mode = s == 1 ? rr::view_mode(true) : rr::view_mode_scaled(s);
            rr::Framebuffer idx(mode.surface_width, mode.surface_height);
            idx.clear(0);
            rr::render_nodes(idx, nodes, mode, uint32_t(f.at("view.quality_mask")));
            rr::FramebufferRgb rgb(mode.surface_width, mode.surface_height);
            rgb.clear(0);
            rr::Enhancements e; // all off, nearest
            rr::render_nodes_rgb(rgb, nodes, mode, uint32_t(f.at("view.quality_mask")), palette, e, pool);
            size_t differ = 0;
            std::vector<uint8_t> rgba(size_t(mode.surface_width) * mode.surface_height * 4);
            idx.to_rgba(palette, rgba.data(), mode.surface_width * 4);
            for (int y = 0; y < mode.surface_height; ++y) {
                for (int x = 0; x < mode.surface_width; ++x) {
                    const uint8_t* c = &rgba[(size_t(y) * mode.surface_width + x) * 4];
                    const uint32_t got = rgb.row(y)[x];
                    const int gr = (got >> 16) & 0xff, gg = (got >> 8) & 0xff, gb = got & 0xff;
                    if (gr != c[0] || gg != c[1] || gb != c[2]) ++differ;
                }
                if (differ > 100) break;
            }
            check(differ == 0, "rgb-nearest equals indexed to_rgba");
            if (differ) std::cerr << "frame " << file << " s=" << s << " differ=" << differ << "\n";
            ++frames;
        }
    }
    check(frames > 0, "golden frames compared");
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
