// Plan 34 Task 4: online mano-a-mano wire protocol shared by the clients and
// rr-netd. Binary, little-endian, one message per channel payload. Decoders
// are pure, bounds-checked and reject anything malformed or out of range;
// nothing that arrives from the network is trusted.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace rr::net {

inline constexpr uint16_t protocol_version = 1;
inline constexpr size_t max_message_bytes = 64 * 1024; // server and client limit
inline constexpr int max_seats = 8;                    // humans per room
inline constexpr int max_input_batch = 16;             // unacknowledged inputs per packet
inline constexpr int max_frames_batch = 64;            // sealed frames per packet
inline constexpr int max_log_chunk = 1024;             // sealed frames per LogChunk
inline constexpr size_t max_alias = 16;
inline constexpr size_t max_chat_text = 60; // original F1-F8 slot size 0x3c (0x00497408)
inline constexpr size_t room_code_length = 6;
inline constexpr size_t max_sdp = 16 * 1024;
inline constexpr size_t max_candidate = 512;
inline constexpr int max_room_list = 32;

using SeatToken = std::array<uint8_t, 16>;
using DataDigest = std::array<uint8_t, 32>;

enum class Platform : uint8_t { unknown = 0, macos, android, web, linux_desktop, windows, bot };

enum class RejectReason : uint8_t {
    protocol = 1, // protocol version differs
    fingerprint,  // simulation fingerprint differs from the room's
    data_digest,  // reference data digest differs from the room's
    room_full,
    no_such_room,
    room_racing, // race already running; join as spectator only
    rate_limited,
    server_full, // room cap reached
    bad_request,
    timeout,
    kicked,
};

enum class RoomStatus : uint8_t { lobby = 0, full = 1, racing = 2 }; // strings 409-411

// Race outcome reported per human; matches the original outcome codes
// (0x00416010): 2 won, 3 lost (finished, not first), 4 busted, 5 wrecked.
enum class Outcome : uint8_t { racing = 1, won = 2, finished = 3, busted = 4, wrecked = 5, left = 6 };

