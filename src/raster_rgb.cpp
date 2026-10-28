// Plan 21 Task 4: RGB raster implementation.
#include "raster_rgb.hpp"
#include <cstdlib>
#include "fog.hpp"
#include <cstring>
#include "texture.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace rr {
namespace {
// Straight-alpha 0xAARRGGBB -> premultiplied doubles.
struct Prem {
    double r = 0, g = 0, b = 0, a = 0;
};
Prem to_prem(uint32_t p) {
    const double a = double(p >> 24) / 255.0;
    return {double((p >> 16) & 0xff) * a, double((p >> 8) & 0xff) * a, double(p & 0xff) * a, a};
}
uint32_t from_prem(Prem p) {
    // Alpha-tested on premultiplied texels (plan Review Focus 2): discard if
    // alpha < 0.5, then un-premultiply. No dark halos.
    if (p.a < 0.5) return 0;
    const double inv = 1.0 / p.a;
    const auto c = [&](double v) { return uint32_t(std::max(0.0, std::min(255.0, v * inv))); };
    return (uint32_t(std::max(0.0, std::min(255.0, p.a * 255.0))) << 24) | (c(p.r) << 16) | (c(p.g) << 8) |
           c(p.b);
}
Prem sample_level(const RgbaMip& mip, double u, double v) {
    // Texel-centre convention: texel i covers [i - 0.5, i + 0.5). Clamp-to-edge.
    const double x = std::max(0.0, std::min(double(mip.width - 1), u - 0.5));
    const double y = std::max(0.0, std::min(double(mip.height - 1), v - 0.5));
    const int x0 = int(x), y0 = int(y);
    const int x1 = std::min(mip.width - 1, x0 + 1), y1 = std::min(mip.height - 1, y0 + 1);
    const double fx = x - x0, fy = y - y0;
    Prem p00 = to_prem(mip.premultiplied[size_t(y0) * mip.width + x0]);
    Prem p10 = to_prem(mip.premultiplied[size_t(y0) * mip.width + x1]);
    Prem p01 = to_prem(mip.premultiplied[size_t(y1) * mip.width + x0]);
    Prem p11 = to_prem(mip.premultiplied[size_t(y1) * mip.width + x1]);
    Prem o;
    o.r = (p00.r * (1 - fx) + p10.r * fx) * (1 - fy) + (p01.r * (1 - fx) + p11.r * fx) * fy;
    o.g = (p00.g * (1 - fx) + p10.g * fx) * (1 - fy) + (p01.g * (1 - fx) + p11.g * fx) * fy;
    o.b = (p00.b * (1 - fx) + p10.b * fx) * (1 - fy) + (p01.b * (1 - fx) + p11.b * fx) * fy;
    o.a = (p00.a * (1 - fx) + p10.a * fx) * (1 - fy) + (p01.a * (1 - fx) + p11.a * fx) * fy;
    return o;
}
Prem sample_nearest(const RgbaMip& mip, double u, double v) {
    const int x = std::max(0, std::min(mip.width - 1, int(std::floor(u))));
    const int y = std::max(0, std::min(mip.height - 1, int(std::floor(v))));
    return to_prem(mip.premultiplied[size_t(y) * mip.width + x]);
}
// Edge walk over x (double) + u/v/fog (double), same fill rules as affine.
struct RgbEdge {
    double x = 0, u = 0, v = 0, fog = 0;
};
// Rows outside [row_lo, row_hi] are not written (the caller's clip); each
// row's value is computed directly from its vertex, so clipping is exact.
void rgb_chain(std::vector<RgbEdge>& rows, std::vector<char>& set, const std::array<RgbVertex, 4>& verts,
               int first, int bottom, int direction, int row_lo, int row_hi) {
    int index = first;
    for (int guard = 0; guard < 4; ++guard) {
        const int next = (index + direction + 4) % 4;
        if (verts[size_t(next)].y < verts[size_t(index)].y)
            throw std::runtime_error("RGB quad requires normalized monotone edges");
        const auto& a = verts[size_t(index)];
        const auto& b = verts[size_t(next)];
        const double dy = double(b.y) - double(a.y);
        if (dy > 0) {
            const double sx = (double(b.x) - a.x) / dy, su = (double(b.u) - a.u) / dy;
            const double sv = (double(b.v) - a.v) / dy, sf = (double(b.fog_q8) - a.fog_q8) / dy;
            const int y0 = std::max(row_lo, a.y), y1 = std::min(row_hi, b.y);
            for (int y = y0; y <= y1; ++y) {
                const double t = double(y) - double(a.y);
                rows[size_t(y)] = {double(a.x) + sx * t, double(a.u) + su * t, double(a.v) + sv * t,
                                   double(a.fog_q8) + sf * t};
                set[size_t(y)] = 1;
            }
        }
        index = next;
        if (verts[size_t(index)].y == bottom) break;
    }
}
using Sampler = Prem (*)(const RgbaMip&, double, double);
// Local copies of the affine Q16 helpers (texture.cpp keeps its own statics).
// Integer-identical by construction; do not "improve".
namespace q16 {
int32_t sword(uint32_t v) {
    int32_t r;
    std::memcpy(&r, &v, 4);
    return r;
}
int32_t wadd(int32_t a, int32_t b) {
    return sword(uint32_t(a) + uint32_t(b));
}
int32_t wsub(int32_t a, int32_t b) {
    return sword(uint32_t(a) - uint32_t(b));
}
int32_t wmul(int32_t a, int32_t b) {
    return sword(uint32_t(a) * uint32_t(b));
}
int32_t fix(int32_t a) {
    return sword(uint32_t(a) << 16);
}
int32_t pq16(int32_t a, int32_t b) {
    return sword(uint32_t(uint64_t(int64_t(a) * b) >> 16));
}
int32_t flr(int32_t a) {
    return a >= 0 ? a / 65536 : int32_t((int64_t(a) - 65535) / 65536);
}
struct Edge {
    int32_t x = 0, u = 0, v = 0;
};
void edge(Edge* rows, const TextureVertex& from, const TextureVertex& to) {
    const int32_t dy = fix(int32_t(to.y) - from.y);
    if (!dy || to.y < from.y) return;
    const int32_t reciprocal = int32_t(0x100000000LL / dy);
    Edge at{fix(from.x), fix(from.u), fix(from.v)};
    const Edge step{pq16(fix(int32_t(to.x) - from.x), reciprocal),
                    pq16(fix(int32_t(to.u) - from.u), reciprocal),
                    pq16(fix(int32_t(to.v) - from.v), reciprocal)};
    const int first = std::max(0, int(from.y)), last = std::min(479, int(to.y));
    const int skip = first - from.y;
    at.x = wadd(at.x, wmul(step.x, skip));
    at.u = wadd(at.u, wmul(step.u, skip));
    at.v = wadd(at.v, wmul(step.v, skip));
    for (int y = first; y <= last; ++y) {
        rows[y] = at;
        at.x = wadd(at.x, step.x);
        at.u = wadd(at.u, step.u);
        at.v = wadd(at.v, step.v);
    }
}
} // namespace q16
void draw_rgb_impl(FramebufferRgb& fb, const std::array<RgbVertex, 4>& verts, const RgbaMip* mips,
                   size_t count, size_t level0, size_t level1, double level_frac, bool transparent,
                   RasterClip clip, Sampler sample, FogState fog = {}) {
    int first = 0, bottom = verts[0].y;
    for (int i = 1; i < 4; ++i) {
        if (verts[i].y < verts[first].y) first = i;
        bottom = std::max(bottom, verts[i].y);
    }
    if (bottom == verts[first].y) return;
    // Reused per-thread edge buffers (no per-quad allocation; Task 3 bands
    // share one thread each, so thread_local is band-safe).
    thread_local std::vector<RgbEdge> left, right;
    thread_local std::vector<char> lset, rset;
    const size_t hh = size_t(fb.height());
    if (left.size() < hh) {
        left.resize(hh);
        right.resize(hh);
        lset.resize(hh);
        rset.resize(hh);
    }
    // Only rows [top, last] are read below, so only they are reset/walked.
    const int top = std::max({0, verts[size_t(first)].y, clip.top});
    const int last = std::min({bottom, clip.bottom, fb.height() - 1});
    if (top > last) return;
    std::fill(lset.begin() + top, lset.begin() + last + 1, 0);
    std::fill(rset.begin() + top, rset.begin() + last + 1, 0);
    rgb_chain(left, lset, verts, first, bottom, -1, top, last);
    rgb_chain(right, rset, verts, first, bottom, 1, top, last);
    const int bound = std::min(clip.right, fb.width());
    if (level0 >= count || level1 >= count) throw std::runtime_error("RGB mip level out of range");
    const RgbaMip& m0 = mips[level0];
    const RgbaMip& m1 = mips[level1];
    for (int y = top; y <= last; ++y) {
        if (!lset[size_t(y)] || !rset[size_t(y)]) continue;
        auto l = left[size_t(y)], r = right[size_t(y)];
        if (int(std::floor(r.x)) <= 0) continue;
        if (int(std::floor(r.x)) < int(std::floor(l.x))) std::swap(l, r);
        int start = int(std::floor(l.x)), end = int(std::floor(r.x));
        if (end - start <= 0 || start >= bound) continue;
        if (start < 0) start = 0;
        end = std::min(end, bound);
        const double span = double(int(std::floor(r.x)) - int(std::floor(l.x)));
        for (int x = start; x < end; ++x) {
            const double t = span > 0 ? (double(x) - std::floor(l.x)) / span : 0;
            const double u = l.u + (r.u - l.u) * t, v = l.v + (r.v - l.v) * t;
            Prem p0 = sample(m0, u, v);
            Prem p;
            if (level1 != level0) {
                Prem p1 = sample(m1, u / 2.0, v / 2.0); // coarser level is half res
                const double f = level_frac;
                p = {p0.r * (1 - f) + p1.r * f, p0.g * (1 - f) + p1.g * f, p0.b * (1 - f) + p1.b * f,
                     p0.a * (1 - f) + p1.a * f};
            } else
                p = p0;
            uint32_t out = from_prem(p);
            if (fog.on && (out >> 24)) {
                // Blend toward the fog colour by the interpolated factor.
                const double f = std::max(0.0, std::min(1.0, (l.fog + (r.fog - l.fog) * t) / 256.0));
                const int fr = int((fog.colour >> 16) & 0xff), fg2 = int((fog.colour >> 8) & 0xff),
                          fb2 = int(fog.colour & 0xff);
                const int pr = int((out >> 16) & 0xff), pg = int((out >> 8) & 0xff), pb = int(out & 0xff);
                const auto blend_ch = [&](int src, int dst) {
                    return uint32_t(std::max(0, std::min(255, int(src + (dst - src) * f))));
                };
                out = (out & 0xff000000u) | (blend_ch(pr, fr) << 16) | (blend_ch(pg, fg2) << 8) |
                      blend_ch(pb, fb2);
            }
            if ((out >> 24) || !transparent) fb.row(y)[x] = out;
        }
    }
}
} // namespace

