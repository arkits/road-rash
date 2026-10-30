// Plan 34 Task 7: the production compatibility gate (see compat.hpp).
#include "net/compat.hpp"
#include "race_setup.hpp"

#include <cctype>
#include <algorithm>
#include <stdexcept>
#include "assets.hpp"
#include "importer.hpp"
#include "sha256.hpp"

namespace rr::net {

uint64_t fnv1a64(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t sim_source_hash() {
#if defined(RR_SIM_SOURCE_HASH)
    // 64 hex chars of the build-time SHA-256 over the simulation sources;
    // the first 16 are the high bytes, as in tools/net_bots.cpp.
    const char* hex = RR_SIM_SOURCE_HASH;
    uint64_t v = 0;
    for (int i = 0; i < 16 && hex[i]; ++i) {
        char c = hex[i];
        const int d = c >= '0' && c <= '9'   ? c - '0'
                      : c >= 'a' && c <= 'f' ? c - 'a' + 10
                      : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                             : -1;
        if (d < 0) return 0x9e3779b97f4a7c15ull;
        v = (v << 4) | uint64_t(d);
    }
    return v ? v : 0x9e3779b97f4a7c15ull;
#else
    return 0x9e3779b97f4a7c15ull; // development fallback (unit tests)
#endif
}

uint64_t self_test_hash() {
    // Fixed vectors over the deterministic core. Any change to the rider
    // table, the countdown, the protocol version or the codecs changes this.
    std::string buf;
    auto u64 = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            buf.push_back(char(v & 0xff));
            v >>= 8;
        }
    };
    u64(network_self_test_hash());
    u64(protocol_version);
    u64(uint64_t(max_seats));
    u64(0x168); // networked countdown (0x00442dd0), kept from the original
    // The grid/AI table (0x00405600): full 15, limited 8, humans-only humans.
    for (int humans = 1; humans <= 8; ++humans)
        for (int ai = 0; ai <= 2; ++ai) u64(uint64_t(net_rider_count(humans, ai)));
    // Codec round-trips with fixed payloads (rejects silently change wire bytes).
    // NOTE: the Hello here is built by hand, not via make_hello: make_hello
    // fills local_fingerprint(), which calls this function.
    Hello h0;
    h0.protocol = protocol_version;
    h0.sim_fingerprint = 0x0123456789abcdefull;
    h0.platform = Platform::macos;
    h0.alias = "Abc";
    auto bytes = encode(Message(h0));
    const auto back = decode(bytes);
    u64(back && std::get_if<Hello>(&*back) ? 0x12345678ull : 0);
    u64(fnv1a64(bytes.data(), bytes.size()));
    const RoomCreate rc{{2, 2, true, true}};
    auto rb = encode(Message(rc));
    u64(fnv1a64(rb.data(), rb.size()));
    // Room-code codec: bare codes and invite links normalise identically.
    for (const char* t : {"234567", "234567", "https://road-rash.archit.xyz/?room=234567"}) {
        const auto code = parse_room_code(t);
        u64(code ? fnv1a64(code->data(), code->size()) : 0);
    }
    if (!valid_room_code("234567") || valid_room_code("000000") || valid_room_code("AAAAA") ||
        parse_room_code("https://road-rash.archit.xyz/?room=OOOOOO").has_value())
        u64(0xdeadbeefull); // alphabet violations must stay rejected
    return fnv1a64(buf.data(), buf.size());
}

DataDigest digest_from_table(const std::vector<std::pair<std::string, std::string>>& rows) {
    auto sorted = rows;
    std::sort(sorted.begin(), sorted.end()); // total-order: string pairs
    std::string buf;
    for (const auto& [path, sha] : sorted) {
        buf += path + '\n';
        for (char c : sha) buf.push_back(char(std::tolower(uint8_t(c))));
        buf += '\n';
    }
    auto hex = sha256(std::vector<uint8_t>(buf.begin(), buf.end()));
    DataDigest digest{};
    for (size_t i = 0; i < digest.size(); ++i)
        digest[i] = uint8_t(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
    return digest;
}
DataDigest verified_data_digest(const Assets& assets, const VerificationProgress& progress) {
    std::vector<std::pair<std::string, std::string>> rows;
    const auto& references = reference_hashes();
    if (progress) progress(0, references.size());
    for (const auto& reference : references) {
        if (!assets.exists(reference.path) || sha256(assets.read(reference.path)) != reference.hash)
            throw std::runtime_error("Multiplayer data verification failed: " + reference.path);
        rows.emplace_back(reference.path, reference.hash);
        if (progress) progress(rows.size(), references.size());
    }
    return digest_from_table(rows);
}

Hello make_hello(const std::string& alias, Platform platform, const DataDigest& digest,
                 const std::optional<SeatToken>& seat_token) {
    Hello h;
    h.protocol = protocol_version;
    h.sim_fingerprint = local_fingerprint();
    h.data_digest = digest;
    h.platform = platform;
    h.alias = alias.substr(0, max_alias);
    h.seat_token = seat_token;
    return h;
}

std::string reject_text(RejectReason reason) {
    switch (reason) {
    case RejectReason::protocol:
        return "Version mismatch: update the game and try again.";
    case RejectReason::fingerprint:
        return "Build mismatch: the room was made by a different game version.";
    case RejectReason::data_digest:
        return "Data mismatch: verify the game data and try again.";
    case RejectReason::room_full:
        return "The room is full.";
    case RejectReason::no_such_room:
        return "No room with that code. Check the code or link.";
    case RejectReason::room_racing:
        return "That race already started. Join as a spectator.";
    case RejectReason::rate_limited:
        return "Too many tries. Wait a moment and try again.";
    case RejectReason::server_full:
        return "The server is full. Try again later.";
    case RejectReason::bad_request:
        return "The request failed. Leave the room and try again.";
    case RejectReason::timeout:
        return "The connection timed out. Check the network and retry.";
    case RejectReason::kicked:
        return "Reconnecting from another device.";
    }
    return "The connection failed.";
}

} // namespace rr::net
