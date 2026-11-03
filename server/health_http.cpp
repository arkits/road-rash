#include "health_http.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rr::netd {

namespace {

#ifdef MSG_NOSIGNAL
constexpr int send_flags = MSG_NOSIGNAL;
#else
constexpr int send_flags = 0;
#endif

void set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }

std::string response(bool found, const std::string& payload) {
    return std::string("HTTP/1.0 ") + (found ? "200 OK" : "404 Not Found") +
           "\r\nContent-Type: application/json\r\nCache-Control: no-store\r\nContent-Length: " +
           std::to_string(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload;
}

} // namespace

HealthHttp::HealthHttp(const std::string& bind, int port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return;
    int one = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    socklen_t len = sizeof a;
    if (::inet_pton(AF_INET, bind.c_str(), &a.sin_addr) != 1 ||
        ::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd_, 16) != 0 ||
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &len) != 0) {
        ::close(fd_);
        fd_ = -1;
        return;
    }
    port_ = ntohs(a.sin_port);
    set_nonblocking(fd_);
}

HealthHttp::~HealthHttp() {
    for (Client& c : clients_) ::close(c.fd);
    if (fd_ >= 0) ::close(fd_);
}

void HealthHttp::poll(uint64_t now_ms, const std::function<std::string()>& body) {
    if (fd_ < 0) return;
    for (int n = 0; n < 8; ++n) {
        const int c = ::accept(fd_, nullptr, nullptr);
        if (c < 0) break;
        if (int(clients_.size()) >= max_pending) { // shed new load; existing requests keep their slots
            ::close(c);
            continue;
        }
        set_nonblocking(c);
        clients_.push_back(Client{c, now_ms + request_timeout_ms});
    }
    for (size_t i = 0; i < clients_.size();) {
        Client& c = clients_[i];
        if (step(c, body) && now_ms < c.deadline_ms) {
            ++i;
            continue;
        }
        ::close(c.fd);
        clients_[i] = std::move(clients_.back());
        clients_.pop_back();
    }
}

bool HealthHttp::step(Client& c, const std::function<std::string()>& body) {
    if (!c.answered) {
        char buf[512];
        for (;;) {
            const ssize_t got = ::recv(c.fd, buf, sizeof buf, 0);
            if (got > 0) {
                c.in.append(buf, size_t(got));
                if (c.in.size() >= max_request_bytes) break;
                continue;
            }
            if (got == 0) break; // peer finished sending; answer what we have
            if (would_block()) {
                // Only the request line matters; wait for the end of the headers.
                if (c.in.find("\r\n\r\n") == std::string::npos && c.in.find("\n\n") == std::string::npos)
                    return true;
                break;
            }
            return false;
        }
        if (c.in.empty()) return false;
        const bool health = c.in.rfind("GET /net/health", 0) == 0 || c.in.rfind("GET /health", 0) == 0;
        c.out = response(health, health ? body() : "{\"error\":\"not found\"}");
        c.answered = true;
    }
    while (c.sent < c.out.size()) {
        const ssize_t put = ::send(c.fd, c.out.data() + c.sent, c.out.size() - c.sent, send_flags);
        if (put > 0) {
            c.sent += size_t(put);
            continue;
        }
        return put < 0 && would_block();
    }
    return false; // fully sent
}

} // namespace rr::netd
