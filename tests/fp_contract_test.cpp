// Plan 33 Task A3: the build must not fuse a*b+c into one FMA. With
// a = 1+2^-27 and b = 1-2^-27 (float: 2^-13), a*b rounds to 1, so the unfused
// a*b-1 is exactly 0 while a fused result is -2^-54 (float: -2^-26).
#include "check.hpp"
#include <cmath>

int main() {
    rr::test::Checks check;
    volatile double va = 1.0 + std::ldexp(1.0, -27), vb = 1.0 - std::ldexp(1.0, -27), vc = -1.0;
    const double a = va, b = vb, c = vc;
    check(a * b + c == 0.0, "double a*b+c is not contracted");
    volatile float fa = 1.0f + std::ldexp(1.0f, -13), fb = 1.0f - std::ldexp(1.0f, -13), fc = -1.0f;
    const float x = fa, y = fb, z = fc;
    check(x * y + z == 0.0f, "float a*b+c is not contracted");
    return check.finish("fp_contract");
}
