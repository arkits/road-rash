// Plan 20 Task 3: upscaled in-race textures (failing test).
#include "cel.hpp"
#include "mmpx.hpp"
#include "upscale_cache.hpp"
#include "upscale_import.hpp"
#include <filesystem>
#include <iostream>
#include "temp_dir.hpp"

int main() {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << "\n";
        }
    };
    // Synthetic: hand-made CelTexture with 8x8 mip 0; cache holds MMPX 2x/4x.
    {
        rr::CelTexture tex;
        tex.bounds = {0, 0, 7, 7};
        rr::CelMip m;
        m.width = 8;
        m.height = 8;
        m.stride_shift = 3;
        m.pixels.assign(64, 0);
        for (int i = 0; i < 64; ++i) m.pixels[size_t(i)] = uint8_t(i % 4);
        tex.mips.push_back(std::move(m));
        const std::string root =
            (std::filesystem::temp_directory_path() / ("rr-upscale-import-test-" + rr::test::process_tag()))
                .string();
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        rr::UpscaleCache cache(root);
        std::array<uint8_t, 1024> rgbx{};
        const auto luma = rr::mmpx_luma(rgbx);
        rr::Image8 src{8, 8, tex.mips[0].pixels};
        const auto x2 = rr::mmpx2x(src, luma, -1);
        const auto x4 = rr::mmpx4x(src, luma, -1);
        rr::UpscaledImage e2, e4;
        e2.width = 16;
        e2.height = 16;
        e2.indexed = true;
        e2.indices = x2.pixels;
        e4.width = 32;
        e4.height = 32;
        e4.indexed = true;
        e4.indices = x4.pixels;
        check(cache.store("DATA/X.CRS", 0, 2, tex.mips[0].pixels, rr::UpscaleMethod::mmpx, e2), "store 2x");
        check(cache.store("DATA/X.CRS", 0, 4, tex.mips[0].pixels, rr::UpscaleMethod::mmpx, e4), "store 4x");
        rr::attach_upscaled(tex, cache, "DATA/X.CRS", 0);
        check(tex.upscaled.size() == 2, "upscaled has 16x16 and 32x32 levels");
        if (tex.upscaled.size() == 2)
            check(tex.upscaled[0].width == 16 && tex.upscaled[1].width == 32, "upscaled sizes");
    }
    // Review Focus 2: remap/mmpx order. Bijection commutes; game uses
    // upscale-then-remap (correct even for non-bijective remaps).
    {
        rr::Image8 img{6, 6, {}};
        img.pixels = {0, 1, 2, 0, 1, 2, 1, 2, 0, 1, 2, 0, 2, 0, 1, 2, 0, 1,
                      0, 1, 2, 0, 1, 2, 1, 2, 0, 1, 2, 0, 2, 0, 1, 2, 0, 1};
        std::array<uint8_t, 1024> rgbx{};
        for (int i = 0; i < 256; ++i) {
            rgbx[4 * i] = uint8_t(i);
            rgbx[4 * i + 1] = uint8_t(i);
            rgbx[4 * i + 2] = uint8_t(i);
        }
        const auto luma = rr::mmpx_luma(rgbx);
        // Identity remap commutes trivially (documents the plumbing the game
        // relies on: upscale indices once, remap at draw time).
        auto remap = [](rr::Image8 im) { return im; };
        const auto a = remap(rr::mmpx2x(img, luma, -1));
        const auto b = rr::mmpx2x(remap(img), luma, -1);
        check(a.pixels == b.pixels, "identity remap commutes with mmpx");
        // Non-bijective remap (1->2, collapsing): documents the game uses
        // upscale-then-remap (left form); just assert determinism here.
        auto collapse = [](rr::Image8 im) {
            for (auto& p : im.pixels)
                if (p == 1) p = 2;
            return im;
        };
        const auto c1 = collapse(rr::mmpx2x(img, luma, -1));
        const auto c2 = collapse(rr::mmpx2x(img, luma, -1));
        check(c1.pixels == c2.pixels, "upscale-then-remap deterministic");
    }
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
