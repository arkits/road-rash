// Plan 33 Task A11: deterministic transcendental math. The bit hash below is
// the cross-platform proof: every compiler/libm must produce the same bits.
#include "check.hpp"
#include "det_math.hpp"
#include "fog.hpp"
#include "sound.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace {
uint64_t bits(double d) {
    uint64_t u;
    std::memcpy(&u, &d, sizeof u);
    return u;
}
double ulps(double a, double b) {
    if (a == b) return 0;
    return std::fabs(a - b) /
           std::max(std::numeric_limits<double>::denorm_min(), std::fabs(std::nextafter(b, b * 2) - b));
}
} // namespace

int main() {
    rr::test::Checks check;
    check(rr::det_exp(0.0) == 1.0, "exp(0) = 1");
    check(rr::det_log(1.0) == 0.0, "log(1) = 0");
    check(rr::det_pow10(0.0) == 1.0, "10^0 = 1");
    for (int k = -60; k <= 60; ++k)
        check(rr::det_log2(std::ldexp(1.0, k)) == double(k), "log2 of a power of two");
    check(rr::det_exp(1000.0) == std::numeric_limits<double>::infinity() && rr::det_exp(-1000.0) == 0.0,
          "exp overflow/underflow");
    check(std::isnan(rr::det_log(-1.0)) && rr::det_log(0.0) == -std::numeric_limits<double>::infinity(),
          "log domain");

    // Accuracy against the host libm, and an FNV-1a hash of the exact bits.
    uint64_t hash = 1469598103934665603ull;
    auto mix = [&](double d) {
        const uint64_t u = bits(d);
        for (int i = 0; i < 8; ++i) hash = (hash ^ ((u >> (8 * i)) & 0xff)) * 1099511628211ull;
    };
    double worst_exp = 0, worst_log = 0;
    for (int i = -40000; i <= 40000; ++i) {
        const double x = double(i) / 1000.0; // [-40, 40]
        const double e = rr::det_exp(x);
        worst_exp = std::max(worst_exp, ulps(e, std::exp(x)));
        mix(e);
        const double y = std::ldexp(1.0 + double(i + 40000) / 80001.0, (i % 61) - 30); // [2^-30, 2^31)
        const double l = rr::det_log(y);
        worst_log = std::max(worst_log, ulps(l, std::log(y)));
        mix(l);
        mix(rr::det_log2(y));
        mix(rr::det_pow10(double(i) / 2000.0 / 4.0));
    }
    std::cout << "worst exp ulps " << worst_exp << ", worst log ulps " << worst_log << '\n';
    check(worst_exp <= 1.0 && worst_log <= 1.0, "within 1 ulp of the host libm");
    std::cout << "det_math bit hash 0x" << std::hex << hash << std::dec << '\n';
    check(hash == 0x2012804f2dd7f3bfull, "bit hash matches the committed cross-platform value");

    // Integer-domain mappings keep their October 2026 macOS values (computed
    // then with Apple libm std::pow/std::exp): hashes of the complete tables.
    auto table_hash = [](auto&& fill) {
        uint64_t h = 1469598103934665603ull;
        fill([&](int v) {
            for (int i = 0; i < 4; ++i) h = (h ^ ((uint32_t(v) >> (8 * i)) & 0xff)) * 1099511628211ull;
        });
        return h;
    };
    const uint64_t volume = table_hash([](auto put) {
        for (int a = -10000; a <= 0; ++a) put(rr::mixer_volume(a));
    });
    const uint64_t pan = table_hash([](auto put) {
        for (int p = -10000; p <= 10000; ++p) put(rr::mixer_pan(p, false));
    });
    const uint64_t fog = table_hash([](auto put) {
        for (int slices : {16, 33, 48, 64})
            for (int strength : {1, 25, 50, 75, 100})
                for (int32_t d = 0; d <= slices * 256 + 512; ++d) put(rr::fog_factor_q8(d, slices, strength));
    });
    std::cout << std::hex << "volume 0x" << volume << " pan 0x" << pan << " fog 0x" << fog << std::dec
              << '\n';
    check(volume == 0xd60ac8d951ecfb3ull, "mixer_volume table unchanged");
    check(pan == 0xd97c7fe8650f96e1ull, "mixer_pan table unchanged");
    check(fog == 0x3f1744b8f436f1ecull, "fog_factor_q8 table unchanged");
    return check.finish("det_math");
}
