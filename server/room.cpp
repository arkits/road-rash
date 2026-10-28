// Plan 34 Task 5: rr-netd rooms, lobby flow, race clock and result table.
#include "room.hpp"

#include <algorithm>
#include <sstream>

namespace rr::netd {
namespace {

constexpr int32_t ticks_at(uint64_t ms) {
    return int32_t(ms * 60 / 1000);
}

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o.push_back('\\');
        if (uint8_t(c) < 0x20) continue;
        o.push_back(c);
    }
    return o;
}

} // namespace

Server::Server(ServerConfig cfg) : cfg_(cfg), rng_(cfg.seed ? cfg.seed : std::random_device{}()) {}
Server::~Server() = default;

Server::ConnId Server::add_connection(std::unique_ptr<net::Channel> ch, uint64_t now) {
    const ConnId id = next_id_++;
    Conn& c = conns_[id];
    c.id = id;
    c.reliable = std::move(ch);
    c.connected_ms = c.last_rx_ms = now;
    c.bucket.tokens = cfg_.rate_burst;
    c.bucket.last_ms = now;
    c.fanout.tokens = cfg_.fanout_burst;
    c.fanout.last_ms = now;
    ++counters_.connections;
    if (int(conns_.size()) > cfg_.max_connections) reject(c, net::RejectReason::server_full);
    return id;
}

void Server::attach_unreliable(ConnId id, std::unique_ptr<net::Channel> ch) {
    if (auto it = conns_.find(id); it != conns_.end()) it->second.unreliable = std::move(ch);
}

int Server::player_count() const {
    int n = 0;
    for (const auto& [code, room] : rooms_)
        for (const Seat& s : room.seats) n += s.occupied && s.conn != 0;
    return n;
}

void Server::emit(const std::string& event, const std::string& fields) {
    if (log) log("{\"event\":\"" + event + "\"" + (fields.empty() ? "" : "," + fields) + "}");
}

std::string Server::health_json(uint64_t) const {
    size_t rtc_open = 0;
    for (const auto& [id, c] : conns_) rtc_open += c.reliable->transport_stats().rtc_open;
    std::ostringstream o;
    o << "{\"rtc_connections\":" << rtc_open << ",\"ok\":true,\"rooms\":" << rooms_.size()
      << ",\"connections\":" << conns_.size() << ",\"players\":" << player_count()
      << ",\"races_started\":" << counters_.races_started
      << ",\"races_finished\":" << counters_.races_finished << ",\"late_inputs\":" << counters_.late_inputs
      << ",\"desyncs\":" << counters_.desyncs << ",\"malformed\":" << counters_.malformed
      << ",\"rate_limited\":" << counters_.rate_limited << ",\"reconnects\":" << counters_.reconnects
      << ",\"invalid\":" << counters_.invalid << ",\"fanout_limited\":" << counters_.fanout_limited
      << ",\"wrong_seat_inputs\":" << counters_.wrong_seat_inputs;
    // Public rooms for the web launcher, which does not speak the binary
    // protocol. Same entries a RoomList gets, without the build filter, so
    // nothing here is more than any connected client can already list.
    o << ",\"public_rooms\":[";
    int listed = 0;
    for (const auto& [code, r] : rooms_) {
        if (!r.options.is_public || listed >= net::max_room_list) continue;
        static constexpr const char* status[] = {"lobby", "full", "racing"};
        const net::RoomListEntry e = list_entry(code, r);
        o << (listed++ ? "," : "") << "{\"code\":\"" << e.code << "\",\"host\":\"" << json_escape(e.host)
          << "\",\"level\":" << int(e.level) << ",\"humans\":" << int(e.humans)
          << ",\"seats\":" << net::max_seats << ",\"status\":\"" << status[int(e.status)] << "\"}";
    }
    o << "]}";
    return o.str();
}

net::RoomListEntry Server::list_entry(const std::string& code, const Room& r) const {
    net::RoomListEntry e;
    e.code = code;
    e.host = r.seats[size_t(r.host)].alias;
    e.level = r.options.level;
    e.humans = uint8_t(std::count_if(r.seats.begin(), r.seats.end(), [](const Seat& s) { return s.occupied; }));
    e.status = r.race ? net::RoomStatus::racing
                      : (e.humans >= net::max_seats ? net::RoomStatus::full : net::RoomStatus::lobby);
    return e;
}

