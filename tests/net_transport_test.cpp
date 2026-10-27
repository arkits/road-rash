// Cross-engine fixture: browser RTCPeerConnection <-> native libdatachannel.
// Uses actual WebSocket signalling and checks codec traffic both ways; not a race.
#include "net/rtc_channel.hpp"
#include <iostream>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
int main() {
    char url[256];
    EM_ASM(
        {
            stringToUTF8(new URLSearchParams(location.search).get('url') || 'ws://127.0.0.1:18412/net', $0,
                         256);
        },
        url);
    auto channel = rr::net::ws_connect(url, rr::net::RtcOptions{.enabled = true});
    bool hello = false, input = false, frames = false;
    auto end = rr::net::steady_ms() + 15000;
    while (rr::net::steady_ms() < end) {
        if (!hello && channel->status() == rr::net::ChannelStatus::open) {
            rr::net::Hello h;
            h.alias = "Browser";
            channel->send(rr::net::encode(h));
            hello = true;
        }
        while (auto b = channel->poll())
            if (auto m = rr::net::decode(*b))
                if (auto f = std::get_if<rr::net::Frames>(&*m))
                    frames = *f == rr::net::Frames{0, 1, {{1, 0, {123}}}};
        if (channel->transport_stats().rtc_open && !input) {
            channel->send(rr::net::encode(rr::net::Input{0, {123}}));
            input = true;
        }
        if (frames) {
            auto s = channel->transport_stats();
            if (s.rtc_sent && s.rtc_received) {
                std::cout << "net_transport browser-native OK\n";
                return 0;
            }
        }
        emscripten_sleep(5);
    }
    std::cerr << "FAIL browser-native RTC exchange\n";
    return 1;
}
#else
#include "net/ws_channel.hpp"
#include <rtc/websocketserver.hpp>
#include <mutex>
#include <thread>
int main() {
    rtc::WebSocketServer::Configuration options;
    options.port = 0;
    options.bindAddress = "127.0.0.1";
    rtc::WebSocketServer server(options);
    std::mutex mutex;
    std::vector<std::unique_ptr<rr::net::Channel>> channels;
    server.onClient([&](auto ws) {
        std::lock_guard lock(mutex);
        channels.push_back(
            rr::net::negotiate_rtc(rr::net::make_ws_channel(ws), {.enabled = true, .server = true}));
    });
    std::cout << "echo ready " << server.port() << "\n" << std::flush;
    const auto end = rr::net::steady_ms() + 30000;
    while (rr::net::steady_ms() < end) {
        {
            std::lock_guard lock(mutex);
            for (auto& c : channels)
                while (auto b = c->poll())
                    if (auto m = rr::net::decode(*b)) {
                        if (std::holds_alternative<rr::net::Hello>(*m))
                            c->send(rr::net::encode(rr::net::Welcome{1}));
                        if (std::get_if<rr::net::Input>(&*m))
                            c->send(rr::net::encode(rr::net::Frames{0, 1, {{1, 0, {123}}}}));
                    }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
#endif
