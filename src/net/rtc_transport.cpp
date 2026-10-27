#include "net/rtc_channel.hpp"

namespace rr::net {
namespace {
bool race_packet(const Message& m) {
    return std::holds_alternative<Input>(m) || std::holds_alternative<Ack>(m) ||
           std::holds_alternative<Frames>(m);
}
bool signal_packet(const Message& m) {
    return std::holds_alternative<RtcOffer>(m) || std::holds_alternative<RtcAnswer>(m) ||
           std::holds_alternative<RtcCandidate>(m) || std::holds_alternative<RtcClose>(m);
}
class Negotiated final : public Channel {
  public:
    Negotiated(std::unique_ptr<Channel> ws, RtcOptions opts, Clock clock)
        : ws_(std::move(ws)), opts_(std::move(opts)), clock_(clock ? std::move(clock) : Clock(steady_ms)) {}
    bool reliable() const override { return true; }
    ChannelStatus status() const override { return ws_->status(); }
    TransportStats transport_stats() const override {
        auto s = stats_;
        s.rtc_open = peer_ && peer_->status() == ChannelStatus::open;
        return s;
    }
    void disable_rtc() override {
        if (started_ && !disabled_ && ws_->status() == ChannelStatus::open) ws_->send(encode(RtcClose{}));
        if (peer_) {
            peer_->close();
            peer_.reset();
            ++stats_.fallbacks;
        }
        disabled_ = true;
    }
    void close() override {
        disable_rtc();
        ws_->close();
    }
    void send(std::span<const uint8_t> bytes) override {
        pump();
        const auto m = decode(bytes);
        if (m && opts_.enabled && opts_.server && std::holds_alternative<Welcome>(*m) && !started_) {
            started_ = true;
            start_peer();
            if (peer_) peer_->offer();
        }
        if (m && race_packet(*m)) {
            if (peer_ && peer_->status() == ChannelStatus::open) {
                peer_->send(bytes);
                ++stats_.rtc_sent;
                return;
            }
            ++stats_.ws_race_sent;
        }
        ws_->send(bytes);
        pump();
    }
    std::optional<std::vector<uint8_t>> poll() override {
        pump();
        for (int budget = 0; budget < 64; ++budget) {
            auto bytes = ws_->poll();
            if (!bytes) break;
            const auto m = decode(*bytes);
            if (!m || !signal_packet(*m)) return bytes;
            if (!opts_.enabled || disabled_) continue;
            if (std::holds_alternative<RtcClose>(*m)) {
                disable_rtc();
                continue;
            }
            if (std::holds_alternative<RtcOffer>(*m) && !opts_.server && !started_) {
                started_ = true;
                start_peer();
            }
            if (!peer_) continue;
            const bool description =
                std::holds_alternative<RtcOffer>(*m) || std::holds_alternative<RtcAnswer>(*m);
            if (description) {
                if (description_ || (opts_.server != std::holds_alternative<RtcAnswer>(*m))) continue;
                description_ = true;
            } else if (++candidates_ > 32)
                continue;
            try {
                peer_->signal(*m);
            } catch (...) {
                disable_rtc();
            }
            pump();
        }
        if (peer_)
            for (int budget = 0; budget < 64; ++budget) {
                auto bytes = peer_->poll();
                if (!bytes) break;
                const auto m = decode(*bytes);
                // A DataChannel never acquires lobby/control privileges.
                if (m && ((opts_.server &&
                           (std::holds_alternative<Input>(*m) || std::holds_alternative<Ack>(*m))) ||
                          (!opts_.server && std::holds_alternative<Frames>(*m)))) {
                    ++stats_.rtc_received;
                    return bytes;
                }
            }
        return {};
    }

  private:
    void start_peer() {
        try {
            peer_ = make_rtc_peer(opts_);
            deadline_ = clock_() + 5000;
        } catch (...) {
            disabled_ = true;
        }
    }
    void pump() {
        if (!peer_) return;
        auto status = peer_->status();
        if (status == ChannelStatus::open) opened_ = true;
        if (status == ChannelStatus::closed || status == ChannelStatus::failed ||
            (!opened_ && clock_() >= deadline_)) {
            disable_rtc();
            return;
        }
        for (int budget = 0; budget < 64; ++budget) {
            auto m = peer_->poll_signal();
            if (!m) break;
            auto b = encode(*m);
            if (!b.empty()) ws_->send(b);
        }
    }
    std::unique_ptr<Channel> ws_;
    std::unique_ptr<RtcPeer> peer_;
    RtcOptions opts_;
    Clock clock_;
    TransportStats stats_;
    uint64_t deadline_ = 0;
    unsigned candidates_ = 0;
    bool started_ = false, description_ = false, disabled_ = false, opened_ = false;
};
} // namespace
std::unique_ptr<Channel> negotiate_rtc(std::unique_ptr<Channel> ws, RtcOptions options, Clock clock) {
    return std::make_unique<Negotiated>(std::move(ws), std::move(options), std::move(clock));
}
} // namespace rr::net
