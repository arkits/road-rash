#include "explode.hpp"
#include "byte_reader.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace rr {
namespace {
constexpr size_t max_unpacked_resource_size = 1u << 24;

// Canonical Huffman code of the PKWARE DCL format, built from run-length code lengths
// (each byte: low nibble = length, high nibble + 1 = repeat count). Codes are stored inverted.
struct Huffman {
    std::array<uint16_t, 14> count{};
    std::vector<uint16_t> symbol;
    explicit Huffman(std::initializer_list<uint8_t> compact) {
        std::vector<uint8_t> length;
        for (uint8_t b : compact)
            for (int n = (b >> 4) + 1; n > 0; --n) length.push_back(b & 15);
        for (uint8_t l : length) ++count[l];
        std::array<uint16_t, 14> offset{};
        for (size_t l = 1; l < 13; ++l) offset[l + 1] = uint16_t(offset[l] + count[l]);
        symbol.resize(length.size());
        for (size_t s = 0; s < length.size(); ++s)
            if (length[s]) symbol[offset[length[s]]++] = uint16_t(s);
    }
};

struct Bits {
    const uint8_t* data;
    size_t size, pos = 0;
    uint32_t buffer = 0;
    int left = 0;
    uint32_t get(int n) {
        while (left < n) {
            if (pos >= size) throw std::runtime_error("Compressed data ends early");
            buffer |= uint32_t(data[pos++]) << left;
            left += 8;
        }
        const uint32_t v = buffer & ((1u << n) - 1);
        buffer >>= n;
        left -= n;
        return v;
    }
    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 13; ++len) {
            code |= int(get(1) ^ 1);
            const int c = h.count[size_t(len)];
            if (code < first + c) return h.symbol[size_t(index + code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        throw std::runtime_error("Invalid compressed code");
    }
};

const Huffman& literal_code() {
    static const Huffman h{11,  124, 8,  7,  28, 7,  188, 13, 76, 4,  10, 8,  12, 10,  12,  10,  8,
                           23,  8,   9,  7,  6,  7,  8,   7,  6,  55, 8,  23, 24, 12,  11,  7,   9,
                           11,  12,  6,  7,  22, 5,  7,   24, 6,  11, 9,  6,  7,  22,  7,   11,  38,
                           7,   9,   8,  25, 11, 8,  11,  9,  12, 8,  12, 5,  38, 5,   38,  5,   11,
                           7,   5,   6,  21, 6,  10, 53,  8,  7,  24, 10, 27, 44, 253, 253, 253, 252,
                           252, 252, 13, 12, 45, 12, 45,  12, 61, 12, 45, 44, 173};
    return h;
}
const Huffman& length_code() {
    static const Huffman h{2, 35, 36, 53, 38, 23};
    return h;
}
const Huffman& distance_code() {
    static const Huffman h{2, 20, 53, 230, 247, 151, 248};
    return h;
}
constexpr uint16_t length_base[16] = {3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264};
constexpr uint8_t length_extra[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
} // namespace

std::vector<uint8_t> explode(const uint8_t* data, size_t size, size_t expected) {
    if (expected > max_unpacked_resource_size) throw std::runtime_error("Unpacked size too large");
    Bits in{data, size};
    const uint32_t coded = in.get(8), dict = in.get(8);
    if (coded > 1) throw std::runtime_error("Invalid literal mode");
    if (dict < 4 || dict > 6) throw std::runtime_error("Invalid dictionary size");
    std::vector<uint8_t> out;
    out.reserve(std::min<size_t>(expected, 65536));
    for (;;) {
        if (in.get(1)) {
            const int s = in.decode(length_code());
            const uint32_t len = length_base[s] + in.get(length_extra[s]);
            if (len == 519) break;
            const int extra = len == 2 ? 2 : int(dict);
            // Two separate reads: the operands of + are unsequenced, and MSVC
            // evaluated get() before decode(), corrupting every resource
            // (plan 33b Task 6, first Windows run on real data).
            const uint32_t high = uint32_t(in.decode(distance_code())) << extra;
            const uint32_t dist = high + in.get(extra) + 1;
            if (dist > out.size()) throw std::runtime_error("Compressed distance too far back");
            if (out.size() + len > expected) throw std::runtime_error("Unpacked data overflows its size");
            for (uint32_t i = 0; i < len; ++i) out.push_back(out[out.size() - dist]);
        } else {
            if (out.size() >= expected) throw std::runtime_error("Unpacked data overflows its size");
            out.push_back(uint8_t(coded ? in.decode(literal_code()) : in.get(8)));
        }
    }
    if (out.size() != expected) throw std::runtime_error("Unpacked size mismatch");
    return out;
}

uint32_t crc32_update(uint32_t crc, const uint8_t* p, size_t n) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return crc;
}

std::vector<uint8_t> unpack_resource(const std::vector<uint8_t>& b) {
    if (b.size() < 16) throw std::runtime_error("Truncated resource header");
    const ByteReader r(b);
    const uint32_t unpacked = r.le<uint32_t>(0), stored = r.le<uint32_t>(4), crc = r.le<uint32_t>(8),
                   compressed = r.le<uint32_t>(12);
    if (unpacked > max_unpacked_resource_size) throw std::runtime_error("Unpacked size too large");
    const auto payload = r.bytes(16, stored);
    if (!compressed) {
        // 0x00415550/0x004157b0: uncompressed payloads copy `stored` bytes directly without reallocating to `unpacked`.
        if (stored != unpacked) throw std::runtime_error("Stored resource size mismatch");
        return std::vector<uint8_t>(payload.begin(), payload.end());
    }
    auto out = explode(payload.data(), payload.size(), unpacked);
    if (~crc32_update(0xffffffffu, out.data(), out.size()) != crc)
        throw std::runtime_error("Resource CRC mismatch");
    return out;
}
} // namespace rr
