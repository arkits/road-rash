// Plan 34 Task 4: WebSocket channel over libdatachannel. Callbacks run on the
// library's threads and only touch the queue under its mutex.
#include "net/ws_channel.hpp"

#include "net/protocol.hpp"
#include "net/rtc_channel.hpp"

#include <rtc/websocket.hpp>

#include <atomic>
#include <deque>
#include <mutex>

namespace rr::net {
namespace {

struct Inbox {
    std::mutex mu;
    std::deque<std::vector<uint8_t>> queue;
    std::atomic<ChannelStatus> status{ChannelStatus::connecting};
    size_t max_queued = 4096;
};

class WsChannel final : public Channel {
  public:
    WsChannel(std::shared_ptr<rtc::WebSocket> ws, size_t max_queued)
        : ws_(std::move(ws)), inbox_(std::make_shared<Inbox>()) {
        inbox_->max_queued = max_queued;
        std::weak_ptr<Inbox> weak = inbox_;
        ws_->onOpen([weak] {
            if (auto in = weak.lock()) in->status = ChannelStatus::open;
        });
        ws_->onClosed([weak] {
            if (auto in = weak.lock())
                if (in->status != ChannelStatus::failed) in->status = ChannelStatus::closed;
        });
        ws_->onError([weak](const std::string&) {
            if (auto in = weak.lock()) in->status = ChannelStatus::failed;
        });
        ws_->onMessage(
            [weak](rtc::binary data) {
                auto in = weak.lock();
                if (!in) return;
                std::lock_guard lock(in->mu);
                if (in->queue.size() >= in->max_queued || data.size() > max_message_bytes) {
                    in->status = ChannelStatus::failed;
                    return;
                }
                std::vector<uint8_t> bytes(data.size());
                for (size_t i = 0; i < data.size(); ++i) bytes[i] = uint8_t(data[i]);
                in->queue.push_back(std::move(bytes));
            },
            [](const rtc::string&) {}); // text frames are not part of the protocol
        if (ws_->isOpen()) inbox_->status = ChannelStatus::open;
    }
    ~WsChannel() override {
        ws_->resetCallbacks();
        close();
    }
    bool reliable() const override { return true; }
    void send(std::span<const uint8_t> bytes) override {
        if (inbox_->status != ChannelStatus::open || bytes.empty()) return;
        try {
            ws_->send(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
        } catch (...) {
            inbox_->status = ChannelStatus::failed;
        }
    }
    std::optional<std::vector<uint8_t>> poll() override {
        std::lock_guard lock(inbox_->mu);
        if (inbox_->queue.empty()) return std::nullopt;
        std::vector<uint8_t> out = std::move(inbox_->queue.front());
        inbox_->queue.pop_front();
        return out;
    }
    ChannelStatus status() const override { return inbox_->status; }
    void close() override {
        try {
            if (!ws_->isClosed()) ws_->close();
        } catch (...) {
        }
        if (inbox_->status != ChannelStatus::failed) inbox_->status = ChannelStatus::closed;
    }

  private:
    std::shared_ptr<rtc::WebSocket> ws_;
    std::shared_ptr<Inbox> inbox_;
};

class FailedChannel final : public Channel {
  public:
    bool reliable() const override { return true; }
    void send(std::span<const uint8_t>) override {}
    std::optional<std::vector<uint8_t>> poll() override { return std::nullopt; }
    ChannelStatus status() const override { return ChannelStatus::failed; }
};

} // namespace

std::unique_ptr<Channel> make_ws_channel(std::shared_ptr<rtc::WebSocket> ws, size_t max_queued) {
    return std::make_unique<WsChannel>(std::move(ws), max_queued);
}

std::unique_ptr<Channel> ws_connect(const std::string& url) {
    return ws_connect(url, {});
}

std::unique_ptr<Channel> ws_connect(const std::string& url, RtcOptions options) {
    // Reject what cannot be a WebSocket URL here: libdatachannel may treat
    // such text as a host name and fail only after a DNS lookup and the
    // connection timeout.
    const bool scheme = url.rfind("ws://", 0) == 0 || url.rfind("wss://", 0) == 0;
    if (!scheme || url.find_first_of(" \t\r\n") != std::string::npos)
        return std::make_unique<FailedChannel>();
    rtc::WebSocket::Configuration config;
    config.maxMessageSize = max_message_bytes;
    config.connectionTimeout = std::chrono::milliseconds(10000);
    config.pingInterval = std::chrono::milliseconds(10000);
    auto ws = std::make_shared<rtc::WebSocket>(config);
    auto channel = make_ws_channel(ws);
    try {
        ws->open(url);
    } catch (...) {
        return std::make_unique<FailedChannel>(); // malformed URL or no transport
    }
    return negotiate_rtc(std::move(channel), std::move(options));
}

} // namespace rr::net
