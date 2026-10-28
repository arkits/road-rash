#include "world_texture.hpp"
#include <algorithm>
#include <cmath>

#include <mutex>
namespace rr {
void attach_world_uv(DrawNode& node, const WorldSlicePair& pair, const Camera& camera,
                     const DepthTable& depths, int32_t logical_width) {
    if (!node.texture || node.texture->mips.empty()) return;
    const auto& b = node.texture->bounds;
    const double period_u = double(std::abs(int64_t(b[2]) - b[0]) + 1),
                 period_v = double(std::abs(int64_t(b[3]) - b[1]) + 1);
    const double centre = double((logical_width & ~1) * 32768);
    const double span = double(pair.near_depth) - double(pair.far_depth);
    std::array<std::array<double, 2>, 4> uv{};
    for (size_t i = 0; i < 4; ++i) {
        const int32_t d = node.view_depth[i];
        const size_t index = size_t(d & ~1) / 2;
        if (d <= 0 || index >= depths.scale.size() || depths.scale[index] <= 0) return;
        const double scale = double(depths.scale[index]); // Q16 view pixels per world unit
        const double x = (double(node.view_points[i].x) - centre) / scale;
        const double y =
            double(camera.height) - (double(node.view_points[i].y) - double(view_horizon) * 65536.0) / scale;
        // Linear frame between the slice pair (corners lie on or between them).
        const double t = span != 0 ? (double(d) - double(pair.far_depth)) / span : 0.0;
        const double road = double(pair.far_centre) +
                            (double(pair.near_centre) - double(pair.far_centre)) * t - double(camera.x);
        const double along = double(pair.far_sample) * 256.0 + (double(d) - double(pair.far_depth));
        uv[i] = {(x - road) / world_units_per_texel,
                 (along / 256.0 * world_units_per_sample + y) / world_units_per_texel};
    }
    // Keep the floats small: shift by whole texture periods.
    const double su = period_u * std::floor(std::min({uv[0][0], uv[1][0], uv[2][0], uv[3][0]}) / period_u);
    const double sv = period_v * std::floor(std::min({uv[0][1], uv[1][1], uv[2][1], uv[3][1]}) / period_v);
    std::array<std::array<float, 2>, 4> out{};
    for (size_t i = 0; i < 4; ++i) out[i] = {float(uv[i][0] - su), float(uv[i][1] - sv)};
    node.world_uv = out;
}

const WorldBlend& world_blend(const Palette& palette) {
    // One table per palette; rebuilt only when the palette bytes change.
    static std::mutex lock;
    static std::array<uint8_t, 1024> key{};
    static WorldBlend table;
    std::lock_guard<std::mutex> guard(lock);
    if (!table.inverse.empty() && key == palette.rgbx) return table;
    key = palette.rgbx;
    for (size_t i = 0; i < 256; ++i)
        table.rgb[i] = uint32_t(palette.rgbx[4 * i]) << 16 | uint32_t(palette.rgbx[4 * i + 1]) << 8 |
                       palette.rgbx[4 * i + 2];
    table.inverse.assign(32768, 0);
    for (uint32_t c = 0; c < 32768; ++c) {
        const int r = int((c >> 10) << 3 | 4), g = int(((c >> 5) & 31) << 3 | 4), b = int((c & 31) << 3 | 4);
        int best = 1, distance = 1 << 30;
        for (int i = 1; i < 256; ++i) { // index 0 is the transparent/black key
            const int dr = r - int(table.rgb[size_t(i)] >> 16 & 0xff),
                      dg = g - int(table.rgb[size_t(i)] >> 8 & 0xff),
                      db = b - int(table.rgb[size_t(i)] & 0xff);
            const int d = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
            if (d < distance) {
                distance = d;
                best = i;
            }
        }
        table.inverse[c] = uint8_t(best);
    }
    return table;
}

WorldQuad world_quad(const DrawNode& node, const ViewMode& mode) {
    WorldQuad q;
    const double divisor = std::ldexp(1.0, mode.coordinate_shift);
    for (size_t i = 0; i < 4; ++i) {
        q.x[i] = double(node.view_points[i].x) * mode.multiplier / divisor;
        q.y[i] = double(node.view_points[i].y) * mode.multiplier / divisor;
        q.inv_z[i] = node.view_depth[i] > 0 ? 256.0 / double(node.view_depth[i]) : 0.0;
        q.uv[i] = {double((*node.world_uv)[i][0]), double((*node.world_uv)[i][1])};
    }
    return q;
}
} // namespace rr
