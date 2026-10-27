// Plan 34 Task 11 Step 2: hostile input against rr-netd's room logic. A
// misbehaving client (fuzzed bytes, server-only message types, wrong-slot and
// overflowing inputs, replayed or impossible hashes and results, fan-out
// floods, spectator floods) must be dropped and counted, and must not change
// what any other seat receives. The socket-level counterpart is net_hostile_ws.
#include "check.hpp"
#include "net/channel.hpp"
#include "net/protocol.hpp"
#include "room.hpp"
#include "sealer.hpp"

#include <climits>
#include <deque>
#include <random>

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
    template <class T> std::vector<T> all() {
        drain();
        std::vector<T> out;
        for (const Message& m : inbox)
            if (const T* t = std::get_if<T>(&m)) out.push_back(*t);
        return out;
    }
    template <class T> std::optional<T> last() {
        auto v = all<T>();
        return v.empty() ? std::nullopt : std::optional<T>(v.back());
    }
    void clear() {
        drain();
        inbox.clear();
    }
    bool open() const { return ch->status() == ChannelStatus::open; }
};

Hello hello(const std::string& alias) {
    Hello h;
    h.alias = alias;
    h.sim_fingerprint = 0x1234;
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

// Seats `n` clients in one room; returns the code. Does not vote.
std::string seat(Server& s, std::vector<Client>& cs, int n) {
    std::string code;
    for (int i = 0; i < n; ++i) {
        cs.push_back(connect(s));
        cs.back().send(hello("Rider" + std::to_string(i)));
        if (i == 0)
            cs.back().send(RoomCreate{{1, 2, true, true}});
        else
            cs.back().send(RoomJoin{code, false});
        run(s, 10);
        if (i == 0) code = cs.back().last<Welcome>()->room_code;
        cs.back().send(PickCharacter{uint8_t(i + 1)});
    }
    run(s, 10);
    return code;
}

void vote_all(Server& s, std::vector<Client>& cs) {
    for (Client& c : cs) c.send(VoteCourse{1});
    run(s, 10);
}

// Word the honest seat sends for tick t.
uint16_t honest_word(int32_t t) {
    return uint16_t(0x4000 | (t & 0x3fff));
}

} // namespace