std::vector<RgbaMip> rgba_mips_for_test(const CelMip& mip, const Palette& palette) {
    RgbaMip out;
    out.width = mip.width;
    out.height = mip.height;
    out.premultiplied.resize(size_t(mip.width) * mip.height);
    for (int y = 0; y < mip.height; ++y)
        for (int x = 0; x < mip.width; ++x) {
            const uint8_t idx = mip.pixels[size_t(y) * mip.width + x];
            if (idx == 0) {
                out.premultiplied[size_t(y) * mip.width + x] = 0;
                continue;
            }
            const uint8_t* c = &palette.rgbx[4 * idx];
            const uint32_t px =
                (255u << 24) | (uint32_t(c[0]) << 16) | (uint32_t(c[1]) << 8) | uint32_t(c[2]);
            const Prem p = to_prem(px);
            // Store premultiplied as packed: re-premultiply on sample; keep exact bytes here.
            out.premultiplied[size_t(y) * mip.width + x] = px;
            (void)p;
        }
    return {out};
}

namespace {
// Mip cache keyed on (texture pointer, palette generation, level).
// Level 0 is the base chain; level k+1 is upscaled[k] converted dense
// (same bytes as the old per-node lambda). Keys never collide (Focus 2).
std::mutex g_mutex;
std::map<std::tuple<const CelTexture*, uint32_t, int>, std::vector<RgbaMip>> g_cache;
std::vector<RgbaMip> convert_mips(const CelTexture& tex, const Palette& palette, int level) {
    if (level > 0) {
        const auto& um = tex.upscaled.at(size_t(level) - 1);
        CelMip dense;
        dense.width = um.width;
        dense.height = um.height;
        dense.stride_shift = um.stride_shift;
        dense.pixels = um.pixels;
        return rgba_mips_for_test(dense, palette);
    }
    std::vector<RgbaMip> out;
    for (const auto& m : tex.mips) {
        RgbaMip r;
        r.width = m.width;
        r.height = m.height;
        r.premultiplied.resize(size_t(m.width) * m.height);
        const int stride = 1 << m.stride_shift;
        const auto& src = m.sampling_storage.empty() ? m.pixels : m.sampling_storage;
        for (int y = 0; y < m.height; ++y)
            for (int x = 0; x < m.width; ++x) {
                const uint8_t idx = src[size_t(y) * stride + x];
                if (idx == 0) {
                    r.premultiplied[size_t(y) * m.width + x] = 0;
                    continue;
                }
                const uint8_t* c = &palette.rgbx[4 * idx];
                r.premultiplied[size_t(y) * m.width + x] =
                    (255u << 24) | (uint32_t(c[0]) << 16) | (uint32_t(c[1]) << 8) | uint32_t(c[2]);
            }
        out.push_back(std::move(r));
    }
    return out;
}
} // namespace

