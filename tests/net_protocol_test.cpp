// Plan 34 Task 4 Step 1: every message round-trips; truncated, extended,
// oversized, wrong-version and out-of-range messages are rejected; encodings
// match the committed synthetic golden vectors (tests/data/net_protocol_vectors.txt).
#include "check.hpp"
#include "net/channel.hpp"
#include "net/protocol.hpp"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

using namespace rr::net;

namespace {

std::string hex(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t v : b) {
        s.push_back(d[v >> 4]);
        s.push_back(d[v & 15]);
    }
    return s;
}

SeatToken token(uint8_t base) {
    SeatToken t{};
    for (size_t i = 0; i < t.size(); ++i) t[i] = uint8_t(base + i);
    return t;
}

Frames frames(int32_t first, uint8_t humans, int count) {
    Frames f;
    f.first_tick = first;
    f.humans = humans;
    for (int i = 0; i < count; ++i) {
        SealedFrame s;
        s.present = uint8_t((1u << humans) - 1u);
        s.late = uint8_t(i & 1);
        for (int h = 0; h < humans; ++h) s.words.push_back(uint16_t(0x0100 * h + i));
        f.frames.push_back(s);
    }
    return f;
}

// One representative of every message type, in type order.
std::vector<std::pair<std::string, Message>> samples() {
    Hello hello;
    hello.sim_fingerprint = 0x0123456789abcdefull;
    for (size_t i = 0; i < hello.data_digest.size(); ++i) hello.data_digest[i] = uint8_t(0xa0 + i);
    hello.platform = Platform::macos;
    hello.alias = "Natasha";
    Hello reconnect = hello;
    reconnect.platform = Platform::android;
    reconnect.seat_token = token(0x10);
    RoomState state;
    state.code = "K7MPQ2";
    state.options = {2, 1, false, true};
    state.status = RoomStatus::lobby;
    state.host_seat = 0;
    state.your_seat = 1;
    state.seats[0] = {true, true, true, 3, 4, 42, "Natasha"};
    state.seats[1] = {true, true, false, 7, 0, 120, "Viper"};
    Start start;
    start.race_id = 77;
    start.seed = 0xdeadbeef;
    start.course = 4;
    start.options = {0, 2, true, true};
    start.start_tick = 600;
    start.your_seat = 1;
    start.humans = {{0, 3, 2, "Natasha"}, {1, 7, 5, "Viper"}};
    Official official;
    official.agreed = true;
    official.rows = {{1, Outcome::won, 0}, {0, Outcome::busted, 5}};
    LogChunk log;
    log.race_id = 77;
    log.last = true;
    log.frames = frames(0, 2, 3);
    return {
        {"hello", hello},
        {"hello_reconnect", reconnect},
        {"welcome", Welcome{9, 1700000000123ull, "K7MPQ2", token(0x40)}},
        {"reject", Reject{RejectReason::fingerprint}},
        {"room_create", RoomCreate{{3, 0, true, false}}},
        {"room_join", RoomJoin{"K7MPQ2", false}},
        {"room_list", RoomList{}},
        {"room_list_reply", RoomListReply{{{"K7MPQ2", "Natasha", 2, 3, RoomStatus::lobby},
                                           {"ABCDEF", "Viper", 0, 8, RoomStatus::full}}}},
        {"room_leave", RoomLeave{}},
        {"room_state", state},
        {"pick_character", PickCharacter{5}},
        {"vote_course", VoteCourse{4}},
        {"chat", Chat{2, "Eat my dust!"}},
        {"chat_relay", ChatRelay{1, 1234, "Eat my dust!"}},
        {"start", start},
        {"rtc_offer", RtcOffer{"v=0\r\n"}},
        {"rtc_answer", RtcAnswer{"v=0\r\na=x\r\n"}},
        {"rtc_candidate", RtcCandidate{"candidate:1 1 UDP 1 5.161.142.20 40000 typ host", "0"}},
        {"input", Input{1000, {0x0001, 0x0003, 0x0103}}},
        {"frames", frames(500, 3, 2)},
        {"ack", Ack{499}},
        {"ping", Ping{123456}},
        {"pong", Pong{123456, 700, 98765}},
        {"hash", Hash{600, 0xfeedfacecafebeefull}},
        {"desync", Desync{660}},
        {"result", Result{4321, Outcome::won, 0, 0x1122334455667788ull}},
        {"official", official},
        {"log_chunk", log},
        {"ready", Ready{true}},
        {"rtc_close", RtcClose{}},
    };
}

