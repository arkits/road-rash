// Native smooth terrain (world_texture.hpp): continuous road-space hill
// texturing. Synthetic textures only.
// - Two sub-quads sharing an edge cover the same pixels once each, with no
//   gap, and sample the same texels as the undivided quad.
// - Texel coordinates wrap within the texture bounds.
// - attach_world_uv anchors the texture to the route: the same ground point
//   seen from two camera positions maps to the same texel.
// - The renderer ignores world_uv unless a blend table is passed.
// - Settings: smooth_terrain defaults on and round-trips.
#include "world_texture.hpp"
#include "framebuffer.hpp"
#include "settings.hpp"
#include "world_renderer.hpp"
#include <cmath>
#include <iostream>
#include <vector>

namespace {
rr::CelTexture column_texture() {
    // 64x16, every texel non-zero; index encodes the column (1..64).
    rr::CelTexture t;
    t.bounds = {0, 0, 63, 15};
    for (int level = 0; level < 3; ++level) {
        rr::CelMip m;
        m.width = 64 >> level;
        m.height = 16 >> level;
        m.stride_shift = unsigned(6 - level);
        m.pixels.resize(size_t(m.width) * size_t(m.height));
        for (int y = 0; y < m.height; ++y)
            for (int x = 0; x < m.width; ++x)
                m.pixels[size_t(y) * size_t(m.width) + size_t(x)] = uint8_t(x + 1);
        t.mips.push_back(m);
    }
    return t;
}
rr::WorldQuad quad(std::array<double, 4> x, std::array<double, 4> y, std::array<double, 4> z,
                   std::array<std::array<double, 2>, 4> uv) {
    rr::WorldQuad q;
    q.x = x;
    q.y = y;
    for (size_t i = 0; i < 4; ++i) q.inv_z[i] = 1.0 / z[i];
    q.uv = uv;
    return q;
}
} // namespace