std::string Server::new_code() {
    for (;;) {
        std::string code;
        for (size_t i = 0; i < net::room_code_length; ++i)
            code.push_back(net::room_code_alphabet[size_t(rng_() % net::room_code_alphabet.size())]);
        if (!rooms_.count(code)) return code;
    }
}

net::SeatToken Server::new_token() {
    net::SeatToken t{};
    for (size_t i = 0; i < t.size(); i += 8) {
        const uint64_t v = rng_();
        for (size_t j = 0; j < 8; ++j) t[i + j] = uint8_t(v >> (8 * j));
    }
    return t;
}

Server::Room* Server::room_of(const Conn& c) {
    if (c.room.empty()) return nullptr;
    auto it = rooms_.find(c.room);
    return it == rooms_.end() ? nullptr : &it->second;
}

int Server::race_slot(const Room& r, int seat) const {
    if (!r.race || seat < 0) return -1;
    const auto& map = r.race->seat_of_slot;
    const auto it = std::find(map.begin(), map.end(), seat);
    return it == map.end() ? -1 : int(it - map.begin());
}

void Server::send(Conn& c, const net::Message& m) {
    if (c.closing || !c.reliable) return;
    const std::vector<uint8_t> b = net::encode(m);
    if (!b.empty()) c.reliable->send(b);
}

void Server::send_race(Conn& c, const net::Message& m) {
    if (c.unreliable && c.unreliable->status() == net::ChannelStatus::open) {
        const std::vector<uint8_t> b = net::encode(m);
        if (!b.empty()) c.unreliable->send(b);
    } else {
        send(c, m);
    }
}

void Server::reject(Conn& c, net::RejectReason why) {
    send(c, net::Reject{why});
    ++counters_.rejected;
    if (why == net::RejectReason::protocol || why == net::RejectReason::rate_limited ||
        why == net::RejectReason::server_full || why == net::RejectReason::timeout ||
        why == net::RejectReason::kicked)
        c.closing = true;
}

bool Server::allow(Conn& c, uint64_t now) {
    RateBucket& b = c.bucket;
    b.tokens = std::min(cfg_.rate_burst, b.tokens + double(now - b.last_ms) * cfg_.rate_per_s / 1000.0);
    b.last_ms = now;
    if (b.tokens >= 1.0) {
        b.tokens -= 1.0;
        return true;
    }
    ++counters_.rate_limited;
    if (now >= b.strike_window_ms) { // one strike per throttled second
        b.strike_window_ms = now + 1000;
        b.dropped_in_window = 0;
        if (++b.strikes > cfg_.rate_strikes) reject(c, net::RejectReason::rate_limited);
    }
    // A flood (a full burst dropped within one second) closes at once.
    if (++b.dropped_in_window > int(cfg_.rate_burst)) reject(c, net::RejectReason::rate_limited);
    return false;
}

bool Server::allow_fanout(Conn& c, uint64_t now) {
    RateBucket& b = c.fanout;
    b.tokens = std::min(cfg_.fanout_burst, b.tokens + double(now - b.last_ms) * cfg_.fanout_per_s / 1000.0);
    b.last_ms = now;
    if (b.tokens >= 1.0) {
        b.tokens -= 1.0;
        return true;
    }
    ++counters_.fanout_limited;
    return false;
}

