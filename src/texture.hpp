#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "raster.hpp"
#include "cel.hpp"
namespace rr {
// Original 0x00418910..0x00419e10 and transparent variants: power-of-two
// row stride 1..256, Q16 row/column and signed wrapped per-pixel increments.
void texture_span(uint8_t* destination, size_t count, const std::vector<uint8_t>& pixels,
                  unsigned stride_shift, int32_t row_q16, int32_t column_q16, int32_t row_step_q16,
                  int32_t column_step_q16, bool transparent);
} // namespace rr

namespace rr {
struct TextureVertex {
    int16_t x = 0, y = 0;
    uint16_t u = 0, v = 0;
};
struct TextureEdge {
    int32_t x = 0, u = 0, v = 0;
};
void texture_edge(std::array<TextureEdge, 480>& rows, const TextureVertex& from, const TextureVertex& to);
// 0x004180b0 selects the smaller of two axis LODs.
size_t texture_mip_level(uint32_t projected_width, uint32_t projected_height, uint32_t source_width,
                         uint32_t source_height, size_t available_levels);
// Normalized convex vertices with UVs already selected (no mip selection here).
void draw_textured_quad(Framebuffer&, const std::array<TextureVertex, 4>&, const std::vector<uint8_t>& pixels,
                        unsigned stride_shift, bool transparent, RasterClip);

// ---- Higher-scale infrastructure (plans 19-21). The baseline path above is
// the original narrow/wrap Q16 arithmetic and stays bit-identical at scale 1.
// Widened types alone do NOT remove Q16 overflow (texture.cpp::fixed shifts
// into a 32-bit accumulator and wraps), so large geometry must be clipped
// geometrically with coherent UVs before entering the original arithmetic,
// or use the wide path below with 64-bit edge math.

// Wide vertex for surfaces above 640x480: int16 x/y overflows past 32767,
// which far-off-screen vertices reach at scale 4. UV stays u1616: original
// 16-bit sampling-storage semantics for imported original mips.
struct TextureVertexWide {
    int32_t x = 0, y = 0;
    uint16_t u = 0, v = 0;
};
// Per-worker scratch, sized to the surface height when the scale changes; no
// allocation per quad and no shared edge arrays between raster workers.
struct RasterScratch {
    std::vector<TextureEdge> rows;
    explicit RasterScratch(int height = 0) { resize(height); }
    void resize(int height);
};
// 64-bit edge for the wide path: int32 TextureEdge.x cannot hold
// |x|>32768 in Q16 (e.g. -40000<<16 wraps to +25536). Steps are still
// computed from the original endpoints before band clipping.
struct TextureEdge64 {
    int64_t x = 0, u = 0, v = 0;
};
struct RasterScratch64 {
    std::vector<TextureEdge64> rows;
    // draw_textured_quad_wide workspace, grown only. Owned per band (not
    // thread_local), so a render worker that first meets the wide path in a
    // later frame does not allocate then (plan 33b: render-threads-alloc).
    std::vector<TextureEdge64> saved_left, saved_right, side_rows, seg_rows;
    explicit RasterScratch64(int height = 0) { resize(height); }
    void resize(int height);
};
// 64-bit edge/intermediate math for the wide path. Steps are computed from
// the ORIGINAL polygon endpoints before band clipping; recomputing from
// clipped endpoints changes rounding.
void texture_edge_wide(RasterScratch64& scratch, const TextureVertexWide& from, const TextureVertexWide& to);
// Signed mip selection: negative results address upscaled levels above mip 0
// (result -1 = first 2x level, -2 = 4x), non-negative the original mip index.
// UV shifts by a negative value are undefined, so callers must branch on the sign. Throws
// std::runtime_error on empty levels, matching texture_mip_level.
int texture_mip_level_signed(uint32_t projected_width, uint32_t projected_height, uint32_t source_width,
                             uint32_t source_height, size_t original_levels, size_t upscaled_levels);
// Wide textured quad: baseline fill rules (right exclusive, bottom inclusive)
// at every scale. `upscaled_storage` selects plain bounds-checked addressing
// with the level width (strides above 256 cannot use the 16-bit
// sampling-storage path); original levels pass their stride shift and keep
// the emulated path unchanged.
void draw_textured_quad_wide(Framebuffer&, const std::array<TextureVertexWide, 4>&,
                             const std::vector<uint8_t>& pixels, int level_width, unsigned stride_shift,
                             bool transparent, RasterClip, bool upscaled_storage, RasterScratch64& scratch);
// Perspective-correct quads with equal per-corner depth MUST call the
// original affine draw_textured_quad above: float arithmetic and 16-pixel
// span subdivision do not reproduce integer wrap/rounding exactly.
struct PerspectiveVertex {
    int32_t x = 0, y = 0;
    float u = 0, v = 0, inv_z = 0;
};
void draw_textured_quad_perspective(Framebuffer&, const std::array<PerspectiveVertex, 4>&, const CelMip&,
                                    bool transparent, RasterClip);
} // namespace rr