std::map<std::string, std::string> load_vectors(const std::string& path) {
    std::map<std::string, std::string> v;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string name, bytes;
        ss >> name >> bytes;
        v[name] = bytes;
    }
    return v;
}

bool rejects(const std::vector<uint8_t>& b) {
    return !decode(b).has_value();
}

} // namespace

int main() {
    rr::test::Checks check;
    const auto all = samples();
    check.eq(all.size(), std::variant_size_v<Message> + 1,
             "a sample for every message type (+ reconnect hello)");

    // Round trip, truncation and trailing-byte rejection for every sample.
    for (const auto& [name, msg] : all) {
        const std::vector<uint8_t> b = encode(msg);
        if (!check(!b.empty(), "encodes: " + name)) continue;
        check.eq(b[0], message_type(msg), "type byte: " + name);
        const auto back = decode(b);
        check(back && *back == msg, "round trip: " + name);
        bool truncated_ok = true;
        for (size_t n = 0; n < b.size(); ++n)
            truncated_ok &= rejects(std::vector<uint8_t>(b.begin(), b.begin() + std::ptrdiff_t(n)));
        check(truncated_ok, "every truncation rejected: " + name);
        std::vector<uint8_t> extended = b;
        extended.push_back(0);
        check(rejects(extended), "trailing byte rejected: " + name);
    }

    // Golden byte vectors (synthetic fixtures). RR_NET_PRINT_VECTORS=1 prints
    // the current encodings for review when the protocol version changes.
    if (std::getenv("RR_NET_PRINT_VECTORS")) {
        for (const auto& [name, msg] : all) std::cout << name << ' ' << hex(encode(msg)) << '\n';
    }
    const auto vectors = load_vectors(std::string(RR_SOURCE_DIR) + "/tests/data/net_protocol_vectors.txt");
    check.eq(vectors.size(), all.size(), "golden vector count");
    for (const auto& [name, msg] : all) {
        const auto it = vectors.find(name);
        check(it != vectors.end() && it->second == hex(encode(msg)), "golden bytes: " + name);
    }
    // Hand-checked layout of two vectors (independent of the file).
    check.eq(hex(encode(Ack{499})), std::string("14f3010000"), "Ack layout: type 20, i32 LE");
    check.eq(hex(encode(Input{1000, {1, 3}})),
             std::string("12e8030000020100"
                         "0300"),
             "Input layout");

    // Wrong version: decode refuses it, but the frozen prefix still names it.
    {
        Hello h = std::get<Hello>(all[0].second);
        std::vector<uint8_t> b = encode(h);
        b[1] = 2; // protocol 2
        check(rejects(b), "other protocol version rejected");
        check(peek_hello_protocol(b) == uint16_t(2), "peek reads the protocol of any version");
        check(!peek_hello_protocol(encode(Ping{1})).has_value(), "peek only reads Hello");
    }
    // Out-of-range fields: encode refuses them, and hand-built bytes are rejected.
    check(encode(PickCharacter{0}).empty() && encode(PickCharacter{9}).empty(),
          "character 0/9 not encodable");
    check(encode(VoteCourse{6}).empty(), "course 6 not encodable");
    check(encode(RoomCreate{{5, 0, true, true}}).empty(), "level 6 not encodable");
    check(encode(RoomCreate{{0, 3, true, true}}).empty(), "ai 3 not encodable");
    check(rejects({10, 0}) && rejects({10, 9}), "character byte out of range");
    check(rejects({11, 0}) && rejects({11, 6}), "course byte out of range");
    check(rejects({4, 0, 0, 2, 0}), "boolean byte 2 rejected");
    check(rejects({0}) && rejects({uint8_t(std::variant_size_v<Message> + 1)}) && rejects({0xff}) &&
              rejects({}),
          "unknown or empty type rejected");
    {
        Hello h = std::get<Hello>(all[0].second);
        h.alias = std::string("bad\x01name");
        check(encode(h).empty(), "control character in alias");
        h.alias = std::string(17, 'a');
        check(encode(h).empty(), "17-byte alias");
        h.alias.clear();
        check(encode(h).empty(), "empty alias");
        check(encode(Chat{0xff, std::string(61, 'x')}).empty(), "61-byte chat");
        check(encode(Chat{8, "hi"}).empty(), "chat slot 8");
        check(encode(RoomJoin{"ABCDE0", false}).empty() && encode(RoomJoin{"abcdef", false}).empty(),
              "room code alphabet");
    }
    {
        Input in{0, std::vector<uint16_t>(17, 1)};
        check(encode(in).empty(), "17 inputs not encodable");
        std::vector<uint8_t> b = encode(Input{0, std::vector<uint16_t>(16, 1)});
        b[5] = 17;
        b.push_back(1);
        b.push_back(0);
        check(rejects(b), "input count 17 on the wire rejected");
        check(encode(Input{-1, {1}}).empty(), "negative input tick");
    }
    {
        Frames f = frames(0, 2, 1);
        f.frames[0].present = 0x04; // seat 2 does not exist in a 2-human race
        check(encode(f).empty(), "present bit outside the race");
        std::vector<uint8_t> b = encode(frames(0, 2, 1));
        b[8] = 0x07; // present byte
        check(rejects(b), "present bit outside the race on the wire");
        f = frames(0, 2, 1);
        f.frames[0].words.pop_back();
        check(encode(f).empty(), "frame with a missing word");
        Frames nine;
        nine.humans = 9;
        nine.frames.push_back(SealedFrame{0, 0, std::vector<uint16_t>(9, 0)});
        check(encode(nine).empty(), "9 humans not encodable");
        check(encode(frames(0, 1, max_frames_batch + 1)).empty(), "frames batch limit");
    }
    {
        LogChunk big;
        big.frames = frames(0, 8, max_log_chunk);
        const std::vector<uint8_t> b = encode(big);
        check(!b.empty() && b.size() <= max_message_bytes, "largest log chunk fits in 64 KB");
        check(decode(b).has_value(), "largest log chunk decodes");
        std::vector<uint8_t> oversized(max_message_bytes + 1, 0);
        oversized[0] = 21;
        check(rejects(oversized), "oversized message rejected before parsing");
    }

    // Room codes and invite links.
    check(parse_room_code("k7mpq2") == std::string("K7MPQ2"), "lower-case code");
    check(parse_room_code(" K7M-PQ2 ") == std::string("K7MPQ2"), "displayed code with dash");
    check(parse_room_code("https://road-rash.archit.xyz/?room=K7MPQ2") == std::string("K7MPQ2"),
          "invite link");
    check(parse_room_code("https://road-rash.archit.xyz/?x=1&room=k7mpq2#top") == std::string("K7MPQ2"),
          "invite link with other parameters");
    check(!parse_room_code("K7MPQ") && !parse_room_code("K7MPQ0") && !parse_room_code(""), "invalid codes");

    // Deterministic simulated network.
    {
        uint64_t now = 0;
        Clock clock = [&] { return now; };
        auto run = [&](bool reliable, NetSim sim) {
            now = 0;
            auto [a, b] = loopback_pair(sim, reliable, clock);
            std::vector<int> got;
            for (int i = 0; i < 400; ++i) {
                const uint8_t m[2] = {uint8_t(i), uint8_t(i >> 8)};
                a->send(m);
                now += 5;
                while (auto p = b->poll()) got.push_back((*p)[0] | (*p)[1] << 8);
            }
            now += 1000;
            while (auto p = b->poll()) got.push_back((*p)[0] | (*p)[1] << 8);
            return got;
        };
        const NetSim chaos{20, 120, 0.2, 0.05, 42};
        const auto r1 = run(false, chaos), r2 = run(false, chaos);
        check(r1 == r2, "unreliable loopback is reproducible");
        check(r1.size() > 280 && r1.size() < 360, "unreliable loopback loses about 20%");
        bool reordered = false;
        for (size_t i = 1; i < r1.size(); ++i) reordered |= r1[i] < r1[i - 1];
        check(reordered, "unreliable loopback reorders");
        const auto rel = run(true, chaos);
        bool in_order = rel.size() == 400;
        for (size_t i = 0; i < rel.size(); ++i) in_order &= rel[i] == int(i);
        check(in_order, "reliable loopback delivers everything in order");
        now = 0;
        auto [a, b] = loopback_pair({50, 50, 0, 0, 1}, true, clock);
        const uint8_t one[1] = {7};
        a->send(one);
        now = 49;
        check(!b->poll(), "not delivered before its latency");
        now = 50;
        check(b->poll().has_value(), "delivered at its latency");
        a->close();
        check(b->status() == ChannelStatus::closed, "closing one end closes the pair");
    }
    return check.finish("net_protocol");
}
