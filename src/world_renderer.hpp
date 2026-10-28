#pragma once
#include "cel.hpp"
#include "view.hpp"
#include "raster.hpp"
#include <optional>
namespace rr {
struct DrawNode {
    std::array<RasterPoint, 4> view_points{}; // original Q16 view coordinates
    const CelTexture* texture = nullptr;      // caller owns imported texture
    std::optional<uint8_t> flat_index;
    bool flip_u = false, flip_v = false;
    std::array<int32_t, 4> view_depth{}; // plan 21 Task 3: Q8 slices ahead per corner; all zero = affine
    // Native smooth terrain (world_texture.hpp): road-space texel coordinates
    // per view_points corner; drawn instead of the corner-mapped texture when
    // the renderer is asked for smooth terrain.
    std::optional<std::array<std::array<float, 2>, 4>> world_uv;
};
// Nodes arrive in the original display-list order. Clear/sky/presentation are external.
void render_nodes(Framebuffer&, const std::vector<DrawNode>&, const ViewMode&, uint32_t flat_mask);
// Perspective variant (plan 21 Task 3): nodes with non-zero view_depth take
// the perspective path, others the affine path. perspective=false == render_nodes.
void render_nodes_perspective(Framebuffer&, const std::vector<DrawNode>&, const ViewMode&, uint32_t flat_mask,
                              bool perspective, const struct WorldBlend* smooth_terrain = nullptr);
} // namespace rr
