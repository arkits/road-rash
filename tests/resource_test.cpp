#include "resource.hpp"
#include "explode.hpp"
#include "rri.hpp"
#include <iostream>
#include <functional>

int main() {
    int failures = 0;
    auto check = [&](bool value) {
        if (!value) ++failures;
    };
    auto reject = [&](const std::vector<uint8_t>& b) {
        try {
            rr::read_resources(b);
            ++failures;
        } catch (const std::runtime_error&) {
        }
    };
    std::vector<uint8_t> b(112);
    auto word = [&](size_t p, uint32_t v) {
        for (int i = 0; i < 4; ++i) b[p + i] = uint8_t(v >> (8 * i));
    };
    auto tag = [&](size_t p, const char* s) {
        for (int i = 0; i < 4; ++i) b[p + i] = s[i];
    };
    tag(0, "CRSR");
    tag(12, "LBTR");
    word(16, 48);
    tag(48, "LBTR");
    word(56, 1);
    tag(64, "CEPS");
    word(68, 7);
    word(72, 96);
    word(76, 16);
    auto items = rr::read_resources(b);
    check(items.size() == 1 && items[0].tag == "SPEC" && items[0].id == 7 && items[0].offset == 96 &&
          items[0].size == 16);
    for (size_t length = 0; length < 112; ++length)
        reject(std::vector<uint8_t>(b.begin(), b.begin() + length));
    word(72, 0xfffffff0);
    reject(b);
    word(72, 96);
    word(76, 0xffffffff);
    reject(b);
    word(76, 16);
    word(56, 0xffffffff);
    reject(b);
    word(56, 1);
    word(16, 0xfffffff0);
    reject(b);
    word(16, 48);
    b[0] = 0;
    reject(b);
    auto reject_unpack = [&](const std::vector<uint8_t>& u) {
        try {
            (void)rr::unpack_resource(u);
            ++failures;
        } catch (const std::runtime_error&) {
        } catch (...) {
            ++failures;
        }
    };
    // Plan 22 Task 3 Step 3: uncompressed resource where stored != unpacked must be rejected
    reject_unpack({100, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4});
    // Oversized expected unpacked length (> 16 MiB) rejected before allocation
    reject_unpack({0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    try {
        const uint8_t raw[2] = {0, 4};
        (void)rr::explode(raw, 2, (1u << 24) + 1);
        ++failures;
    } catch (const std::runtime_error&) {
    }
    {
        // Plan 33b Task 6: PKWARE DCL example from zlib's contrib/blast
        // (literals, then a back-reference whose distance has extra bits).
        // MSVC read those bits out of order before explode.cpp sequenced them.
        const uint8_t blast[8] = {0x00, 0x04, 0x82, 0x24, 0x25, 0x8f, 0x80, 0x7f};
        const auto out = rr::explode(blast, sizeof blast, 13);
        if (std::string(out.begin(), out.end()) != "AIAIAIAIAIAIA") {
            std::cerr << "FAIL explode back-reference\n";
            ++failures;
        }
    }
    // Plan 22 Task 3 Step 3 (F4): oversized SOF dimensions in decode_rri must throw std::runtime_error without OOM
    std::vector<uint8_t> jpg = {
        255, 216, 255, 224, 0,   16,  74,  70,  73,  70,  0,   1,   1,   0,   0,    1,    0,   1,   0,   0,
        255, 219, 0,   67,  0,   8,   6,   6,   7,   6,   5,   8,   7,   7,   7,    9,    9,   8,   10,  12,
        20,  13,  12,  11,  11,  12,  25,  18,  19,  15,  20,  29,  26,  31,  30,   29,   26,  28,  28,  32,
        36,  46,  39,  32,  34,  44,  35,  28,  28,  40,  55,  41,  44,  48,  49,   52,   52,  52,  31,  39,
        57,  61,  56,  50,  60,  46,  51,  52,  50,  255, 192, 0,   11,  8,   0x10, 0x01, 0,   1,   1,   1,
        17,  0,   255, 196, 0,   31,  0,   0,   1,   5,   1,   1,   1,   1,   1,    1,    0,   0,   0,   0,
        0,   0,   0,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,   255,  196, 0,   181, 16,
        0,   2,   1,   3,   3,   2,   4,   3,   5,   5,   4,   4,   0,   0,   1,    125,  1,   2,   3,   0,
        4,   17,  5,   18,  33,  49,  65,  6,   19,  81,  97,  7,   34,  113, 20,   50,   129, 145, 161, 8,
        35,  66,  177, 193, 21,  82,  209, 240, 36,  51,  98,  114, 130, 9,   10,   22,   23,  24,  25,  26,
        37,  38,  39,  40,  41,  42,  52,  53,  54,  55,  56,  57,  58,  67,  68,   69,   70,  71,  72,  73,
        74,  83,  84,  85,  86,  87,  88,  89,  90,  99,  100, 101, 102, 103, 104,  105,  106, 115, 116, 117,
        118, 119, 120, 121, 122, 131, 132, 133, 134, 135, 136, 137, 138, 146, 147,  148,  149, 150, 151, 152,
        153, 154, 162, 163, 164, 165, 166, 167, 168, 169, 170, 178, 179, 180, 181,  182,  183, 184, 185, 186,
        194, 195, 196, 197, 198, 199, 200, 201, 202, 210, 211, 212, 213, 214, 215,  216,  217, 218, 225, 226,
        227, 228, 229, 230, 231, 232, 233, 234, 241, 242, 243, 244, 245, 246, 247,  248,  249, 250, 255, 218,
        0,   8,   1,   1,   0,   0,   63,  0,   43,  255, 217};
    try {
        (void)rr::decode_rri(jpg);
        ++failures;
    } catch (const std::runtime_error&) {
    }
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
