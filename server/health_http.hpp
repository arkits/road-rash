// rr-netd's GET /net/health responder. It is polled from the race event loop,
// so it never blocks: sockets are non-blocking, a request may arrive over
// several polls, and a client that stalls is closed at its deadline.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rr::netd {

class HealthHttp {
  public:
    static constexpr int max_pending = 32;           // open request sockets
    static constexpr uint64_t request_timeout_ms = 2000;
    static constexpr size_t max_request_bytes = 2048;

    // port 0 picks a free port (tests); see port().
    HealthHttp(const std::string& bind, int port);
    ~HealthHttp();
    HealthHttp(const HealthHttp&) = delete;
    HealthHttp& operator=(const HealthHttp&) = delete;

    bool ok() const { return fd_ >= 0; }
    int port() const { return port_; }
    int pending() const { return int(clients_.size()); }
    // Accepts, reads and writes whatever is ready without waiting. `body` is
    // called only when a health request needs an answer.
    void poll(uint64_t now_ms, const std::function<std::string()>& body);

  private:
    struct Client {
        int fd = -1;
        uint64_t deadline_ms = 0;
        std::string in, out;
        size_t sent = 0;
        bool answered = false;
    };
    // Returns false when the client is finished and must be closed.
    bool step(Client&, const std::function<std::string()>& body);

    int fd_ = -1;
    int port_ = 0;
    std::vector<Client> clients_;
};

} // namespace rr::netd
