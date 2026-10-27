// Plan 34 Task 5 Step 1: rr-netd room logic driven by in-memory channels and
// a virtual clock: create/join/list, rejections, picks, votes, chat, start,
// host hand-over, reconnect by seat token, timeouts, race frames, results,
// rate limiting and malformed input.
#include "check.hpp"
#include "net/channel.hpp"
#include "net/protocol.hpp"
#include "room.hpp"

#include <deque>
#include <map>

using namespace rr::net;
using rr::netd::Server;
using rr::netd::ServerConfig;

namespace {

uint64_t now = 0;
const Clock clock_fn = [] { return now; };

struct Client {
    std::unique_ptr<Channel> ch;
    std::deque<Message> inbox;
    void send(const Message& m) { ch->send(encode(m)); }
    void raw(std::vector<uint8_t> b) { ch->send(b); }
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
    template <class T> std::vector<T> all() {
        drain();
        std::vector<T> out;
        for (const Message& m : inbox)
            if (const T* t = std::get_if<T>(&m)) out.push_back(*t);
        return out;
    }
    void clear() {
        drain();
        inbox.clear();
    }
};

Hello hello(const std::string& alias, uint64_t fp = 0x1234) {
    Hello h;
    h.alias = alias;
    h.sim_fingerprint = fp;
    h.data_digest[0] = 0xab;
    h.platform = Platform::bot;
    return h;
}

Client connect(Server& s) {
    auto [a, b] = loopback_pair({0, 0, 0, 0, 1}, true, clock_fn);
    s.add_connection(std::move(b), now);
    return Client{std::move(a), {}};
}

void run(Server& s, uint64_t ms, uint64_t step = 4) {
    const uint64_t end = now + ms;
    while (now < end) {
        now += std::min(step, end - now);
        s.step(now);
    }
}

} // namespace

