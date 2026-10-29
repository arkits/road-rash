#include "world_renderer.hpp"
#include "golden.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
int main(int argc, char** argv) {
    // Synthetic checks run even when the owned-game oracle captures are absent.
    {
        rr::Framebuffer fb(320, 240);
        fb.clear(125);
        rr::CelTexture texture;
        texture.bounds = {0, 0, 7, 7};
        texture.flat_index = 99;
        rr::CelMip mip;
        mip.width = mip.height = 8;
        mip.stride_shift = 3;
        mip.pixels.assign(64, 34);
        texture.mips.push_back(mip);
        rr::DrawNode node;
        node.texture = &texture;
        node.view_points = {{{2 * 65536, 3 * 65536},
                             {10 * 65536, 3 * 65536},
                             {10 * 65536, 11 * 65536},
                             {2 * 65536, 11 * 65536}}};
        rr::render_nodes(fb, {node, {}}, rr::view_mode(false), 16);
        if (fb.row(6)[6] != 34 || fb.row(0)[0] != 125) {
            std::cerr << "Synthetic textured node failed\n";
            return 1;
        }
        texture.flags = 16;
        rr::render_nodes(fb, {node}, rr::view_mode(false), 16);
        if (fb.row(6)[6] != 99) {
            std::cerr << "Quality mask dispatch failed\n";
            return 1;
        }
        node.texture = nullptr;
        node.flat_index = 17;
        rr::render_nodes(fb, {node}, rr::view_mode(false), 16);
        if (fb.row(6)[6] != 17) {
            std::cerr << "Flat override failed\n";
            return 1;
        }
        try {
            rr::render_nodes(fb, {}, rr::view_mode(true), 16);
            std::cerr << "Mismatched framebuffer accepted\n";
            return 1;
        } catch (const std::runtime_error&) {
        }
    }
    // Plan 19 Task 3: any surface size through the scaled view mapping, with
    // an upscaled level above mip 0 sampled when the quad covers 2x+.
    {
        rr::CelTexture texture;
        texture.bounds = {0, 0, 15, 15};
        rr::CelMip mip0;
        mip0.width = mip0.height = 16;
        mip0.stride_shift = 4;
        mip0.pixels.assign(256, 11);
        texture.mips.push_back(mip0);
        rr::CelMip up;
        up.width = up.height = 32;
        up.stride_shift = 5;
        up.pixels.assign(1024, 0);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) up.pixels[size_t(y) * 32 + x] = uint8_t((y / 2) * 16 + (x / 2) + 1);
        texture.upscaled.push_back(up);
        rr::DrawNode node;
        node.texture = &texture;
        node.view_points = {{{0, 0}, {64 * 65536, 0}, {64 * 65536, 64 * 65536}, {0, 64 * 65536}}};
        // 64x64 surface pixels at s=1: covers 4x the 16-wide source, so
        // upscaled[0] (2x) is selected. Corner (0,0) samples the edge texel
        // (2,2) -> block (1,1) -> 18, proving the upscaled level is sampled
        // (mip 0 would give flat 11 everywhere).
        rr::Framebuffer fb(640, 480);
        fb.clear(0);
        rr::render_nodes(fb, {node}, rr::view_mode(true), 0);
        if (fb.row(0)[0] != 18) {
            std::cerr << "Upscaled level not sampled, got " << int(fb.row(0)[0]) << "\n";
            return 1;
        }
        // The same quad at 8x8 surface pixels samples mip 0 or below (index 11).
        rr::Framebuffer small(320, 240);
        small.clear(0);
        rr::DrawNode tiny;
        tiny.texture = &texture;
        tiny.view_points = {{{0, 0}, {8 * 65536, 0}, {8 * 65536, 8 * 65536}, {0, 8 * 65536}}};
        rr::render_nodes(small, {tiny}, rr::view_mode(false), 0);
        if (small.row(4)[4] != 11) {
            std::cerr << "Small quad should sample mip 0, got " << int(small.row(4)[4]) << "\n";
            return 1;
        }
        // With upscaled empty the 64x64 quad samples mip 0.
        rr::CelTexture plain;
        plain.bounds = {0, 0, 15, 15};
        plain.mips.push_back(mip0);
        rr::DrawNode big;
        big.texture = &plain;
        big.view_points = {{{0, 0}, {64 * 65536, 0}, {64 * 65536, 64 * 65536}, {0, 64 * 65536}}};
        rr::render_nodes(fb, {big}, rr::view_mode(true), 0);
        if (fb.row(4)[4] != 11) {
            std::cerr << "Empty upscaled should fall back to mip 0, got " << int(fb.row(4)[4]) << "\n";
            return 1;
        }
        // The 64-logical-px quad at s=3 also selects upscaled[0]; its corner
        // samples an edge texel (inset (1,1) block), proving the wide path.
        rr::Framebuffer wide(1920, 1440);
        wide.clear(0);
        rr::render_nodes(wide, {node}, rr::view_mode_scaled(3), 0);
        if (wide.row(0)[0] != 18) {
            std::cerr << "Wide path should sample upscaled[0], got " << int(wide.row(0)[0]) << "\n";
            return 1;
        }
    }
    // Flat quad area scales with s*s: Q16 (10.5,20.25)..(100.75,90.5) at s=3.
    {
        auto flat_count = [](int s) {
            rr::Framebuffer fb(640 * s, 480 * s);
            fb.clear(0);
            rr::DrawNode node;
            node.flat_index = 7;
            node.view_points = {{{int32_t(10.5 * 65536), int32_t(20.25 * 65536)},
                                 {int32_t(100.5 * 65536), int32_t(20.25 * 65536)},
                                 {int32_t(100.5 * 65536), int32_t(90.5 * 65536)},
                                 {int32_t(10.5 * 65536), int32_t(90.5 * 65536)}}};
            rr::render_nodes(fb, {node}, rr::view_mode_scaled(s), 0);
            size_t n = 0;
            for (int y = 0; y < fb.height(); ++y)
                for (int x = 0; x < fb.width(); ++x) n += fb.row(y)[x] != 0;
            return n;
        };
        const size_t one = flat_count(1), three = flat_count(3);
        const size_t perimeter = 2 * (270 + 210);
        if (!(three + perimeter >= 9 * one && three <= 9 * one + perimeter)) {
            std::cerr << "Flat area s=3 (" << three << ") not ~9x s=1 (" << one << ")\n";
            return 1;
        }
    }
    const std::string path = argc > 1 ? argv[1] : rr::golden::oracle_dir() + "/raster-fixtures.tsv";
    if (!std::filesystem::exists(path)) {
        std::cerr << "SKIP missing nodes oracle\n";
        return 0;
    }
    size_t failures = 0;
    try {
        const auto golden = rr::golden::load(path);
        if (golden.exe != "73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2" ||
            golden.frames.empty())
            throw std::runtime_error("Wrong node golden");
        const auto& fields = golden.frames.front();
        for (int c = 0; c < (fields.count("nodes.32.high") ? 33 : 32); ++c) {
            const std::string prefix = "nodes." + std::to_string(c) + ".";
            const auto mode = rr::view_mode(fields.at(prefix + "high") != 0);
            std::array<rr::CelTexture, 12> textures;
            std::vector<rr::DrawNode> nodes(12);
            for (int n = 0; n < 12; ++n) {
                const std::string key = prefix + std::to_string(n) + ".";
                auto& node = nodes[n];
                auto& texture = textures[n];
                texture.bounds = {0, 0, 31, 31};
                texture.flags = uint32_t(fields.at(key + "flags"));
                texture.flat_index = uint8_t(fields.at(key + "index"));
                for (int level = 0; level < 3; ++level) {
                    rr::CelMip mip;
                    mip.width = mip.height = 32 >> level;
                    mip.stride_shift = unsigned(5 - level);
                    mip.pixels.resize(size_t(1024) >> (level * 2));
                    for (size_t i = 0; i < mip.pixels.size(); ++i)
                        mip.pixels[i] = uint8_t(i * 37 + i / 7 + level * 53);
                    texture.mips.push_back(std::move(mip));
                }
                if (c == 32) {
                    texture.bounds = {0, 0, 255, 31};
                    texture.mips.clear();
                    for (int level = 0; level < 2; ++level) {
                        rr::CelMip mip;
                        mip.width = 256 >> level;
                        mip.height = 32 >> level;
                        mip.stride_shift = 8 - level;
                        mip.pixels.resize(size_t(mip.width) * mip.height);
                        mip.sampling_storage.resize(65536);
                        for (size_t i = 0; i < mip.sampling_storage.size(); ++i) {
                            const size_t at = i + (level ? 8192 : 0);
                            mip.sampling_storage[i] = uint8_t(at * 37 + at / 7);
                        }
                        std::copy_n(mip.sampling_storage.begin(), mip.pixels.size(), mip.pixels.begin());
                        texture.mips.push_back(std::move(mip));
                    }
                }
                if (fields.at(key + "flat") >= 0)
                    node.flat_index = uint8_t(fields.at(key + "flat"));
                else
                    node.texture = &texture;
                node.flip_u = fields.at(key + "flip_u") != 0;
                node.flip_v = fields.at(key + "flip_v") != 0;
                for (int i = 0; i < 4; ++i)
                    node.view_points[i] = {int32_t(fields.at(key + "point." + std::to_string(i * 2))),
                                           int32_t(fields.at(key + "point." + std::to_string(i * 2 + 1)))};
            }
            rr::Framebuffer fb(mode.surface_width, mode.surface_height);
            fb.clear(125);
            rr::render_nodes(fb, nodes, mode, uint32_t(fields.at(prefix + "mask")));
            const auto raw = std::filesystem::path(path).parent_path() /
                             (golden.scenario + "-nodes" + std::to_string(c) + ".raw");
            std::ifstream in(raw, std::ios::binary);
            if (!in) throw std::runtime_error("Missing original node pixels");
            const std::vector<uint8_t> expected((std::istreambuf_iterator<char>(in)), {});
            if (expected.size() != size_t(fb.width()) * fb.height())
                throw std::runtime_error("Wrong node image size");
            size_t pixels = 0;
            for (int y = 0; y < fb.height(); ++y)
                for (int x = 0; x < fb.width(); ++x)
                    if (fb.row(y)[x] != expected[size_t(y) * fb.width() + x]) {
                        if (!pixels) std::cerr << "case " << c << " first mismatch " << x << "," << y << '\n';
                        ++pixels;
                    }
            if (pixels) {
                ++failures;
                std::cerr << "case " << c << ": " << pixels << " mismatches\n";
            }
        }
        std::cout << "Original display lists compared; " << failures << " failures\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
