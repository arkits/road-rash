# Plan 34: Mano-a-mano online multiplayer

> **Status (October 26, 2026):** Active and part of the definition of done (owner decision, October 26, 2026). Supersedes [plan 09](09-multiplayer.md). The original netcode is traced in [../MULTIPLAYER.md](../MULTIPLAYER.md). The owner also decided:
> - multiplayer does **not** need to reproduce the original's netcode or transports;
> - it must work **over the internet** with no LAN discovery or port forwarding;
> - the owner's VPS (`agnee`, see [../WEB_HOSTING.md](../WEB_HOSTING.md)) is the server.
>
> This replaces the October 26 draft of this plan (original wire format, Wine interop, LAN ports).

> **For agentic workers:** Read [00-INDEX.md](00-INDEX.md), this whole plan and [../MULTIPLAYER.md](../MULTIPLAYER.md) first. Use superpowers:subagent-driven-development or superpowers:executing-plans. Steps use `- [ ]` checkboxes. Claim and serialize shared files (AGENTS.md):
> - `src/race.*`, `src/race_schedule.cpp`, `src/traffic.*`, `src/track_ring.*`;
> - `src/race_screen.cpp`, `src/game_main.cpp`, `src/frontend.*`;
> - `CMakeLists.txt`.
>
> VPS changes (Dokku app, Nginx route, firewall) are owner-approved here only for the items listed in Task 8. Anything else on `agnee` needs a fresh request.

**Goal:** Two to eight people play Road Rash races together over the internet:
- from macOS, Android, Linux, Windows and the browser, in any mix;
- by sharing a room code or invite link;
- in the original mano-a-mano menus and rules (level, character, course vote, AI bikers, cars, F1–F8 chat, results);
- with a race that feels as responsive as single player and comes out identical on every screen.

---

## Architecture

### Overview

```
 client (Mac / Android / browser / Linux / Windows)          rr-netd on agnee (no game data, no sim)
 ┌──────────────────────────────────────────┐   inputs   ┌──────────────────────────────────────┐
 │ Front end ── Race screen ── Rollback ────┼──────────► │ rooms, roster, options, chat         │
 │                              engine      │            │ 60 Hz tick clock per race            │
 │                                │         │ ◄──────────┼ input sealer: one authoritative      │
 │                     deterministic Race   │   sealed   │   input timeline per race            │
 │                     (every rider, car,   │   frames   │ race log: reconnect, spectate,       │
 │                      pedestrian, cop)    │            │   desync check                       │
 └──────────────────────────────────────────┘            └──────────────────────────────────────┘
        wss:// on 443 via Cloudflare + Nginx (lobby, control, fallback race traffic)
        WebRTC DataChannel, UDP direct to the VPS IP (race traffic when available)
```

Each client (macOS, Android, browser, Linux, Windows) has three layers:

| Layer | What it does |
| --- | --- |
| Front end | Original menus: rooms, vote and chat |
| Race screen | Draws the latest predicted tick; HUD, camera and sound for the local rider |
| Rollback engine | Holds the input timeline and per-tick snapshots, predicts other players' inputs, re-simulates; drives the shared deterministic `Race` |

Each client talks to the server over a channel:
- WebSocket over TLS (`wss://`, port 443) carries the lobby, control and fallback traffic;
- a WebRTC DataChannel (UDP, unreliable and unordered) carries the race inputs and frames.

The server, `rr-netd` on the VPS `agnee`, runs as a Dokku app behind Nginx. Cloudflare proxies its WebSocket traffic. It holds:
- rooms, roster, options and chat;
- a 60 Hz tick clock per race;
- the input sealer, which publishes the authoritative input timeline;
- a race log (inputs and hashes) used for reconnect, spectators and desync checks.

It holds **no game data** and runs **no simulation**.

### Decisions

**1. The race is the existing deterministic simulation, run identically by every client.**
- The port's simulation is integer-only, 60 Hz, single-RNG and already verified bit-exact against the original.
- Every client runs the **whole race**: every human, AI rider, car, pedestrian and cop.
- Only **inputs** cross the network: one 16-bit control word per human per tick.

The consequences:
- every screen agrees on who hit whom, every crash, every car and the finish order;
- AI and traffic need no replication or ownership hand-off;
- a modified client can only send inputs, never positions;
- bandwidth is a few KB/s.

**2. Netcode: rollback against a server-sealed input timeline.**
- **Prediction:** each client simulates ahead of the server by about half the round trip plus jitter. It uses its own real input and, for others, their last known input (repeat-last).
- **Sealing:** for each tick the server *seals* a frame: every human's input for that tick. An input that arrives after the deadline is replaced by that player's previous input and flagged.
- **Correction:** when a sealed frame differs from what a client predicted, the client restores its snapshot of the first wrong tick and re-simulates to the present. This includes the client's own input, if the server replaced a late one.

Because the server owns the timeline:
- a slow or vanished player never stalls the others;
- there is no host advantage and no host migration;
- the log is a complete, replayable record of the race.

This is the model fighting and racing games use (GGPO-style rollback with a server-authoritative input stream).

**3. Transport: one `Channel` interface, two implementations.**
- **WebSocket (TLS, port 443) first.** It works from every platform and network, passes Cloudflare, and needs no new firewall rule. It carries all lobby and control messages, and race traffic until a DataChannel is up.
- **WebRTC DataChannel to the server second.** It is unreliable and unordered (`maxRetransmits = 0`), so a lost packet never delays newer inputs (no TCP head-of-line blocking).
  - The server is a WebRTC peer with a public IP. It offers its own host candidate, and clients behind any NAT connect out to it, so no STUN, TURN or peer-to-peer is needed.
  - Signalling (SDP and candidates) travels over the already-open WebSocket.
  - When UDP is blocked, the race simply stays on the WebSocket; rollback absorbs the extra jitter.
