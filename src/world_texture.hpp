#pragma once
// Native terrain texturing (not original). The original maps each terrain
// quad's whole texture onto its four corners; thin hill strips squash and
// stretch it per slice, which reads as streaks. This path tiles the texture
// continuously in road space (lateral offset and route distance), perspective
// correct, wrapping within the texture bounds, with a mip level per span.
// Settings::smooth_terrain selects it; off keeps the original raster.
#include "cel.hpp"
#include "framebuffer.hpp"
#include "raster.hpp"
#include "road.hpp"
#include "world_renderer.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace rr {
// Road-space scale: world units per texel and per route sample.
constexpr double world_units_per_texel = 4.0;
constexpr double world_units_per_sample = 320.0;

// Corner frame of one slice pair, near and far: Q8 depth, absolute route
// sample and road centre (road_x + camera.x, world units).
struct WorldSlicePair {
    int32_t far_depth = 0, near_depth = 0;
    uint32_t far_sample = 0, near_sample = 0;
    int32_t far_centre = 0, near_centre = 0;
};
// Inverts the scene projection (px = centre + (x - camera.x) * scale,
// py = (camera.height - y) * scale + horizon) at each corner's view_depth
// and stores road-space texel coordinates in node.world_uv. Leaves the node
// unchanged without a texture or with a corner depth outside the table.
void attach_world_uv(DrawNode&, const WorldSlicePair&, const Camera&, const DepthTable&,
                     int32_t logical_width);

struct WorldQuad {
    std::array<double, 4> x{}, y{}; // surface pixels
    std::array<double, 4> inv_z{};  // 1 / depth
    std::array<std::array<double, 2>, 4> uv{};
};
// Builds the surface-space quad (mode maps Q16 view coordinates).
WorldQuad world_quad(const DrawNode&, const ViewMode&);

// Palette averaging for anisotropic samples: palette RGB and a 15-bit RGB
// to nearest-index table, rebuilt when the palette bytes change.
struct WorldBlend {
    std::array<uint32_t, 256> rgb{};
    std::vector<uint8_t> inverse; // 32768 entries, index by r5 << 10 | g5 << 5 | b5
};
const WorldBlend& world_blend(const Palette&);

