// Plan 34 Task 5: rr-netd, the online mano-a-mano server. One event loop
// thread drives rr::netd::Server; libdatachannel threads only queue new
// connections and messages. Configuration comes from the environment:
//   RR_NETD_PORT        WebSocket port (default 8080; Nginx proxies /net to it)
//   RR_NETD_HTTP_PORT   plain HTTP port for GET /net/health (default 8081)
//   RR_NETD_BIND        bind address (default 0.0.0.0)
//   RR_NETD_LOG_DIR     race logs, kept 7 days (default ./race-logs)
//   RR_NETD_MAX_ROOMS   room cap (default 500)
//   RR_NETD_MIN_HUMANS  humans needed to start (default 2)
#include "room.hpp"

#include "net/ws_channel.hpp"
#include "net/rtc_channel.hpp"

#include <rtc/websocketserver.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#ifndef RR_GIT_REVISION
#define RR_GIT_REVISION "unknown"
#endif

namespace {

std::atomic<bool> stop{false};

int env_int(const char* name, int fallback) {
    const char* v = std::getenv(name);
    return v && *v ? std::atoi(v) : fallback;
}
std::string env_str(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return v && *v ? v : fallback;
}

uint64_t wall_ms() {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

void log_line(const std::string& json) {
    std::cout << "{\"t\":" << wall_ms() << ",\"svc\":\"rr-netd\"," << json.substr(1) << '\n' << std::flush;
}

// Minimal non-blocking HTTP/1.0 responder: GET /net/health and /health only.
class Health {
  public:
    Health(const std::string& bind, int port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return;
        int one = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(uint16_t(port));
        ::inet_pton(AF_INET, bind.c_str(), &a.sin_addr);
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd_, 16) != 0) {
            ::close(fd_);
            fd_ = -1;
            return;
        }
        ::fcntl(fd_, F_SETFL, O_NONBLOCK);
    }
    ~Health() {
        if (fd_ >= 0) ::close(fd_);
    }
    bool ok() const { return fd_ >= 0; }
    void serve(const std::string& body) {
        for (int n = 0; n < 8; ++n) {
            const int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) return;
            timeval tv{0, 200000};
            ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            char buf[512];
            const ssize_t got = ::recv(c, buf, sizeof buf - 1, 0);
            std::string req(buf, size_t(got > 0 ? got : 0));
            const bool health = req.rfind("GET /net/health", 0) == 0 || req.rfind("GET /health", 0) == 0;
            const std::string payload = health ? body : "{\"error\":\"not found\"}";
            const std::string resp =
                std::string("HTTP/1.0 ") + (health ? "200 OK" : "404 Not Found") +
                "\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nContent-Length: " +
                std::to_string(payload.size()) + "\r\n\r\n" + payload;
            (void)::send(c, resp.data(), resp.size(), 0);
            ::close(c);
        }
    }

  private:
    int fd_ = -1;
};

void write_record(const std::filesystem::path& dir, const rr::netd::RaceRecord& r) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto path =
        dir / ("race-" + std::to_string(wall_ms()) + "-" + std::to_string(r.start.race_id) + ".rrlog");
    std::ofstream out(path, std::ios::binary);
    // Format: protocol-encoded Start, then LogChunks of the sealed frames,
    // then Hash messages for every reported tick/slot and the Official table;
    // each message prefixed by its u32 little-endian length.
    auto put = [&](const rr::net::Message& m) {
        const std::vector<uint8_t> b = rr::net::encode(m);
        const uint32_t n = uint32_t(b.size());
        const uint8_t len[4] = {uint8_t(n), uint8_t(n >> 8), uint8_t(n >> 16), uint8_t(n >> 24)};
        out.write(reinterpret_cast<const char*>(len), 4);
        out.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    };
    put(r.start);
    for (size_t first = 0; first < r.frames.size(); first += rr::net::max_log_chunk) {
        rr::net::LogChunk c;
        c.race_id = r.start.race_id;
        c.frames.first_tick = int32_t(first);
        c.frames.humans = uint8_t(r.start.humans.size());
        const size_t end = std::min(r.frames.size(), first + rr::net::max_log_chunk);
        c.frames.frames.assign(r.frames.begin() + std::ptrdiff_t(first),
                               r.frames.begin() + std::ptrdiff_t(end));
        c.last = end == r.frames.size();
        put(c);
    }
    for (const auto& [tick, by_slot] : r.hashes)
        if (tick >= 0)
            for (const auto& [slot, h] : by_slot)
                if (slot >= 0) put(rr::net::Hash{tick, h});
    for (int32_t t : r.desync_ticks) put(rr::net::Desync{t});
    put(r.official);
}

