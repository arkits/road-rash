// Browser WebSocket implementation of the same main-thread Channel contract.
#include "net/channel.hpp"
#include "net/protocol.hpp"
#include "net/rtc_channel.hpp"
#include <emscripten.h>

// clang-format off
EM_JS(int, rr_ws_open, (const char* url), {
    Module.rrSockets ||= new Map();
    Module.rrSocketNext = (Module.rrSocketNext || 0) + 1;
    const id = Module.rrSocketNext;
    const state = { status: 0, queue: [], ws: null };
    Module.rrSockets.set(id, state);
    try {
        const ws = new WebSocket(UTF8ToString(url)); state.ws = ws; ws.binaryType = 'arraybuffer';
        ws.onopen = () => { state.status = 1; };
        ws.onerror = () => { state.status = 3; };
        ws.onclose = () => { if (state.status !== 3) state.status = 2; };
        ws.onmessage = event => {
            if (!(event.data instanceof ArrayBuffer) || event.data.byteLength > 65536 || state.queue.length >= 4096) {
                state.status = 3; ws.close(); return;
            }
            state.queue.push(new Uint8Array(event.data));
        };
    } catch (error) { state.status = 3; }
    return id;
});
EM_JS(int, rr_ws_status, (int id), { return Module.rrSockets.get(id)?.status ?? 2; });
EM_JS(void, rr_ws_send, (int id, const uint8_t* bytes, int size), {
    const state = Module.rrSockets.get(id); if (!state || state.status !== 1) return;
    if (state.ws.bufferedAmount > 1048576) { state.status = 3; state.ws.close(); return; }
    try { state.ws.send(HEAPU8.slice(bytes, bytes + size)); } catch (error) { state.status = 3; }
});
EM_JS(int, rr_ws_size, (int id), {
    const state = Module.rrSockets.get(id); return state?.queue.length ? state.queue[0].length : -1;
});
EM_JS(void, rr_ws_read, (int id, uint8_t* bytes), {
    const state = Module.rrSockets.get(id); if (state?.queue.length) HEAPU8.set(state.queue.shift(), bytes);
});
EM_JS(void, rr_ws_close, (int id), {
    const state = Module.rrSockets.get(id);
    if (state) { state.status = 2; state.ws?.close(); Module.rrSockets.delete(id); }
});
EM_JS(int, rr_ws_rtc_optin, (), {
    const p = new URLSearchParams(globalThis.location?.search ?? ""); // Node bots have no location
    return p.get("rtc") === "1" && p.get("udp") !== "off";
});
// clang-format on
namespace rr::net {
namespace {
class BrowserSocket final : public Channel {
  public:
    explicit BrowserSocket(const std::string& url) : id_(rr_ws_open(url.c_str())) {}
    ~BrowserSocket() override { close(); }
    bool reliable() const override { return true; }
    ChannelStatus status() const override { return ChannelStatus(rr_ws_status(id_)); }
    void send(std::span<const uint8_t> bytes) override {
        if (bytes.size() <= max_message_bytes) rr_ws_send(id_, bytes.data(), int(bytes.size()));
    }
    std::optional<std::vector<uint8_t>> poll() override {
        const int size = rr_ws_size(id_);
        if (size < 0) return {};
        std::vector<uint8_t> bytes(size_t(size), 0);
        rr_ws_read(id_, bytes.data());
        return bytes;
    }
    void close() override {
        if (id_) {
            rr_ws_close(id_);
            id_ = 0;
        }
    }

  private:
    int id_;
};
} // namespace
std::unique_ptr<Channel> ws_connect(const std::string& url) {
    return ws_connect(url, {});
}
std::unique_ptr<Channel> ws_connect(const std::string& url, RtcOptions options) {
    options.enabled = options.enabled || rr_ws_rtc_optin();
    return negotiate_rtc(std::make_unique<BrowserSocket>(url), std::move(options));
}
} // namespace rr::net