int main() {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << "\n";
        }
    };
    const auto texture = column_texture();
    const rr::RasterClip clip{0, 0, 320, 239};
    // 1. A ground strip (far row y=60, near row y=200) and the same strip
    // split at its perspective-correct middle depth.
    {
        const double zf = 8, zn = 2, zm = 2 * zf * zn / (zf + zn) * 1.0; // harmonic middle
        auto row_at = [&](double z) { return 60 + (200 - 60) * (1 / z - 1 / zf) / (1 / zn - 1 / zf); };
        auto x_at = [&](double world_x, double z) { return 160 + world_x * 40 / z; };
        auto corner_v = [&](double z) { return z * 8; };
        const double ym = row_at(zm);
        const auto whole = quad(
            {x_at(-30, zf), x_at(30, zf), x_at(30, zn), x_at(-30, zn)}, {60, 60, 200, 200}, {zf, zf, zn, zn},
            {{{-30, corner_v(zf)}, {30, corner_v(zf)}, {30, corner_v(zn)}, {-30, corner_v(zn)}}});
        const auto far = quad(
            {x_at(-30, zf), x_at(30, zf), x_at(30, zm), x_at(-30, zm)}, {60, 60, ym, ym}, {zf, zf, zm, zm},
            {{{-30, corner_v(zf)}, {30, corner_v(zf)}, {30, corner_v(zm)}, {-30, corner_v(zm)}}});
        const auto near = quad(
            {x_at(-30, zm), x_at(30, zm), x_at(30, zn), x_at(-30, zn)}, {ym, ym, 200, 200}, {zm, zm, zn, zn},
            {{{-30, corner_v(zm)}, {30, corner_v(zm)}, {30, corner_v(zn)}, {-30, corner_v(zn)}}});
        std::vector<uint8_t> a(320 * 240, 0), b(320 * 240, 0);
        std::vector<int> count(320 * 240, 0);
        rr::draw_world_quad(whole, texture, false, clip, 320, 240, nullptr,
                            [&](int x, int y, uint8_t i, double) { a[size_t(y) * 320 + size_t(x)] = i; });
        for (const auto* q : {&far, &near})
            rr::draw_world_quad(*q, texture, false, clip, 320, 240, nullptr,
                                [&](int x, int y, uint8_t i, double) {
                                    b[size_t(y) * 320 + size_t(x)] = i;
                                    ++count[size_t(y) * 320 + size_t(x)];
                                });
        size_t covered = 0, twice = 0, gaps = 0, differ = 0;
        for (size_t p = 0; p < a.size(); ++p) {
            covered += a[p] != 0;
            twice += count[p] > 1;
            gaps += a[p] != 0 && b[p] == 0;
            differ += a[p] != 0 && b[p] != 0 && std::abs(int(a[p]) - int(b[p])) > 1;
        }
        check(covered > 20000, "strip covers its area");
        check(twice == 0, "split strip: no pixel drawn twice");
        check(gaps == 0, "split strip: no gap");
        check(differ * 100 < covered, "split strip samples the undivided texels");
    }
    // 2. Wrap: u shifted by one period draws the same pixels.
    {
        const std::array<double, 4> x{40, 280, 280, 40}, y{20, 20, 220, 220}, z{4, 4, 4, 4};
        std::vector<uint8_t> a(320 * 240, 0), b(320 * 240, 0);
        rr::draw_world_quad(quad(x, y, z, {{{0, 0}, {48, 0}, {48, 12}, {0, 12}}}), texture, false, clip, 320,
                            240, nullptr,
                            [&](int px, int py, uint8_t i, double) { a[size_t(py) * 320 + size_t(px)] = i; });
        rr::draw_world_quad(quad(x, y, z, {{{64, 16}, {112, 16}, {112, 28}, {64, 28}}}), texture, false, clip,
                            320, 240, nullptr,
                            [&](int px, int py, uint8_t i, double) { b[size_t(py) * 320 + size_t(px)] = i; });
        check(a == b, "uv wraps within the bounds");
    }
    // 3. Road anchoring: the same ground point from two camera positions.
    {
        rr::DepthTable depths;
        for (int i = 0; i < 4352; ++i) depths.scale.push_back(i ? (1 << 24) / std::max(2 * i, 102) : 163840);
        const rr::Camera camera{0, 150};
        auto project = [&](int32_t world_x, int32_t depth) {
            const int32_t scale = depths.scale[size_t(depth & ~1) / 2];
            return rr::RasterPoint{int32_t(160 * 65536 + int64_t(world_x) * scale),
                                   int32_t(int64_t(camera.height) * scale + rr::view_horizon * 65536)};
        };
        auto uv_of = [&](uint32_t far_sample, int32_t far_depth) {
            rr::DrawNode n;
            n.texture = &texture;
            const int32_t near_depth = far_depth - 256;
            n.view_points = {project(-200, far_depth), project(200, far_depth), project(200, near_depth),
                             project(-200, near_depth)};
            n.view_depth = {far_depth, far_depth, near_depth, near_depth};
            rr::attach_world_uv(n, {far_depth, near_depth, far_sample, far_sample - 1, 100, 100}, camera,
                                depths, 320);
            return n.world_uv;
        };
        // Sample 40 at depth 2048 (camera at sample 32), then at 1536 (camera at 34).
        const auto a = uv_of(40, 2048), b = uv_of(40, 1536);
        check(a && b, "world uv attached");
        if (a && b) {
            const double du = std::fmod(double((*a)[0][0]) - double((*b)[0][0]) + 640.0, 64.0);
            const double dv = std::fmod(double((*a)[0][1]) - double((*b)[0][1]) + 1600.0, 16.0);
            check(std::min(du, 64 - du) < 0.5 && std::min(dv, 16 - dv) < 0.5, "ground point keeps its texel");
        }
        rr::DrawNode flat;
        rr::attach_world_uv(flat, {2048, 1792, 40, 39, 0, 0}, camera, depths, 320);
        check(!flat.world_uv, "untextured node stays untouched");
    }
    // 4. Renderer: world_uv is ignored without a blend table.
    {
        rr::DrawNode n;
        n.texture = &texture;
        n.view_points = {rr::RasterPoint{20 << 16, 20 << 16},
                         {200 << 16, 20 << 16},
                         {200 << 16, 200 << 16},
                         {20 << 16, 200 << 16}};
        n.view_depth = {512, 512, 512, 512};
        rr::Framebuffer a(320, 240), b(320, 240);
        rr::render_nodes(a, {n}, rr::view_mode(false), 16);
        n.world_uv = std::array<std::array<float, 2>, 4>{{{0, 0}, {5, 0}, {5, 5}, {0, 5}}};
        rr::render_nodes_perspective(b, {n}, rr::view_mode(false), 16, false, nullptr);
        bool same = true;
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 320; ++x) same &= a.row(y)[x] == b.row(y)[x];
        check(same, "off: original raster unchanged");
    }
    // 5. Settings default and round trip.
    {
        check(rr::parse_settings("").smooth_terrain, "smooth_terrain defaults on");
        auto s = rr::parse_settings("smooth_terrain=0\n");
        check(!s.smooth_terrain, "smooth_terrain=0 parses");
        check(!rr::parse_settings(rr::format_settings(s)).smooth_terrain, "smooth_terrain round trips");
    }
    if (failures) return 1;
    std::cout << "world-texture: all checks passed\n";
    return 0;
}
