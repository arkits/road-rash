// Plan 33 Task A1: 128-bit fixed-point helpers without __int128 (MSVC and
// clang-cl have none). Results equal the former __int128 expressions bit for
// bit; tests/wide_math_test.cpp checks every path against the others.
#pragma once
#include <cstdint>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace rr {
// floor(a / 65536). C++20 defines >> on negative values as arithmetic, so this
// equals the former `a >= 0 ? a / 65536 : int64_t((__int128(a) - 65535) / 65536)`.
constexpr int64_t floor_q16_i64(int64_t a) {
    return a >> 16;
}

// Portable reference: low 64 bits of the signed 128-bit product a*b,
// arithmetically shifted right by 16 (the former
// `int64_t((__int128(a) * b) >> 16)`, whose int64 conversion wraps).
constexpr int64_t mul_shr16_portable(int64_t a, int64_t b) {
    const uint64_t ua = uint64_t(a), ub = uint64_t(b);
    const uint64_t a0 = ua & 0xffffffffu, a1 = ua >> 32, b0 = ub & 0xffffffffu, b1 = ub >> 32;
    const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    const uint64_t lo = (p00 & 0xffffffffu) | (mid << 32);
    uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32); // unsigned high word
    // Signed high word: subtract the other operand for each negative input.
    if (a < 0) hi -= ub;
    if (b < 0) hi -= ua;
    return int64_t((lo >> 16) | (hi << 48));
}

inline int64_t mul_shr16(int64_t a, int64_t b) {
#if defined(__SIZEOF_INT128__)
    return int64_t((__int128(a) * b) >> 16);
#elif defined(_MSC_VER) && defined(_M_X64)
    int64_t hi = 0;
    const uint64_t lo = uint64_t(_mul128(a, b, &hi));
    return int64_t((lo >> 16) | (uint64_t(hi) << 48));
#elif defined(_MSC_VER) && defined(_M_ARM64)
    const uint64_t lo = uint64_t(a) * uint64_t(b);
    const uint64_t hi = uint64_t(__mulh(a, b));
    return int64_t((lo >> 16) | (hi << 48));
#else
    return mul_shr16_portable(a, b);
#endif
}
} // namespace rr
