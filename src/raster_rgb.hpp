// Plan 21 Task 4: RGB raster with bilinear/trilinear filtering.
// Texels are premultiplied at conversion; filtering blends four texels, then
// alpha-tests (discard if alpha < 0.5) and un-premultiplies — no dark halos.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "cel.hpp"
#include "framebuffer.hpp"
#include "framebuffer_rgb.hpp"
#include "raster.hpp"
#include "render_threads.hpp"
#include "settings.hpp"
#include "view.hpp"
#include "world_renderer.hpp"

namespace rr {
struct RgbaMip {
    int width = 0, height = 0;
    std::vector<uint32_t> premultiplied; // alpha 0 where index 0 was transparent
};
// Converted once per (texture, palette generation); remaps apply to the index
// image before conversion (caller converts the remapped CelTexture).
const std::vector<RgbaMip>& rgba_mips(const CelTexture&, const Palette&, uint32_t palette_generation);
// Levelled form (Task 2): level 0 is the base mip chain, level k+1 is upscaled[k].
// Base and upscaled levels never share a cache key (Review Focus 2).
const std::vector<RgbaMip>& rgba_mips(const CelTexture&, const Palette&, uint32_t palette_generation,
                                      int level);
uint32_t palette_generation(const Palette&); // FNV-1a over rgbx; equal palettes -> equal value
void clear_rgba_mip_cache(); // only between frames (course load / teardown); invalidates references returned by rgba_mips
size_t rgba_mip_cache_size(); // test hook
// Test hook: convert one mip without caching.
std::vector<RgbaMip> rgba_mips_for_test(const CelMip&, const Palette&);
struct RgbVertex {
    int32_t x = 0, y = 0;
    float u = 0, v = 0;
    uint16_t fog_q8 = 0; // plan 21 Task 5: per-vertex fog (0..256)
};
struct FogState {
    bool on = false;
    uint32_t colour = 0; // 0x00RRGGBB
};
void draw_rgb_quad_bilinear(FramebufferRgb&, const std::array<RgbVertex, 4>&, const RgbaMip*, size_t,
                            bool transparent, RasterClip, FogState fog = {});
void draw_rgb_quad_trilinear(FramebufferRgb&, const std::array<RgbVertex, 4>&, const RgbaMip*, size_t,
                             bool transparent, RasterClip, float lod, FogState fog = {});
void render_nodes_rgb(FramebufferRgb&, const std::vector<DrawNode>&, const ViewMode&, uint32_t flat_mask,
                      const Palette&, const Enhancements&, RenderThreads& pool);
// Fog-aware entry (plan 21 Task 5): fog colour + per-node depths from view_depth.
// fog_strength 0 == render_nodes_rgb exactly.
void render_nodes_rgb_fog(FramebufferRgb&, const std::vector<DrawNode>&, const ViewMode&, uint32_t flat_mask,
                          const Palette&, const Enhancements&, RenderThreads& pool, uint32_t fog_colour_rgb,
                          int draw_slices = 33);
} // namespace rr