void Server::step(uint64_t now) {
    for (auto& [id, c] : conns_) {
        net::Channel* channels[2] = {c.reliable.get(), c.unreliable.get()};
        for (net::Channel* ch : channels) {
            if (!ch) continue;
            for (int n = 0; n < 512 && !c.closing; ++n) { // bounded work per step
                auto payload = ch->poll();
                if (!payload) break;
                c.last_rx_ms = now;
                if (!allow(c, now)) continue;
                if (const auto v = net::peek_hello_protocol(*payload); v && *v != net::protocol_version) {
                    reject(c, net::RejectReason::protocol);
                    break;
                }
                const auto m = net::decode(*payload);
                if (!m) {
                    ++counters_.malformed;
                    continue;
                }
                // Race traffic only is accepted on the unreliable channel.
                if (ch == c.unreliable.get() && !std::holds_alternative<net::Input>(*m) &&
                    !std::holds_alternative<net::Ack>(*m) && !std::holds_alternative<net::Ping>(*m) &&
                    !std::holds_alternative<net::Pong>(*m)) {
                    ++counters_.malformed;
                    continue;
                }
                handle(c, *m, now);
            }
        }
        if (c.hello && now >= c.next_ping_ms) {
            c.next_ping_ms = now + 2000;
            send(c, net::Ping{uint32_t(now)});
        }
        if (!c.hello && now - c.connected_ms > cfg_.hello_timeout_ms) reject(c, net::RejectReason::timeout);
        if (now - c.last_rx_ms > cfg_.idle_timeout_ms) reject(c, net::RejectReason::timeout);
        const auto st = c.reliable ? c.reliable->status() : net::ChannelStatus::closed;
        if (st == net::ChannelStatus::closed || st == net::ChannelStatus::failed) c.closing = true;
    }
    // Drop closed connections; their seats wait for a reconnect.
    for (auto it = conns_.begin(); it != conns_.end();) {
        Conn& c = it->second;
        if (!c.closing) {
            ++it;
            continue;
        }
        if (Room* r = room_of(c)) {
            if (c.seat >= 0 && r->seats[size_t(c.seat)].conn == c.id) disconnect_seat(*r, c.seat, now);
            std::erase(r->spectators, c.id);
        }
        if (c.reliable) c.reliable->close();
        if (c.unreliable) c.unreliable->close();
        emit("disconnect", "\"conn\":" + std::to_string(c.id));
        it = conns_.erase(it);
    }
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        Room& r = it->second;
        for (int s = 0; s < net::max_seats; ++s) {
            Seat& seat = r.seats[size_t(s)];
            if (seat.occupied && seat.conn == 0 && !r.race &&
                now - seat.disconnected_ms > cfg_.reconnect_window_ms)
                vacate(r, s, now);
        }
        if (r.race) step_race(r, now);
        const bool empty =
            std::none_of(r.seats.begin(), r.seats.end(), [](const Seat& s) { return s.occupied; });
        if (empty && r.spectators.empty()) {
            emit("room_closed", "\"room\":\"" + r.code + "\"");
            it = rooms_.erase(it);
            continue;
        }
        if (!r.race && now - r.lobby_since_ms > cfg_.lobby_timeout_ms) {
            for (Seat& s : r.seats)
                if (s.conn)
                    if (auto c = conns_.find(s.conn); c != conns_.end())
                        reject(c->second, net::RejectReason::timeout);
            emit("room_timeout", "\"room\":\"" + r.code + "\"");
            for (auto& [id, c] : conns_)
                if (c.room == r.code) c.room.clear();
            it = rooms_.erase(it);
            continue;
        }
        ++it;
    }
}

void Server::handle(Conn& c, const net::Message& m, uint64_t now) {
    if (const auto* h = std::get_if<net::Hello>(&m)) return on_hello(c, *h, now);
    if (!c.hello) {
        reject(c, net::RejectReason::bad_request);
        c.closing = true;
        return;
    }
    // Server-to-client types are never valid from a client.
    if (std::holds_alternative<net::Welcome>(m) || std::holds_alternative<net::Reject>(m) ||
        std::holds_alternative<net::RoomListReply>(m) || std::holds_alternative<net::RoomState>(m) ||
        std::holds_alternative<net::ChatRelay>(m) || std::holds_alternative<net::Start>(m) ||
        std::holds_alternative<net::Frames>(m) || std::holds_alternative<net::Desync>(m) ||
        std::holds_alternative<net::Official>(m) || std::holds_alternative<net::LogChunk>(m)) {
        ++counters_.invalid;
        return;
    }
    if ((std::holds_alternative<net::Chat>(m) || std::holds_alternative<net::PickCharacter>(m) ||
         std::holds_alternative<net::VoteCourse>(m) || std::holds_alternative<net::Ready>(m) ||
         std::holds_alternative<net::RoomCreate>(m) || std::holds_alternative<net::RoomJoin>(m)) &&
        !allow_fanout(c, now))
        return;
    if (const auto* p = std::get_if<net::Ping>(&m)) {
        Room* r = room_of(c);
        const int32_t tick = r && r->race ? int32_t(ticks_at(now) - ticks_at(r->race->tick0_ms)) : 0;
        send_race(c, net::Pong{p->t, tick, uint32_t(now)});
        return;
    }
    if (const auto* p = std::get_if<net::Pong>(&m)) { // reply to the server's own Ping
        c.rtt_ms = uint16_t(std::min<uint64_t>(uint32_t(now) - p->t, 65535));
        return;
    }
    if (const auto* x = std::get_if<net::RoomCreate>(&m)) return on_create(c, *x, now);
    if (const auto* x = std::get_if<net::RoomJoin>(&m)) return on_join(c, *x, now);
    if (std::holds_alternative<net::RoomList>(m)) {
        net::RoomListReply reply;
        for (const auto& [code, r] : rooms_) {
            if (!r.options.is_public || int(reply.rooms.size()) >= net::max_room_list) continue;
            if (r.sim_fingerprint != c.info.sim_fingerprint || r.digest != c.info.data_digest) continue;
            reply.rooms.push_back(list_entry(code, r));
        }
        send(c, reply);
        return;
    }
    if (std::holds_alternative<net::RoomLeave>(m)) return on_leave(c, now);
    Room* r = room_of(c);
    if (!r) {
        if (std::holds_alternative<net::Input>(m)) ++counters_.wrong_seat_inputs;
        else if (std::holds_alternative<net::Hash>(m) || std::holds_alternative<net::Result>(m)) ++counters_.invalid;
        return;
    }
    if (const auto* x = std::get_if<net::Chat>(&m)) return on_chat(c, *x);
    if (const auto* x = std::get_if<net::Input>(&m)) return on_input(c, *x);
    if (const auto* x = std::get_if<net::Ack>(&m)) {
        if (r->race) c.acked = std::clamp(x->sealed_tick, c.acked, r->race->sealer->sealed_tick());
        return;
    }
    if (const auto* x = std::get_if<net::Hash>(&m)) return on_hash(c, *x);
    if (const auto* x = std::get_if<net::Result>(&m)) return on_result(c, *x, now);
    if (c.seat < 0 || r->race) return; // lobby-only actions below need a seat
    Seat& seat = r->seats[size_t(c.seat)];
    if (const auto* x = std::get_if<net::PickCharacter>(&m)) {
        seat.character = x->character;
    } else if (const auto* x = std::get_if<net::VoteCourse>(&m)) {
        seat.vote = x->course;
    } else if (const auto* x = std::get_if<net::Ready>(&m)) {
        seat.ready = x->ready;
    } else {
        return; // server-to-client message types are ignored
    }
    broadcast_state(*r);
    maybe_start(*r, now);
}

