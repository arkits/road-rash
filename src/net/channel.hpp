// Plan 34 Task 4: one transport interface for the WebSocket (reliable,
// ordered) and WebRTC DataChannel (unreliable, unordered) links, plus a
// deterministic simulated network for tests. Network callbacks only fill a
// lock-protected queue; the main thread polls, so the simulation never runs
// on a network thread.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace rr::net {

enum class ChannelStatus : uint8_t { connecting, open, closed, failed };

struct TransportStats {
    uint64_t rtc_sent = 0, rtc_received = 0, ws_race_sent = 0, fallbacks = 0;
    bool rtc_open = false;
};

class Channel {
  public:
    virtual bool reliable() const = 0;
    virtual TransportStats transport_stats() const { return {}; }
    virtual void disable_rtc() {}
    virtual void send(std::span<const uint8_t>) = 0;
    virtual std::optional<std::vector<uint8_t>> poll() = 0; // non-blocking
    virtual ChannelStatus status() const = 0;
    virtual void close() {}
    virtual ~Channel() = default;
};

// Milliseconds on a monotonic clock. Tests pass a virtual clock so that the
// simulated network is fully deterministic.
using Clock = std::function<uint64_t()>;
uint64_t steady_ms();

// Simulated network: per-message latency uniform in [min_ms, max_ms], loss and
// duplication probabilities (unreliable only), seeded and reproducible.
// Reliable pairs never drop, duplicate or reorder (latency is made monotonic).
struct NetSim {
    int min_ms = 0, max_ms = 0;
    double loss = 0.0, duplicate = 0.0;
    uint32_t seed = 1;
};
std::pair<std::unique_ptr<Channel>, std::unique_ptr<Channel>> loopback_pair(NetSim, bool reliable,
                                                                            Clock clock = {});

// libdatachannel WebSocket client (wss:// or ws://). Defined in
// ws_channel.cpp, which is built only with RR_NET_TRANSPORT.
std::unique_ptr<Channel> ws_connect(const std::string& url);

} // namespace rr::net
