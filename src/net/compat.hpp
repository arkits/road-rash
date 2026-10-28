// Plan 34 Task 7: the production compatibility gate. Every client sends a
// Hello carrying the protocol version, a simulation fingerprint and the
// reference data digest; a room only admits clients whose three values match
// the room's (enforced by rr-netd, see server/room.cpp). Startup verifies
// the installed reference data before constructing the Hello.
#pragma once
#include "net/protocol.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rr {
class Assets;
}
namespace rr::net {

// FNV-1a 64 over bytes; the one hash used for fingerprints (integer-only, so
// it is identical on every platform for the same input).
uint64_t fnv1a64(const void* data, size_t size);
inline uint64_t fnv1a64_str(const std::string& s) {
    return fnv1a64(s.data(), s.size());
}

// Build-time hash of the simulation sources (RR_SIM_SOURCE_HASH, wired in
// cmake/net.cmake over the race/traffic/police/pedestrian/ring/net sources).
// Falls back to a fixed development constant when the build did not define it
// (unit tests); production builds always define it.
uint64_t sim_source_hash();

// Startup self-test over the deterministic core that needs no game data:
// a synthetic multi-human simulation, codec vectors, rider counts, room codes
// and the countdown constant. Returns their combined hash.
// Cross-platform simulation equality itself is proven by the determinism
// matrix (plan 34 Task 3, tools/determinism_matrix.sh), which runs a fixed
// multi-human input log on every shipped platform.
uint64_t self_test_hash();

// The handshake fingerprint: the source hash combined with the self-test, so
// a stale binary and a passing-but-divergent build both fail the gate.
inline uint64_t local_fingerprint() {
    static const uint64_t mixed = sim_source_hash() ^ (self_test_hash() * 1099511628211ull);
    return mixed ? mixed : 0x9e3779b97f4a7c15ull;
}

// Reference data digest: SHA-256 (hex -> 32 bytes) over the canonical
// "path + sha" lines of the committed reference table. Identical for every
// build from this source; the per-file verification against the user's disc
// happens at import/startup before any Hello is sent.
// Reports only successfully verified files; failures never advance progress.
using VerificationProgress = std::function<void(size_t completed, size_t total)>;
DataDigest verified_data_digest(const Assets&, const VerificationProgress& progress = {});
DataDigest digest_from_table(const std::vector<std::pair<std::string, std::string>>& rows);

// Production Hello. The caller fills alias/platform/seat_token; the gate
// fills protocol, fingerprint and digest.
Hello make_hello(const std::string& alias, Platform platform, const DataDigest& digest,
                 const std::optional<SeatToken>& seat_token = std::nullopt);

// Human-readable menu text for a rejection (original strings are gameplay;
// these errors are native, so they use native slots in the frontend).
std::string reject_text(RejectReason reason);

} // namespace rr::net
