// Plan 21 Task 3: perspective-correct textures (failing test).
// - Equal-depth quad renders byte-identical to the affine path.
// - Road-like trapezoid with vertical-gradient texture: the mid-row sampled
//   texture row equals the analytic perspective value within +-1 texel, and
//   differs measurably from the affine sample (documents the original warp).
// - Fill rule: the trapezoid covers exactly the pixels the affine version covers.
#include "texture.hpp"
#include <cstdlib>
#include "cel.hpp"
#include "framebuffer.hpp"
#include <cmath>
#include <iostream>

namespace {
rr::CelMip gradient_mip() {
    rr::CelMip m;
    m.width = 64;
    m.height = 64;
    m.stride_shift = 6;
    m.pixels.resize(64 * 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            m.pixels[size_t(y) * 64 + x] = uint8_t((y % 63) + 1); // never 0: coverage == nonzero count
    return m;
}
} // namespace

int main() {
    int failures = 0;
    auto check = [&](bool v, const char* what) {
        if (!v) {
            ++failures;
            std::cerr << "FAIL " << what << "\n";
        }
    };
    const auto mip = gradient_mip();
    const rr::RasterClip clip{0, 0, 639, 479};
    // 1. Equal depths == affine.
    {
        rr::Framebuffer a(640, 480), p(640, 480);
        a.clear(0);
        p.clear(0);
        std::array<rr::TextureVertex, 4> av{
            {{100, 100, 0, 0}, {300, 100, 63, 0}, {300, 300, 63, 63}, {100, 300, 0, 63}}};
        rr::draw_textured_quad(a, av, mip.pixels, mip.stride_shift, false, clip);
        std::array<rr::PerspectiveVertex, 4> pv{
            {{100, 100, 0, 0, 1}, {300, 100, 63, 0, 1}, {300, 300, 63, 63, 1}, {100, 300, 0, 63, 1}}};
        rr::draw_textured_quad_perspective(p, pv, mip, false, clip);
        size_t differ = 0;
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x) differ += a.row(y)[x] != p.row(y)[x];
        check(differ == 0, "equal depths match affine");
        if (differ) std::cerr << "equal-depth differ=" << differ << "\n";
    }
    // 2/3. Road trapezoid: bottom 200px wide at depth 1, top 50px at depth 4.
    {
        // Screen: bottom y=400 (x 220..420), top y=100 (x 295..345).
        rr::Framebuffer a(640, 480), p(640, 480);
        a.clear(0);
        p.clear(0);
        std::array<rr::TextureVertex, 4> av{
            {{220, 400, 0, 63}, {420, 400, 63, 63}, {345, 100, 63, 0}, {295, 100, 0, 0}}};
        rr::draw_textured_quad(a, av, mip.pixels, mip.stride_shift, false, clip);
        const float zn = 1.0f, zf = 4.0f;
        std::array<rr::PerspectiveVertex, 4> pv{{{220, 400, 0, 63, 1 / zn},
                                                 {420, 400, 63, 63, 1 / zn},
                                                 {345, 100, 63, 0, 1 / zf},
                                                 {295, 100, 0, 0, 1 / zf}}};
        rr::draw_textured_quad_perspective(p, pv, mip, false, clip);
        // Fill rule: same coverage.
        size_t ca = 0, cp = 0;
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x) {
                ca += a.row(y)[x] != 0;
                cp += p.row(y)[x] != 0;
            }
        check(ca == cp, "perspective covers the affine pixels");
        if (ca != cp) {
            for (int y = 0; y < 480; ++y) {
                int ra = 0, rp = 0;
                for (int x = 0; x < 640; ++x) {
                    ra += a.row(y)[x] != 0;
                    rp += p.row(y)[x] != 0;
                }
                if ((ra != rp) || (y >= 98 && y <= 106))
                    std::cerr << "row " << y << " affine=" << ra << " persp=" << rp << "\n";
            }
        }
        if (ca != cp) std::cerr << "coverage affine=" << ca << " persp=" << cp << "\n";
        // Mid-row perspective value: 1/z is linear in screen y for this
        // trapezoid; at y=250, t=(400-250)/300=0.5, 1/z=0.5*(1-0.25)+0.25=0.625,
        // v/z is linear in y: 0 at top, 63 at bottom -> 31.5 at mid.
        // v = (v/z)/(1/z) = 31.5/0.625 = 50.4 (affine would give 31.5).
        const int mid = 250;
        int got = -1;
        for (int x = 300; x < 340; ++x)
            if (p.row(mid)[x] != 0) {
                got = p.row(mid)[x];
                break;
            }
        const double analytic = 31.5 / 0.625 + 1.0; // +1 for the nonzero gradient offset
        check(got >= 0 && std::abs(got - analytic) <= 1.0, "mid-row matches analytic perspective v");
        if (!(got >= 0 && std::abs(got - analytic) <= 1.0))
            std::cerr << "mid-row got=" << got << " analytic=" << analytic << "\n";
        int aff = -1;
        for (int x = 300; x < 340; ++x)
            if (a.row(mid)[x] != 0) {
                aff = a.row(mid)[x];
                break;
            }
        check(aff >= 0 && std::abs(aff - analytic) > 2.0, "affine measurably warps there");
        if (!(aff >= 0 && std::abs(aff - analytic) > 2.0))
            std::cerr << "affine mid-row=" << aff << " (expected warp)\n";
    }
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