// Two-triangle perspective raster over the clip; write(x, y, index, inv_z) per
// covered pixel (plus its 1/z) whose texel is opaque (or any texel when !transparent).
// With a blend table, the mip comes from the short footprint axis (capped
// so a level keeps at least 8 rows) and up to 8 samples along the long axis
// are averaged; without one, one nearest sample from the long axis level.
template <class Write>
void draw_world_quad(const WorldQuad& q, const CelTexture& texture, bool transparent, RasterClip clip,
                     int fb_width, int fb_height, const WorldBlend* blend, Write&& write) {
    if (texture.mips.empty()) return;
    const auto& b = texture.bounds;
    const int32_t bu0 = std::min(b[0], b[2]), bv0 = std::min(b[1], b[3]);
    const double period_u = double(std::abs(int64_t(b[2]) - b[0]) + 1),
                 period_v = double(std::abs(int64_t(b[3]) - b[1]) + 1);
    int levels = 1;
    while (levels < int(texture.mips.size()) && texture.mips[size_t(levels)].height >= 8 &&
           texture.mips[size_t(levels)].width >= 8)
        ++levels;
    auto texel = [&](int level, double u, double v) {
        const auto& mip = texture.mips[size_t(level)];
        const double fu = u - period_u * std::floor(u / period_u);
        const double fv = v - period_v * std::floor(v / period_v);
        const int tu = std::min(std::max((bu0 + int(fu)) >> level, 0), mip.width - 1);
        const int tv = std::min(std::max((bv0 + int(fv)) >> level, 0), mip.height - 1);
        return mip.pixels[size_t(tv) * size_t(mip.width) + size_t(tu)];
    };
    const int top_clip = std::max(0, clip.top), bottom_clip = std::min(fb_height - 1, clip.bottom);
    const int right_clip = std::min(fb_width, clip.right); // exclusive
    const int left_clip = std::max(0, clip.left);
    auto triangle = [&](int i0, int i1, int i2) {
        const double x0 = q.x[size_t(i0)], y0 = q.y[size_t(i0)], x1 = q.x[size_t(i1)], y1 = q.y[size_t(i1)],
                     x2 = q.x[size_t(i2)], y2 = q.y[size_t(i2)];
        const double area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
        if (std::abs(area) < 1e-9) return;
        // Plane coefficients a + bx*x + by*y for attribute values at the corners.
        auto plane = [&](double f0, double f1, double f2, double& bx, double& by, double& a) {
            bx = ((f1 - f0) * (y2 - y0) - (f2 - f0) * (y1 - y0)) / area;
            by = ((f2 - f0) * (x1 - x0) - (f1 - f0) * (x2 - x0)) / area;
            a = f0 - bx * x0 - by * y0;
        };
        const double w0 = q.inv_z[size_t(i0)], w1 = q.inv_z[size_t(i1)], w2 = q.inv_z[size_t(i2)];
        double wbx, wby, wa, ubx, uby, ua, vbx, vby, va;
        plane(w0, w1, w2, wbx, wby, wa);
        plane(q.uv[size_t(i0)][0] * w0, q.uv[size_t(i1)][0] * w1, q.uv[size_t(i2)][0] * w2, ubx, uby, ua);
        plane(q.uv[size_t(i0)][1] * w0, q.uv[size_t(i1)][1] * w1, q.uv[size_t(i2)][1] * w2, vbx, vby, va);
        const int ymin = std::max(top_clip, int(std::ceil(std::min({y0, y1, y2}) - 0.5)));
        const int ymax = std::min(bottom_clip, int(std::ceil(std::max({y0, y1, y2}) - 0.5)) - 1);
        const std::array<std::array<double, 2>, 3> v = {{{x0, y0}, {x1, y1}, {x2, y2}}};
        const double orient = area > 0 ? 1.0 : -1.0;
        for (int y = ymin; y <= ymax; ++y) {
            const double cy = y + 0.5;
            double lo = -1e30, hi = 1e30;
            bool empty = false;
            for (int e = 0; e < 3; ++e) {
                const auto& p = v[size_t(e)];
                const auto& n = v[size_t((e + 1) % 3)];
                // Inside when orient * cross(n - p, c - p) >= 0, linear in cx.
                const double ex = n[0] - p[0], ey = n[1] - p[1];
                const double k = -ey * orient, c = (ex * (cy - p[1]) + ey * p[0]) * orient;
                if (std::abs(k) < 1e-12) {
                    if (c < 0) empty = true;
                } else if (k > 0)
                    lo = std::max(lo, -c / k);
                else
                    hi = std::min(hi, -c / k);
            }
            if (empty || lo >= hi) continue;
            const int xa = std::max(left_clip, int(std::ceil(lo - 0.5)));
            const int xb = std::min(right_clip, int(std::ceil(hi - 0.5)));
            int level = 0, taps = 1;
            double step_u = 0, step_v = 0;
            for (int x = xa; x < xb; ++x) {
                const double cx = x + 0.5;
                const double w = wa + wbx * cx + wby * cy;
                if (w <= 0) continue;
                const double U = ua + ubx * cx + uby * cy, V = va + vbx * cx + vby * cy;
                const double u = U / w, vv = V / w;
                if (((x - xa) & 7) == 0) {
                    // Texel footprint per pixel from the screen derivatives.
                    const double w2 = w * w;
                    const double dudx = (ubx * w - U * wbx) / w2, dvdx = (vbx * w - V * wbx) / w2;
                    const double dudy = (uby * w - U * wby) / w2, dvdy = (vby * w - V * wby) / w2;
                    const double ax = std::sqrt(dudx * dudx + dvdx * dvdx),
                                 ay = std::sqrt(dudy * dudy + dvdy * dvdy);
                    const double major = std::max(ax, ay), minor = std::min(ax, ay);
                    const double lod = blend ? minor : major;
                    level = lod > 1.0 ? std::min(levels - 1, int(std::log2(lod))) : 0;
                    taps = 1;
                    if (blend) {
                        // Samples along the long axis, one per level texel.
                        const double texel_size = double(1 << level);
                        taps = std::min(8, std::max(1, int(std::ceil(major / texel_size))));
                        step_u = (ax >= ay ? dudx : dudy) / taps;
                        step_v = (ax >= ay ? dvdx : dvdy) / taps;
                    }
                }
                if (taps == 1) {
                    const uint8_t index = texel(level, u, vv);
                    if (index || !transparent) write(x, y, index, w);
                    continue;
                }
                uint32_t r = 0, g = 0, bl = 0, n = 0;
                for (int k = 0; k < taps; ++k) {
                    const double o = k - (taps - 1) * 0.5;
                    const uint8_t index = texel(level, u + step_u * o, vv + step_v * o);
                    if (!index && transparent) continue;
                    const uint32_t c = blend->rgb[index];
                    r += c >> 16 & 0xff;
                    g += c >> 8 & 0xff;
                    bl += c & 0xff;
                    ++n;
                }
                if (!n) continue;
                r /= n;
                g /= n;
                bl /= n;
                write(x, y, blend->inverse[size_t((r >> 3) << 10 | (g >> 3) << 5 | (bl >> 3))], w);
            }
        }
    };
    triangle(0, 1, 2);
    triangle(0, 2, 3);
}
} // namespace rr
