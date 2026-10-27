// Plan 34 Task 7: the lobby room client against a real Server over the
// deterministic loopback network: the compatibility gate, host/join/list by
// code and invite link, picks, votes, Start, chat, results, Official,
// rematch, seat-token reconnect and the race-client channel handover.
#include "check.hpp"
#include "career.hpp"
#include "net/channel.hpp"
#include "net/client.hpp"
#include "net/compat.hpp"
#include "net/mano.hpp"
#include "net/protocol.hpp"
#include "room.hpp"

#include <algorithm>

using namespace rr::net;
using rr::netd::Server;
using rr::netd::ServerConfig;

namespace {

uint64_t now = 0;
const Clock clock_fn = [] { return now; };
const DataDigest kDigest = digest_from_table({{"ROADRASH.EXE", "aa"}, {"DATA/BIKESPEC.RSC", "bb"}});

Hello hello_for(const std::string& alias, std::optional<SeatToken> token = std::nullopt) {
    return make_hello(alias, Platform::bot, kDigest, token);
}

struct Pilot {
    RoomClient client;
    static RoomClient make(Hello h, Server& s) {
        RoomClient::Factory factory = [&s]() {
            auto [a, b] = loopback_pair({0, 0, 0, 0, 1}, true, clock_fn);
            s.add_connection(std::move(b), now);
            return std::move(a);
        };
        return RoomClient(std::move(factory), h);
    }
};

void run(Server& s, uint64_t ms, uint64_t step = 4) {
    const uint64_t end = now + ms;
    while (now < end) {
        now += std::min(step, end - now);
        s.step(now);
    }
}

template <class... P> void pump(Server& s, P&... pilots) {
    for (int i = 0; i < 3; ++i) {
        (pilots.client.update(now), ...);
        run(s, 5);
        (pilots.client.update(now), ...);
    }
}

} // namespace

