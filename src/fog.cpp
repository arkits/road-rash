// Plan 21 Task 5: fog + grading implementation. All procedural; no data.
#include "fog.hpp"
#include "det_math.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rr {
uint16_t fog_factor_q8(int32_t depth_q8, int draw_slices, int strength) {
    if (strength <= 0) return 0;
    if (depth_q8 <= 0) return 0;
    if (draw_slices <= 0) throw std::runtime_error("Invalid draw slice count");
    const double far = double(draw_slices) * 256.0;
    const double t = std::min(1.0, double(depth_q8) / far);
    // Exponential-squared: 1 - exp(-(k*t)^2), k chosen so t=1 -> ~1.
    const double k = 2.0;
    const double f = (1.0 - det_exp(-(k * t) * (k * t))) / (1.0 - det_exp(-k * k));
    const double scaled = f * double(strength) / 100.0;
    return uint16_t(std::max(0.0, std::min(256.0, scaled * 256.0)));
}

uint32_t fog_colour(const uint32_t* argb, int width, int height) {
    if (!argb || width <= 0 || height <= 0) throw std::runtime_error("Invalid fog source");
    const int rows = std::min(8, height);
    uint64_t r = 0, g = 0, b = 0, n = 0;
    for (int y = height - rows; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const uint32_t p = argb[size_t(y) * width + x];
            r += (p >> 16) & 0xff;
            g += (p >> 8) & 0xff;
            b += p & 0xff;
            ++n;
        }
    if (!n) throw std::runtime_error("Empty fog source");
    return (uint32_t(r / n) << 16) | (uint32_t(g / n) << 8) | uint32_t(b / n);
}

std::vector<uint32_t> grade_lut(Grade grade) {
    std::vector<uint32_t> lut(size_t(17 * 17 * 17));
    const auto lat = [](int i) { return std::min(255, i * 16); };
    for (int r = 0; r < 17; ++r)
        for (int g = 0; g < 17; ++g)
            for (int b = 0; b < 17; ++b) {
                int R = lat(r), G = lat(g), B = lat(b);
                switch (grade) {
                case Grade::none:
                    break;
                case Grade::warm: // gentle warmth: lift red, press blue
                    R = std::min(255, R + (255 - R) * 8 / 100);
                    B = B - B * 8 / 100;
                    break;
                case Grade::vivid: // saturation stretch around grey
                {
                    const int lum = (R * 2126 + G * 7152 + B * 722) / 10000;
                    R = std::max(0, std::min(255, lum + (R - lum) * 130 / 100));
                    G = std::max(0, std::min(255, lum + (G - lum) * 130 / 100));
                    B = std::max(0, std::min(255, lum + (B - lum) * 130 / 100));
                    break;
                }
                case Grade::dusk: // cool shadows, warm highlights
                {
                    const int lum = (R * 2126 + G * 7152 + B * 722) / 10000;
                    const int w = lum * 100 / 255; // 0 at black, 100 at white
                    R = std::max(0, std::min(255, R + ((255 - R) * 10 - R * 6) * w / 10000 +
                                                      (0 - R) * 6 * (100 - w) / 10000));
                    G = std::max(0, std::min(255, G + ((255 - G) * 4 - G * 4) * w / 10000 +
                                                      (0 - G) * 2 * (100 - w) / 10000));
                    B = std::max(0, std::min(255, B + ((255 - B) * 0 - B * 0) * w / 10000 +
                                                      (40 - B) * 10 * (100 - w) / 10000));
                    break;
                }
                }
                lut[size_t((r * 17 + g) * 17 + b)] = (uint32_t(R) << 16) | (uint32_t(G) << 8) | uint32_t(B);
            }
    return lut;
}

uint32_t grade_apply(const std::vector<uint32_t>& lut, uint32_t rgb) {
    if (lut.size() != size_t(17 * 17 * 17)) throw std::runtime_error("Invalid grade LUT");
    const double r = double((rgb >> 16) & 0xff) / 16.0, g = double((rgb >> 8) & 0xff) / 16.0;
    const double b = double(rgb & 0xff) / 16.0;
    const int r0 = std::max(0, std::min(15, int(r))), g0 = std::max(0, std::min(15, int(g)));
    const int b0 = std::max(0, std::min(15, int(b)));
    const double fr = std::max(0.0, std::min(1.0, r - r0)), fg = std::max(0.0, std::min(1.0, g - g0));
    const double fb = std::max(0.0, std::min(1.0, b - b0));
    double R = 0, G = 0, B = 0;
    for (int dr = 0; dr < 2; ++dr)
        for (int dg = 0; dg < 2; ++dg)
            for (int db = 0; db < 2; ++db) {
                const double w = (dr ? fr : 1 - fr) * (dg ? fg : 1 - fg) * (db ? fb : 1 - fb);
                const uint32_t c = lut[size_t(((r0 + dr) * 17 + (g0 + dg)) * 17 + (b0 + db))];
                R += double((c >> 16) & 0xff) * w;
                G += double((c >> 8) & 0xff) * w;
                B += double(c & 0xff) * w;
            }
    const auto c = [](double v) { return uint32_t(std::max(0.0, std::min(255.0, v))); };
    return (c(R) << 16) | (c(G) << 8) | c(B);
}
} // namespace rr
