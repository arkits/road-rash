// Plan 33 Task A1: the 128-bit helpers equal the former __int128 code (where
// the compiler has it) and the portable reference on every platform.
#include "check.hpp"
#include "wide_math.hpp"
#include <cstdint>
#include <limits>
#include <vector>

int main() {
    rr::test::Checks check;
    constexpr int64_t lo = std::numeric_limits<int64_t>::min(), hi = std::numeric_limits<int64_t>::max();
    std::vector<int64_t> values{0,
                                1,
                                -1,
                                2,
                                -2,
                                65535,
                                65536,
                                -65535,
                                -65536,
                                -65537,
                                65537,
                                lo,
                                lo + 1,
                                hi,
                                hi - 1,
                                int64_t(1) << 32,
                                -(int64_t(1) << 32),
                                (int64_t(1) << 47) - 1,
                                -(int64_t(1) << 47),
                                0x7fffffff,
                                -0x7fffffffLL - 1,
                                0x123456789abcdefLL,
                                -0x123456789abcdefLL};
    uint64_t state = 0x9e3779b97f4a7c15ull;
    for (int i = 0; i < 4000; ++i) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        const int shift = int(state >> 58); // vary magnitude
        values.push_back(int64_t(state) >> shift);
    }
    bool floor_ok = true, portable_ok = true, int128_ok = true;
    for (int64_t a : values) {
#if defined(__SIZEOF_INT128__)
        floor_ok &= rr::floor_q16_i64(a) == (a >= 0 ? a / 65536 : int64_t((__int128(a) - 65535) / 65536));
#else
        floor_ok &= rr::floor_q16_i64(a) == (a >= 0 ? a / 65536 : -((-(a + 1)) / 65536) - 1);
#endif
        for (int64_t b : {values[size_t(state++ % values.size())], int64_t(65536 / 3), int64_t(1),
                          int64_t(-1), int64_t(65536), lo, hi}) {
            portable_ok &= rr::mul_shr16(a, b) == rr::mul_shr16_portable(a, b);
#if defined(__SIZEOF_INT128__)
            int128_ok &= rr::mul_shr16_portable(a, b) == int64_t((__int128(a) * b) >> 16);
#endif
        }
    }
    check(floor_ok, "floor_q16 equals the former floor64 expression");
    check(portable_ok, "mul_shr16 equals the portable reference");
    check(int128_ok, "portable reference equals the former __int128 expression");
    static_assert(rr::floor_q16_i64(-1) == -1 && rr::floor_q16_i64(-65536) == -1 &&
                  rr::floor_q16_i64(-65537) == -2);
    static_assert(rr::mul_shr16_portable(65536, 3) == 3 && rr::mul_shr16_portable(-65536, 3) == -3);
    return check.finish("wide_math");
}