struct Hello {
    uint16_t protocol = protocol_version;
    uint64_t sim_fingerprint = 0;
    DataDigest data_digest{};
    Platform platform = Platform::unknown;
    std::string alias;
    std::optional<SeatToken> seat_token; // present when reconnecting
    bool operator==(const Hello&) const = default;
};
struct Welcome {
    uint32_t client_id = 0;
    uint64_t server_time_ms = 0;
    std::string room_code;  // non-empty when a seat token re-took a seat
    SeatToken seat_token{}; // the token to keep for this connection's seat
    bool operator==(const Welcome&) const = default;
};
struct Reject {
    RejectReason reason = RejectReason::bad_request;
    bool operator==(const Reject&) const = default;
};
struct RoomOptions {
    uint8_t level = 0;     // 0..4 (Level 1-5)
    uint8_t ai = 2;        // 0 humans only, 1 limited, 2 full (strings 497-503)
    bool cars = true;      // strings 398-399
    bool is_public = true; // "Public games" / "Unlisted games" (426/421)
    bool operator==(const RoomOptions&) const = default;
};
struct RoomCreate {
    RoomOptions options;
    bool operator==(const RoomCreate&) const = default;
};
struct RoomJoin {
    std::string code;
    bool spectate = false;
    bool operator==(const RoomJoin&) const = default;
};
struct RoomList {
    bool operator==(const RoomList&) const = default;
};
struct RoomListEntry {
    std::string code;
    std::string host;
    uint8_t level = 0;
    uint8_t humans = 0;
    RoomStatus status = RoomStatus::lobby;
    bool operator==(const RoomListEntry&) const = default;
};
struct RoomListReply {
    std::vector<RoomListEntry> rooms;
    bool operator==(const RoomListReply&) const = default;
};
struct RoomLeave {
    bool operator==(const RoomLeave&) const = default;
};
struct SeatInfo {
    bool occupied = false;
    bool connected = false;
    bool ready = false;
    uint8_t character = 0; // 0 none, 1..8 menu characters
    uint8_t vote = 0;      // 0 none, 1..5 original course order (CITY, HIWAY, MEDLY, NAPA, CANYN)
    uint16_t ping_ms = 0;
    std::string alias;
    bool operator==(const SeatInfo&) const = default;
};
struct RoomState {
    std::string code;
    RoomOptions options;
    RoomStatus status = RoomStatus::lobby;
    uint8_t host_seat = 0;
    uint8_t your_seat = 0xff; // 0xff spectator
    std::array<SeatInfo, max_seats> seats{};
    bool operator==(const RoomState&) const = default;
};
struct PickCharacter {
    uint8_t character = 1; // 1..8
    bool operator==(const PickCharacter&) const = default;
};
struct VoteCourse {
    uint8_t course = 1; // 1..5
    bool operator==(const VoteCourse&) const = default;
};
struct Chat {
    uint8_t slot = 0xff; // 0..7 = F1-F8 slot, 0xff = free text
    std::string text;    // the message text in both cases (the server never sees the slots)
    bool operator==(const Chat&) const = default;
};
struct ChatRelay {
    uint8_t seat = 0;
    int32_t tick = -1; // sealed race tick, or -1 in the lobby
    std::string text;
    bool operator==(const ChatRelay&) const = default;
};
struct StartHuman {
    uint8_t seat = 0; // 0..7, also the grid row (0x004317a0)
    uint8_t character = 1;
    uint8_t bike = 0;
    std::string alias;
    bool operator==(const StartHuman&) const = default;
};
struct Start {
    uint32_t race_id = 0;
    uint32_t seed = 0;
    uint8_t course = 4; // original order 1..5
    RoomOptions options;
    int32_t start_tick = 0;     // server tick at which race tick 0 is simulated
    uint16_t countdown = 0x168; // networked countdown (0x00442dd0)
    uint8_t your_seat = 0xff;
    std::vector<StartHuman> humans; // seat order, 1..8
    bool operator==(const Start&) const = default;
};
struct RtcOffer {
    std::string sdp;
    bool operator==(const RtcOffer&) const = default;
};
struct RtcAnswer {
    std::string sdp;
    bool operator==(const RtcAnswer&) const = default;
};
struct RtcCandidate {
    std::string candidate;
    std::string mid;
    bool operator==(const RtcCandidate&) const = default;
};
struct Input {
    int32_t first_tick = 0;
    std::vector<uint16_t> words; // 1..16 consecutive ticks
    bool operator==(const Input&) const = default;
};
// One sealed tick: which seats are human-driven (others are AI-driven, i.e. a
// departed human), which inputs were substituted (late or missing), and one
// word per seat of the race (seat order).
struct SealedFrame {
    uint8_t present = 0;
    uint8_t late = 0;
    std::vector<uint16_t> words;
    bool operator==(const SealedFrame&) const = default;
};
struct Frames {
    int32_t first_tick = 0;
    uint8_t humans = 0; // words per frame
    std::vector<SealedFrame> frames;
    bool operator==(const Frames&) const = default;
};
struct Ack {
    int32_t sealed_tick = -1;
    bool operator==(const Ack&) const = default;
};
struct Ping {
    uint32_t t = 0;
    bool operator==(const Ping&) const = default;
};
struct Pong {
    uint32_t t = 0;
    int32_t server_tick = 0;
    uint32_t server_ms = 0;
    bool operator==(const Pong&) const = default;
};
struct Hash {
    int32_t tick = 0;
    uint64_t h = 0;
    bool operator==(const Hash&) const = default;
};
struct Desync {
    int32_t tick = 0;
    bool operator==(const Desync&) const = default;
};
struct Result {
    int32_t tick = 0;
    Outcome outcome = Outcome::racing;
    uint8_t place = 0;
    uint64_t h = 0;
    bool operator==(const Result&) const = default;
};
struct OfficialRow {
    uint8_t seat = 0;
    Outcome outcome = Outcome::racing;
    uint8_t place = 0;
    bool operator==(const OfficialRow&) const = default;
};
struct Official {
    bool agreed = false; // every reported result hash matched ("official", 441)
    std::vector<OfficialRow> rows;
    bool operator==(const Official&) const = default;
};
struct LogChunk {
    uint32_t race_id = 0;
    bool last = false;
    Frames frames; // up to max_log_chunk
    bool operator==(const LogChunk&) const = default;
};
struct Ready {
    bool ready = true;
    bool operator==(const Ready&) const = default;
};

// Additive transport control: a reliable, permanent fallback for this connection.
struct RtcClose {
    bool operator==(const RtcClose&) const = default;
};

using Message =
    std::variant<Hello, Welcome, Reject, RoomCreate, RoomJoin, RoomList, RoomListReply, RoomLeave, RoomState,
                 PickCharacter, VoteCourse, Chat, ChatRelay, Start, RtcOffer, RtcAnswer, RtcCandidate, Input,
                 Frames, Ack, Ping, Pong, Hash, Desync, Result, Official, LogChunk, Ready, RtcClose>;

// Message type byte = variant index + 1 (0 is never valid on the wire).
inline uint8_t message_type(const Message& m) {
    return uint8_t(m.index() + 1);
}

std::vector<uint8_t> encode(const Message&);
std::optional<Message> decode(std::span<const uint8_t>); // rejects Hello of another protocol
// The type byte and Hello's protocol field are frozen across versions, so a
// server can answer an old client with Reject{protocol}.
std::optional<uint16_t> peek_hello_protocol(std::span<const uint8_t>);

// Room codes: 6 characters from an ambiguity-free alphabet (no 0/O, 1/I/L).
inline constexpr std::string_view room_code_alphabet = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
bool valid_room_code(std::string_view);
// Accepts a bare code (any case) or an invite link ".../?room=CODE"; returns
// the normalised upper-case code.
std::optional<std::string> parse_room_code(std::string_view text);

} // namespace rr::net
