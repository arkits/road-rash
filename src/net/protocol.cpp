// Plan 34 Task 4: wire codecs. Every field is range-checked on decode, strings
// are length-prefixed and limited per field, and a message with trailing
// bytes is rejected, so encode(decode(x)) == x for every accepted x.
#include "net/protocol.hpp"

#include <algorithm>
#include <type_traits>

namespace rr::net {
namespace {

class Writer {
  public:
    std::vector<uint8_t> out;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        u16(uint16_t(v));
        u16(uint16_t(v >> 16));
    }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void u64(uint64_t v) {
        u32(uint32_t(v));
        u32(uint32_t(v >> 32));
    }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void bytes(std::span<const uint8_t> b) { out.insert(out.end(), b.begin(), b.end()); }
    void str8(const std::string& s) { // callers guarantee s.size() <= 255
        u8(uint8_t(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
    void str16(const std::string& s) {
        u16(uint16_t(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
};

class Reader {
  public:
    explicit Reader(std::span<const uint8_t> b) : b_(b) {}
    bool ok() const { return ok_; }
    bool done() const { return ok_ && pos_ == b_.size(); }
    uint8_t u8() {
        if (!need(1)) return 0;
        return b_[pos_++];
    }
    uint16_t u16() {
        const uint16_t lo = u8();
        return uint16_t(lo | uint16_t(u8()) << 8);
    }
    uint32_t u32() {
        const uint32_t lo = u16();
        return lo | uint32_t(u16()) << 16;
    }
    int32_t i32() { return int32_t(u32()); }
    uint64_t u64() {
        const uint64_t lo = u32();
        return lo | uint64_t(u32()) << 32;
    }
    bool boolean() {
        const uint8_t v = u8();
        if (v > 1) ok_ = false;
        return v == 1;
    }
    template <size_t N> void bytes(std::array<uint8_t, N>& a) {
        if (!need(N)) return;
        std::copy_n(b_.begin() + std::ptrdiff_t(pos_), N, a.begin());
        pos_ += N;
    }
    std::string str8(size_t max) { return str(u8(), max); }
    std::string str16(size_t max) { return str(u16(), max); }
    void fail() { ok_ = false; }
    template <class T> T range(T v, T lo, T hi) {
        if (v < lo || v > hi) ok_ = false;
        return v;
    }

  private:
    std::string str(size_t n, size_t max) {
        if (n > max || !need(n)) {
            ok_ = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(b_.data() + pos_), n);
        pos_ += n;
        return s;
    }
    bool need(size_t n) {
        if (!ok_ || b_.size() - pos_ < n) {
            ok_ = false;
            return false;
        }
        return true;
    }
    std::span<const uint8_t> b_;
    size_t pos_ = 0;
    bool ok_ = true;
};

// Display names and chat: printable single-byte text (Windows-1252 as the
// original's 60-byte slots), no control characters.
bool text_ok(const std::string& s, size_t max, bool allow_empty) {
    if (s.size() > max || (!allow_empty && s.empty())) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        const auto u = uint8_t(c);
        return u >= 0x20 && u != 0x7f;
    });
}

bool options_ok(const RoomOptions& o) {
    return o.level <= 4 && o.ai <= 2;
}

void put(Writer& w, const RoomOptions& o) {
    w.u8(o.level);
    w.u8(o.ai);
    w.boolean(o.cars);
    w.boolean(o.is_public);
}
RoomOptions get_options(Reader& r) {
    RoomOptions o;
    o.level = r.range<uint8_t>(r.u8(), 0, 4);
    o.ai = r.range<uint8_t>(r.u8(), 0, 2);
    o.cars = r.boolean();
    o.is_public = r.boolean();
    return o;
}

void put_frames(Writer& w, const Frames& f) {
    w.i32(f.first_tick);
    w.u16(uint16_t(f.frames.size()));
    w.u8(f.humans);
    for (const SealedFrame& s : f.frames) {
        w.u8(s.present);
        w.u8(s.late);
        for (uint16_t word : s.words) w.u16(word);
    }
}
Frames get_frames(Reader& r, int max_count) {
    Frames f;
    f.first_tick = r.i32();
    const int count = r.range<int>(r.u16(), 1, max_count);
    f.humans = r.range<uint8_t>(r.u8(), 1, max_seats);
    if (!r.ok()) return f;
    const uint8_t mask = uint8_t((1u << f.humans) - 1u);
    f.frames.resize(size_t(count));
    for (SealedFrame& s : f.frames) {
        s.present = r.u8();
        s.late = r.u8();
        if ((s.present | s.late) & ~mask) r.fail();
        s.words.resize(f.humans);
        for (uint16_t& word : s.words) word = r.u16();
    }
    return f;
}
bool frames_ok(const Frames& f, int max_count) {
    if (f.frames.empty() || int(f.frames.size()) > max_count || f.humans < 1 || f.humans > max_seats)
        return false;
    const uint8_t mask = uint8_t((1u << f.humans) - 1u);
    return std::all_of(f.frames.begin(), f.frames.end(), [&](const SealedFrame& s) {
        return s.words.size() == f.humans && !((s.present | s.late) & ~mask);
    });
}

bool valid(const Message& m) {
    return std::visit(
        [](const auto& v) -> bool {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Hello>)
                return v.protocol == protocol_version && text_ok(v.alias, max_alias, false) &&
                       v.platform <= Platform::bot;
            else if constexpr (std::is_same_v<T, Welcome>)
                return v.room_code.empty() || valid_room_code(v.room_code);
            else if constexpr (std::is_same_v<T, Reject>)
                return v.reason >= RejectReason::protocol && v.reason <= RejectReason::kicked;
            else if constexpr (std::is_same_v<T, RoomCreate>)
                return options_ok(v.options);
            else if constexpr (std::is_same_v<T, RoomJoin>)
                return valid_room_code(v.code);
            else if constexpr (std::is_same_v<T, RoomListReply>)
                return int(v.rooms.size()) <= max_room_list &&
                       std::all_of(v.rooms.begin(), v.rooms.end(), [](const RoomListEntry& e) {
                           return valid_room_code(e.code) && text_ok(e.host, max_alias, true) &&
                                  e.level <= 4 && e.humans <= max_seats && e.status <= RoomStatus::racing;
                       });
            else if constexpr (std::is_same_v<T, RoomState>)
                return valid_room_code(v.code) && options_ok(v.options) && v.status <= RoomStatus::racing &&
                       v.host_seat < max_seats && (v.your_seat < max_seats || v.your_seat == 0xff) &&
                       std::all_of(
                           v.seats.begin(), v.seats.end(),
                           [](const SeatInfo& s) {
                               return text_ok(s.alias, max_alias, true) && s.character <= 8 && s.vote <= 5;
                           });
            else if constexpr (std::is_same_v<T, PickCharacter>)
                return v.character >= 1 && v.character <= 8;
            else if constexpr (std::is_same_v<T, VoteCourse>)
                return v.course >= 1 && v.course <= 5;
            else if constexpr (std::is_same_v<T, Chat>)
                return (v.slot < 8 || v.slot == 0xff) && text_ok(v.text, max_chat_text, false);
            else if constexpr (std::is_same_v<T, ChatRelay>)
                return v.seat < max_seats && v.tick >= -1 && text_ok(v.text, max_chat_text, false);
            else if constexpr (std::is_same_v<T, Start>)
                return v.course >= 1 && v.course <= 5 && options_ok(v.options) && !v.humans.empty() &&
                       int(v.humans.size()) <= max_seats &&
                       (v.your_seat < max_seats || v.your_seat == 0xff) &&
                       std::all_of(v.humans.begin(), v.humans.end(), [](const StartHuman& h) {
                           return h.seat < max_seats && h.character >= 1 && h.character <= 8 && h.bike < 15 &&
                                  text_ok(h.alias, max_alias, false);
                       });
            else if constexpr (std::is_same_v<T, RtcOffer> || std::is_same_v<T, RtcAnswer>)
                return !v.sdp.empty() && v.sdp.size() <= max_sdp;
            else if constexpr (std::is_same_v<T, RtcCandidate>)
                return v.candidate.size() <= max_candidate && v.mid.size() <= 64;
            else if constexpr (std::is_same_v<T, Input>)
                return !v.words.empty() && int(v.words.size()) <= max_input_batch && v.first_tick >= 0;
            else if constexpr (std::is_same_v<T, Frames>)
                return v.first_tick >= 0 && frames_ok(v, max_frames_batch);
            else if constexpr (std::is_same_v<T, Ack>)
                return v.sealed_tick >= -1;
            else if constexpr (std::is_same_v<T, Hash> || std::is_same_v<T, Desync>)
                return v.tick >= 0;
            else if constexpr (std::is_same_v<T, Result>)
                return v.tick >= 0 && v.outcome >= Outcome::racing && v.outcome <= Outcome::left &&
                       v.place < 16;
            else if constexpr (std::is_same_v<T, Official>)
                return int(v.rows.size()) <= max_seats &&
                       std::all_of(v.rows.begin(), v.rows.end(), [](const OfficialRow& o) {
                           return o.seat < max_seats && o.outcome >= Outcome::racing &&
                                  o.outcome <= Outcome::left && o.place < 16;
                       });
            else if constexpr (std::is_same_v<T, LogChunk>)
                return v.frames.first_tick >= 0 && frames_ok(v.frames, max_log_chunk);
            else
                return true;
        },
        m);
}

void encode_body(Writer& w, const Message& m) {
    std::visit(
        [&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Hello>) {
                w.u16(v.protocol);
                w.u64(v.sim_fingerprint);
                w.bytes(v.data_digest);
                w.u8(uint8_t(v.platform));
                w.str8(v.alias);
                w.boolean(v.seat_token.has_value());
                if (v.seat_token) w.bytes(*v.seat_token);
            } else if constexpr (std::is_same_v<T, Welcome>) {
                w.u32(v.client_id);
                w.u64(v.server_time_ms);
                w.str8(v.room_code);
                w.bytes(v.seat_token);
            } else if constexpr (std::is_same_v<T, Reject>) {
                w.u8(uint8_t(v.reason));
            } else if constexpr (std::is_same_v<T, RoomCreate>) {
                put(w, v.options);
            } else if constexpr (std::is_same_v<T, RoomJoin>) {
                w.str8(v.code);
                w.boolean(v.spectate);
            } else if constexpr (std::is_same_v<T, RoomListReply>) {
                w.u8(uint8_t(v.rooms.size()));
                for (const RoomListEntry& e : v.rooms) {
                    w.str8(e.code);
                    w.str8(e.host);
                    w.u8(e.level);
                    w.u8(e.humans);
                    w.u8(uint8_t(e.status));
                }
            } else if constexpr (std::is_same_v<T, RoomState>) {
                w.str8(v.code);
                put(w, v.options);
                w.u8(uint8_t(v.status));
                w.u8(v.host_seat);
                w.u8(v.your_seat);
                for (const SeatInfo& s : v.seats) {
                    w.u8(uint8_t((s.occupied ? 1 : 0) | (s.connected ? 2 : 0) | (s.ready ? 4 : 0)));
                    w.u8(s.character);
                    w.u8(s.vote);
                    w.u16(s.ping_ms);
                    w.str8(s.alias);
                }
            } else if constexpr (std::is_same_v<T, PickCharacter>) {
                w.u8(v.character);
            } else if constexpr (std::is_same_v<T, VoteCourse>) {
                w.u8(v.course);
            } else if constexpr (std::is_same_v<T, Chat>) {
                w.u8(v.slot);
                w.str8(v.text);
            } else if constexpr (std::is_same_v<T, ChatRelay>) {
                w.u8(v.seat);
                w.i32(v.tick);
                w.str8(v.text);
            } else if constexpr (std::is_same_v<T, Start>) {
                w.u32(v.race_id);
                w.u32(v.seed);
                w.u8(v.course);
                put(w, v.options);
                w.i32(v.start_tick);
                w.u16(v.countdown);
                w.u8(v.your_seat);
                w.u8(uint8_t(v.humans.size()));
                for (const StartHuman& h : v.humans) {
                    w.u8(h.seat);
                    w.u8(h.character);
                    w.u8(h.bike);
                    w.str8(h.alias);
                }
            } else if constexpr (std::is_same_v<T, RtcOffer> || std::is_same_v<T, RtcAnswer>) {
                w.str16(v.sdp);
            } else if constexpr (std::is_same_v<T, RtcCandidate>) {
                w.str16(v.candidate);
                w.str8(v.mid);
            } else if constexpr (std::is_same_v<T, Input>) {
                w.i32(v.first_tick);
                w.u8(uint8_t(v.words.size()));
                for (uint16_t word : v.words) w.u16(word);
            } else if constexpr (std::is_same_v<T, Frames>) {
                put_frames(w, v);
            } else if constexpr (std::is_same_v<T, Ack>) {
                w.i32(v.sealed_tick);
            } else if constexpr (std::is_same_v<T, Ping>) {
                w.u32(v.t);
            } else if constexpr (std::is_same_v<T, Pong>) {
                w.u32(v.t);
                w.i32(v.server_tick);
                w.u32(v.server_ms);
            } else if constexpr (std::is_same_v<T, Hash>) {
                w.i32(v.tick);
                w.u64(v.h);
            } else if constexpr (std::is_same_v<T, Desync>) {
                w.i32(v.tick);
            } else if constexpr (std::is_same_v<T, Result>) {
                w.i32(v.tick);
                w.u8(uint8_t(v.outcome));
                w.u8(v.place);
                w.u64(v.h);
            } else if constexpr (std::is_same_v<T, Official>) {
                w.boolean(v.agreed);
                w.u8(uint8_t(v.rows.size()));
                for (const OfficialRow& o : v.rows) {
                    w.u8(o.seat);
                    w.u8(uint8_t(o.outcome));
                    w.u8(o.place);
                }
            } else if constexpr (std::is_same_v<T, LogChunk>) {
                w.u32(v.race_id);
                w.boolean(v.last);
                put_frames(w, v.frames);
            } else if constexpr (std::is_same_v<T, Ready>) {
                w.boolean(v.ready);
            }
        },
        m);
}

std::optional<Message> decode_body(uint8_t type, Reader& r) {
    switch (type) {
    case 1: {
        Hello v;
        v.protocol = r.u16();
        v.sim_fingerprint = r.u64();
        r.bytes(v.data_digest);
        v.platform = Platform(r.range<uint8_t>(r.u8(), 0, uint8_t(Platform::bot)));
        v.alias = r.str8(max_alias);
        if (r.boolean()) {
            SeatToken t{};
            r.bytes(t);
            v.seat_token = t;
        }
        return v;
    }
    case 2: {
        Welcome v;
        v.client_id = r.u32();
        v.server_time_ms = r.u64();
        v.room_code = r.str8(room_code_length);
        r.bytes(v.seat_token);
        return v;
    }
    case 3:
        return Reject{RejectReason(r.u8())};
    case 4:
        return RoomCreate{get_options(r)};
    case 5: {
        RoomJoin v;
        v.code = r.str8(room_code_length);
        v.spectate = r.boolean();
        return v;
    }
    case 6:
        return RoomList{};
    case 7: {
        RoomListReply v;
        const int n = r.range<int>(r.u8(), 0, max_room_list);
        for (int i = 0; i < n && r.ok(); ++i) {
            RoomListEntry e;
            e.code = r.str8(room_code_length);
            e.host = r.str8(max_alias);
            e.level = r.u8();
            e.humans = r.u8();
            e.status = RoomStatus(r.range<uint8_t>(r.u8(), 0, 2));
            v.rooms.push_back(std::move(e));
        }
        return v;
    }
    case 8:
        return RoomLeave{};
    case 9: {
        RoomState v;
        v.code = r.str8(room_code_length);
        v.options = get_options(r);
        v.status = RoomStatus(r.range<uint8_t>(r.u8(), 0, 2));
        v.host_seat = r.u8();
        v.your_seat = r.u8();
        for (SeatInfo& s : v.seats) {
            const uint8_t flags = r.range<uint8_t>(r.u8(), 0, 7);
            s.occupied = flags & 1;
            s.connected = flags & 2;
            s.ready = flags & 4;
            s.character = r.u8();
            s.vote = r.u8();
            s.ping_ms = r.u16();
            s.alias = r.str8(max_alias);
        }
        return v;
    }
    case 10:
        return PickCharacter{r.u8()};
    case 11:
        return VoteCourse{r.u8()};
    case 12: {
        Chat v;
        v.slot = r.u8();
        v.text = r.str8(max_chat_text);
        return v;
    }
    case 13: {
        ChatRelay v;
        v.seat = r.u8();
        v.tick = r.i32();
        v.text = r.str8(max_chat_text);
        return v;
    }
    case 14: {
        Start v;
        v.race_id = r.u32();
        v.seed = r.u32();
        v.course = r.u8();
        v.options = get_options(r);
        v.start_tick = r.i32();
        v.countdown = r.u16();
        v.your_seat = r.u8();
        const int n = r.range<int>(r.u8(), 1, max_seats);
        for (int i = 0; i < n && r.ok(); ++i) {
            StartHuman h;
            h.seat = r.u8();
            h.character = r.u8();
            h.bike = r.u8();
            h.alias = r.str8(max_alias);
            v.humans.push_back(std::move(h));
        }
        return v;
    }
    case 15:
        return RtcOffer{r.str16(max_sdp)};
    case 16:
        return RtcAnswer{r.str16(max_sdp)};
    case 17: {
        RtcCandidate v;
        v.candidate = r.str16(max_candidate);
        v.mid = r.str8(64);
        return v;
    }
    case 18: {
        Input v;
        v.first_tick = r.i32();
        const int n = r.range<int>(r.u8(), 1, max_input_batch);
        if (r.ok()) {
            v.words.resize(size_t(n));
            for (uint16_t& word : v.words) word = r.u16();
        }
        return v;
    }
    case 19:
        return get_frames(r, max_frames_batch);
    case 20:
        return Ack{r.i32()};
    case 21:
        return Ping{r.u32()};
    case 22: {
        Pong v;
        v.t = r.u32();
        v.server_tick = r.i32();
        v.server_ms = r.u32();
        return v;
    }
    case 23: {
        Hash v;
        v.tick = r.i32();
        v.h = r.u64();
        return v;
    }
    case 24:
        return Desync{r.i32()};
    case 25: {
        Result v;
        v.tick = r.i32();
        v.outcome = Outcome(r.u8());
        v.place = r.u8();
        v.h = r.u64();
        return v;
    }
    case 26: {
        Official v;
        v.agreed = r.boolean();
        const int n = r.range<int>(r.u8(), 0, max_seats);
        for (int i = 0; i < n && r.ok(); ++i) {
            OfficialRow o;
            o.seat = r.u8();
            o.outcome = Outcome(r.u8());
            o.place = r.u8();
            v.rows.push_back(o);
        }
        return v;
    }
    case 27: {
        LogChunk v;
        v.race_id = r.u32();
        v.last = r.boolean();
        v.frames = get_frames(r, max_log_chunk);
        return v;
    }
    case 28:
        return Ready{r.boolean()};
    case 29:
        return RtcClose{};
    default:
        return std::nullopt;
    }
}

} // namespace

std::vector<uint8_t> encode(const Message& m) {
    if (!valid(m)) return {}; // never put an out-of-range message on the wire
    Writer w;
    w.u8(message_type(m));
    encode_body(w, m);
    if (w.out.size() > max_message_bytes) return {};
    return std::move(w.out);
}

std::optional<Message> decode(std::span<const uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > max_message_bytes) return std::nullopt;
    Reader r(bytes.subspan(1));
    std::optional<Message> m = decode_body(bytes[0], r);
    if (!m || !r.done() || !valid(*m)) return std::nullopt;
    return m;
}

std::optional<uint16_t> peek_hello_protocol(std::span<const uint8_t> bytes) {
    if (bytes.size() < 3 || bytes[0] != 1) return std::nullopt;
    return uint16_t(bytes[1] | bytes[2] << 8);
}

bool valid_room_code(std::string_view code) {
    return code.size() == room_code_length && std::all_of(code.begin(), code.end(), [](char c) {
               return room_code_alphabet.find(c) != std::string_view::npos;
           });
}

std::optional<std::string> parse_room_code(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' ||
                             text.back() == '\r' || text.back() == '/'))
        text.remove_suffix(1);
    if (const size_t q = text.find("room="); q != std::string_view::npos) {
        text = text.substr(q + 5);
        if (const size_t end = text.find_first_of("&#"); end != std::string_view::npos)
            text = text.substr(0, end);
    }
    std::string code;
    for (char c : text) {
        if (c == '-' || c == ' ') continue; // "ABC-DEF" as shown on screen
        code.push_back(c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c);
    }
    if (!valid_room_code(code)) return std::nullopt;
    return code;
}

} // namespace rr::net
