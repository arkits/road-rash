#include "check.hpp"
#include "net/rtc_channel.hpp"
#include <thread>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
using namespace rr::net;
int main() {
    rr::test::Checks check;
    auto pair = loopback_pair({}, true);
    auto server = negotiate_rtc(std::move(pair.first), {.enabled = true, .server = true});
    auto client = negotiate_rtc(std::move(pair.second), {.enabled = true});
    auto pump = [&] {
        server->poll();
        client->poll();
    };
    auto wait = [&](auto pred) {
        auto until = steady_ms() + 6000;
        while (steady_ms() < until) {
            if (pred()) return true;
#ifdef __EMSCRIPTEN__
            emscripten_sleep(2);
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
#endif
        }
        return false;
    };
    server->send(encode(Welcome{1}));
    check(wait([&] {
              pump();
              return server->transport_stats().rtc_open && client->transport_stats().rtc_open;
          }),
          "real UDP/SCTP peers negotiate over reliable signalling");
    if (check.failures) return check.finish("net_rtc");
    Input input{5, {1, 2, 3}};
    client->send(encode(input));
    check(wait([&] {
              client->poll();
              auto p = server->poll();
              return p && decode(*p) == Message(input);
          }),
          "input travels on actual unordered zero-retransmit DataChannel");
    check(client->transport_stats().rtc_sent == 1 && server->transport_stats().rtc_received == 1,
          "DataChannel counters prove selected route");
    Chat chat{0xff, "reliable control"};
    client->send(encode(chat));
    check(wait([&] {
              auto p = server->poll();
              return p && decode(*p) == Message(chat);
          }),
          "chat stays on WebSocket");
    check(server->transport_stats().rtc_received == 1, "control did not use UDP");
    Frames frames{0, 1, {{1, 0, {42}}}};
    server->send(encode(frames));
    check(wait([&] {
              auto p = client->poll();
              return p && decode(*p) == Message(frames);
          }),
          "sealed frames travel on DataChannel");
    client->disable_rtc();
    check(wait([&] {
              pump();
              return !server->transport_stats().rtc_open;
          }),
          "mid-session close makes both sides fall back");
    frames.first_tick = 1;
    server->send(encode(frames));
    check(wait([&] {
              auto p = client->poll();
              return p && decode(*p) == Message(frames);
          }),
          "frames continue over surviving WebSocket");
    check(server->status() == ChannelStatus::open && client->status() == ChannelStatus::open,
          "RTC close cannot close reliable control");
    uint64_t now = 100;
    auto blocked = loopback_pair({}, true);
    auto a = negotiate_rtc(std::move(blocked.first), {.enabled = true, .server = true}, [&] { return now; });
    auto b = negotiate_rtc(std::move(blocked.second), {}, [&] { return now; });
    a->send(encode(Welcome{2}));
    b->poll();
    now = 5101;
    a->poll();
    check(a->transport_stats().fallbacks == 1 && !a->transport_stats().rtc_open,
          "blocked negotiation falls back at five seconds");
    b->send(encode(input));
    check(wait([&] {
              auto p = a->poll();
              return p && decode(*p) == Message(input);
          }),
          "UDP-disabled client works with RTC-enabled server");
    return check.finish("net_rtc");
}
