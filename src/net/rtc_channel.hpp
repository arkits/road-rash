#pragma once
#include "net/channel.hpp"
#include "net/protocol.hpp"

namespace rr::net {
struct RtcOptions {
    bool enabled = false; // opt in until loss measurements justify the default
    bool server = false;
    std::string public_ip;
    uint16_t port_begin = 1024, port_end = 65535;
};
class RtcPeer : public Channel {
  public:
    bool reliable() const final { return false; }
    virtual void offer() = 0;
    virtual void signal(const Message&) = 0;
    virtual std::optional<Message> poll_signal() = 0;
};
std::unique_ptr<Channel> ws_connect(const std::string&, RtcOptions);
std::unique_ptr<RtcPeer> make_rtc_peer(const RtcOptions&);
// Keeps negotiation and race packets on one connection across lobby/race handovers.
// Controls, catch-up and result messages always retain the WebSocket.
std::unique_ptr<Channel> negotiate_rtc(std::unique_ptr<Channel>, RtcOptions, Clock clock = {});
} // namespace rr::net