uint32_t palette_generation(const Palette& p) {
    uint32_t h = 2166136261u;
    for (uint8_t b : p.rgbx) {
        h ^= b;
        h *= 16777619u;
    }
    return h;
}
void clear_rgba_mip_cache() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cache.clear();
}
size_t rgba_mip_cache_size() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_cache.size();
}
const std::vector<RgbaMip>& rgba_mips(const CelTexture& tex, const Palette& palette, uint32_t generation) {
    return rgba_mips(tex, palette, generation, 0);
}
const std::vector<RgbaMip>& rgba_mips(const CelTexture& tex, const Palette& palette, uint32_t generation,
                                      int level) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto key = std::make_tuple(&tex, generation, level);
    const auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;
    return g_cache.emplace(key, convert_mips(tex, palette, level)).first->second;
}

void draw_rgb_quad_bilinear(FramebufferRgb& fb, const std::array<RgbVertex, 4>& verts, const RgbaMip* mips,
                            size_t count, bool transparent, RasterClip clip, FogState fog) {
    draw_rgb_impl(fb, verts, mips, count, 0, 0, 0.0, transparent, clip, sample_level, fog);
}
// Nearest-exact path: the SAME Q16 edge/span arithmetic as draw_textured_quad,
// sinking palette colours instead of indices. Guarantees byte-identity with
// to_rgba(indexed) for filter=nearest.
void draw_rgb_quad_nearest_q16(FramebufferRgb& fb, const std::array<TextureVertex, 4>& vertices,
                               const uint8_t* pixels, size_t pixel_count, unsigned shift,
                               const Palette& palette, bool transparent, RasterClip clip) {
    if (clip.left != 0 || shift > 8 || !pixel_count || pixel_count % (size_t(1) << shift))
        throw std::runtime_error("Invalid textured quad storage/clip");
    int first = 0, bottom = vertices[0].y;
    for (int i = 1; i < 4; ++i) {
        if (vertices[i].y < vertices[first].y) first = i;
        bottom = std::max(bottom, int(vertices[i].y));
    }
    if (bottom == vertices[first].y) return;
    q16::Edge left[480]{}, right[480]{};
    auto chain = [&](q16::Edge* rows, int direction) {
        int index = first;
        for (int guard = 0; guard < 4; ++guard) {
            const int next = (index + direction + 4) % 4;
            if (vertices[size_t(next)].y < vertices[size_t(index)].y)
                throw std::runtime_error("Textured quad requires normalized monotone edges");
            q16::edge(rows, vertices[size_t(index)], vertices[size_t(next)]);
            index = next;
            if (vertices[size_t(index)].y == bottom) return;
        }
        throw std::runtime_error("Invalid texture edge chain");
    };
    chain(left, -1);
    chain(right, 1);
    const int top = std::max({0, int(vertices[first].y), int(clip.top)});
    const int last = std::min({479, bottom, int(clip.bottom), fb.height() - 1});
    const int bound = std::min(int(clip.right), fb.width());
    for (int y = top; y <= last; ++y) {
        auto l = left[y], r = right[y];
        if (q16::flr(r.x) <= 0) continue;
        if (q16::flr(r.x) < q16::flr(l.x)) std::swap(l, r);
        int start = q16::flr(l.x), end = q16::flr(r.x);
        const int length = end - start;
        if (!length || start >= bound) continue;
        const int32_t reciprocal = 65536 / length;
        const int32_t du = q16::pq16(q16::wsub(r.u, l.u), reciprocal);
        const int32_t dv = q16::pq16(q16::wsub(r.v, l.v), reciprocal);
        if (start < 0) {
            const int skip = -start;
            l.u = q16::wadd(l.u, q16::wmul(du, skip));
            l.v = q16::wadd(l.v, q16::wmul(dv, skip));
            start = 0;
        }
        end = std::min(end, bound);
        for (int x = start; x < end; ++x) {
            const uint32_t uu = uint32_t(l.u), vv = uint32_t(l.v);
            const uint32_t address = (((vv & 0xffff0000u) << shift) + uu) >> 16;
            if (address >= pixel_count) throw std::runtime_error("Texture span coordinate outside storage");
            const uint8_t index = pixels[address];
            if (index || !transparent) {
                const uint8_t* c = &palette.rgbx[4 * index];
                fb.row(y)[x] = (255u << 24) | (uint32_t(c[0]) << 16) | (uint32_t(c[1]) << 8) | uint32_t(c[2]);
            }
            l.u = q16::wadd(l.u, du);
            l.v = q16::wadd(l.v, dv);
        }
    }
}
// Wide-surface nearest-exact sink: mirrors draw_textured_quad_wide's chain
// merge (64-bit edges) with a palette sink. Guarantees byte-identity at s > 1.
void draw_rgb_quad_nearest_wide(FramebufferRgb& fb, const std::array<TextureVertexWide, 4>& vertices,
                                const uint8_t* pixels, size_t pixel_count, unsigned stride_shift,
                                const Palette& palette, bool transparent, RasterClip clip,
                                RasterScratch64& scratch) {
    if (scratch.rows.empty()) throw std::runtime_error("Raster scratch not sized");
    if (stride_shift > 8 || !pixel_count || pixel_count % (size_t(1) << stride_shift))
        throw std::runtime_error("Invalid textured quad storage/clip");
    constexpr int64_t kEmpty = int64_t(INT32_MIN) * 65536 * 4;
    const int64_t height = int64_t(scratch.rows.size());
    int first = 0;
    int64_t bottom = vertices[0].y;
    for (int i = 1; i < 4; ++i) {
        if (vertices[i].y < vertices[first].y) first = i;
        bottom = std::max(bottom, int64_t(vertices[i].y));
    }
    if (bottom == vertices[first].y) return;
    const size_t rows = scratch.rows.size();
    std::vector<TextureEdge64> saved_left(rows), saved_right(rows);
    for (size_t i = 0; i < rows; ++i) saved_left[i] = {kEmpty, 0, 0}, saved_right[i] = {kEmpty, 0, 0};
    auto chain = [&](std::vector<TextureEdge64>& save, int direction) {
        int index = first;
        RasterScratch64 side{int(height)}, seg{int(height)};
        for (int64_t y = 0; y < height; ++y) side.rows[size_t(y)] = {kEmpty, 0, 0};
        for (int guard = 0; guard < 4; ++guard) {
            const int next = (index + direction + 4) % 4;
            if (vertices[size_t(next)].y < vertices[size_t(index)].y)
                throw std::runtime_error("Textured quad requires normalized monotone edges");
            TextureVertexWide a{vertices[size_t(index)].x, vertices[size_t(index)].y,
                                vertices[size_t(index)].u, vertices[size_t(index)].v};
            TextureVertexWide b{vertices[size_t(next)].x, vertices[size_t(next)].y, vertices[size_t(next)].u,
                                vertices[size_t(next)].v};
            for (auto& e : seg.rows) e = {kEmpty, 0, 0};
            texture_edge_wide(seg, a, b);
            for (int64_t y = std::max<int64_t>(0, a.y); y <= std::min<int64_t>(height - 1, b.y); ++y)
                if (seg.rows[size_t(y)].x != kEmpty) side.rows[size_t(y)] = seg.rows[size_t(y)];
            index = next;
            if (vertices[size_t(index)].y == bottom) break;
        }
        save = std::move(side.rows);
    };
    chain(saved_left, -1);
    chain(saved_right, 1);
    auto floor64 = [](int64_t a) { return a >= 0 ? a / 65536 : int64_t((__int128(a) - 65535) / 65536); };
    const int64_t top = std::max<int64_t>({0, vertices[size_t(first)].y, clip.top});
    const int64_t last = std::min<int64_t>({height - 1, bottom, clip.bottom, fb.height() - 1});
    const int64_t bound = std::min<int64_t>(clip.right, fb.width());
    const int64_t stride = int64_t(1) << stride_shift;
    for (int64_t y = top; y <= last; ++y) {
        auto l = saved_left[size_t(y)], r = saved_right[size_t(y)];
        if (l.x == kEmpty || r.x == kEmpty) continue;
        if (floor64(r.x) <= 0) continue;
        if (floor64(r.x) < floor64(l.x)) std::swap(l, r);
        int64_t start = floor64(l.x), end = floor64(r.x);
        const int64_t length = end - start;
        if (!length || start >= bound) continue;
        const int64_t reciprocal = 65536 / length;
        auto prod = [&](int64_t d) { return int64_t((__int128(d) * reciprocal) >> 16); };
        int64_t du = prod(int64_t(r.u) - l.u), dv = prod(int64_t(r.v) - l.v);
        int64_t cu = l.u, cv = l.v;
        if (start < 0) {
            const int64_t skip = -start;
            cu += du * skip;
            cv += dv * skip;
            start = 0;
        }
        end = std::min(end, bound);
        for (int64_t x = start; x < end; ++x) {
            const uint32_t uu = uint32_t(int32_t(cu)), vv = uint32_t(int32_t(cv));
            const uint32_t address = (((vv & 0xffff0000u) << stride_shift) + uu) >> 16;
            // NOTE: stride_shift<=8 so the shift form matches texture_span.
            const uint32_t addr = (((vv & 0xffff0000u) >> 16) * uint32_t(stride)) + (uu >> 16);
            (void)address;
            if (addr >= pixel_count) throw std::runtime_error("Texture span coordinate outside storage");
            const uint8_t index = pixels[addr];
            if (index || !transparent) {
                const uint8_t* c = &palette.rgbx[4 * index];
                fb.row(int(y))[x] =
                    (255u << 24) | (uint32_t(c[0]) << 16) | (uint32_t(c[1]) << 8) | uint32_t(c[2]);
            }
            cu += du;
            cv += dv;
        }
    }
}
// sampler for draw_rgb_impl nearest over RgbaMips (kept for the test-hook path)
void draw_rgb_impl_nearest(FramebufferRgb& fb, const std::array<RgbVertex, 4>& verts, const RgbaMip* mips,
                           size_t count, size_t level, bool transparent, RasterClip clip) {
    draw_rgb_impl(fb, verts, mips, count, level, level, 0.0, transparent, clip, sample_nearest);
}
void draw_rgb_quad_trilinear(FramebufferRgb& fb, const std::array<RgbVertex, 4>& verts, const RgbaMip* mips,
                             size_t count, bool transparent, RasterClip clip, float lod, FogState fog) {
    const size_t l0 = std::min(count - 1, size_t(lod));
    const size_t l1 = std::min(count - 1, l0 + 1);
    draw_rgb_impl(fb, verts, mips, count, l0, l1, std::max(0.0, std::min(1.0, double(lod) - l0)), transparent,
                  clip, sample_level, fog);
}