- **Library: [libdatachannel](https://github.com/paullouisageneau/libdatachannel)** (MPL-2.0, C++17). It has a WebSocket client and server plus WebRTC DataChannels on macOS, Linux, Windows and Android, so client and server share one library. In the browser, the same messages use the browser's own `WebSocket` and `RTCPeerConnection` through Emscripten JS glue (or `datachannel-wasm`, which mirrors the libdatachannel API).
- **Pin the version** and add its licence and dependency licences (libjuice, usrsctp, the TLS library) to `third_party/` and the user guide (00-INDEX code rules).

**4. Server: `rr-netd`, C++20 in this repository (`server/`).**
- It shares the message codecs in `src/net/` with the clients, so client and server cannot disagree on bytes.
- A Rust server was considered on October 26, 2026 and rejected by the owner: it would need a second codec, a spec-plus-vectors agreement layer and a different WebRTC stack from the clients. Do not reopen it.
- It is a single process with rooms in memory and a 60 Hz sealing timer per racing room.
- Deployment is a Dokku app `road-rash-net` on `agnee`, reached at `wss://road-rash.archit.xyz/net` through the existing Nginx and Cloudflare. There is no new DNS name and no new certificate.
- WebRTC UDP goes **directly** to the VPS IP (`5.161.142.20`), not through Cloudflare, which does not proxy arbitrary UDP. It uses a small fixed UDP port range that the owner opens in Task 8.

**5. Identity and rooms: no accounts.**
- **Display name:** the player's mano-a-mano alias.
- **Rooms:**
  - a room has a short code (6 characters, ambiguity-free alphabet, about 2^30 codes) and an invite link `https://road-rash.archit.xyz/?room=CODE`. Native clients accept the code;
  - a room is public or unlisted, like the original's "Public games" and "Unlisted games". Public rooms appear in the Join list with the original columns Host / Level / Avail / Status.
- **Seats:** each player gets a random 128-bit **seat token** for reconnecting. It is kept in memory and in the client's settings for the life of the room.

**6. Compatibility gate.**

The simulation must be bit-identical on every client. The handshake therefore carries:
- the protocol version;
- a **simulation fingerprint**: a hash of the build's `src/` simulation sources plus a self-test, a fixed input log run for N ticks with a hash of the result, computed at start-up;
- the **reference data digest** (the disc EXE and data hashes the client verified; already known to every build through `reference_hashes.inc`).

A room only admits clients whose three values match the host's. The game data never leaves the client.

**7. Single player stays untouched.**
- All multiplayer code is behind `RaceConfig::net`. When it is empty, `make verify-sim`, the oracle goldens and offline replays stay bit-identical.
- The multi-human generalisation in Task 2 must reduce exactly to today's behaviour when there is one human.

### The hard part: a simulation with several centres

The original simulation is **player-centred**:
- the track ring of built slices surrounds the player;
- scheduler LOD intervals depend on distance from the camera;
- traffic spawns relative to the camera;
- police spawn relative to the player;
- `Race` treats `riders_[0]` as "the player".

The original's networked version avoided this: each machine simulated its own neighbourhood and replicated state. With every client simulating the whole race, all of those rules must become deterministic functions of **shared** state only. The rule:

> **No simulation input may depend on which client is running it.** Camera, HUD, sound perspective, rendering and the local player index are *views*, derived from the shared state and never fed back into it.

Concretely (Task 2): every player-centred rule is generalised to a **set of humans**:
- one ring window per human, merged;
- LOD distance to the nearest human;
- traffic spawn zones around each human, using the original networked gates `0x0044f0d0`/`0x0044df60` and density ×4 as the starting rule;
- police targeting the nearest eligible human.

With one human, each generalisation must reduce to the current code path; the offline goldens prove that.

### Race flow

1. **Lobby.**
   - The host picks the level, AI bikers (None / Limited / Full), cars (on/off) and public/unlisted.
   - Players join and pick characters; the roster announcements use the original strings 402–408.
   - Players vote on the course ("Choose a race. %d/%d players have chosen.").
   - F1–F8 chat works in the lobby.
2. **Start.**
   - The server sends `Start{race_id, seed, course, level, options, roster[slot → alias, character, bike], start_tick}`. It also sends the server tick at which the green light comes, after the original networked countdown of 0x168 ticks.
   - Every client builds the race through `make_race_setup` with the same `NetRaceConfig`. Every client then holds the same initial state, which the first hash exchange confirms.
3. **Race.** The server seals ticks. Clients predict, roll back and render. F1–F8 chat messages are server-relayed, timestamped events shown as the original shows them; they are not simulation input.
4. **Leaving and reconnecting.**
   - A player who disconnects becomes **AI-driven from a sealed tick**. The server flags it in the frame, so the switch happens on the same tick for everyone.
   - A player who reconnects within 60 s with their seat token gets the race log and fast-forwards (no rendering, no sound) to the live tick, then takes the rider back at a sealed tick.
   - Spectators and late joiners use the same catch-up path.
5. **Finish.**
   - Each human's outcome (finished #n, wrecked, busted) appears as the original announcements 427–429.
   - The race ends when every human is done, or 60 s after the last human still racing falls more than one lap of ticks behind the leader. That timeout is a native rule, chosen because the original's "you are the master" wait has no host here.
   - The server collects each client's final result hash; matching hashes make the results "official" (441).
   - Mano-a-mano results have no career effect (original rule). Only first place counts as won (`0x00416010`). They play the normal result-screen videos: the Task 1 trace found no result path that plays `DUEL`.

### Rollback engine, in detail

**Input timeline.**
- Local input is sampled once per simulated tick into a ring.
- An optional local input delay of 0–3 ticks trades responsiveness for fewer rollbacks. It is adaptive, and capped by a user setting.
- Each client packet carries the latest unacknowledged inputs (up to 16 ticks), so a lost packet is repaired by the next one with no retransmit.

**Clock.**
- Ping/pong over the active channel measures round trip and offset.
- The client keeps its prediction lead at about half the round trip plus the p95 jitter plus one tick.
- It corrects drift by **time dilation**: 59–61 simulated ticks per second, never jumps.

**Snapshots.**
- `Race`, `TrafficSystem`, `PedestrianSystem`, `TrackRing` and the RNG are saved each tick into a ring of `max_rollback + 1` slots (default 16 ticks, about 267 ms).
- A snapshot is a flat byte copy of the mutable state. Assets and immutable tables are referenced, never copied.
- Restoring rebinds the internal pointers.

**Re-simulation.**
- When sealed frames arrive, compare them with the predictions. At the first mismatch, restore and re-simulate to the current tick in one frame.
- If the client falls behind the rollback window, it stalls rendering of new ticks until sealed frames catch up, and slows its clock.

**Side effects.**
- Sound events from re-simulated ticks are deduplicated by `(tick, source, event)`. An event that was already played and is still predicted is not replayed. An event that disappears after a correction is allowed to finish, as is standard for rollback.
- Cutscenes, results and chat use only sealed ticks.

**Presentation.** The screen shows the latest predicted tick. Remote riders can be smoothed *in the renderer only* (plan 21 interpolation) to hide corrections; the simulation state is never smoothed.

**Desync detection.**
- Every 60 sealed ticks, each client sends a 64-bit hash of its sealed-tick snapshot, and the server compares them.
- On a mismatch, the server:
  - records the race log and the hashes (kept 7 days on the VPS);
  - tells clients, which report it through the existing Sentry integration;
  - resynchronises the minority clients by log fast-forward.

  A desync is always treated as a bug to fix, never as acceptable.

### Messages (binary, little-endian; `src/net/protocol.hpp`)

| Message | Direction | Channel | Content |
| --- | --- | --- | --- |
| `Hello` | C→S | WS | protocol, simulation fingerprint, data digest, platform, alias, seat token (if reconnecting) |
| `Welcome` / `Reject{reason}` | S→C | WS | client id, server time, room (if reconnecting) |
| `RoomCreate{level, ai, cars, public}` / `RoomJoin{code}` / `RoomList` / `RoomLeave` | C→S | WS | lobby actions |
| `RoomState` | S→C | WS | code, options, seats (alias, character, vote, ready, ping), host |
| `PickCharacter`, `VoteCourse`, `Chat{slot 0..7 or text}` | C→S | WS | lobby and race events (text ≤ 60 bytes, as the original) |
| `Start` | S→C | WS | see race flow |
| `RtcOffer` / `RtcAnswer` / `RtcCandidate` | both | WS | DataChannel signalling |
| `Input{first_tick, count ≤ 16, words[]}` | C→S | DC or WS | unacknowledged local inputs |
| `Frames{first_tick, count, per tick: present mask, late mask, words[]}` | S→C | DC or WS | sealed frames, repeated until acknowledged |
| `Ack{sealed_tick}` | C→S | DC or WS | highest contiguous sealed tick held |
| `Ping{t}` / `Pong{t, server_tick}` | both | DC and WS | clock sync |
| `Hash{tick, h64}` / `Desync{tick}` | both | WS | determinism check |
| `Result{tick, outcome, place, h64}` / `Official{table}` | both | WS | race end |
| `LogChunk` | S→C | WS | reconnect and spectate catch-up |

Every decoder is bounds-checked and fuzzed, and drops (and counts) malformed messages; nothing from the network is trusted. Server limits:
- 8 players per room;
- a configurable room cap;
- per-connection message rate limits;
- a 64 KB maximum message size;
- idle and lobby timeouts.

**Bandwidth estimate** at 60 Hz with 8 players, before transport overhead:
- client→server: about 40 B per packet, about 2.4 KB/s;
- server→client: about 30 B per sealed tick plus repeats, about 3 KB/s.

### Gameplay rules kept from the original networked mode

The following are kept, because they are gameplay rather than transport:
- the countdown 0x168;
- traffic density ×4 and the spawn gates around remote riders;
- the heartbeat handicap rubber band (`0x00442f60`), recomputed deterministically inside the simulation every 0x39 ticks;
- no qualification bits; only first place is a win;
- the mano menus, strings and chat slots.

The following are dropped as transport artefacts, and documented as deviations:
- AI ownership hand-off;
- snapshot replication;
- `DAT_0049bce8` shared draws;
- modem, IPX and LAN discovery;
- host relay.

---

## Tasks

### Task 1: Original mano-a-mano gameplay rules

**Files:** Update `docs/MULTIPLAYER.md`, `docs/REVERSE_ENGINEERING.md`.

- [x] **Step 1: Static traces.** Record each with its address, from disassembly and exports:
  - the networked grid count and slot layout in `0x00405600`;
  - AI biker counts for None / Limited / Full;
  - how the `0x00442f60` handicap byte is applied to riders;
  - the chat display in a race (where and how long);
  - the finish, wreck and bust flow when other humans are still racing (strings 427–430, 441–442);
  - the `DUEL` video trigger;
  - which restroom options are locked in mode 3 (`0x00444fb0`).
- [x] **Step 2: Capture if needed.** (Not needed: every rule was traced statically, October 26.) Only if a rule stays ambiguous, capture it from two original instances under Wine, using WSOCK32 IAT hooks in `oracle/aweman_proxy.c` that remap the ports per instance. This was the old Task 1; it is now optional evidence.
- [x] **Step 3:** Commit: `docs: original mano-a-mano gameplay rules`.

**Done (October 26, 2026):** traces in [../MULTIPLAYER.md](../MULTIPLAYER.md) "Gameplay rules the port keeps". Corrections: `DUEL` is not a mano result video, so Task 7 plays the normal outcome videos; strings 427–429 are unreferenced, so their use is a native announcement.

**Acceptance:** each rule that Task 2 or Task 7 uses has an address or a capture behind it.

---

### Task 2: Multi-human deterministic race

**Files:**
- Modify: `src/race.hpp`, `src/race.cpp`, `src/race_schedule.cpp`, `src/race_setup.*`, `src/traffic.*`, `src/track_ring.*`, `src/police.*`, `src/pedestrians.*`, `src/hud.*`, `src/race_screen.cpp`
- Create: `tests/race_multi_test.cpp`

**Interfaces:**
```cpp
namespace rr {
struct NetRaceConfig {                      // empty humans = offline, bit-identical to today
    std::vector<HumanSeat> humans;          // slot, character, bike, alias; slot 0..7
    int ai_bikers = 2;                      // 0 none, 1 limited, 2 full (Task 1 counts)
    bool cars = true;
    uint32_t seed = 0;                      // srand for the race (replaces local seeding)
};
// RaceConfig gains `NetRaceConfig net;`.
// Race::step takes one control word per human, in slot order; offline passes one.
void Race::step(std::span<const uint32_t> controls);
int Race::human_rider(int slot) const;      // view helper; never read by the simulation
}
```

- [x] **Step 1: Inventory.** (Done October 26: "Task 2 notes" below.) List every place the simulation reads "the player": `riders_[0]` special cases, camera distance, ring centre, traffic and police anchors, HUD-coupled state, `set_demo_hold`. Use `rg` and LSP references. Record the list and the generalisation chosen for each in this plan's Task 2 notes and in REVERSE_ENGINEERING.md.
- [ ] **Step 2: Separate view from simulation.** (Partly done October 26: the simulation reads the view's seat nowhere; `bike()`/`camera()` still mean seat 0 and the camera still follows rider 0, which is a deterministic shared centre until C1 below.) Move camera and HUD perspective, sound listener and render-only state out of the simulation path, so the simulation never reads them. Offline goldens must stay bit-identical; run `make verify-sim` and the oracle tests.
- [ ] **Step 3: Generalise to a set of humans** (Done October 26, all classes: A, B, C1 one ring window per seat, C2 nearest-window LOD and every-window parking/wake, C3 one traffic window per seat with its own timers, slice cursor and 16-car pool partition, retirement/teardown outside every window, C4 police anchored on the human whose think or ring window spawns it with every human's relationship row, D nearest human for the AI, E per-human outcome.) (the "several centres" rule above). One human must take exactly the old code path, re-proved by the goldens.
- [ ] **Step 4: Multiplayer gameplay rules.** (Done: grid rows and A.I. counts, the in-simulation handicap, AI hand-over at a tick, per-human outcome; the 0x168 countdown is the client's tick mapping (`countdown_step` for race ticks < 0x168). Traffic ×4 and the `0x0044df60` gates landed with C3.) Grid layout and AI counts (Task 1), countdown 0x168, traffic ×4 and the spawn gates, the in-simulation handicap, a departed human handing over to the AI brain at a given tick, and per-human outcome without ending the race.
- [ ] **Step 5: Tests.** (`tests/race_multi_test.cpp` covers the first three bullets plus C1/C3/C4 cases; every offline test and `make verify-sim` unchanged.)
  - Two and eight humans with scripted inputs produce identical per-tick field hashes across two runs.
  - Swapping which slot the *view* follows changes no simulation hash.
  - A human handing over to AI at tick T is deterministic.
  - Every offline test and golden is unchanged.
- [ ] **Step 6:** Commit: `race: multi-human deterministic races`.

---

#### Task 2 notes: inventory of player-centred simulation reads (October 26, 2026)

Found with `rg 'riders_\[0\]|player|camera'` over `src/race*.cpp`,
`src/traffic.cpp`, `src/track_ring.cpp`, `src/police.cpp`,
`src/pedestrians.cpp` (baseline `796f251`). Classes and the generalisation
chosen for each (one human must reduce to the old path):

| Class | Sites | Generalisation |
| --- | --- | --- |
| **A. Who is driven by controls** | `Race::step` (`step_rider(..., i == 0)`), `think_half`/`run_bike_node`/`run_half_node`/`move_bike_live` `player` flags (`race_schedule.cpp:305/394/454`), nitro charge (`:366`), crash recovery controls, fight start from controls (`race.cpp:3645`), `set_demo_hold` | per-rider human slot; `step(span)` gives each human its own word; AI riders keep the AI branch; demo hold applies to rider 0 only offline |
| **B. "Local human" gates with simulation effects** | horn `random() % 10` on car contact (`race.cpp:765`), player-only grunt/crash sound blocks in the contact code (`:1106/1126/1155/1255/1419/1635/1832/2006/2078`), swish events (`:3852`), pedestrian contact (`race_pedestrians.cpp:541`) | the original's "local human" (`+0x324 == 0`) becomes **any human**: every client runs the same draws. Sound events carry their rider; the view keeps only the local human's (presentation filter, never fed back) |
| **C1. Camera / ring centre** | `camera_position()` follows rider 0's half (`race.cpp:4090`); `update_camera`, `camera_heading_`; ring advance by camera delta (`:4372`); ring setup and DZHR spawns (`race_pedestrians.cpp:50/84/114/273/313`) | **one ring window per human, merged**: slices are built once when they enter the union of the human windows, in ascending human-slot order, so every client consumes ring draws identically; with one human the union is the old window. The camera itself becomes a per-view object outside the simulation |
| **C2. Scheduler LOD and parking** | `live_interval(..., ring_camera, ..., player_object)` (`race_schedule.cpp:181`), window parking (`:193`), parked pace vs the player (`:515`), wake gating on the player's crash (`:531`), standings `pace_band` from the player's place (`:280`) | distance to the **nearest human** ring camera; `player_object` = any human's bike/half; parked pace and pace band against the nearest human; crash gate on that human |
| **C3. Traffic** | `TrafficSystem::step(road, camera+0x100, camera+0x5100, ..., player pos/lat/speed)` (`race.cpp:4380`), countdown step (`:4175/4202`), dual-roadway route = player's route (`:726/736/4311`) | spawn window per human with the original networked gates (`0x0044f0d0`/`0x0044df60`: no spawn between a human and the nearest other human ahead within 0x4600 / behind within 0x2d00, no odd kinds) and density ×4 when `net` is set; car route per car, not the player's |
| **C4. Police** | `spawn_cop` from rider 0 (`race.cpp:3429`), player relationship toward the cop (`:3495`), chase target | target and spawn anchor = nearest eligible human; relationship rows for every human |
| **D. AI reading the player** | replan reads the player's place (`race.cpp:2582`) and distance (`:2633`), AI attack stand-in strikes the player (`:3648`) | nearest human by track position (ties: lower slot) |
| **E. Race end** | `finished()`/`Race::step` early return on rider 0, outcome on rider 0 | per-human outcome; the race runs until every human is done (plus the plan's 60 s rule); `finished()` keeps meaning "the view's human" |
| **View only (no change to the sim)** | `bike()`, `route()`, `camera()`, `horizon_scroll`, `player_in_tunnel`, `last_target`, HUD, sound listener | become functions of a view slot (`Race::human_rider(slot)`), default slot 0 |

### Task 3: Snapshots, rollback core and cross-platform determinism

**Files:**
- Create: `src/net/rollback.hpp`, `src/net/rollback.cpp`, `src/race_state.hpp`, `src/race_state.cpp`
- Create: `tests/race_state_test.cpp`, `tests/rollback_test.cpp`, `tools/determinism_matrix.sh`
- Modify: `src/race.*`, `src/traffic.*`, `src/pedestrians.*`, `src/track_ring.*`, `.github/workflows/` (CI)

**Interfaces:**
```cpp
namespace rr {
struct RaceState { std::vector<uint8_t> bytes; };          // flat mutable state, no pointers
void save_state(const RaceInstance&, RaceState&);          // Race + traffic + pedestrians + ring + RNG
void load_state(RaceInstance&, const RaceState&);          // rebinds internal pointers
uint64_t state_hash(const RaceState&);
namespace net {
class Rollback {
  public:
    Rollback(RaceInstance&, int humans, int local_slot, int max_rollback = 16);
    void local_input(int32_t tick, uint32_t word);
    void sealed(int32_t tick, std::span<const uint32_t> words, uint8_t late_mask);
    int advance_to(int32_t tick);                          // returns ticks re-simulated
    int32_t confirmed_tick() const;
    uint64_t confirmed_hash() const;                       // hash at the newest sealed tick
};
}}
```

- [x] **Step 1: Save/load tests first.** (`tests/race_state_test.cpp`: crashes, walking, police, pedestrians, a split, the finish, two humans; warmed saves allocate nothing.)
  - Run N ticks, save, run M, load, run M again: the field dumps and RNG are identical. Cover crashes, walking, police, pedestrians, splits and finish.
  - Saving never allocates in steady state.
- [x] **Step 2: Implement `save_state`/`load_state`.** (`src/race_state.*`. Implementation choice: a `RaceState` holds copies of the mutable objects rather than a byte blob; immutable data is shared (`routes_`, hazard/DZHR tables, course) and the large mutable tables are copy-on-write (`CowVector` statics and ring terrain, ring DZHR lists), restore copies back into the same live objects so cross-object pointers stay valid. The ring terrain the simulation reads moved from the shared scene into the race (`Race::apply_ring_terrain`); the scene keeps it for rendering. Sizes/times in PERFORMANCE.md.) Split each system into immutable data (referenced) and mutable state (copied). Record the size and the save/load time per tick.
- [x] **Step 3: Rollback tests.** (`tests/rollback_test.cpp`, `src/net/rollback.*`: depths 0–16 with two humans, 0/1/4/8/16 with eight, late substitutions and an AI hand-over; equal final and per-60-tick hashes.) Scripted two- and eight-human races with random late remote inputs give the same final hash as a straight run of the sealed inputs, for every rollback depth up to the maximum.
- [ ] **Step 4: Cost budget.** (Mac done: worst frame p50 0.22 ms, max 0.78 ms. MANGMI AIR X **blocked**: no device.) Measure simulation step, save and load in the profiler's `sim` phase on the Mac and the MANGMI AIR X. The budget is a worst-case frame (16 re-simulated ticks plus saves) of **≤ 4 ms on the device**. If the budget is missed, the agreed fallbacks are, in order: save every 2nd tick, a smaller window with adaptive input delay, then profiling the step. Record the outcome in PERFORMANCE.md.
- [ ] **Step 5: Cross-platform determinism.** (October 26: `tools/determinism_matrix.sh` + `rr-determinism`. Bit-identical per-60-tick hashes on macOS ARM64 (AppleClang), Linux x86-64 (Debian trixie GCC, Docker) and wasm32 (Emscripten 6.0.10 under Node), for the synthetic 8-human race and the 8-human disc race (6000 ticks). CI: `.github/workflows/determinism.yml` runs macOS, Linux and wasm against `tests/data/determinism_synthetic.txt`. No difference needed fixing. Open: Windows (plan 33), Android ARM64 (**blocked**: no device or emulator image).)
  - `tools/determinism_matrix.sh` runs a fixed multi-human input log on macOS ARM64, Linux x86-64 (CI), wasm32 (Node, CI), Windows x86-64 (with plan 33) and Android ARM64 (device or emulator). It compares per-tick hashes.
  - Fix every difference at its source. Known risks: `std::sort` of equal keys (use stable or tie-broken comparisons), signed shifts and overflow (UBSan), integer widths, and uninitialised padding in snapshots.
  - Wire the macOS, Linux and wasm runs into CI.
- [x] **Step 6:** Commit (Steps 1–3 and 5 except Windows/Android; Step 4 device part blocked): `net: race snapshots, rollback core, determinism matrix`.

---

### Task 4: Protocol, codecs and channels

**Files:**
- Create: `src/net/protocol.hpp`, `src/net/protocol.cpp`, `src/net/channel.hpp`, `src/net/ws_channel.cpp`, `src/net/loopback_channel.cpp`, `fuzz/fuzz_net_protocol.cpp`, `tests/net_protocol_test.cpp`
- Add: `third_party/libdatachannel` (pinned, as a CMake subproject or FetchContent with a hash), licence files
- Modify: `CMakeLists.txt`, `docs/USER_GUIDE.md` (third-party notice)

**Done (October 26, 2026):** `rr-net` (codecs, loopback network; no SDL) always builds; `rr-net-transport` adds libdatachannel v0.24.6 (pinned commit, OpenSSL static on macOS) behind `RR_NET_TRANSPORT` (default on for native desktop). Protocol notes: `Start.your_seat` and Frames words use the **race slot** (humans compacted in seat order); `StartHuman.seat` is the room seat; `Welcome` is also sent when a seat is taken, carrying the seat token; `Ready` and `RoomListReply` were added. The type byte and Hello's protocol field are frozen so old clients get `Reject{protocol}`.

**Interfaces:**
```cpp
namespace rr::net {
// One struct per message in the table above; encode/decode are pure and bounds-checked.
std::optional<Message> decode(std::span<const uint8_t>);   // std::variant of all messages
std::vector<uint8_t> encode(const Message&);
class Channel {                                           // reliable (WS) or unreliable (DC)
  public:
    virtual bool reliable() const = 0;
    virtual void send(std::span<const uint8_t>) = 0;
    virtual std::optional<std::vector<uint8_t>> poll() = 0;  // non-blocking, main thread
    virtual ChannelStatus status() const = 0;
    virtual ~Channel() = default;
};
std::unique_ptr<Channel> ws_connect(const std::string& url);
// Tests: deterministic simulated network with latency, jitter, loss, duplication, reordering.
struct NetSim { int min_ms, max_ms; double loss, duplicate; uint32_t seed; };
std::pair<std::unique_ptr<Channel>, std::unique_ptr<Channel>> loopback_pair(NetSim, bool reliable);
}
```

- [x] **Step 1: Codec tests first.** Every message round-trips. Truncated, oversized, wrong-version and out-of-range messages are rejected. Golden byte vectors for each message are committed as synthetic fixtures.
- [x] **Step 2: Implement codecs and the libdatachannel WebSocket channel.** The library's callbacks fill a lock-protected queue, and the main thread only polls, so the simulation never runs on a network thread.
- [x] **Step 3: Fuzz target** in the plan 22 set and CI. (`fuzz/fuzz_net_protocol.cpp`, seeds from `fuzz-seeds`, run by `tools/fuzz.sh`; the repository has no fuzz CI workflow yet, that is plan 22's open item.)
- [x] **Step 4:** Commit: `net: protocol codecs and WebSocket channel`.

---

### Task 5: Server `rr-netd`

**Files:**
- Create: `server/CMakeLists.txt`, `server/main.cpp`, `server/room.hpp`, `server/room.cpp`, `server/sealer.hpp`, `server/sealer.cpp`, `server/Dockerfile`, `tests/server_room_test.cpp`, `tests/server_sealer_test.cpp`, `tools/net_bots.cpp`

**Built (October 26, 2026):** `server/room.*` (lobby, rooms, race clock, results), `server/sealer.*`, `server/main.cpp` (`/net/health` on a plain HTTP port, JSON logs, `.rrlog` race logs pruned after 7 days), `server/Dockerfile`; `server/CMakeLists.txt` also builds standalone. Rules chosen: a race starts when every seat has a character and a course vote (≥ `min_humans`, default 2); the course is the most-voted, ties go to the host's vote, then the lowest course; a tick is sealed 3 ticks after its time; a flood (a full burst dropped in one second) or 4 throttled seconds close the connection.

- [x] **Step 1: Room tests first** (the server logic is a pure class, driven by in-memory channels):
  - create, join by code, list public rooms;
  - full-room, wrong-fingerprint and wrong-digest rejections;
  - character pick, course vote, chat, start;
  - leave, and the host leaving before start (the next seat becomes host);
  - reconnect by seat token, and idle and lobby timeouts.
- [x] **Step 2: Sealer tests.** Inputs on time, inputs late (sealed as repeat-last with the late flag), a player vanishing (handed to AI at a sealed tick), unacknowledged frames repeated, and hash comparison with desync handling.
- [x] **Step 3: Implement.**
  - One event loop; 60 Hz per racing room from a monotonic clock, catching up after a stall without drift.
  - Limits as in the architecture section.
  - Structured logs, a `/net/health` endpoint, and counters (rooms, players, round trip, late inputs, desyncs).
  - Race logs kept 7 days.
- [x] **Step 4: Headless bots.** (October 26: `net-bots` runs the real `net::Client` + `Rollback` + `Race`; local WebSocket smoke with 2 and 8 clients, shared sealed cutoff and official matching results. Staging follows Task 8.) `tools/net_bots.cpp` connects N scripted clients that run the real `Rollback` + `Race`. It needs the local disc data, as the game does. Use it for end-to-end tests against a local `rr-netd`, and later against staging.
- [x] **Step 5:** Commit (Steps 1–3; Step 4 lands with Task 6): `server: rr-netd rooms and input sealer`.

---

### Task 6: Client netcode in the race

**Files:**
- Create: `src/net/client.hpp`, `src/net/client.cpp`, `tests/net_client_test.cpp`
- Modify: `src/race_screen.cpp`, `src/game_main.cpp`, `src/sound.*`, `src/race_overlay.*`

- [x] **Step 1: In-process end-to-end test.** Two to eight `net::Client` instances and one server `Room` run over `loopback_pair` with round trips of 0/50/150/300 ms, jitter of ±30 ms, and loss of 0/5/20%. Assert:
  - every client's sealed-tick hashes are equal at every check;
  - final results are equal;
  - rollback depth stays within the window, or the client stalls cleanly;
  - there are no unbounded queues.
- [x] **Step 2: Clock sync and time dilation, and adaptive input delay.** Ping/pong on the active channel, bounded 64-sample RTT/jitter history, 59–61 Hz drift correction, adaptive delay capped at 0–3 ticks. See [client handoff](../agent-handoffs/net-client-20261026.md).
- [x] **Step 3: `RaceScreen` integration.** (October 26: room client injection, detached local-seat presentation, confirmed effects/results, chat and live quit prompt; menu launch follows Task 7.)
  - Poll channels once per frame, then advance the rollback to the target tick.
  - Render the newest predicted tick.
  - Deduplicate sound events during re-simulation; trigger results and cutscenes only on sealed ticks.
  - Show F1–F8 chat and the finished/wrecked/busted announcements.
  - Escape opens the quit prompt (string 316) without pausing the race.
- [x] **Step 4: Renderer-only smoothing of remote riders** after corrections, behind a setting, default on.
- [x] **Step 5: Network HUD option:** round trip, rollback depth and late inputs, off by default.
- [x] **Step 6:** Commit: `net: online races in the race screen`.

**Task 6 evidence (October 26):** [screen handoff](../agent-handoffs/net-screen-20261027.md). Debug 115/115 and Release 114/114; final focused Debug presentation/tooling checks pass after the local HUD/strings and sparse-seat changes. Host/Join launch and rematch UI are Task 7, not exposed by this screen API alone. Full-course, platform and production acceptance remain later tasks.

---

### Task 7: Mano-a-mano front end

**Files:** Modify `src/frontend.hpp`, `src/frontend.cpp`, `src/native_screens.*`, `src/native_strings.cpp`, `src/settings.*`, `src/career.cpp`, `src/platform_web.cpp` (invite links).

- [x] **Step 1: Screens.**
  - Enable main-menu item 2.
  - Screen 0x0b keeps the original items, with these mappings:
    - **Host** creates a room (level popup, then native AI bikers / cars / public options in the original popup style);
    - **Join** opens the room list or code entry;
    - **Bulletin Board** is the course vote on screen 0x0c;
    - **Chat Messages** is screen 0x0d;
    - **Restroom** is unchanged;
    - **Disconnect** leaves the room.
  - The original transport popup is removed: there is one transport.
- [x] **Step 2: Native screens in the original menu style.** Room code display and copy, join by code, the public room list (Host / Level / Avail / Status), roster with characters and pings, "connecting…", errors (version mismatch, room full, server unreachable). Strings use native slots. Each replacement is listed in REVERSE_ENGINEERING.md "Not ported or replaced natively".
- [x] **Step 3: Invite links.** The browser reads `?room=` and opens Join. Native clients accept a pasted code or link. Android also handles `https://road-rash.archit.xyz/?room=` app links if the manifest allows.
- [x] **Step 4: Results.** Original mano results: places from the official table, no career change, the result-screen video (not `DUEL`, Task 1), back to screen 0x0b with the room kept for a rematch.
- [x] **Step 5: Tests.** Drive the front end through host → vote → race → results → rematch with the in-process server.
- [x] **Step 6:** Commit: `frontend: online mano-a-mano`.

---

Implementation/evidence: October 27, 2026, [Task 7 handoff](../agent-handoffs/net-frontend-20261027.md). Browser transport compiles; Android has no matching app-link intent filter, so registration and cross-play acceptance remain Task 10. The in-process frontend test uses a scripted agreed cutoff, not a completed full course.

### Task 8: VPS deployment

October 27 live deployment: `road-rash-net` is serving public WSS `/net` and
HTTPS `/net/health`. The restricted CI key, persistent race logs, loopback
Nginx ports and exact-revision checks are installed. Native public bots passed;
[enabled production CI 40534691545](https://github.com/arkits/road-rash/actions/runs/40534691545)
passed two Linux clients with matching hashes/agreed results. Existing web
health/assets and TLS deploy-hook validation passed. See
[live handoff](../agent-handoffs/net-live-20261027.md).
Step 3 stays open until Task 9 provides actual peers/negotiation/fallback;
WebSocket-only results do not establish UDP acceptance. No unused UDP range
was published/opened, and no other app/global proxy/firewall setting changed.

**Owner-approved scope on `agnee`:**
- a new Dokku app `road-rash-net`;
- an Nginx `location /net` WebSocket upgrade proxy in the existing `road-rash` TLS server block;
- one UDP port range for WebRTC (default 40000–40099/udp) opened in the host firewall and published by the container;
- a GitHub Actions deploy key restricted the way `DOKKU_ROAD_RASH_KEY` is.

Nothing else on the server changes.

**Files:** Create `deploy/road-rash-net/` (Dockerfile reference, `app.json` healthcheck, Nginx snippet), `.github/workflows/net-deploy.yml`. Update `docs/WEB_HOSTING.md`.

- [x] **Step 1: Deploy workflow.** Build `rr-netd` (Linux x86-64 release) on pushes to `main` that touch `server/` or `src/net/`. Deploy with `dokku git:from-archive`, as the web app does. Health-check `/net/health` and check the exact Git revision.
- [x] **Step 2: Nginx route.** WebSocket upgrade headers, a long read timeout and a body size limit. Cloudflare passes WebSockets on the proxied hostname. Confirm the TLS renewal hook still validates.
- [ ] **Step 3: WebRTC UDP.**
  - Publish the port range and advertise `5.161.142.20` as the server's host candidate (configurable).
  - Confirm from an outside network that a DataChannel connects.
  - Confirm that the WebSocket fallback works when UDP is blocked; test from a network that blocks UDP, or with a client flag that blocks it.
- [x] **Step 4: Post-deploy smoke.** Two `net_bots` from CI race a short scripted race against production and require equal hashes and official results.
- [x] **Step 5:** Record everything in WEB_HOSTING.md: app, ports, firewall change, rollback procedure. Commit: `deploy: rr-netd on agnee`.

---

### Task 9: WebRTC DataChannel transport

**Files:** Create `src/net/rtc_channel.cpp`, `src/net/rtc_channel_web.cpp`, `web/net_glue.js` (or `datachannel-wasm`). Modify `server/` for the server-side peer, and `src/net/client.cpp` for upgrade and fallback.

- [x] **Step 1: Server peer.** For each client, the server creates a peer connection with one unreliable, unordered DataChannel. Signalling goes over the client's WebSocket. Port range and public IP come from configuration.
- [x] **Step 2: Clients.** Native clients use libdatachannel; the browser uses `RTCPeerConnection` through JS glue. Race traffic moves to the DataChannel once it is open. If it fails within 5 s, or closes mid-race, traffic falls back to the WebSocket without a race-visible event beyond a rollback.
- [x] **Step 3: Measure.** At 5% loss, compare WebSocket and DataChannel on rollback depth p50/p95/p99, late-input rate and stalls. Record the result in VERIFICATION.md. The DataChannel stays default only if it measurably helps.
- [x] **Step 4:** Commit: `net: WebRTC DataChannel race transport`.

---

October 27: local implementation/gates complete; RTC remains opt-in after mixed 5% loss measurements (fewer stalls/late inputs, no p99 improvement, ~2.8x bytes). Native/browser peers, timeout/close fallback, one-port UDP mux and rolling port allocation verified. Live UDP receipt is recorded in [RTC handoff](../agent-handoffs/net-rtc-20261027.md).

### Task 10: Platforms

- [ ] **Step 1: macOS.** Outgoing connections only, so there is no firewall or Local Network prompt. Confirm this on a signed package.
- [ ] **Step 2: Android.**
  - Permissions `INTERNET` and `ACCESS_NETWORK_STATE`.
  - Going to the background in a race keeps the seat for the reconnect window.
  - A Wi-Fi↔cellular switch reconnects with the seat token and fast-forwards.
  - Test on the MANGMI AIR X: record the observed race, lifecycle and reconnect in VERIFICATION.md.
- [ ] **Step 3: Browser (plan 23).** Same rooms and races; cross-play with native in both directions. The game data comes from the existing hosted manifest. The Asyncify main loop polls the channel each frame.
- [ ] **Step 4: Linux and Windows** with plan 33, in the determinism matrix and the cross-play tests.
- [ ] **Step 5:** Commit: `platform: online mano-a-mano on all targets`.

---

### Task 11: Acceptance

- [ ] **Step 1: Soak.**
  - 200 in-process eight-player races under simulated network chaos.
  - A one-hour production session with four real clients (Mac, Android, two browsers) on different networks, one of them cellular.
  - 50 concurrent bot rooms against staging, recording server CPU and memory.
- [ ] **Step 2: Hostile input.** Fuzzed and replayed garbage, wrong-slot inputs, oversized and flooded messages against a local `rr-netd`. Each must be dropped, counted and rate-limited, with no crash and no effect on other seats.
- [ ] **Step 3: Docs.**
  - USER_GUIDE.md: how to host, join, invite and chat; troubleshooting.
  - MULTIPLAYER.md: a "Port design" pointer to this plan and the deviations list.
  - PROGRESS.md, CURRENT_STATUS.md / `agent-status.json`, and VERIFICATION.md (commands, outcomes and evidence paths).
- [ ] **Step 4:** Commit: `docs: online mano-a-mano acceptance`.

**Plan acceptance:**
- all offline goldens are unchanged;
- the determinism matrix is green on every shipped platform;
- the in-process chaos suite and production soak pass with no undetected desync and no stall over 1 s;
- cross-play works across Mac, Android, browser, Linux and Windows;
- the hardware observations are recorded;
- the server runs on `agnee` with the deploy smoke test green.
