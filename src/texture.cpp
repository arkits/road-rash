#include "texture.hpp"
#include "wide_math.hpp"
#include <stdexcept>
namespace rr {
void texture_span(uint8_t* destination, size_t count, const std::vector<uint8_t>& pixels, unsigned shift,
                  int32_t row, int32_t column, int32_t row_step, int32_t column_step, bool transparent) {
    if (shift > 8 || (count && !destination) || pixels.empty() || pixels.size() % (size_t(1) << shift))
        throw std::runtime_error("Invalid texture span storage");
    uint32_t u = uint32_t(row), v = uint32_t(column);
    for (size_t i = 0; i < count; ++i) {
        const uint32_t address = (((u & 0xffff0000u) << shift) + v) >> 16;
        if (address >= pixels.size())
            throw std::runtime_error("Texture span coordinate outside storage: " + std::to_string(address) +
                                     " / " + std::to_string(pixels.size()) + " shift " +
                                     std::to_string(shift) + " row " + std::to_string(int32_t(u)) +
                                     " column " + std::to_string(int32_t(v)));
        const auto index = pixels[address];
        if (index || !transparent) destination[i] = index;
        u += uint32_t(row_step);
        v += uint32_t(column_step);
    }
}
} // namespace rr

#include <algorithm>
#include <cstring>
#include <climits>
namespace rr {
namespace {
int32_t signed_word(uint32_t v) {
    int32_t result;
    std::memcpy(&result, &v, 4);
    return result;
}
int32_t wrap_add(int32_t a, int32_t b) {
    return signed_word(uint32_t(a) + uint32_t(b));
}
int32_t wrap_sub(int32_t a, int32_t b) {
    return signed_word(uint32_t(a) - uint32_t(b));
}
int32_t wrap_mul(int32_t a, int32_t b) {
    return signed_word(uint32_t(a) * uint32_t(b));
}
int32_t fixed(int32_t a) {
    return signed_word(uint32_t(a) << 16);
}
int32_t product_q16(int32_t a, int32_t b) {
    return signed_word(uint32_t(uint64_t(int64_t(a) * b) >> 16));
}
int32_t floor_q16(int32_t a) {
    return a >= 0 ? a / 65536 : int32_t((int64_t(a) - 65535) / 65536);
}
} // namespace
void texture_edge(std::array<TextureEdge, 480>& rows, const TextureVertex& from, const TextureVertex& to) {
    const int32_t dy = fixed(int32_t(to.y) - from.y);
    if (!dy || to.y < from.y) return;
    const int32_t reciprocal = int32_t(0x100000000LL / dy);
    TextureEdge at{fixed(from.x), fixed(from.u), fixed(from.v)};
    const TextureEdge step{product_q16(fixed(int32_t(to.x) - from.x), reciprocal),
                           product_q16(fixed(int32_t(to.u) - from.u), reciprocal),
                           product_q16(fixed(int32_t(to.v) - from.v), reciprocal)};
    const int first = std::max(0, int(from.y)), last = std::min(479, int(to.y));
    const int skip = first - from.y;
    at.x = wrap_add(at.x, wrap_mul(step.x, skip));
    at.u = wrap_add(at.u, wrap_mul(step.u, skip));
    at.v = wrap_add(at.v, wrap_mul(step.v, skip));
    for (int y = first; y <= last; ++y) {
        rows[y] = at;
        at.x = wrap_add(at.x, step.x);
        at.u = wrap_add(at.u, step.u);
        at.v = wrap_add(at.v, step.v);
    }
}
size_t texture_mip_level(uint32_t width, uint32_t height, uint32_t source_width, uint32_t source_height,
                         size_t levels) {
    if (!width || !height || !source_width || !source_height || !levels || levels > 9)
        throw std::runtime_error("Invalid mip dimensions");
    auto axis = [&](uint32_t projected, uint32_t source) {
        size_t level = 0;
        while (projected < (source >> 1) && level + 1 < levels) {
            ++level;
            source >>= 1;
        }
        return level;
    };
    return std::min(axis(width, source_width), axis(height, source_height));
}
void draw_textured_quad(Framebuffer& fb, const std::array<TextureVertex, 4>& vertices,
                        const std::vector<uint8_t>& pixels, unsigned shift, bool transparent,
                        RasterClip clip) {
    if (clip.left != 0 || shift > 8 || pixels.empty() || pixels.size() % (size_t(1) << shift))
        throw std::runtime_error("Invalid textured quad storage/clip");
    int first = 0, bottom = vertices[0].y;
    for (int i = 1; i < 4; ++i) {
        if (vertices[i].y < vertices[first].y) first = i;
        bottom = std::max(bottom, int(vertices[i].y));
    }
    if (bottom == vertices[first].y) return;
    std::array<TextureEdge, 480> left{}, right{};
    auto chain = [&](std::array<TextureEdge, 480>& rows, int direction) {
        int index = first;
        for (int guard = 0; guard < 4; ++guard) {
            const int next = (index + direction + 4) % 4;
            if (vertices[next].y < vertices[index].y)
                throw std::runtime_error("Textured quad requires normalized monotone edges");
            texture_edge(rows, vertices[index], vertices[next]);
            index = next;
            if (vertices[index].y == bottom) return;
        }
        throw std::runtime_error("Invalid texture edge chain");
    };
    chain(left, -1);
    chain(right, 1);
    const int top = std::max({0, int(vertices[first].y), int(clip.top)}),
              last = std::min({479, bottom, int(clip.bottom), fb.height() - 1});
    const int bound = std::min(int(clip.right), fb.width());
    for (int y = top; y <= last; ++y) {
        auto l = left[y], r = right[y];
        if (floor_q16(r.x) <= 0) continue; // original pre-swap test
        if (floor_q16(r.x) < floor_q16(l.x)) std::swap(l, r);
        int start = floor_q16(l.x), end = floor_q16(r.x), length = end - start;
        if (!length || start >= bound) continue;
        const int32_t reciprocal = 65536 / length;
        const int32_t du = product_q16(wrap_sub(r.u, l.u), reciprocal),
                      dv = product_q16(wrap_sub(r.v, l.v), reciprocal);
        if (start < 0) {
            const int skip = -start;
            l.u = wrap_add(l.u, wrap_mul(du, skip));
            l.v = wrap_add(l.v, wrap_mul(dv, skip));
            start = 0;
        }
        end = std::min(end, bound);
        if (start < end)
            texture_span(fb.row(y) + start, size_t(end - start), pixels, shift, l.v, l.u, dv, du,
                         transparent);
    }
}
namespace {
int64_t floor_q16_64(int64_t a) {
    return floor_q16_i64(a);
}
constexpr int64_t empty_edge_value() {
    return int64_t(INT32_MIN) * 65536 * 4;
}
} // namespace
void RasterScratch::resize(int height) {
    if (height < 0 || height > 8192) throw std::runtime_error("Raster scratch height out of range");
    rows.assign(size_t(height), TextureEdge{});
}
void RasterScratch64::resize(int height) {
    if (height < 0 || height > 8192) throw std::runtime_error("Raster scratch height out of range");
    rows.assign(size_t(height), TextureEdge64{});
}
void texture_edge_wide_vec(std::vector<TextureEdge64>& rows, const TextureVertexWide& from,
                           const TextureVertexWide& to) {
    if (rows.empty()) throw std::runtime_error("Raster scratch not sized");
    const int64_t height = int64_t(rows.size());
    const int64_t dy = (int64_t(to.y) - from.y) << 16;
    if (!dy || to.y < from.y) return;
    for (int64_t y = 0; y < height; ++y) rows[size_t(y)] = {empty_edge_value(), 0, 0};
    const int64_t reciprocal = int64_t(0x100000000LL) / dy;
    auto prod = [&](int64_t a) { return mul_shr16(a, reciprocal); };
    int64_t ax = int64_t(from.x) << 16, au = int64_t(from.u) << 16, av = int64_t(from.v) << 16;
    const int64_t sx = prod((int64_t(to.x) - from.x) << 16);
    const int64_t su = prod((int64_t(to.u) - from.u) << 16);
    const int64_t sv = prod((int64_t(to.v) - from.v) << 16);
    const int64_t first = std::max<int64_t>(0, from.y), last = std::min<int64_t>(height - 1, to.y);
    const int64_t skip = first - from.y;
    ax += sx * skip;
    au += su * skip;
    av += sv * skip;
    for (int64_t y = first; y <= last; ++y) {
        rows[size_t(y)] = {ax, au, av};
        ax += sx;
        au += su;
        av += sv;
    }
    for (int64_t y = 0; y < std::min(first, height); ++y) rows[size_t(y)] = {empty_edge_value(), 0, 0};
}
void texture_edge_wide(RasterScratch64& scratch, const TextureVertexWide& from, const TextureVertexWide& to) {
    if (scratch.rows.empty()) throw std::runtime_error("Raster scratch not sized");
    const int64_t height = int64_t(scratch.rows.size());
    const int64_t dy = (int64_t(to.y) - from.y) << 16;
    if (!dy || to.y < from.y) return;
    for (int64_t y = 0; y < height; ++y) scratch.rows[size_t(y)] = {empty_edge_value(), 0, 0};
    const int64_t reciprocal = int64_t(0x100000000LL) / dy;
    auto prod = [&](int64_t a) { return mul_shr16(a, reciprocal); };
    int64_t ax = int64_t(from.x) << 16, au = int64_t(from.u) << 16, av = int64_t(from.v) << 16;
    const int64_t sx = prod((int64_t(to.x) - from.x) << 16);
    const int64_t su = prod((int64_t(to.u) - from.u) << 16);
    const int64_t sv = prod((int64_t(to.v) - from.v) << 16);
    const int64_t first = std::max<int64_t>(0, from.y), last = std::min<int64_t>(height - 1, to.y);
    const int64_t skip = first - from.y;
    ax += sx * skip;
    au += su * skip;
    av += sv * skip;
    for (int64_t y = first; y <= last; ++y) {
        scratch.rows[size_t(y)] = {ax, au, av};
        ax += sx;
        au += su;
        av += sv;
    }
    // Clip the refill to the scratch: a segment wholly below the surface
    // (first >= height) leaves every row at the sentinel above, so there is
    // nothing more to do. Pre-fix this ran to `first` and wrote past the end.
    for (int64_t y = 0; y < std::min(first, height); ++y)
        scratch.rows[size_t(y)] = {empty_edge_value(), 0, 0};
}
int texture_mip_level_signed(uint32_t width, uint32_t height, uint32_t source_width, uint32_t source_height,
                             size_t original_levels, size_t upscaled_levels) {
    if (!width || !height || !source_width || !source_height || !original_levels || original_levels > 9)
        throw std::runtime_error("Invalid mip dimensions");
    if (upscaled_levels > 2) throw std::runtime_error("Too many upscaled levels");
    const size_t base = texture_mip_level(width, height, source_width, source_height, original_levels);
    if (base != 0) return int(base);
    // Projected size exceeds mip 0: step up while the image covers 2x/4x the source.
    size_t up = 0;
    while (up < upscaled_levels && uint64_t(width) >= uint64_t(source_width) << (up + 1) &&
           uint64_t(height) >= uint64_t(source_height) << (up + 1))
        ++up;
    return -int(up);
}
void draw_textured_quad_wide(Framebuffer& fb, const std::array<TextureVertexWide, 4>& vertices,
                             const std::vector<uint8_t>& pixels, int level_width, unsigned stride_shift,
                             bool transparent, RasterClip clip, bool upscaled_storage,
                             RasterScratch64& scratch) {
    if (scratch.rows.empty()) throw std::runtime_error("Raster scratch not sized");
    if (upscaled_storage) {
        if (level_width <= 0 || level_width > 8192 || pixels.empty())
            throw std::runtime_error("Invalid upscaled textured quad storage");
    } else if (stride_shift > 8 || pixels.empty() || pixels.size() % (size_t(1) << stride_shift)) {
        throw std::runtime_error("Invalid textured quad storage/clip");
    }
    const int64_t height = int64_t(scratch.rows.size());
    int first = 0;
    int64_t bottom = vertices[0].y;
    for (int i = 1; i < 4; ++i) {
        if (vertices[i].y < vertices[first].y) first = i;
        bottom = std::max(bottom, int64_t(vertices[i].y));
    }
    if (bottom == vertices[first].y) return;
    const int64_t empty_sentinel = empty_edge_value();
    const size_t rows = scratch.rows.size();
    // Reused workspace in the caller's per-band scratch. Grown only.
    auto& saved_left = scratch.saved_left;
    auto& saved_right = scratch.saved_right;
    auto& side_rows = scratch.side_rows;
    auto& seg_rows = scratch.seg_rows;
    if (saved_left.size() < rows) {
        saved_left.resize(rows);
        saved_right.resize(rows);
        side_rows.resize(size_t(height));
        seg_rows.resize(size_t(height));
    }
    for (size_t i = 0; i < rows; ++i)
        saved_left[i] = {empty_sentinel, 0, 0}, saved_right[i] = {empty_sentinel, 0, 0};
    auto chain = [&](std::vector<TextureEdge64>& save, int direction) {
        int index = first;
        for (int64_t y = 0; y < height; ++y) side_rows[size_t(y)] = {empty_sentinel, 0, 0};
        for (int guard = 0; guard < 4; ++guard) {
            const int next = (index + direction + 4) % 4;
            if (vertices[size_t(next)].y < vertices[size_t(index)].y)
                throw std::runtime_error("Textured quad requires normalized monotone edges");
            TextureVertexWide a{vertices[size_t(index)].x, vertices[size_t(index)].y,
                                vertices[size_t(index)].u, vertices[size_t(index)].v};
            TextureVertexWide b{vertices[size_t(next)].x, vertices[size_t(next)].y, vertices[size_t(next)].u,
                                vertices[size_t(next)].v};
            for (auto& e : seg_rows) e = {empty_sentinel, 0, 0};
            texture_edge_wide_vec(seg_rows, a, b);
            for (int64_t y = std::max<int64_t>(0, a.y); y <= std::min<int64_t>(height - 1, b.y); ++y)
                if (seg_rows[size_t(y)].x != empty_sentinel) side_rows[size_t(y)] = seg_rows[size_t(y)];
            index = next;
            if (vertices[size_t(index)].y == bottom) break;
        }
        save = side_rows;
    };
    chain(saved_left, -1);
    chain(saved_right, 1);
    const int64_t top = std::max<int64_t>({0, vertices[size_t(first)].y, clip.top});
    const int64_t last = std::min<int64_t>({height - 1, bottom, clip.bottom, fb.height() - 1});
    const int64_t bound = std::min<int64_t>(clip.right, fb.width());
    const int64_t stride = upscaled_storage ? int64_t(level_width) : int64_t(1) << stride_shift;
    for (int64_t y = top; y <= last; ++y) {
        auto l = saved_left[size_t(y)], r = saved_right[size_t(y)];
        if (l.x == empty_sentinel || r.x == empty_sentinel) continue;
        if (floor_q16_64(r.x) <= 0) continue; // original pre-swap test
        if (floor_q16_64(r.x) < floor_q16_64(l.x)) std::swap(l, r);
        int64_t start = floor_q16_64(l.x), end = floor_q16_64(r.x), length = end - start;
        if (!length || start >= bound) continue;
        const int64_t reciprocal = 65536 / length;
        auto prod = [&](int64_t d) { return mul_shr16(d, reciprocal); };
        int64_t du = prod(int64_t(r.u) - l.u), dv = prod(int64_t(r.v) - l.v);
        int64_t cu = l.u, cv = l.v;
        if (start < 0) {
            const int64_t skip = -start;
            cu += du * skip;
            cv += dv * skip;
            start = 0;
        }
        end = std::min(end, bound);
        if (start >= end) continue;
        if (upscaled_storage) {
            uint8_t* dst = fb.row(int(y)) + start;
            const size_t w = pixels.size();
            for (int64_t x = start; x < end; ++x) {
                int64_t col = floor_q16_64(cu), rowv = floor_q16_64(cv);
                if (col >= 0 && rowv >= 0) {
                    size_t addr = size_t(rowv) * size_t(stride) + size_t(col);
                    if (addr >= w) throw std::runtime_error("Upscaled texel outside storage");
                    const uint8_t index = pixels[addr];
                    if (index || !transparent) dst[x - start] = index;
                }
                cu += du;
                cv += dv;
            }
        } else {
            if (start < 0 || end > fb.width()) throw std::runtime_error("Textured span outside framebuffer");
            texture_span(fb.row(int(y)) + start, size_t(end - start), pixels, stride_shift, int32_t(cv),
                         int32_t(cu), int32_t(dv), int32_t(du), transparent);
        }
    }
}
} // namespace rr