// Task 3: single-threaded resolve output. Everything the draw needs is
// computed up front (mapped points, mip pointers, fog corners); bands only
// clip rows, so the union is bit-identical to one thread (Focus 1: the edge
// walk still starts at the polygon top; the clip only skips writes).
struct ResolvedNode {
    enum class Kind { flat, trilinear, nearest_impl, nearest_q16, nearest_wide, bilinear } kind;
    std::array<RgbVertex, 4> pts{};
    const RgbaMip* mips = nullptr;
    size_t mip_count = 0, level = 0;
    float lod = 0;
    bool transparent = false;
    FogState fog{};
    uint32_t flat_argb = 0;
    // Nearest-indexed variants (palette sink over index pixels):
    std::array<TextureVertex, 4> nv{};
    std::array<TextureVertexWide, 4> nvw{};
    const uint8_t* srcpx = nullptr;
    size_t srcpx_size = 0;
    unsigned stride_shift = 0;
    const Palette* palette = nullptr;
    int miny = 0, maxy = 0; // mapped row range, for band culling
};
static void draw_resolved(FramebufferRgb& fb, const ResolvedNode& r, RasterClip clip,
                          RasterScratch64& scratch) {
    switch (r.kind) {
    case ResolvedNode::Kind::flat: {
        thread_local RgbaMip flat_mip{1, 1, {0}};
        flat_mip.premultiplied[0] = r.flat_argb;
        draw_rgb_impl(fb, r.pts, &flat_mip, 1, 0, 0, 0.0, false, clip, sample_nearest, r.fog);
        break;
    }
    case ResolvedNode::Kind::trilinear:
        draw_rgb_quad_trilinear(fb, r.pts, r.mips, r.mip_count, r.transparent, clip, r.lod, r.fog);
        break;
    case ResolvedNode::Kind::nearest_impl:
        draw_rgb_impl(fb, r.pts, r.mips, r.mip_count, r.level, r.level, 0.0, r.transparent, clip,
                      sample_nearest, r.fog);
        break;
    case ResolvedNode::Kind::nearest_q16:
        draw_rgb_quad_nearest_q16(fb, r.nv, r.srcpx, r.srcpx_size, r.stride_shift, *r.palette, r.transparent,
                                  clip);
        break;
    case ResolvedNode::Kind::nearest_wide:
        draw_rgb_quad_nearest_wide(fb, r.nvw, r.srcpx, r.srcpx_size, r.stride_shift, *r.palette,
                                   r.transparent, clip, scratch);
        break;
    case ResolvedNode::Kind::bilinear:
        draw_rgb_quad_bilinear(fb, r.pts, r.mips, r.mip_count, r.transparent, clip, r.fog);
        break;
    }
}