void Server::on_hello(Conn& c, const net::Hello& h, uint64_t now) {
    if (c.hello) {
        reject(c, net::RejectReason::bad_request);
        return;
    }
    c.hello = true;
    c.info = h;
    emit("hello", "\"conn\":" + std::to_string(c.id) + ",\"platform\":" + std::to_string(int(h.platform)) +
                      ",\"alias\":\"" + json_escape(h.alias) + "\"");
    if (h.seat_token) {
        for (auto& [code, r] : rooms_) {
            for (int s = 0; s < net::max_seats; ++s) {
                Seat& seat = r.seats[size_t(s)];
                if (!seat.occupied || seat.token != *h.seat_token || seat.outcome == net::Outcome::left)
                    continue;
                if (r.sim_fingerprint != h.sim_fingerprint || r.digest != h.data_digest) continue;
                if (seat.conn)
                    if (auto old = conns_.find(seat.conn); old != conns_.end()) {
                        old->second.room.clear(); // the new connection takes over
                        old->second.seat = -1;
                        reject(old->second, net::RejectReason::kicked);
                    }
                seat.conn = c.id;
                seat.alias = h.alias;
                c.room = code;
                c.seat = s;
                ++counters_.reconnects;
                send(c, net::Welcome{c.id, now, code, seat.token});
                broadcast_state(r);
                if (r.race) {
                    net::Start st = r.race->start;
                    st.your_seat = uint8_t(race_slot(r, s));
                    send(c, st);
                    c.acked = -1;
                    send_catch_up(c, r);
                }
                emit("reconnect", "\"room\":\"" + code + "\",\"seat\":" + std::to_string(s));
                return;
            }
        }
    }
    send(c, net::Welcome{c.id, now, {}, {}});
}

void Server::on_create(Conn& c, const net::RoomCreate& m, uint64_t now) {
    if (!c.room.empty()) return reject(c, net::RejectReason::bad_request);
    if (int(rooms_.size()) >= cfg_.max_rooms) return reject(c, net::RejectReason::server_full);
    Room r;
    r.code = new_code();
    r.options = m.options;
    r.sim_fingerprint = c.info.sim_fingerprint;
    r.digest = c.info.data_digest;
    r.created_ms = r.lobby_since_ms = now;
    Seat& s = r.seats[0];
    s.occupied = true;
    s.conn = c.id;
    s.token = new_token();
    s.alias = c.info.alias;
    c.room = r.code;
    c.seat = 0;
    const std::string code = r.code;
    Room& stored = rooms_.emplace(code, std::move(r)).first->second;
    ++counters_.rooms_created;
    send(c, net::Welcome{c.id, now, code, stored.seats[0].token});
    broadcast_state(stored);
    emit("room_created", "\"room\":\"" + code + "\",\"public\":" + (m.options.is_public ? "true" : "false"));
}

