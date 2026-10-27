#include "framebuffer_rgb.hpp"
#include <algorithm>
#include <stdexcept>

namespace rr {
void FramebufferRgb::resize(int w, int h) {
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) throw std::runtime_error("Invalid RGB framebuffer size");
    w_ = w;
    h_ = h;
    pixels_.assign(size_t(w) * h, 0);
}
void FramebufferRgb::clear(uint32_t argb) {
    std::fill(pixels_.begin(), pixels_.end(), argb);
}
} // namespace rr
