// Plan 34 Task 4 Step 2: the libdatachannel WebSocket channel carries protocol
// messages both ways over a real localhost socket; callbacks only queue, the
// test thread polls.
#include "check.hpp"
#include "net/channel.hpp"
#include "net/protocol.hpp"
#include "net/system_ca.hpp"
#include "net/ws_channel.hpp"

#include <rtc/websocketserver.hpp>

#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

using namespace rr::net;

int main() {
    rr::test::Checks check;
    // Plan 33b Task 6: wss:// trusts the OS roots where OpenSSL is static.
    {
        const std::string& file = system_ca_file();
#if defined(_WIN32) || defined(__APPLE__)
        check(!file.empty() && system_ca_count() > 20, "OS root store exported");
#else
        // Plan 33a: a distribution bundle, when the host has one.
        if (!file.empty()) check(system_ca_count() > 20, "distribution CA bundle found");
#endif
        if (!file.empty()) {
            std::ifstream in(file, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            const std::string pem = text.str();
            int begins = 0;
            for (size_t at = pem.find("-----BEGIN CERTIFICATE-----"); at != std::string::npos;
                 at = pem.find("-----BEGIN CERTIFICATE-----", at + 1))
                ++begins;
            check(begins == system_ca_count(), "one PEM block per root");
            check(&system_ca_file() == &file, "written once per process");
        }
    }
    rtc::WebSocketServer::Configuration config;
    config.port = 0; // ephemeral
    config.bindAddress = "127.0.0.1";
    config.maxMessageSize = max_message_bytes;
    rtc::WebSocketServer server(config);
    std::mutex mu;
    std::unique_ptr<Channel> accepted;
    server.onClient([&](std::shared_ptr<rtc::WebSocket> ws) {
        std::lock_guard lock(mu);
        accepted = make_ws_channel(ws);
    });
    auto client = ws_connect("ws://127.0.0.1:" + std::to_string(server.port()) + "/net");
    check(client->reliable(), "WebSocket channel is reliable");
    auto wait_for = [](auto&& pred) {
        const uint64_t until = steady_ms() + 5000;
        do {
            if (pred()) return true; // evaluate once per pass: pred may consume a message
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (steady_ms() < until);
        return false;
    };
    check(wait_for([&] { return client->status() == ChannelStatus::open; }), "client opens");
    check(wait_for([&] {
              std::lock_guard lock(mu);
              return accepted && accepted->status() == ChannelStatus::open;
          }),
          "server accepts");
    if (check.failures) return check.finish("net_ws");

    const std::vector<uint8_t> ping = encode(Ping{77});
    for (int i = 0; i < 100; ++i) client->send(ping);
    int got = 0;
    check(wait_for([&] {
              std::lock_guard lock(mu);
              while (auto m = accepted->poll()) got += decode(*m) == Message(Ping{77});
              return got == 100;
          }),
          "100 client messages arrive intact and in order");
    LogChunk big;
    big.frames.humans = 8;
    for (int i = 0; i < max_log_chunk; ++i)
        big.frames.frames.push_back({0xff, 0, std::vector<uint16_t>(8, uint16_t(i))});
    {
        std::lock_guard lock(mu);
        const auto bytes = encode(big);
        check(!bytes.empty(), "largest message encodes");
        accepted->send(encode(Ping{5}));
        accepted->send(bytes);
    }
    std::optional<std::vector<uint8_t>> reply;
    check(wait_for([&] { return (reply = client->poll()).has_value(); }), "small message reaches the client");
    check(reply && decode(*reply) == Message(Ping{5}), "server messages keep their order");
    check(wait_for([&] { return (reply = client->poll()).has_value(); }),
          "largest message reaches the client");
    check(reply && decode(*reply) == Message(big), "largest message intact");
    client->close();
    check(wait_for([&] {
              std::lock_guard lock(mu);
              return accepted->status() == ChannelStatus::closed;
          }),
          "close reaches the server");
    {
        std::lock_guard lock(mu);
        accepted.reset();
    }
    server.stop();
    auto bad = ws_connect("not a url");
    check(wait_for([&] { return bad->status() == ChannelStatus::failed; }), "malformed URL fails");
    return check.finish("net_ws");
}
