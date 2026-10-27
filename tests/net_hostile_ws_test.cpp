// Plan 34 Task 11 Step 2: hostile input over real WebSocket sockets. Two
// honest clients share a room while attackers send text frames, oversized
// frames, fuzzed binary, server-only messages and a flood. The honest seats
// must keep working; the server counts what it dropped.
//
// By default the test runs rr::netd::Server in this process exactly as
// server/main.cpp wires it. With RR_NETD_URL=ws://host:port/net it targets an
// external rr-netd instead (counters are then read from its /net/health).
#include "check.hpp"
#include "net/channel.hpp"
#include "net/protocol.hpp"
#include "net/ws_channel.hpp"
#include "room.hpp"

#include <rtc/websocket.hpp>
#include <rtc/websocketserver.hpp>

#include <cstdlib>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

using namespace rr::net;

namespace {

struct Peer {
    std::unique_ptr<Channel> ch;
    std::deque<Message> inbox;
    void send(const Message& m) { ch->send(encode(m)); }
    void drain() {
        while (auto p = ch->poll())
            if (auto m = decode(*p)) inbox.push_back(*m);
    }
    template <class T> std::optional<T> last() {
        drain();
        std::optional<T> out;
        for (const Message& m : inbox)
            if (const T* t = std::get_if<T>(&m)) out = *t;
        return out;
    }
    template <class T> size_t count() {
        drain();
        size_t n = 0;
        for (const Message& m : inbox) n += std::holds_alternative<T>(m);
        return n;
    }
};

Hello hello(const std::string& alias) {
    Hello h;
    h.alias = alias;
    h.sim_fingerprint = 0x5eed'0000'0000'0001ull; // a build no real client has
    h.data_digest[0] = 0x42;
    h.platform = Platform::bot;
    return h;
}

} // namespace

int main() {
    rr::test::Checks check;
    const char* external = std::getenv("RR_NETD_URL");

    // In-process server, wired as server/main.cpp does.
    std::unique_ptr<rtc::WebSocketServer> ws;
    std::unique_ptr<rr::netd::Server> server;
    std::mutex mu;
    std::vector<std::shared_ptr<rtc::WebSocket>> incoming;
    std::string url;
    if (external) {
        url = external;
    } else {
        rtc::WebSocketServer::Configuration wcfg;
        wcfg.port = 0;
        wcfg.bindAddress = "127.0.0.1";
        wcfg.maxMessageSize = max_message_bytes;
        ws = std::make_unique<rtc::WebSocketServer>(wcfg);
        ws->onClient([&](std::shared_ptr<rtc::WebSocket> c) {
            std::lock_guard lock(mu);
            incoming.push_back(std::move(c));
        });
        rr::netd::ServerConfig cfg;
        cfg.seed = 3;
        server = std::make_unique<rr::netd::Server>(cfg);
        url = "ws://127.0.0.1:" + std::to_string(ws->port()) + "/net";
    }
    auto pump = [&] {
        if (!server) return;
        {
            std::lock_guard lock(mu);
            for (auto& c : incoming) server->add_connection(make_ws_channel(c), steady_ms());
            incoming.clear();
        }
        server->step(steady_ms());
    };
    auto wait_for = [&](auto&& pred, uint64_t ms = 5000) {
        const uint64_t until = steady_ms() + ms;
        do {
            pump();
            if (pred()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (steady_ms() < until);
        return false;
    };
    auto settle = [&](uint64_t ms) { wait_for([] { return false; }, ms); };

    Peer a{ws_connect(url), {}}, b{ws_connect(url), {}};
    check(wait_for(
              [&] { return a.ch->status() == ChannelStatus::open && b.ch->status() == ChannelStatus::open; }),
          "honest clients connect");
    a.send(hello("Honest A"));
    a.send(RoomCreate{{1, 2, true, false}});
    check(wait_for([&] { return a.last<Welcome>() && !a.last<Welcome>()->room_code.empty(); }),
          "room created");
    if (check.failures) return check.finish("net_hostile_ws");
    const std::string code = a.last<Welcome>()->room_code;
    b.send(hello("Honest B"));
    b.send(RoomJoin{code, false});
    check(wait_for([&] { return b.last<RoomState>() && b.last<RoomState>()->your_seat == 1; }), "B joins");

    // Attacker 1: a raw socket sending text frames, an oversized binary frame
    // and fuzzed binary frames without ever saying Hello.
    auto raw = std::make_shared<rtc::WebSocket>();
    raw->open(url);
    check(wait_for([&] { return raw->isOpen(); }), "raw attacker connects");
    try {
        raw->send(std::string("GET / HTTP/1.1\r\n\r\n"));
        std::mt19937 rng(77);
        for (int i = 0; i < 200; ++i) {
            rtc::binary junk(size_t(1 + rng() % 64));
            for (std::byte& x : junk) x = std::byte(rng());
            raw->send(junk);
        }
        raw->send(rtc::binary(max_message_bytes * 2, std::byte{0x15}));
    } catch (...) { /* the server may already have closed it */
    }

    // Attacker 2: a hello'd connection sending server-only messages, garbage
    // after a valid type byte, and race messages while in no room.
    Peer x{ws_connect(url), {}};
    check(wait_for([&] { return x.ch->status() == ChannelStatus::open; }), "attacker 2 connects");
    x.send(hello("Mallory"));
    x.send(Official{});
    x.send(Desync{59});
    x.send(ChatRelay{0, 0, "forged"});
    x.send(Input{0, {1, 2, 3}});
    x.send(Hash{59, 1});
    x.send(Result{0, Outcome::won, 0, 1});
    x.ch->send(std::vector<uint8_t>{message_type(Start{}), 0xff, 0xff});

    // Attacker 3: a flood far past the rate limit.
    Peer f{ws_connect(url), {}};
    check(wait_for([&] { return f.ch->status() == ChannelStatus::open; }), "flooder connects");
    f.send(hello("Flood"));
    for (int i = 0; i < 20000 && f.ch->status() == ChannelStatus::open; ++i) f.send(Ping{uint32_t(i)});
    check(wait_for(
              [&] {
                  return f.ch->status() == ChannelStatus::closed || f.ch->status() == ChannelStatus::failed;
              },
              10000),
          "flooder disconnected");
    check(wait_for([&] { return !raw->isOpen(); }, 10000), "raw attacker without Hello disconnected");
    settle(300);

    // Honest seats are unaffected: chat still flows, nobody was told anything forged.
    const size_t before = b.count<ChatRelay>();
    a.send(Chat{0xff, "still racing"});
    check(wait_for([&] { return b.count<ChatRelay>() > before; }), "honest chat still relayed");
    check(a.ch->status() == ChannelStatus::open && b.ch->status() == ChannelStatus::open,
          "honest clients stay connected");
    check(!a.last<Desync>() && !b.last<Desync>() && !a.last<Official>() && !b.last<Official>(),
          "no forged message reached an honest client");
    check(b.last<RoomState>() && b.last<RoomState>()->seats[0].occupied &&
              b.last<RoomState>()->seats[1].occupied,
          "both seats still held");

    if (server) {
        const auto& c = server->counters();
        check(c.malformed > 0 && c.rate_limited > 0 && c.invalid >= 3 && c.wrong_seat_inputs >= 1 &&
                  c.rejected > 0,
              "server counted malformed, rate-limited, invalid and wrong-seat traffic");
        std::cout << server->health_json(steady_ms()) << '\n';
    }
    a.ch->close();
    b.ch->close();
    x.ch->close();
    settle(100);
    if (ws) ws->stop();
    return check.finish("net_hostile_ws");
}
