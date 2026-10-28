#include "world_renderer.hpp"
#include "texture.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace rr {
namespace {
int16_t narrow(int32_t v) {
    const uint16_t bits = uint16_t(v);
    int16_t result;
    std::memcpy(&result, &bits, 2);
    return result;
}
} // namespace
static void render_nodes_impl(Framebuffer& fb, const std::vector<DrawNode>& nodes, const ViewMode& mode,
                              uint32_t flat_mask, bool perspective_on) {
    if (fb.width() != mode.surface_width || fb.height() != mode.surface_height)
        throw std::runtime_error("View/framebuffer size mismatch");
    const RasterClip clip{0, 0, fb.width(), fb.height() - 1};
    const bool wide_surface = mode.surface_width > 640 || mode.surface_height > 480;
    RasterScratch64 scratch(wide_surface ? fb.height() : 0);
    for (const auto& node : nodes) {
        if (!node.texture && !node.flat_index) continue;
        std::array<RasterPoint, 4> points;
        for (size_t i = 0; i < 4; ++i)
            points[i] = {mode.map(node.view_points[i].x), mode.map(node.view_points[i].y)};
        int32_t minx = points[0].x, maxx = minx, miny = points[0].y, maxy = miny;
        for (const auto& p : points) {
            minx = std::min(minx, p.x);
            maxx = std::max(maxx, p.x);
            miny = std::min(miny, p.y);
            maxy = std::max(maxy, p.y);
        }
        if (minx > clip.right || maxx < clip.left || miny > clip.bottom || maxy < clip.top) continue;
        // Mip-less textures (0x004436a0 null pixel pointer for zero-size
        // sheet frames) contribute no pixels; the node still exists.
        if (node.texture && node.texture->mips.empty() && !(node.texture->flags & flat_mask)) continue;
        if (!node.texture || (node.texture->flags & flat_mask)) {
            draw_flat_quad(fb, {points, node.texture ? node.texture->flat_index : *node.flat_index}, clip);
            continue;
        }
        const auto& texture = *node.texture;
        if (texture.flags & 0x2000)
            throw std::runtime_error("Dynamic rider texture must be specialized before rendering");
        bool flip_u = node.flip_u;
        if (!normalize_quad(points, texture.flags, &flip_u)) continue;
        const auto& bounds = texture.bounds;
        const uint32_t src_w = uint32_t(std::abs(int64_t(bounds[2]) - bounds[0]) + 1),
                       src_h = uint32_t(std::abs(int64_t(bounds[3]) - bounds[1]) + 1);
        const uint16_t u0 = uint16_t(flip_u ? bounds[2] - 1 : bounds[0] + 1),
                       u1 = uint16_t(flip_u ? bounds[0] + 1 : bounds[2] - 1);
        const uint16_t v0 = uint16_t(node.flip_v ? bounds[3] - 1 : bounds[1] + 1),
                       v1 = uint16_t(node.flip_v ? bounds[1] + 1 : bounds[3] - 1);
        const std::array<std::array<uint16_t, 2>, 4> uv = {{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}};
        if (perspective_on &&
            (node.view_depth[0] | node.view_depth[1] | node.view_depth[2] | node.view_depth[3])) {
            // Plan 21 Task 3: perspective path. Mip level selected as today;
            // upscaled levels fall back to affine (no perspective upscaled path yet).
            const int psigned =
                texture_mip_level_signed(uint32_t(maxx - minx + 1), uint32_t(maxy - miny + 1), src_w, src_h,
                                         texture.mips.size(), texture.upscaled.size());
            if (psigned >= 0) {
                const size_t plevel = size_t(psigned);
                const auto& pmip = texture.mips.at(plevel);
                std::array<PerspectiveVertex, 4> pv;
                for (size_t i = 0; i < 4; ++i) {
                    const float inv = node.view_depth[i] > 0 ? 256.0f / float(node.view_depth[i]) : 0.0f;
                    pv[i] = {points[i].x, points[i].y, float(uv[i][0] >> plevel), float(uv[i][1] >> plevel),
                             inv};
                }
                draw_textured_quad_perspective(fb, pv, pmip, (texture.flags & 2) != 0, clip);
                continue;
            }
            // else fall through to affine
        }
        if (wide_surface) {
            // Plan 19 Task 3: int32 surface points (Review Focus 5); negative
            // levels sample upscaled[-level-1] with uv scaled by 2^(-level).
            const int signed_level =
                texture_mip_level_signed(uint32_t(maxx - minx + 1), uint32_t(maxy - miny + 1), src_w, src_h,
                                         texture.mips.size(), texture.upscaled.size());
            std::array<TextureVertexWide, 4> vertices;
            if (signed_level < 0) {
                const size_t up = size_t(-signed_level - 1);
                const auto& mip = texture.upscaled.at(up);
                const unsigned scale = unsigned(1) << (up + 1);
                for (size_t i = 0; i < 4; ++i)
                    vertices[i] = {points[i].x, points[i].y, uint16_t(uv[i][0] * scale),
                                   uint16_t(uv[i][1] * scale)};
                draw_textured_quad_wide(fb, vertices, mip.pixels, mip.width, mip.stride_shift,
                                        (texture.flags & 2) != 0, clip, true, scratch);
            } else {
                const size_t level = size_t(signed_level);
                for (size_t i = 0; i < 4; ++i)
                    vertices[i] = {points[i].x, points[i].y, uint16_t(uv[i][0] >> level),
                                   uint16_t(uv[i][1] >> level)};
                const auto& mip = texture.mips.at(level);
                draw_textured_quad_wide(
                    fb, vertices, mip.sampling_storage.empty() ? mip.pixels : mip.sampling_storage, mip.width,
                    mip.stride_shift, (texture.flags & 2) != 0, clip, false, scratch);
            }
            continue;
        }
        // Narrow surface: signed selection for the upscaled hook, int16 path
        // for original levels (bit-identical when upscaled is empty).
        const int signed_level =
            texture_mip_level_signed(uint32_t(maxx - minx + 1), uint32_t(maxy - miny + 1), src_w, src_h,
                                     texture.mips.size(), texture.upscaled.size());
        if (signed_level < 0) {
            const size_t up = size_t(-signed_level - 1);
            const auto& mip = texture.upscaled.at(up);
            const unsigned scale = unsigned(1) << (up + 1);
            std::array<TextureVertexWide, 4> vertices;
            for (size_t i = 0; i < 4; ++i)
                vertices[i] = {points[i].x, points[i].y, uint16_t(uv[i][0] * scale),
                               uint16_t(uv[i][1] * scale)};
            RasterScratch64 narrow_scratch(fb.height());
            draw_textured_quad_wide(fb, vertices, mip.pixels, mip.width, mip.stride_shift,
                                    (texture.flags & 2) != 0, clip, true, narrow_scratch);
            continue;
        }
        const size_t level = texture_mip_level(uint32_t(maxx - minx + 1), uint32_t(maxy - miny + 1), src_w,
                                               src_h, texture.mips.size());
        std::array<TextureVertex, 4> vertices;
        for (size_t i = 0; i < 4; ++i)
            vertices[i] = {narrow(points[i].x), narrow(points[i].y), uint16_t(uv[i][0] >> level),
                           uint16_t(uv[i][1] >> level)};
        const auto& mip = texture.mips.at(level);
        draw_textured_quad(fb, vertices, mip.sampling_storage.empty() ? mip.pixels : mip.sampling_storage,
                           mip.stride_shift, (texture.flags & 2) != 0, clip);
    }
}
void render_nodes(Framebuffer& fb, const std::vector<DrawNode>& nodes, const ViewMode& mode,
                  uint32_t flat_mask) {
    render_nodes_impl(fb, nodes, mode, flat_mask, false);
}
void render_nodes_perspective(Framebuffer& fb, const std::vector<DrawNode>& nodes, const ViewMode& mode,
                              uint32_t flat_mask, bool perspective) {
    render_nodes_impl(fb, nodes, mode, flat_mask, perspective);
}
} // namespace rr