void Server::on_join(Conn& c, const net::RoomJoin& m, uint64_t now) {
    if (!c.room.empty()) return reject(c, net::RejectReason::bad_request);
    auto it = rooms_.find(m.code);
    if (it == rooms_.end()) return reject(c, net::RejectReason::no_such_room);
    Room& r = it->second;
    if (r.sim_fingerprint != c.info.sim_fingerprint) return reject(c, net::RejectReason::fingerprint);
    if (r.digest != c.info.data_digest) return reject(c, net::RejectReason::data_digest);
    if (m.spectate) {
        if (int(r.spectators.size()) >= cfg_.max_spectators) return reject(c, net::RejectReason::room_full);
        c.room = r.code;
        c.seat = -1;
        r.spectators.push_back(c.id);
        send(c, net::Welcome{c.id, now, r.code, {}});
        broadcast_state(r);
        if (r.race) {
            net::Start st = r.race->start;
            st.your_seat = 0xff;
            send(c, st);
            send_catch_up(c, r);
        }
        return;
    }
    if (r.race) return reject(c, net::RejectReason::room_racing);
    int free = -1;
    for (int s = 0; s < net::max_seats && free < 0; ++s)
        if (!r.seats[size_t(s)].occupied) free = s;
    if (free < 0) return reject(c, net::RejectReason::room_full);
    Seat& s = r.seats[size_t(free)];
    s = Seat{};
    s.occupied = true;
    s.conn = c.id;
    s.token = new_token();
    s.alias = c.info.alias;
    c.room = r.code;
    c.seat = free;
    send(c, net::Welcome{c.id, now, r.code, s.token});
    broadcast_state(r);
    emit("join", "\"room\":\"" + r.code + "\",\"seat\":" + std::to_string(free));
}

void Server::on_leave(Conn& c, uint64_t now) {
    Room* r = room_of(c);
    if (!r) return;
    std::erase(r->spectators, c.id);
    if (c.seat >= 0) {
        Seat& seat = r->seats[size_t(c.seat)];
        if (r->race) {
            disconnect_seat(*r, c.seat, now);
            seat.outcome = net::Outcome::left; // a deliberate leave cannot reconnect
        } else {
            vacate(*r, c.seat, now);
        }
    }
    c.room.clear();
    c.seat = -1;
    if (r->race || std::any_of(r->seats.begin(), r->seats.end(), [](const Seat& s) { return s.occupied; }))
        broadcast_state(*r);
}

void Server::vacate(Room& r, int s, uint64_t) {
    Seat& seat = r.seats[size_t(s)];
    seat = Seat{};
    if (r.host == s) { // next occupied seat becomes host
        for (int k = 1; k < net::max_seats; ++k) {
            const int n = (s + k) % net::max_seats;
            if (r.seats[size_t(n)].occupied) {
                r.host = n;
                break;
            }
        }
    }
    broadcast_state(r);
}

void Server::disconnect_seat(Room& r, int s, uint64_t now) {
    Seat& seat = r.seats[size_t(s)];
    seat.conn = 0;
    seat.disconnected_ms = now;
    if (r.race) {
        const int slot = race_slot(r, s);
        if (slot >= 0) r.race->sealer->set_human(slot, false); // AI-driven from the next sealed tick
    }
    broadcast_state(r);
}

void Server::on_chat(Conn& c, const net::Chat& m) {
    Room* r = room_of(c);
    if (!r || c.seat < 0) return;
    net::ChatRelay relay{uint8_t(c.seat), r->race ? r->race->sealer->sealed_tick() : -1, m.text};
    for (const Seat& s : r->seats)
        if (s.occupied && s.conn)
            if (auto it = conns_.find(s.conn); it != conns_.end()) send(it->second, relay);
    for (ConnId id : r->spectators)
        if (auto it = conns_.find(id); it != conns_.end()) send(it->second, relay);
}

void Server::on_input(Conn& c, const net::Input& m) {
    Room* r = room_of(c);
    const int slot = r ? race_slot(*r, c.seat) : -1;
    if (slot < 0) {
        ++counters_.wrong_seat_inputs;
        return;
    }
    r->race->sealer->input(slot, m.first_tick, m.words);
}

