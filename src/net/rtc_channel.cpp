#include "net/rtc_channel.hpp"
#include <rtc/rtc.hpp>
#include <atomic>
#include <deque>
#include <mutex>

namespace rr::net {
namespace {
struct Inbox {
    std::mutex mu;
    std::deque<Message> signals;
    std::deque<std::vector<uint8_t>> packets;
    std::atomic<ChannelStatus> state{ChannelStatus::connecting};
    std::shared_ptr<rtc::DataChannel> data;
};
void attach(const std::shared_ptr<Inbox>& box, std::shared_ptr<rtc::DataChannel> data) {
    std::unique_lock lock(box->mu);
    if (box->data) {
        data->close();
        return;
    }
    if (!data->reliability().unordered || data->reliability().maxRetransmits != 0 ||
        data->label() != "race") {
        box->state = ChannelStatus::failed;
        data->close();
        return;
    }
    box->data = data;
    lock.unlock();
    std::weak_ptr<Inbox> weak = box;
    data->onOpen([weak] {
        if (auto b = weak.lock()) b->state = ChannelStatus::open;
    });
    data->onClosed([weak] {
        if (auto b = weak.lock()) b->state = ChannelStatus::closed;
    });
    data->onError([weak](const std::string&) {
        if (auto b = weak.lock()) b->state = ChannelStatus::failed;
    });
    data->onMessage([weak](rtc::message_variant message) {
        auto b = weak.lock();
        if (!b) return;
        auto bytes = std::get_if<rtc::binary>(&message);
        if (!bytes) return;
        std::lock_guard lock(b->mu);
        if (bytes->empty() || bytes->size() > max_message_bytes || b->packets.size() >= 4096) {
            b->state = ChannelStatus::failed;
            return;
        }
        const auto* p = reinterpret_cast<const uint8_t*>(bytes->data());
        b->packets.emplace_back(p, p + bytes->size());
    });
    if (data->isOpen()) box->state = ChannelStatus::open;
}
class Peer final : public RtcPeer {
  public:
    explicit Peer(const RtcOptions& options) : box_(std::make_shared<Inbox>()) {
        rtc::Configuration cfg;
        cfg.portRangeBegin = options.port_begin;
        cfg.portRangeEnd = options.port_end;
        cfg.maxMessageSize = max_message_bytes;
        // Server peers share the configured UDP range instead of exhausting one port per seat.
        cfg.enableIceUdpMux = options.server;
        pc_ = std::make_shared<rtc::PeerConnection>(cfg);
        std::weak_ptr<Inbox> weak = box_;
        pc_->onLocalDescription([weak](rtc::Description d) {
            if (auto b = weak.lock()) {
                std::lock_guard lock(b->mu);
                if (b->signals.size() >= 64) {
                    b->state = ChannelStatus::failed;
                    return;
                }
                if (d.type() == rtc::Description::Type::Offer)
                    b->signals.emplace_back(RtcOffer{std::string(d)});
                else
                    b->signals.emplace_back(RtcAnswer{std::string(d)});
            }
        });
        pc_->onLocalCandidate([weak, ip = options.public_ip](rtc::Candidate c) {
            if (auto b = weak.lock()) {
                if (!ip.empty()) c.changeAddress(ip);
                std::lock_guard lock(b->mu);
                if (b->signals.size() >= 64) {
                    b->state = ChannelStatus::failed;
                    return;
                }
                b->signals.emplace_back(RtcCandidate{std::string(c), c.mid()});
            }
        });
        pc_->onStateChange([weak](rtc::PeerConnection::State s) {
            if (auto b = weak.lock()) {
                if (s == rtc::PeerConnection::State::Failed) b->state = ChannelStatus::failed;
                if (s == rtc::PeerConnection::State::Disconnected || s == rtc::PeerConnection::State::Closed)
                    b->state = ChannelStatus::closed;
            }
        });
        pc_->onDataChannel([weak](std::shared_ptr<rtc::DataChannel> dc) {
            if (auto b = weak.lock()) attach(b, std::move(dc));
        });
    }
    ~Peer() override {
        close();
        pc_->resetCallbacks();
    }
    void offer() override {
        rtc::DataChannelInit init;
        init.reliability.unordered = true;
        init.reliability.maxRetransmits = 0;
        attach(box_, pc_->createDataChannel("race", init));
    }
    void signal(const Message& m) override {
        if (auto o = std::get_if<RtcOffer>(&m))
            pc_->setRemoteDescription(rtc::Description(o->sdp, "offer"));
        else if (auto a = std::get_if<RtcAnswer>(&m))
            pc_->setRemoteDescription(rtc::Description(a->sdp, "answer"));
        else if (auto c = std::get_if<RtcCandidate>(&m))
            pc_->addRemoteCandidate(rtc::Candidate(c->candidate, c->mid));
    }
    std::optional<Message> poll_signal() override {
        std::lock_guard lock(box_->mu);
        if (box_->signals.empty()) return {};
        auto m = std::move(box_->signals.front());
        box_->signals.pop_front();
        return m;
    }
    ChannelStatus status() const override { return box_->state; }
    void send(std::span<const uint8_t> bytes) override {
        std::shared_ptr<rtc::DataChannel> dc;
        {
            std::lock_guard lock(box_->mu);
            dc = box_->data;
        }
        if (!dc || status() != ChannelStatus::open || bytes.size() > max_message_bytes) return;
        try {
            if (dc->bufferedAmount() > 1048576) {
                box_->state = ChannelStatus::failed;
                return;
            }
            dc->send(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
        } catch (...) {
            box_->state = ChannelStatus::failed;
        }
    }
    std::optional<std::vector<uint8_t>> poll() override {
        std::lock_guard lock(box_->mu);
        if (box_->packets.empty()) return {};
        auto p = std::move(box_->packets.front());
        box_->packets.pop_front();
        return p;
    }
    void close() override {
        box_->state = ChannelStatus::closed;
        try {
            pc_->close();
        } catch (...) {
        }
    }

  private:
    std::shared_ptr<Inbox> box_;
    std::shared_ptr<rtc::PeerConnection> pc_;
};
} // namespace
std::unique_ptr<RtcPeer> make_rtc_peer(const RtcOptions& options) {
    return std::make_unique<Peer>(options);
}
} // namespace rr::net