static void render_nodes_rgb_impl(FramebufferRgb& fb, const std::vector<DrawNode>& nodes,
                                  const ViewMode& mode, uint32_t flat_mask, const Palette& palette,
                                  const Enhancements& enh, RenderThreads& pool, bool fog_on,
                                  uint32_t fog_colour, int draw_slices) {
    if (fb.width() != mode.surface_width || fb.height() != mode.surface_height)
        throw std::runtime_error("View/RGB framebuffer size mismatch");
    const RasterClip clip{0, 0, fb.width(), fb.height() - 1};
    const uint32_t generation = palette_generation(palette); // per call; content-keyed
    // Reused per call (NOT thread_local: workers read it via the band
    // lambda, and thread_local captures resolve to the worker's own empty
    // instance). Resolve completes before dispatch; pool.run's mutex gives
    // the happens-before edge.
    static std::vector<ResolvedNode> resolved;
    resolved.clear();
    int node_miny = 0, node_maxy = 0;
    const auto push_node = [&](ResolvedNode& r) {
        r.miny = node_miny;
        r.maxy = node_maxy;
        resolved.push_back(r);
    };
    resolved.reserve(nodes.size());
    for (const auto& node : nodes) {
        if (!node.texture && !node.flat_index) continue;
        std::array<RgbVertex, 4> pts;
        for (size_t i = 0; i < 4; ++i)
            pts[i] = {mode.map(node.view_points[i].x), mode.map(node.view_points[i].y), 0, 0};
        int minx = pts[0].x, maxx = minx, miny = pts[0].y, maxy = miny;
        for (const auto& p : pts) {
            minx = std::min(minx, p.x);
            maxx = std::max(maxx, p.x);
            miny = std::min(miny, p.y);
            maxy = std::max(maxy, p.y);
        }
        if (minx > clip.right || maxx < clip.left || miny > clip.bottom || maxy < clip.top) continue;
        node_miny = miny;
        node_maxy = maxy; // normalize_quad permutes corners only
        if (node.texture && node.texture->mips.empty() && !(node.texture->flags & flat_mask)) continue;
        bool rgb_flip = node.flip_u;
        std::array<RasterPoint, 4> nq;
        for (size_t i = 0; i < 4; ++i)
            nq[i] = {mode.map(node.view_points[i].x), mode.map(node.view_points[i].y)};
        if (node.texture && !normalize_quad(nq, node.texture->flags, &rgb_flip)) continue;
        // Fog follows the normalize permutation: match reordered corners back
        // to originals by coordinates (first match; coincident corners share
        // an edge, so any match fogs correctly).
        uint16_t corner_fog[4] = {};
        if (fog_on) {
            for (size_t i = 0; i < 4; ++i) {
                const int32_t f = node.view_depth[i] > 0
                                      ? int32_t(fog_factor_q8(node.view_depth[i], draw_slices, enh.fog))
                                      : 0;
                corner_fog[i] = uint16_t(f);
            }
        }
        for (size_t i = 0; i < 4; ++i) {
            pts[i] = {nq[i].x, nq[i].y, 0, 0};
            if (fog_on) {
                for (size_t j = 0; j < 4; ++j) {
                    const auto ox = mode.map(node.view_points[j].x), oy = mode.map(node.view_points[j].y);
                    if (ox == nq[i].x && oy == nq[i].y) {
                        pts[i].fog_q8 = corner_fog[j];
                        break;
                    }
                }
            }
        }
        bool flip_u = rgb_flip;
        if (!node.texture || (node.texture->flags & flat_mask)) {
            uint8_t fi = node.texture ? node.texture->flat_index : *node.flat_index;
            const uint8_t* c = &palette.rgbx[4 * fi];
            const uint32_t argb =
                (255u << 24) | (uint32_t(c[0]) << 16) | (uint32_t(c[1]) << 8) | uint32_t(c[2]);
            // Flat fill over the mapped bbox-clipped quad is wrong; reuse the
            // edge walk with a 1x1 opaque mip (one per call, outside the loop).
            std::array<RgbVertex, 4> fp = pts;
            for (auto& v : fp) {
                v.u = 0;
                v.v = 0;
            }
            FogState flat_fog{fog_on && (fp[0].fog_q8 | fp[1].fog_q8 | fp[2].fog_q8 | fp[3].fog_q8),
                              fog_colour};
            ResolvedNode r;
            r.kind = ResolvedNode::Kind::flat;
            r.pts = fp;
            r.fog = flat_fog;
            r.flat_argb = argb;
            push_node(r);
            continue;
        }
        const auto& texture = *node.texture;
        if (texture.flags & 0x2000)
            throw std::runtime_error("Dynamic rider texture must be specialized before rendering");
        // UV selection mirrors the affine path (bounds inset by 1, flip aware).
        const auto& bounds = texture.bounds;
        const uint32_t src_w = uint32_t(std::abs(int64_t(bounds[2]) - bounds[0]) + 1);
        const uint32_t src_h = uint32_t(std::abs(int64_t(bounds[3]) - bounds[1]) + 1);
        // (flip already resolved into rgb_flip above; uvs follow it.)
        // the flag bit that swaps u is what matters for RGB (no wrap rounding).
        const uint16_t u0 = uint16_t(flip_u ? bounds[2] - 1 : bounds[0] + 1);
        const uint16_t u1 = uint16_t(flip_u ? bounds[0] + 1 : bounds[2] - 1);
        const uint16_t v0 = uint16_t(node.flip_v ? bounds[3] - 1 : bounds[1] + 1);
        const uint16_t v1 = uint16_t(node.flip_v ? bounds[1] + 1 : bounds[3] - 1);
        const uint16_t uu[4] = {u0, u1, u1, u0}, vv[4] = {v0, v0, v1, v1};
        const auto& mips = rgba_mips(texture, palette, generation);
        if (mips.empty()) continue;
        // Plan 20 Task 3: upscaled levels serve projections >= 2x the source
        // (same signed rule as the indexed path); uv scales by 2^(-level).
        const int signed_level =
            texture_mip_level_signed(uint32_t(maxx - minx + 1), uint32_t(maxy - miny + 1), src_w, src_h,
                                     texture.mips.size(), texture.upscaled.size());
        const std::vector<RgbaMip>* mip_set = &mips;
        size_t level = 0;
        unsigned uv_scale = 1;
        if (signed_level < 0) {
            const size_t up = size_t(-signed_level - 1);
            // Cached dense conversion (same bytes as the old per-node lambda).
            mip_set = &rgba_mips(texture, palette, generation, int(up) + 1);
            level = 0;
            uv_scale = unsigned(1) << (up + 1);
        } else {
            level = size_t(signed_level);
        }
        for (size_t i = 0; i < 4; ++i) {
            pts[i].u = float(uu[i] >> level) * uv_scale;
            pts[i].v = float(vv[i] >> level) * uv_scale;
        }
        const bool transparent = (texture.flags & 2) != 0;
        if (enh.filter == TextureFilter::trilinear && mips.size() > 1) {
            // Fractional LOD from the projected size (same choice as mip level).
            const double proj = double(std::max(maxx - minx + 1, maxy - miny + 1));
            const double src = double(std::max(src_w, src_h));
            const float lod = float(std::max(0.0, std::log2(src / std::max(1.0, proj))));
            FogState tex_fog{fog_on && (pts[0].fog_q8 | pts[1].fog_q8 | pts[2].fog_q8 | pts[3].fog_q8),
                             fog_colour};
            ResolvedNode rt;
            rt.kind = ResolvedNode::Kind::trilinear;
            rt.pts = pts;
            rt.mips = mip_set->data();
            rt.mip_count = mip_set->size();
            rt.transparent = transparent;
            rt.lod = lod;
            rt.fog = tex_fog;
            push_node(rt);
        } else if (enh.filter == TextureFilter::nearest) {
            // Fogged nearest goes through the float path (no exactness claim
            // with fog on); unfogged keeps the Q16-exact walk.
            const bool node_fog = fog_on && (pts[0].fog_q8 | pts[1].fog_q8 | pts[2].fog_q8 | pts[3].fog_q8);
            if (node_fog) {
                FogState nf{true, fog_colour};
                ResolvedNode rn;
                rn.kind = ResolvedNode::Kind::nearest_impl;
                rn.pts = pts;
                rn.mips = mip_set->data();
                rn.mip_count = mip_set->size();
                rn.level = level;
                rn.transparent = transparent;
                rn.fog = nf;
                push_node(rn);
                continue;
            }
            // Upscaled active: Q16 walk over the upscaled index pixels.
            if (signed_level < 0) {
                const auto& srcmip = texture.upscaled.at(size_t(-signed_level - 1));
                ResolvedNode ru;
                ru.srcpx = srcmip.pixels.data();
                ru.srcpx_size = srcmip.pixels.size();
                ru.stride_shift = srcmip.stride_shift;
                ru.palette = &palette;
                ru.transparent = transparent;
                if (fb.width() > 640 || fb.height() > 480) {
                    ru.kind = ResolvedNode::Kind::nearest_wide;
                    for (size_t i = 0; i < 4; ++i)
                        ru.nvw[i] = {pts[i].x, pts[i].y, uint16_t(uint16_t(uu[i] >> level) * uv_scale),
                                     uint16_t(uint16_t(vv[i] >> level) * uv_scale)};
                } else {
                    ru.kind = ResolvedNode::Kind::nearest_q16;
                    for (size_t i = 0; i < 4; ++i)
                        ru.nv[i] = {int16_t(std::max(-32768, std::min(32767, pts[i].x))),
                                    int16_t(std::max(-32768, std::min(32767, pts[i].y))),
                                    uint16_t(uint16_t(uu[i] >> level) * uv_scale),
                                    uint16_t(uint16_t(vv[i] >> level) * uv_scale)};
                }
                push_node(ru);
                continue;
            }
            // Nearest-exact: Q16 walk over the index pixels (byte-identical).
            const auto& srcmip = texture.mips.at(level);
            const auto& srcpx = srcmip.sampling_storage.empty() ? srcmip.pixels : srcmip.sampling_storage;
            ResolvedNode rq;
            rq.srcpx = srcpx.data();
            rq.srcpx_size = srcpx.size();
            rq.stride_shift = srcmip.stride_shift;
            rq.palette = &palette;
            rq.transparent = transparent;
            if (fb.width() > 640 || fb.height() > 480) {
                rq.kind = ResolvedNode::Kind::nearest_wide;
                for (size_t i = 0; i < 4; ++i)
                    rq.nvw[i] = {pts[i].x, pts[i].y, uint16_t(uu[i] >> level), uint16_t(vv[i] >> level)};
            } else {
                rq.kind = ResolvedNode::Kind::nearest_q16;
                for (size_t i = 0; i < 4; ++i)
                    rq.nv[i] = {int16_t(std::max(-32768, std::min(32767, pts[i].x))),
                                int16_t(std::max(-32768, std::min(32767, pts[i].y))),
                                uint16_t(uu[i] >> level), uint16_t(vv[i] >> level)};
            }
            push_node(rq);
        } else {
            FogState bi_fog{fog_on && (pts[0].fog_q8 | pts[1].fog_q8 | pts[2].fog_q8 | pts[3].fog_q8),
                            fog_colour};
            ResolvedNode rb;
            rb.kind = ResolvedNode::Kind::bilinear;
            rb.pts = pts;
            rb.mips = mip_set->data();
            rb.mip_count = mip_set->size();
            rb.transparent = transparent;
            rb.fog = bi_fog;
            push_node(rb);
        }
    }
    // Band dispatch: every band draws all resolved nodes with a row clip.
    // The edge walk starts at the polygon top (Focus 1); the clip only skips
    // writes, so the union is bit-identical to one thread. Scratch comes from
    // the pool (plan 29 Task 4); the job captures one pointer (SBO-clean).
    // More bands than workers: the pool hands bands out as workers free up,
    // so cheap sky rows and busy road rows balance. Nodes entirely outside a
    // band are skipped (their draw would write nothing there).
    const int bands = pool.count() > 1 ? std::min(fb.height(), pool.count() * 4) : 1;
    struct Ctx {
        FramebufferRgb* fb;
        const std::vector<ResolvedNode>* nodes;
    };
    Ctx ctx{&fb, &resolved};
    Ctx* pc = &ctx;
    pool.run(bands, fb.height(), [pc](int, int top, int bottom, RasterScratch64& scratch) {
        const RasterClip band{0, top, pc->fb->width(), bottom};
        for (const auto& r : *pc->nodes)
            if (r.maxy >= top && r.miny <= bottom) draw_resolved(*pc->fb, r, band, scratch);
    });
}
void render_nodes_rgb(FramebufferRgb& fb, const std::vector<DrawNode>& nodes, const ViewMode& mode,
                      uint32_t flat_mask, const Palette& palette, const Enhancements& enh,
                      RenderThreads& pool) {
    render_nodes_rgb_impl(fb, nodes, mode, flat_mask, palette, enh, pool, false, 0, 33);
}
void render_nodes_rgb_fog(FramebufferRgb& fb, const std::vector<DrawNode>& nodes, const ViewMode& mode,
                          uint32_t flat_mask, const Palette& palette, const Enhancements& enh,
                          RenderThreads& pool, uint32_t fog_colour_rgb, int draw_slices) {
    render_nodes_rgb_impl(fb, nodes, mode, flat_mask, palette, enh, pool, enh.fog > 0, fog_colour_rgb,
                          draw_slices);
}
} // namespace rr