void Server::on_hash(Conn& c, const net::Hash& m) {
    Room* r = room_of(c);
    const int slot = r ? race_slot(*r, c.seat) : -1;
    if (slot < 0) return;
    // Only the client's check ticks, already sealed and not yet decided: a
    // replayed or off-cadence hash cannot grow the record or re-trigger a
    // desync (each of which sends the whole log).
    auto& hashes = r->race->record.hashes;
    if (m.tick < 0 || m.tick > r->race->sealer->sealed_tick() || (m.tick + 1) % cfg_.hash_every != 0 ||
        (hashes.count(m.tick) && hashes[m.tick].count(-1))) {
        ++counters_.invalid;
        return;
    }
    auto& at = hashes[m.tick];
    at[slot] = m.h;
    int expected = 0;
    for (int s : r->race->seat_of_slot) expected += r->seats[size_t(s)].conn != 0;
    if (int(at.size()) < expected) return;
    std::map<uint64_t, int> votes;
    for (const auto& [s, h] : at) ++votes[h];
    if (votes.size() <= 1) return;
    // Majority hash wins (ties: the lowest slot's value). Minority clients
    // are told and fast-forwarded from the log; a desync is always a bug.
    uint64_t majority = at.begin()->second;
    int best = 0, ties = 0;
    for (const auto& [h, n] : votes) {
        if (n > best || (n == best && h == at.begin()->second)) majority = h;
        if (n > best) best = n, ties = 1;
        else if (n == best) ++ties;
    }
    ++counters_.desyncs;
    r->race->record.desync_ticks.push_back(m.tick);
    emit("desync", "\"room\":\"" + r->code + "\",\"tick\":" + std::to_string(m.tick) +
                       ",\"strict_majority\":" + (ties == 1 ? "true" : "false"));
    for (const auto& [s, h] : at) {
        // Without a strict majority nobody can be shown wrong; telling one
        // side would let a lying seat (e.g. slot 0 in a duel) end an honest
        // client's race. The record and log keep the evidence.
        if (h == majority || ties > 1) continue;
        const int seat = r->race->seat_of_slot[size_t(s)];
        if (auto it = conns_.find(r->seats[size_t(seat)].conn); it != conns_.end()) {
            send(it->second, net::Desync{m.tick});
            it->second.acked = -1;
            send_catch_up(it->second, *r);
        }
    }
    at.clear();
    at[-1] = majority; // checked marker (slot -1 never reports)
}

void Server::on_result(Conn& c, const net::Result& m, uint64_t now) {
    Room* r = room_of(c);
    const int slot = r ? race_slot(*r, c.seat) : -1;
    if (slot < 0 || r->race->finished) return;
    // A result names a confirmed tick; a future one is a lie that would start
    // the finish timeout for everyone.
    if (m.tick < 0 || m.tick > r->race->sealer->sealed_tick()) {
        ++counters_.invalid;
        return;
    }
    Seat& seat = r->seats[size_t(c.seat)];
    if (seat.result) return;
    seat.result = m;
    seat.outcome = m.outcome;
    (void)now; // the finish timeout starts in step_race once enough seats reported
}

void Server::maybe_start(Room& r, uint64_t now) {
    int humans = 0;
    for (const Seat& s : r.seats) {
        if (!s.occupied) continue;
        if (!s.conn || !s.character || !s.vote) return;
        ++humans;
    }
    if (humans < cfg_.min_humans) return;
    // Course: most votes; ties go to the host's vote if tied, else the lowest course.
    std::array<int, 6> votes{};
    for (const Seat& s : r.seats)
        if (s.occupied) ++votes[s.vote];
    const int top = *std::max_element(votes.begin() + 1, votes.end());
    int course = 0;
    const int host_vote = r.seats[size_t(r.host)].vote;
    if (votes[size_t(host_vote)] == top) course = host_vote;
    for (int k = 1; k <= 5 && !course; ++k)
        if (votes[size_t(k)] == top) course = k;
    Race race;
    race.start.race_id = r.next_race_id++;
    race.start.seed = uint32_t(rng_());
    race.start.course = uint8_t(course);
    race.start.options = r.options;
    race.start.start_tick = ticks_at(cfg_.load_ms);
    for (int s = 0; s < net::max_seats; ++s) {
        Seat& seat = r.seats[size_t(s)];
        if (!seat.occupied) continue;
        race.seat_of_slot.push_back(s);
        race.start.humans.push_back({uint8_t(s), seat.character, 0, seat.alias});
        seat.outcome = net::Outcome::racing;
        seat.result.reset();
    }
    race.sealer = std::make_unique<Sealer>(humans);
    race.tick0_ms = now + cfg_.load_ms;
    race.record.start = race.start;
    r.race = std::move(race);
    ++counters_.races_started;
    for (int s : r.race->seat_of_slot) {
        if (auto it = conns_.find(r.seats[size_t(s)].conn); it != conns_.end()) {
            net::Start st = r.race->start;
            st.your_seat = uint8_t(race_slot(r, s));
            it->second.acked = it->second.sent_upto = -1;
            send(it->second, st);
        }
    }
    for (ConnId id : r.spectators)
        if (auto it = conns_.find(id); it != conns_.end()) {
            net::Start st = r.race->start;
            st.your_seat = 0xff;
            it->second.acked = it->second.sent_upto = -1;
            send(it->second, st);
        }
    broadcast_state(r);
    emit("race_start", "\"room\":\"" + r.code + "\",\"humans\":" + std::to_string(humans) +
                           ",\"course\":" + std::to_string(course));
}