int main() {
    rr::test::Checks check;
    ServerConfig cfg;
    cfg.seed = 11;

    // --- Sealer: extreme ticks neither overflow (UBSan) nor get buffered. ---
    {
        rr::netd::Sealer sl(2);
        const std::vector<uint16_t> words(16, 0xffff);
        sl.input(0, INT32_MAX - 3, words);
        sl.input(0, INT32_MIN, words);
        sl.input(9, 0, words); // no such slot
        sl.seal_through(0);
        check(sl.counters().dropped_ahead == 16 && sl.counters().dropped_old == 16,
              "extreme ticks dropped and counted");
        check(sl.log().size() == 1 && sl.log()[0].late == 3, "nothing buffered from them");
    }

    // --- Lobby: fan-out floods are cut by their own bucket. ---
    {
        now = 0;
        Server s(cfg);
        std::vector<Client> cs;
        const std::string code = seat(s, cs, 3);
        for (Client& c : cs) c.clear();
        // 400 messages stay inside the general 480-message burst.
        for (int i = 0; i < 200; ++i) cs[0].send(Chat{0xff, "spam"});
        for (int i = 0; i < 200; ++i) cs[0].send(PickCharacter{uint8_t(1 + i % 8)});
        run(s, 4);
        const size_t relays = cs[1].all<ChatRelay>().size(), states = cs[1].all<RoomState>().size();
        check(relays + states <= size_t(cfg.fanout_burst),
              "a chat/pick burst reaches others at most burst times");
        check(cs[1].inbox.size() < 128 && cs[2].all<ChatRelay>().size() == relays,
              "other clients' event queues stay far below the 128-event failure bound");
        check(s.counters().fanout_limited >= 390, "excess fan-out messages counted");
        check(cs[0].open(), "a chatty but within-rate client is not disconnected");
        run(s, 2000);
        cs[1].clear();
        cs[0].send(Chat{0xff, "after"});
        run(s, 10);
        check(!cs[1].all<ChatRelay>().empty(), "the bucket refills: normal chat resumes");

        // Spectator flood: capped per room, join/leave loops are fan-out limited.
        std::vector<Client> watchers;
        for (int i = 0; i <= cfg.max_spectators; ++i) {
            watchers.push_back(connect(s));
            watchers.back().send(hello("Watch" + std::to_string(i)));
            watchers.back().send(RoomJoin{code, true});
        }
        run(s, 10);
        int rejected = 0;
        for (Client& w : watchers)
            if (auto r = w.last<Reject>(); r && r->reason == RejectReason::room_full) ++rejected;
        check(rejected == 1, "spectators beyond the cap are refused");
        cs[1].clear();
        for (int i = 0; i < 200; ++i) {
            watchers[0].send(RoomLeave{});
            watchers[0].send(RoomJoin{code, true});
        }
        run(s, 4);
        check(cs[1].all<RoomState>().size() <= size_t(2 * cfg.fanout_burst) + 2,
              "a spectate/leave loop cannot flood the seats with room states");
    }

    // --- Race: three seats; seat 0 hostile, seat 1 honest, seat 2 honest. ---
    {
        now = 0;
        Server s(cfg);
        std::vector<Client> cs;
        const std::string code = seat(s, cs, 3);
        vote_all(s, cs);
        check(cs[1].last<Start>().has_value(), "race started");
        Client spectator = connect(s);
        spectator.send(hello("Spec"));
        spectator.send(RoomJoin{code, true});
        Client stranger = connect(s);
        stranger.send(hello("Stranger"));
        run(s, 10);

        // Honest inputs in 16-tick packets, kept about 100 ticks ahead of the
        // race clock (the sealer refuses more than 240 ticks ahead).
        const uint64_t tick0_ms = now - 10 + cfg.load_ms; // Start was sent in the vote step
        int32_t sent = 0;
        auto feed_honest = [&] {
            const int32_t race_tick = now > tick0_ms ? int32_t((now - tick0_ms) * 60 / 1000) : 0;
            for (; sent < race_tick + 100; sent += 16) {
                Input in{sent, {}};
                for (int k = 0; k < 16; ++k) in.words.push_back(honest_word(sent + k));
                cs[1].send(in);
                cs[2].send(in);
            }
        };
        feed_honest();
        run(s, cfg.load_ms + 500);

        // Wrong-slot inputs: a spectator and a stranger outside any room.
        const uint64_t wrong = s.counters().wrong_seat_inputs;
        spectator.send(Input{100, {1, 2, 3}});
        stranger.send(Input{100, {1, 2, 3}});
        // Overflowing and absurd ticks from the hostile seat.
        cs[0].send(Input{INT32_MAX - 3, std::vector<uint16_t>(16, 0xffff)});
        cs[0].send(Input{INT32_MIN, std::vector<uint16_t>(16, 0xffff)});
        // Server-to-client types are never valid from a client.
        const uint64_t dropped0 = s.counters().invalid + s.counters().malformed;
        cs[0].send(Frames{0, 3, {{7, 0, {1, 2, 3}}}});
        cs[0].send(Official{});
        cs[0].send(Desync{59});
        cs[0].send(Start{});
        cs[0].send(ChatRelay{1, 5, "forged"});
        // Oversized and truncated payloads.
        std::vector<uint8_t> huge(max_message_bytes + 1, 0);
        huge[0] = message_type(Input{});
        cs[0].raw(huge);
        cs[0].raw({message_type(Input{}), 1});
        run(s, 50);
        check(s.counters().wrong_seat_inputs == wrong + 2,
              "spectator and stranger inputs counted, not sealed");
        // >= : the decoder already refuses some extreme Input ticks.
        check(s.counters().invalid + s.counters().malformed >= dropped0 + 7,
              "server-only types and oversized/truncated payloads counted");
        check(cs[1].all<Desync>().empty() && cs[1].all<Official>().empty() && cs[2].all<Start>().size() == 1,
              "forged server messages are never relayed");

        // Fuzzed bytes: random type bytes and payloads, valid or not, as fast as
        // the general rate limit allows. Rate-limited, never fatal to others.
        std::mt19937 rng(1234);
        for (int burst = 0; burst < 50; ++burst) {
            for (int i = 0; i < 20; ++i) { // 200/s, under the 240/s general limit
                std::vector<uint8_t> b(size_t(rng() % 40));
                for (uint8_t& x : b) x = uint8_t(rng());
                if (!b.empty() && rng() % 2) b[0] = uint8_t(1 + rng() % 29); // a real type byte
                // Not Hello (closes as a repeat) or RoomLeave (a real leave).
                if (b.size() > 1 && b[0] != message_type(Hello{}) && b[0] != message_type(RoomLeave{}))
                    cs[0].raw(b);
            }
            feed_honest();
            run(s, 100);
        }
        check(s.counters().malformed > 100, "fuzzed garbage counted as malformed");

        // Hash abuse: off-cadence, negative and future ticks are refused.
        const uint64_t dropped1 = s.counters().invalid + s.counters().malformed;
        cs[0].send(Hash{60, 1});
        cs[0].send(Hash{-1, 1});
        cs[0].send(Hash{INT32_MAX, 1});
        run(s, 10);
        check(s.counters().invalid + s.counters().malformed == dropped1 + 3, "impossible hash ticks counted");
        // Seat 0 lies at a check tick: it is the strict minority and is told.
        // (Inboxes are kept: the timeline check below needs every frame.)
        cs[0].send(Hash{59, 0xbad});
        cs[1].send(Hash{59, 0x600d});
        cs[2].send(Hash{59, 0x600d});
        run(s, 10);
        check(s.counters().desyncs == 1, "desync counted");
        check(cs[1].all<Desync>().empty() && cs[2].all<Desync>().empty(), "honest seats are not told");
        // Replaying the lie cannot re-trigger the desync or its log catch-up.
        const size_t chunks = cs[0].all<LogChunk>().size();
        check(chunks > 0, "liar fast-forwarded from the log");
        for (int i = 0; i < 50; ++i) cs[0].send(Hash{59, 0xbad + uint64_t(i)});
        run(s, 10);
        check(s.counters().desyncs == 1 && cs[0].all<LogChunk>().size() == chunks,
              "a replayed hash for a decided tick is ignored");

        // A future result cannot start the finish timeout for everyone.
        cs[0].send(Result{1000000, Outcome::won, 0, 1});
        cs[0].send(Result{-5, Outcome::won, 0, 1});
        // Nor can one seat of three claiming a finish at a real sealed tick:
        // the timeout waits until half the connected seats have reported.
        cs[0].send(Result{30, Outcome::won, 0, 1});
        const uint64_t until = now + cfg.finish_timeout_ms + 2000;
        while (now < until) {
            feed_honest();
            run(s, 1000, 8);
        }
        check(cs[1].all<Official>().empty(), "an impossible or lone result does not end the race");

        // No effect on other seats: every sealed word of seats 1 and 2 is
        // exactly what they sent, present and on time, from tick 0.
        std::vector<SealedFrame> got;
        int32_t expect = 0;
        bool contiguous = true;
        for (const Frames& f : cs[1].all<Frames>()) {
            contiguous &= f.first_tick == expect;
            expect += int32_t(f.frames.size());
            got.insert(got.end(), f.frames.begin(), f.frames.end());
        }
        bool exact = got.size() > 3000;
        for (size_t t = 0; t < got.size() && exact; ++t)
            exact = got[t].words.size() == 3 && got[t].words[1] == honest_word(int32_t(t)) &&
                    got[t].words[2] == honest_word(int32_t(t)) && (got[t].present & 6) == 6 &&
                    (got[t].late & 6) == 0;
        for (size_t t = 0; t < got.size(); ++t)
            if (got[t].words.size() != 3 || got[t].words[1] != honest_word(int32_t(t)) || (got[t].late & 6)) {
                break;
            }
        check(contiguous && exact, "honest seats' timeline is exactly their own inputs");
        check(cs[1].open() && cs[2].open(), "honest seats stay connected");
    }

    // --- Two-seat tie: a lying slot 0 cannot end the honest seat's race. ---
    {
        now = 0;
        Server s(cfg);
        std::vector<Client> cs;
        seat(s, cs, 2);
        vote_all(s, cs);
        run(s, cfg.load_ms + 1500);
        for (Client& c : cs) c.clear();
        cs[0].send(Hash{59, 0xbad});
        cs[1].send(Hash{59, 0x600d});
        run(s, 10);
        check(s.counters().desyncs == 1, "duel desync recorded");
        check(cs[1].all<Desync>().empty() && cs[0].all<Desync>().empty(), "a tie tells neither seat");
        // In a duel one report is half the room: the backstop timeout runs
        // (documented residual risk) and the result is unofficial.
        cs[0].send(Result{30, Outcome::won, 0, 1});
        for (uint64_t t = 0; t < cfg.finish_timeout_ms + 1000; t += 1000) {
            for (Client& c : cs) c.send(Ping{uint32_t(now)}); // stay clear of the idle timeout
            run(s, 1000, 50);
        }
        const auto off = cs[1].last<Official>();
        check(off && !off->agreed, "a lone duel result ends unofficial after the timeout");
    }

    // --- Flood past the general limit: the flooder alone is closed. ---
    {
        now = 0;
        Server s(cfg);
        std::vector<Client> cs;
        seat(s, cs, 2);
        for (Client& c : cs) c.clear();
        for (int i = 0; i < 5000; ++i) cs[0].send(Ping{uint32_t(i)});
        run(s, 3000, 50);
        check(!cs[0].open() && s.counters().rate_limited > 0, "flooder rate-limited and closed");
        check(cs[1].open(), "the other seat stays connected");
        check(cs[1].last<RoomState>() && !cs[1].last<RoomState>()->seats[0].connected,
              "the flooder's seat shows as disconnected");
    }

    return check.finish("server_hostile");
}