int main() {
    rr::test::Checks check;

    // Compatibility gate units (no network).
    {
        const uint64_t a = self_test_hash(), b = self_test_hash();
        check(a != 0 && a == b, "self-test hash stable");
        check(local_fingerprint() != 0, "fingerprint non-zero");
        const DataDigest d1 = digest_from_table({{"x", "y"}});
        const DataDigest d2 = digest_from_table({{"x", "y"}});
        const DataDigest d3 = digest_from_table({{"x", "z"}});
        check(d1 == d2 && d1 != d3, "reference digest stable and sensitive");
        const Hello h = make_hello("0123456789abcdefGH", Platform::macos, d1);
        check(h.protocol == protocol_version, "hello protocol");
        check(h.alias == "0123456789abcdef", "hello alias truncated to 16");
        check(h.sim_fingerprint == local_fingerprint(), "hello carries the fingerprint");
        for (int r = 1; r <= 11; ++r) check(!reject_text(RejectReason(r)).empty(), "reject text present");
        check(parse_room_code("ab23cd").value_or("") == "AB23CD", "bare code normalises");
        check(parse_room_code("https://road-rash.archit.xyz/?room=ab23cd").value_or("") == "AB23CD",
              "invite link parses");
        check(!parse_room_code("??????").has_value(), "garbage code rejected");
        check(!parse_room_code("ABCDE").has_value(), "short code rejected");
    }

    ServerConfig cfg;
    cfg.seed = 7;
    Server s(cfg);

    Pilot host{Pilot::make(hello_for("Natasha"), s)};
    pump(s, host);
    check(host.client.conn() == RoomClient::Conn::open, "host welcomed");
    check(host.client.error().empty(), "host no error");

    host.client.create_room(RoomOptions{1, 2, true, true});
    pump(s, host);
    check(host.client.in_room(), "host in room");
    const std::string code = host.client.room()->code;
    check(code.size() == 6 && valid_room_code(code), "room code valid");
    check(host.client.room()->your_seat == 0 && host.client.is_host(), "host is seat 0");
    check(host.client.seat_token().has_value(), "host holds a seat token");

    // Room list shows the public room to a compatible stranger.
    Pilot stranger{Pilot::make(hello_for("Stranger"), s)};
    pump(s, stranger);
    stranger.client.request_list();
    pump(s, stranger);
    check(stranger.client.rooms().size() == 1 && stranger.client.rooms()[0].code == code,
          "public room listed");
    check(stranger.client.rooms()[0].host == "Natasha", "list host alias");

    // Join by bare code; a second join path uses the full invite link.
    Pilot guest{Pilot::make(hello_for("Axle"), s)};
    pump(s, guest);
    guest.client.join_room_text(code);
    pump(s, guest, host);
    check(guest.client.in_room() && guest.client.room()->your_seat == 1, "guest is seat 1");
    check(host.client.occupied_count() == 2, "roster has two");

    Pilot link{Pilot::make(hello_for("Link"), s)};
    pump(s, link);
    link.client.join_room_text("https://road-rash.archit.xyz/?room=" + code);
    pump(s, link, host, guest);
    check(link.client.in_room(), "invite-link join works");

    // Rejections: wrong fingerprint, unknown room.
    Pilot bad_fp{Pilot::make(hello_for("Bad"), s)};
    pump(s, bad_fp);
    {
        Hello tampered = hello_for("Bad");
        tampered.sim_fingerprint ^= 1;
        Pilot p{Pilot::make(tampered, s)};
        pump(s, p);
        p.client.join_room_text(code);
        pump(s, p);
        check(!p.client.in_room() && p.client.error().find("Build") != std::string::npos,
              "fingerprint mismatch rejected");
    }
    bad_fp.client.join_room_text("ZZZZZZ");
    pump(s, bad_fp);
    check(!bad_fp.client.in_room() && bad_fp.client.error().find("No room") != std::string::npos,
          "unknown room rejected");

    // Seat-token reconnect in the lobby keeps the seat.
    {
        Pilot re{Pilot::make(hello_for("Axle", guest.client.seat_token()), s)};
        pump(s, re, host);
        check(re.client.in_room() && re.client.my_seat() == 1, "reconnect keeps seat 1");
        guest = std::move(re); // old channel was kicked; drop it
        pump(s, guest, host);
        check(host.client.occupied_count() == 3, "roster intact after reconnect");
    }

    // Roster announcements: a fresh join, character pick and leave, all while
    // still in the lobby (joining mid-race is rejected as room_racing).
    Pilot late{Pilot::make(hello_for("Late"), s)};
    pump(s, late);
    late.client.join_room_text(code);
    pump(s, late, host);
    bool saw_join = false;
    for (const auto& e : host.client.take_roster())
        if (e.kind == RoomClient::RosterEvent::Kind::joined && e.alias == "Late") saw_join = true;
    check(saw_join, "join announced");
    late.client.pick_character(4);
    pump(s, late, host);
    bool saw_char = false;
    for (const auto& e : host.client.take_roster())
        if (e.kind == RoomClient::RosterEvent::Kind::character && e.value == 4) saw_char = true;
    check(saw_char, "character pick announced");
    late.client.leave();
    pump(s, late, host);
    check(!late.client.in_room(), "leave clears the room");

    // Picks and votes; the server starts once every occupied seat voted.
    host.client.pick_character(1);
    guest.client.pick_character(2);
    link.client.pick_character(3);
    host.client.vote_course(4);
    guest.client.vote_course(4);
    pump(s, host, guest, link);
    check(host.client.vote_count() == 2 && host.client.occupied_count() == 3, "two of three voted");
    link.client.vote_course(4);
    pump(s, host, guest, link);
    const auto hs = host.client.take_start(), gs = guest.client.take_start(), ls = link.client.take_start();
    check(hs && gs && ls, "everyone gets Start");
    check(hs->race_id == gs->race_id && gs->race_id == ls->race_id, "same race");
    check(hs->course == 4 && hs->humans.size() == 3, "voted course and roster");
    check(hs->your_seat == 0 && gs->your_seat == 1 && ls->your_seat == 2, "seat identities");
    check(!host.client.has_pending_race(), "Start cleared on take");

    // Lobby chat relays resolve aliases.
    host.client.resume(host.client.release_channel());
    guest.client.resume(guest.client.release_channel());
    link.client.resume(link.client.release_channel());
    host.client.send_chat_text("good luck");
    pump(s, host, guest);
    const auto chat = guest.client.take_chat();
    check(chat.size() == 1 && chat[0].alias == "Natasha" && chat[0].text == "good luck",
          "chat relay with alias");

    // This room-only fixture deliberately does not simulate a race. Resume
    // lobby polling after inspecting Start; the production frontend instead
    // lends these channels to the real rollback clients.
    host.client.resume(host.client.release_channel());
    guest.client.resume(guest.client.release_channel());
    link.client.resume(link.client.release_channel());
    // Results from every seat make the table official; the room survives for
    // a rematch with votes cleared and characters kept. A result names a
    // sealed tick (rr-netd refuses future ones), so let the race clock pass it.
    run(s, cfg.load_ms + 2000, 50);
    pump(s, host, guest, link);
    host.client.send_result(100, Outcome::won, 1, 0xabcdef);
    guest.client.send_result(100, Outcome::finished, 2, 0xabcdef);
    link.client.send_result(100, Outcome::finished, 3, 0xabcdef);
    pump(s, host, guest, link);
    const auto ho = host.client.take_official(), go = guest.client.take_official();
    check(ho && go && ho->agreed && ho->rows.size() == 3, "official agreed table");
    check(host.client.in_room(), "room kept after the race");
    pump(s, host, guest, link);
    {
        bool votes_cleared = true, chars_kept = true;
        for (const SeatInfo& seat : host.client.room()->seats)
            if (seat.occupied) {
                votes_cleared &= seat.vote == 0;
                chars_kept &= seat.character != 0;
            }
        check(votes_cleared && chars_kept, "rematch resets votes, keeps characters");
    }
    host.client.vote_course(2);
    guest.client.vote_course(2);
    link.client.vote_course(2);
    pump(s, host, guest, link);
    const auto hs2 = host.client.take_start();
    check(hs2 && hs2->race_id == hs->race_id + 1 && hs2->course == 2, "rematch starts a new race");

    // Channel handover to a race client and back, with leftover lobby
    // messages injected instead of lost.
    {
        auto channel = host.client.release_channel();
        check(channel && host.client.detached(), "channel released");
        host.client.update(now); // no-op while detached
        Client::Factory factory = [](const Start&) -> std::unique_ptr<rr::RaceSetup> { return nullptr; };
        Client race(std::move(channel), hello_for("Natasha"), std::move(factory));
        race.skip_hello(); // the lobby already said Hello on this connection
        race.update(now, [](int32_t) { return uint16_t(0); });
        run(s, 20);
        race.update(now, [](int32_t) { return uint16_t(0); });
        while (auto m = race.poll_event()) {
            if (std::holds_alternative<RoomState>(*m) || std::holds_alternative<ChatRelay>(*m) ||
                std::holds_alternative<Official>(*m))
                host.client.inject(*m, now);
        }
        host.client.resume(race.release_channel());
        check(!host.client.detached(), "channel resumed");
        pump(s, host);
        check(host.client.error().empty(), "no spurious error after handover");
    }

    // Official-results mapping: places from the table, only first is won,
    // normal videos (never DUEL), and no career mutation (checked by the
    // caller contract: mano_official takes no Career at all).
    {
        Official off;
        off.agreed = true;
        off.rows = {{0, Outcome::won, 1}, {1, Outcome::finished, 2}, {2, Outcome::busted, 0}};
        const std::vector<std::string> names = {"Natasha", "Axle", "Cydney"};
        const rr::ManoResult w = rr::mano_official(off, names, 0);
        check(w.screen == rr::screen_id::won && w.place == 0 && w.video_base == "WIN" &&
                  w.table.size() == 3 && w.table[0].player && w.table[0].name == "Natasha",
              "winner maps to won");
        const rr::ManoResult l = rr::mano_official(off, names, 1);
        check(l.screen == rr::screen_id::lost && l.place == 1 && l.video_base == "LOSE" &&
                  !l.table[0].player && l.table[1].player,
              "finished-not-first maps to lost");
        const rr::ManoResult b = rr::mano_official(off, names, 2);
        check(b.screen == rr::screen_id::busted && b.video_base == "BUSTED", "busted maps");
        const rr::ManoResult s = rr::mano_official(off, names, 7);
        check(s.screen == rr::screen_id::lost && !s.table[0].player, "missing seat is a spectator");
        check(w.video_base != "DUEL" && l.video_base != "DUEL", "no DUEL video");
    }

    return check.finish("net_mano");
}