void Server::send_catch_up(Conn& c, Room& r) {
    const Sealer& sealer = *r.race->sealer;
    const int32_t sealed = sealer.sealed_tick();
    int32_t first = 0;
    do {
        net::LogChunk chunk;
        chunk.race_id = r.race->start.race_id;
        const int32_t n = std::min<int32_t>(net::max_log_chunk, sealed - first + 1);
        if (n <= 0) break;
        chunk.frames = sealer.frames(first, n);
        chunk.last = first + n > sealed;
        send(c, chunk);
        first += n;
    } while (first <= sealed);
    c.sent_upto = sealed;
}

void Server::step_race(Room& r, uint64_t now) {
    Race& race = *r.race;
    if (now >= race.tick0_ms) {
        const int32_t current = ticks_at(now) - ticks_at(race.tick0_ms);
        const int32_t before = race.sealer->sealed_tick();
        race.sealer->seal_through(current - cfg_.seal_delay_ticks);
        if (race.sealer->sealed_tick() != before) {
            // Hand reconnected seats back once they hold the timeline.
            for (size_t slot = 0; slot < race.seat_of_slot.size(); ++slot) {
                const Seat& seat = r.seats[size_t(race.seat_of_slot[slot])];
                if (seat.conn && !race.sealer->human(int(slot)) && seat.outcome != net::Outcome::left)
                    if (auto it = conns_.find(seat.conn);
                        it != conns_.end() && it->second.acked >= race.sealer->sealed_tick() - 30)
                        race.sealer->set_human(int(slot), true);
            }
        }
    }
    const int32_t sealed = race.sealer->sealed_tick();
    auto feed = [&](Conn& c) {
        const bool rtc = (c.unreliable && c.unreliable->status() == net::ChannelStatus::open) ||
                         c.reliable->transport_stats().rtc_open;
        if (rtc) { // repeat everything unacknowledged
            c.sent_upto = std::max(c.sent_upto, c.acked);
            if (now < c.next_rtc_send_ms) return;
            c.next_rtc_send_ms = now + 16;
            if (sealed > c.acked) send_race(c, race.sealer->frames(c.acked + 1, cfg_.frames_per_packet));
            return;
        }
        while (c.sent_upto < sealed) { // reliable: each frame once
            const net::Frames f = race.sealer->frames(c.sent_upto + 1, cfg_.frames_per_packet);
            send(c, f);
            c.sent_upto += int32_t(f.frames.size());
        }
    };
    for (int s : race.seat_of_slot)
        if (r.seats[size_t(s)].conn)
            if (auto it = conns_.find(r.seats[size_t(s)].conn); it != conns_.end()) feed(it->second);
    for (ConnId id : r.spectators)
        if (auto it = conns_.find(id); it != conns_.end()) feed(it->second);
    // Race end: every human reported a result or is gone, or the wait timed out.
    bool all = true;
    for (int s : race.seat_of_slot) {
        const Seat& seat = r.seats[size_t(s)];
        all &= seat.result.has_value() || seat.conn == 0;
    }
    const bool any_connected = std::any_of(race.seat_of_slot.begin(), race.seat_of_slot.end(),
                                           [&](int s) { return r.seats[size_t(s)].conn != 0; });
    // Honest clients report the same terminal tick, so the timeout only waits
    // for stragglers: it starts once at least half the connected seats have
    // reported, so one lying seat in a room of three or more cannot end the race.
    int connected = 0, reported = 0;
    for (int s : race.seat_of_slot) {
        const Seat& seat = r.seats[size_t(s)];
        connected += seat.conn != 0;
        reported += seat.conn != 0 && seat.result.has_value();
    }
    if (!race.first_result_ms && reported > 0 && 2 * reported >= connected) race.first_result_ms = now;
    counters_.late_inputs = race.sealer->counters().late;
    if ((all && (race.first_result_ms || !any_connected)) ||
        (race.first_result_ms && now - race.first_result_ms > cfg_.finish_timeout_ms))
        finish_race(r, now); // resets r.race
}

