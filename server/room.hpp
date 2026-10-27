// Plan 34 Task 5: rr-netd lobby, rooms and races as one pure class. It owns
// the client channels, is driven by `step(now_ms)` from a single event loop,
// holds no game data and runs no simulation. Tests drive it with loopback
// channels and a virtual clock.
#pragma once
#include "net/channel.hpp"
#include "net/protocol.hpp"
#include "sealer.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace rr::netd {

struct ServerConfig {
    int max_rooms = 500;
    int max_connections = 4000;
    int min_humans = 2;                // humans needed to start a race
    uint64_t hello_timeout_ms = 10000; // connection must say Hello
    uint64_t idle_timeout_ms = 30000;  // no message at all
    uint64_t lobby_timeout_ms = 30 * 60 * 1000;
    uint64_t reconnect_window_ms = 60000;
    uint64_t load_ms = 3000;  // Start -> race tick 0
    int seal_delay_ticks = 3; // a tick is sealed this many ticks after its time
    int frames_per_packet = 32;
    double rate_per_s = 240; // token bucket refill, messages per second
    double rate_burst = 480;
    int rate_strikes = 3;               // throttled seconds before the connection is closed
    uint64_t finish_timeout_ms = 60000; // results wait for missing reports
    // Messages every other seat sees (chat, pick, vote, ready, create/join):
    // a separate, tighter bucket so one connection cannot flood the others'
    // bounded event queues (net::Client fails at 128 queued events).
    double fanout_per_s = 4;
    double fanout_burst = 8;
    int max_spectators = 16; // per room
    int hash_every = 60;     // Hash cadence; matches net::Rollback's default
    uint64_t seed = 0;                  // 0: seed from std::random_device
};

struct Counters {
    uint64_t connections = 0, rooms_created = 0, races_started = 0, races_finished = 0;
    uint64_t malformed = 0, rate_limited = 0, rejected = 0, desyncs = 0, late_inputs = 0;
    uint64_t reconnects = 0, wrong_seat_inputs = 0;
    uint64_t invalid = 0;        // well-formed but impossible here (tick range, cadence, direction)
    uint64_t fanout_limited = 0; // fan-out messages dropped by their own bucket
};

// A finished race's complete record (Start + sealed timeline + hashes), for
// the 7-day race log, reconnect and desync investigation.
struct RaceRecord {
    net::Start start;
    std::vector<net::SealedFrame> frames;
    std::map<int32_t, std::map<int, uint64_t>> hashes; // tick -> seat -> hash
    std::vector<int32_t> desync_ticks;
    net::Official official;
};

class Server {
  public:
    using ConnId = uint32_t;
    explicit Server(ServerConfig = {});
    ~Server();

    ConnId add_connection(std::unique_ptr<net::Channel>, uint64_t now_ms);
    // Moves race traffic of a connection to an unreliable channel (WebRTC
    // DataChannel, Task 9). Lobby and control stay on the reliable one.
    void attach_unreliable(ConnId, std::unique_ptr<net::Channel>);
    void step(uint64_t now_ms);

    const Counters& counters() const { return counters_; }
    int room_count() const { return int(rooms_.size()); }
    int connection_count() const { return int(conns_.size()); }
    int player_count() const;
    std::string health_json(uint64_t now_ms) const;
    // Called with every finished race (the server's log sink).
    std::function<void(const RaceRecord&)> on_race_finished;
    std::function<void(const std::string&)> log; // structured log lines (JSON)

  private:
    struct RateBucket {
        double tokens = 0;
        uint64_t last_ms = 0;
        int strikes = 0;
        int dropped_in_window = 0;
        uint64_t strike_window_ms = 0;
    };
    struct Conn {
        ConnId id = 0;
        std::unique_ptr<net::Channel> reliable;
        std::unique_ptr<net::Channel> unreliable;
        uint64_t connected_ms = 0, last_rx_ms = 0;
        bool hello = false;
        net::Hello info;
        std::string room; // code, empty when not in a room
        int seat = -1;    // -1 spectator / none
        RateBucket bucket;
        RateBucket fanout; // tokens/last_ms only
        int32_t acked = -1; // highest contiguous sealed tick the client holds
        uint64_t next_rtc_send_ms = 0;
        int32_t sent_upto = -1; // reliable stream: highest tick already sent
        uint16_t rtt_ms = 0;
        uint64_t next_ping_ms = 0; // server Ping (clients answer Pong) for the roster ping column
        bool closing = false;
    };
    struct Seat {
        bool occupied = false;
        ConnId conn = 0; // 0 when disconnected
        net::SeatToken token{};
        std::string alias;
        uint8_t character = 0, vote = 0;
        bool ready = false;
        uint64_t disconnected_ms = 0;
        net::Outcome outcome = net::Outcome::racing;
        std::optional<net::Result> result;
    };
    struct Race {
        net::Start start;
        uint64_t tick0_ms = 0;
        std::unique_ptr<Sealer> sealer;
        std::vector<int> seat_of_slot; // race slot -> room seat
        RaceRecord record;
        uint64_t first_result_ms = 0;
        bool finished = false;
    };
    struct Room {
        std::string code;
        net::RoomOptions options;
        std::array<Seat, net::max_seats> seats{};
        int host = 0;
        uint64_t sim_fingerprint = 0;
        net::DataDigest digest{};
        uint64_t created_ms = 0, lobby_since_ms = 0;
        std::optional<Race> race;
        std::vector<ConnId> spectators;
        uint32_t next_race_id = 1;
    };

    void handle(Conn&, const net::Message&, uint64_t now);
    void on_hello(Conn&, const net::Hello&, uint64_t now);
    void on_create(Conn&, const net::RoomCreate&, uint64_t now);
    void on_join(Conn&, const net::RoomJoin&, uint64_t now);
    void on_leave(Conn&, uint64_t now);
    void on_chat(Conn&, const net::Chat&);
    void on_input(Conn&, const net::Input&);
    void on_hash(Conn&, const net::Hash&);
    void on_result(Conn&, const net::Result&, uint64_t now);
    void maybe_start(Room&, uint64_t now);
    void step_race(Room&, uint64_t now);
    void finish_race(Room&, uint64_t now);
    void send_catch_up(Conn&, Room&);
    void broadcast_state(Room&);
    void send(Conn&, const net::Message&);
    void send_race(Conn&, const net::Message&); // unreliable when available
    void reject(Conn&, net::RejectReason);
    void vacate(Room&, int seat, uint64_t now);
    void disconnect_seat(Room&, int seat, uint64_t now);
    bool allow(Conn&, uint64_t now);
    bool allow_fanout(Conn&, uint64_t now);
    std::string new_code();
    net::SeatToken new_token();
    Room* room_of(const Conn&);
    int race_slot(const Room&, int seat) const;
    void emit(const std::string& event, const std::string& fields);

    ServerConfig cfg_;
    std::mt19937_64 rng_;
    std::map<ConnId, Conn> conns_;
    std::map<std::string, Room> rooms_;
    ConnId next_id_ = 1;
    Counters counters_;
};

} // namespace rr::netd