int main() {
    rr::test::Checks check;
    ServerConfig cfg;
    cfg.seed = 7;
    Server s(cfg);

    Client a = connect(s);
    a.send(hello("Natasha"));
    run(s, 10);
    check(a.last<Welcome>().has_value(), "hello answered");
    a.clear();
    a.send(RoomCreate{{1, 2, true, true}});
    run(s, 10);
    const auto wa = a.last<Welcome>();
    const auto sa = a.last<RoomState>();
    check(wa && valid_room_code(wa->room_code), "created room has a code");
    check(sa && sa->your_seat == 0 && sa->host_seat == 0 && sa->seats[0].occupied && sa->options.level == 1,
          "creator holds seat 0 and is host");
    const std::string code = wa ? wa->room_code : "";

    Client b = connect(s);
    b.send(hello("Viper"));
    b.send(RoomJoin{code, false});
    run(s, 10);
    const auto sb = b.last<RoomState>();
    check(sb && sb->your_seat == 1 && sb->seats[1].alias == "Viper", "join by code takes seat 1");
    check(a.last<RoomState>()->seats[1].occupied, "host sees the new seat");
    const SeatToken b_token = b.last<Welcome>()->seat_token;

    // Public list; unlisted rooms and other builds are hidden.
    Client u = connect(s);
    u.send(hello("Hidden"));
    u.send(RoomCreate{{0, 0, false, false}});
    Client l = connect(s);
    l.send(hello("Looker"));
    l.send(RoomList{});
    Client other = connect(s);
    other.send(hello("Other", 0x9999));
    other.send(RoomList{});
    run(s, 10);
    const auto list = l.last<RoomListReply>();
    check(list && list->rooms.size() == 1 && list->rooms[0].code == code &&
              list->rooms[0].host == "Natasha" && list->rooms[0].humans == 2 &&
              list->rooms[0].status == RoomStatus::lobby,
          "public room listed with host/level/avail/status");
    check(other.last<RoomListReply>()->rooms.empty(), "rooms of another build are not listed");

    // Rejections.
    other.send(RoomJoin{code, false});
    Client wd = connect(s);
    Hello bad_digest = hello("Digest");
    bad_digest.data_digest[0] = 0;
    wd.send(bad_digest);
    wd.send(RoomJoin{code, false});
    Client nr = connect(s);
    nr.send(hello("Nobody"));
    nr.send(RoomJoin{"ZZZZZZ", false});
    Client old = connect(s);
    std::vector<uint8_t> v2 = encode(hello("Old"));
    v2[1] = 2;
    old.raw(v2);
    run(s, 10);
    check(other.last<Reject>() && other.last<Reject>()->reason == RejectReason::fingerprint,
          "wrong fingerprint");
    check(wd.last<Reject>() && wd.last<Reject>()->reason == RejectReason::data_digest, "wrong data digest");
    check(nr.last<Reject>() && nr.last<Reject>()->reason == RejectReason::no_such_room, "unknown room");
    check(old.last<Reject>() && old.last<Reject>()->reason == RejectReason::protocol, "old protocol");
    check(old.ch->status() == ChannelStatus::closed, "old protocol connection closed");

    // Fill the room to 8; the ninth is rejected.
    std::vector<Client> extra;
    for (int i = 0; i < 6; ++i) {
        extra.push_back(connect(s));
        extra.back().send(hello("Rider" + std::to_string(i)));
        extra.back().send(RoomJoin{code, false});
    }
    Client ninth = connect(s);
    ninth.send(hello("Ninth"));
    ninth.send(RoomJoin{code, false});
    run(s, 10);
    check(ninth.last<Reject>() && ninth.last<Reject>()->reason == RejectReason::room_full,
          "ninth player rejected");
    check(a.last<RoomState>()->status == RoomStatus::full, "room shows Full");
    for (Client& e : extra) e.send(RoomLeave{});
    run(s, 10);
    check(a.last<RoomState>()->status == RoomStatus::lobby && !a.last<RoomState>()->seats[2].occupied,
          "leaving frees the seats");

    // Picks, votes and chat.
    a.send(PickCharacter{3});
    b.send(PickCharacter{7});
    a.send(Chat{0, "Eat my dust!"});
    run(s, 10);
    const auto relays = b.all<ChatRelay>();
    check(!relays.empty() && relays.back().seat == 0 && relays.back().tick == -1 &&
              relays.back().text == "Eat my dust!",
          "lobby chat relayed with seat and no tick");
    check(a.last<RoomState>()->seats[1].character == 7, "character pick shown to everyone");

    // Host leaves before the start: the next seat becomes host. Then the host
    // returns as a new player.
    a.send(RoomLeave{});
    run(s, 10);
    check(b.last<RoomState>()->host_seat == 1, "next seat becomes host");
    a.clear();
    a.send(RoomJoin{code, false});
    a.send(PickCharacter{3});
    run(s, 10);
    check(a.last<RoomState>() && a.last<RoomState>()->your_seat == 0, "rejoin takes the free seat 0");

    // Reconnect by seat token (lobby): B's connection drops and comes back.
    b.ch->close();
    run(s, 10);
    check(!a.last<RoomState>()->seats[1].connected && a.last<RoomState>()->seats[1].occupied,
          "dropped seat kept for reconnect");
    Client b2 = connect(s);
    Hello hb = hello("Viper");
    hb.seat_token = b_token;
    b2.send(hb);
    run(s, 10);
    check(b2.last<Welcome>() && b2.last<Welcome>()->room_code == code && b2.last<RoomState>()->your_seat == 1,
          "seat token re-takes seat 1");
    check(s.counters().reconnects == 1, "reconnect counted");

    // Votes start the race: course 4 (both), seed from the server.
    a.clear();
    b2.clear();
    a.send(VoteCourse{4});
    b2.send(VoteCourse{2});
    run(s, 10);
    // Everyone voted: tie 1-1 goes to the host's vote (seat 1 is host now).
    const auto st_a = a.last<Start>();
    const auto st_b = b2.last<Start>();
    check(st_a && st_b, "race starts once every seat picked and voted");
    if (!st_a || !st_b) return check.finish("server_room");
    check(st_a->course == 2, "tie goes to the host's vote");
    check(st_a->humans.size() == 2 && st_a->your_seat == 0 && st_b->your_seat == 1 &&
              st_a->seed == st_b->seed,
          "same Start for both, own race slot each");
    check(st_a->countdown == 0x168, "networked countdown");
    check(a.last<RoomState>()->status == RoomStatus::racing, "room shows Racing");
    Client late = connect(s);
    late.send(hello("Late"));
    late.send(RoomJoin{code, false});
    run(s, 4);
    check(late.last<Reject>() && late.last<Reject>()->reason == RejectReason::room_racing,
          "join refused while racing");

    // Race: inputs arrive, frames are sealed and delivered in order.
    for (int t = 0; t < 120; ++t) {
        a.send(Input{t, {uint16_t(t)}});
        b2.send(Input{t, {uint16_t(1000 + t)}});
    }
    run(s, cfg.load_ms + 1000);
    std::vector<SealedFrame> got;
    int32_t expect = 0;
    bool contiguous = true;
    for (const Frames& f : a.all<Frames>()) {
        contiguous &= f.first_tick == expect;
        expect += int32_t(f.frames.size());
        got.insert(got.end(), f.frames.begin(), f.frames.end());
    }
    check(contiguous && got.size() >= 55, "frames contiguous from tick 0");
    check(got.size() > 10 && got[10].words == std::vector<uint16_t>{10, 1010} && got[10].present == 3 &&
              got[10].late == 0,
          "sealed words are the sent inputs");
    // Ping/Pong carries the race tick.
    a.send(Ping{uint32_t(now)});
    run(s, 4);
    check(a.last<Pong>() && a.last<Pong>()->server_tick >= 55, "pong carries the race tick");
    // B vanishes: AI-driven from a sealed tick, the race goes on.
    b2.ch->close();
    run(s, 500);
    std::vector<SealedFrame> after;
    for (const Frames& f : a.all<Frames>()) after.insert(after.end(), f.frames.begin(), f.frames.end());
    check(!after.empty() && (after.back().present & 2) == 0, "vanished player is AI-driven");
    // B reconnects mid-race with the seat token: Start + log catch-up.
    Client b3 = connect(s);
    b3.send(hb);
    run(s, 10);
    const auto logs = b3.all<LogChunk>();
    check(b3.last<Start>() && b3.last<Start>()->your_seat == 1, "reconnect mid-race gets Start");
    check(!logs.empty() && logs.front().frames.first_tick == 0 && logs.back().last,
          "log catch-up from tick 0");
    b3.send(Ack{100000}); // clamped to the sealed tick
    for (int t = 0; t < 400; ++t) b3.send(Input{int32_t(after.size() + got.size() + size_t(t)), {7}});
    run(s, 300);
    std::vector<SealedFrame> back;
    for (const Frames& f : a.all<Frames>()) back.insert(back.end(), f.frames.begin(), f.frames.end());
    check(!back.empty() && (back.back().present & 2) != 0, "reconnected player takes the rider back");

    // Desync: a different hash for the same check tick is detected. Two seats
    // tie, so neither is told (server_hostile covers the strict minority).
    a.send(Hash{59, 0xaaaa});
    b3.send(Hash{59, 0xbbbb});
    run(s, 10);
    check(s.counters().desyncs == 1, "desync counted");
    check(!a.last<Desync>() && !b3.last<Desync>(), "a tie names no minority");

    // Results: matching end hash -> official.
    rr::netd::RaceRecord record;
    s.on_race_finished = [&](const rr::netd::RaceRecord& r) { record = r; };
    a.send(Result{90, Outcome::won, 0, 0x42}); // results name a sealed tick
    b3.send(Result{90, Outcome::finished, 1, 0x42});
    run(s, 10);
    const auto off = a.last<Official>();
    check(off && off->agreed && off->rows.size() == 2 && off->rows[0].outcome == Outcome::won,
          "official results");
    check(record.frames.size() > 100 && record.start.race_id == st_a->race_id,
          "race record handed to the log");
    check(a.last<RoomState>()->status == RoomStatus::lobby && a.last<RoomState>()->seats[0].vote == 0,
          "back to the lobby for a rematch, votes cleared");

    // Rate limiting: a flood is dropped and the flooder disconnected; others unaffected.
    Client flood = connect(s);
    flood.send(hello("Flood"));
    run(s, 4);
    for (int i = 0; i < 20000; ++i) flood.send(Ping{1});
    run(s, 5000, 50);
    check(s.counters().rate_limited > 0, "flood rate-limited");
    check(flood.ch->status() == ChannelStatus::closed, "flooder disconnected");
    a.clear();
    a.send(Chat{0xff, "still here"});
    run(s, 10);
    check(!b3.all<ChatRelay>().empty(), "other seats unaffected");

    // Malformed input is counted and dropped.
    const uint64_t malformed = s.counters().malformed;
    a.raw({0x13, 1, 2});
    a.raw({0xee});
    run(s, 10);
    check(s.counters().malformed == malformed + 2, "malformed messages counted");

    // Timeouts: no Hello, then idle.
    Client silent = connect(s);
    run(s, cfg.hello_timeout_ms + 100, 100);
    check(silent.ch->status() == ChannelStatus::closed, "hello timeout");
    run(s, cfg.idle_timeout_ms + 100, 1000);
    check(a.ch->status() == ChannelStatus::closed, "idle timeout closes quiet connections");
    // Lobby timeout and seat expiry eventually remove every room.
    run(s, cfg.reconnect_window_ms + 1000, 1000);
    check(s.room_count() == 0, "rooms closed once empty");
    check(s.health_json(now).find("\"ok\":true") != std::string::npos, "health JSON");
    // Unreliable race channel: frames are repeated until acknowledged, so a
    // lossy DataChannel still delivers the whole timeline.
    {
        ServerConfig c2 = cfg;
        Server s2(c2);
        auto [ra, sa2] = loopback_pair({0, 0, 0, 0, 1}, true, clock_fn);
        const auto id = s2.add_connection(std::move(sa2), now);
        Client x{std::move(ra), {}};
        Client y = [&] {
            auto [rb, sb2] = loopback_pair({0, 0, 0, 0, 1}, true, clock_fn);
            s2.add_connection(std::move(sb2), now);
            return Client{std::move(rb), {}};
        }();
        auto [ua, ub] = loopback_pair({10, 60, 0.3, 0.1, 9}, false, clock_fn);
        s2.attach_unreliable(id, std::move(ub));
        x.send(hello("Lossy"));
        x.send(RoomCreate{{0, 0, false, false}});
        run(s2, 10);
        y.send(hello("Wired"));
        y.send(RoomJoin{x.last<Welcome>()->room_code, false});
        for (Client* k : {&x, &y}) {
            k->send(PickCharacter{1});
            k->send(VoteCourse{1});
        }
        run(s2, 10);
        std::map<int32_t, SealedFrame> held;
        int32_t contiguous = -1;
        for (int ms = 0; ms < int(c2.load_ms) + 3000; ms += 4) {
            run(s2, 4);
            y.clear();
            while (auto p = ua->poll())
                if (auto m = decode(*p))
                    if (auto* f = std::get_if<Frames>(&*m))
                        for (size_t i = 0; i < f->frames.size(); ++i)
                            held[f->first_tick + int32_t(i)] = f->frames[i];
            while (held.count(contiguous + 1)) ++contiguous;
            ua->send(encode(Ack{contiguous}));
        }
        check(contiguous >= 150,
              "lossy unreliable channel still delivers every sealed tick (repeat until acked)");
    }
    return check.finish("server_room");
}