void Server::finish_race(Room& r, uint64_t now) {
    Race& race = *r.race;
    race.finished = true;
    net::Official off;
    std::optional<std::pair<int32_t, uint64_t>> agreed_on;
    off.agreed = true;
    for (int s : race.seat_of_slot) {
        Seat& seat = r.seats[size_t(s)];
        net::OfficialRow row{uint8_t(s), net::Outcome::left, 15};
        if (seat.result) {
            row.outcome = seat.result->outcome;
            row.place = seat.result->place;
            const std::pair<int32_t, uint64_t> key{seat.result->tick, seat.result->h};
            if (agreed_on && *agreed_on != key) off.agreed = false;
            agreed_on = key;
        } else if (seat.conn) {
            off.agreed = false; // a seat still racing never confirmed this table
        }
        off.rows.push_back(row);
    }
    if (!agreed_on) off.agreed = false;
    race.record.frames = race.sealer->log();
    race.record.official = off;
    for (int s : race.seat_of_slot)
        if (r.seats[size_t(s)].conn)
            if (auto it = conns_.find(r.seats[size_t(s)].conn); it != conns_.end()) send(it->second, off);
    for (ConnId id : r.spectators)
        if (auto it = conns_.find(id); it != conns_.end()) send(it->second, off);
    if (on_race_finished) on_race_finished(race.record);
    ++counters_.races_finished;
    emit("race_end", "\"room\":\"" + r.code + "\",\"official\":" + (off.agreed ? "true" : "false") +
                         ",\"ticks\":" + std::to_string(race.sealer->sealed_tick() + 1));
    r.race.reset();
    r.lobby_since_ms = now;
    for (int s = 0; s < net::max_seats; ++s) {
        Seat& seat = r.seats[size_t(s)];
        if (!seat.occupied) continue;
        if (seat.conn == 0 || seat.outcome == net::Outcome::left) {
            if (seat.outcome == net::Outcome::left || now - seat.disconnected_ms > cfg_.reconnect_window_ms) {
                vacate(r, s, now);
                continue;
            }
        }
        seat.vote = 0; // rematch: vote again (screen 0x0c); characters are kept
        seat.ready = false;
        seat.result.reset();
        seat.outcome = net::Outcome::racing;
    }
    broadcast_state(r);
}

void Server::broadcast_state(Room& r) {
    net::RoomState st;
    st.code = r.code;
    st.options = r.options;
    int humans = 0;
    for (int s = 0; s < net::max_seats; ++s) {
        const Seat& seat = r.seats[size_t(s)];
        net::SeatInfo& info = st.seats[size_t(s)];
        info.occupied = seat.occupied;
        info.connected = seat.conn != 0;
        info.ready = seat.ready;
        info.character = seat.character;
        info.vote = seat.vote;
        info.alias = seat.alias;
        if (auto it = conns_.find(seat.conn); seat.conn && it != conns_.end())
            info.ping_ms = it->second.rtt_ms;
        humans += seat.occupied;
    }
    st.status = r.race                     ? net::RoomStatus::racing
                : humans >= net::max_seats ? net::RoomStatus::full
                                           : net::RoomStatus::lobby;
    st.host_seat = uint8_t(r.host);
    for (int s = 0; s < net::max_seats; ++s) {
        const Seat& seat = r.seats[size_t(s)];
        if (!seat.occupied || !seat.conn) continue;
        if (auto it = conns_.find(seat.conn); it != conns_.end()) {
            st.your_seat = uint8_t(s);
            send(it->second, st);
        }
    }
    st.your_seat = 0xff;
    for (ConnId id : r.spectators)
        if (auto it = conns_.find(id); it != conns_.end()) send(it->second, st);
}

} // namespace rr::netd