void prune_logs(const std::filesystem::path& dir) {
    std::error_code ec;
    const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * 7);
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().extension() == ".rrlog" && e.last_write_time(ec) < cutoff)
            std::filesystem::remove(e.path(), ec);
}

} // namespace

int main() {
    std::signal(SIGTERM, [](int) { stop = true; });
    std::signal(SIGINT, [](int) { stop = true; });
    std::signal(SIGPIPE, SIG_IGN);

    rr::netd::ServerConfig cfg;
    cfg.max_rooms = env_int("RR_NETD_MAX_ROOMS", cfg.max_rooms);
    cfg.min_humans = env_int("RR_NETD_MIN_HUMANS", cfg.min_humans);
    cfg.seed = uint32_t(env_int("RR_NETD_SEED", 0));
    const std::string bind = env_str("RR_NETD_BIND", "0.0.0.0");
    const std::filesystem::path log_dir = env_str("RR_NETD_LOG_DIR", "race-logs");
    rr::net::RtcOptions rtc_options;
    rtc_options.enabled = env_int("RR_NETD_RTC", 0) != 0;
    rtc_options.server = true;
    rtc_options.public_ip = env_str("RR_NETD_PUBLIC_IP", "");
    const int port_begin = env_int("RR_NETD_UDP_BEGIN", 40000), port_end = env_int("RR_NETD_UDP_END", 40099);
    in_addr public_address{};
    if (port_begin < 1024 || port_end > 65535 || port_begin > port_end ||
        (!rtc_options.public_ip.empty() &&
         inet_pton(AF_INET, rtc_options.public_ip.c_str(), &public_address) != 1)) {
        std::cerr << "invalid RTC public IPv4 or UDP range\n";
        return 2;
    }
    rtc_options.port_begin = uint16_t(port_begin);
    rtc_options.port_end = uint16_t(port_end);
    rr::netd::Server server(cfg);
    server.log = log_line;
    server.on_race_finished = [&](const rr::netd::RaceRecord& r) { write_record(log_dir, r); };

    std::mutex mu;
    std::vector<std::shared_ptr<rtc::WebSocket>> incoming;
    rtc::WebSocketServer::Configuration wcfg;
    wcfg.port = uint16_t(env_int("RR_NETD_PORT", 8080));
    wcfg.bindAddress = bind;
    wcfg.maxMessageSize = rr::net::max_message_bytes;
    rtc::WebSocketServer ws(wcfg);
    ws.onClient([&](std::shared_ptr<rtc::WebSocket> client) {
        std::lock_guard lock(mu);
        incoming.push_back(std::move(client));
    });
    Health health(bind, env_int("RR_NETD_HTTP_PORT", 8081));
    log_line(std::string("{\"event\":\"start\",\"rev\":\"") + RR_GIT_REVISION + "\",\"ws_port\":" +
             std::to_string(ws.port()) + ",\"health\":" + (health.ok() ? "true" : "false") + "}");

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    uint64_t next_prune = 0;
    while (!stop) {
        const uint64_t now =
            uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count());
        {
            std::lock_guard lock(mu);
            for (auto& c : incoming)
                server.add_connection(rr::net::negotiate_rtc(rr::net::make_ws_channel(c), rtc_options), now);
            incoming.clear();
        }
        server.step(now);
        if (health.ok()) {
            std::string body = server.health_json(now);
            body.insert(body.size() - 1, std::string(",\"rev\":\"") + RR_GIT_REVISION + "\"");
            health.serve(body);
        }
        if (now >= next_prune) {
            next_prune = now + 3600 * 1000;
            prune_logs(log_dir);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    log_line("{\"event\":\"stop\"}");
    ws.stop();
    return 0;
}
