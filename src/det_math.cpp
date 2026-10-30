// Plan 33 Task A11: deterministic exp/log, adapted from fdlibm e_exp.c and
// e_log.c. Original notice:
//
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunSoft, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this software is freely
// granted, provided that this notice is preserved.
#include "det_math.hpp"
#include <cmath>
#include <limits>

namespace rr {
namespace {
constexpr double ln2_hi = 6.93147180369123816490e-01; // trailing zeros: k*ln2_hi is exact
constexpr double ln2_lo = 1.90821492927058770002e-10;
constexpr double inv_ln2 = 1.44269504088896338700e+00;
} // namespace

double det_exp(double x) {
    constexpr double P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03,
                     P3 = 6.61375632143793436117e-05, P4 = -1.65339022054652515390e-06,
                     P5 = 4.13813679705723846039e-08;
    if (std::isnan(x)) return x;
    if (x > 7.09782712893383973096e+02) return std::numeric_limits<double>::infinity();
    if (x < -7.45133219101941108420e+02) return 0.0;
    const double ax = std::fabs(x);
    if (ax < 3.7252902984e-09) return 1.0 + x; // |x| < 2^-28
    // x = k*ln2 + r, |r| <= 0.5*ln2, with r carried as hi - lo.
    int k = 0;
    double hi = x, lo = 0.0;
    if (ax > 0.5 * 6.93147180559945286227e-01) {
        k = int(std::floor(inv_ln2 * x + 0.5));
        const double t = double(k);
        hi = x - t * ln2_hi;
        lo = t * ln2_lo;
    }
    const double r = hi - lo;
    const double t = r * r;
    const double c = r - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    if (k == 0) return 1.0 - ((r * c) / (c - 2.0) - r);
    const double y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
    return std::ldexp(y, k);
}

double det_log(double x) {
    constexpr double Lg1 = 6.666666666666735130e-01, Lg2 = 3.999999999940941908e-01,
                     Lg3 = 2.857142874366239149e-01, Lg4 = 2.222219843214978396e-01,
                     Lg5 = 1.818357216161805012e-01, Lg6 = 1.531383769920937332e-01,
                     Lg7 = 1.479819860511658591e-01;
    if (std::isnan(x) || x < 0.0) return std::numeric_limits<double>::quiet_NaN();
    if (x == 0.0) return -std::numeric_limits<double>::infinity();
    if (std::isinf(x)) return x;
    // x = 2^k * m with sqrt(2)/2 < m < sqrt(2); frexp is exact.
    int k = 0;
    double m = std::frexp(x, &k); // [0.5, 1)
    if (m < 7.07106781186547524401e-01) {
        m *= 2.0;
        --k;
    }
    const double f = m - 1.0; // exact (Sterbenz)
    const double s = f / (2.0 + f);
    const double dk = double(k);
    const double z = s * s, w = z * z;
    const double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    const double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    const double R = t2 + t1;
    const double hfsq = 0.5 * f * f;
    return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
}

double det_log2(double x) {
    // Exact for powers of two (frexp), so whole mip levels stay whole.
    int k = 0;
    const double m = std::frexp(x, &k);
    if (std::isfinite(x) && x > 0.0 && m == 0.5) return double(k - 1);
    return det_log(x) * inv_ln2;
}

double det_pow10(double x) {
    constexpr double ln10 = 2.30258509299404568402e+00;
    return det_exp(x * ln10);
}
} // namespace rr
