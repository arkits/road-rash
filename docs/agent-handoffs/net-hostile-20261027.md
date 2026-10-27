# net-hostile-20261027

Task: plan 34 Task 11 Step 2, hostile input against `rr-netd`.

Baseline: `a4af434` (origin/main). Worktree `/Users/archit/Dev/rr/.p34-hostile-20261027`
(detached, pushed to `main`), build dirs `build-hostile` (Debug) and
`build-hostile-san` (ASan+UBSan, server tests only). Logs:
`/Users/archit/Dev/rr/.plan34-logs/hostile/`.

Chosen because the Task 10 platform slice is held by another live session
(sentinel PID 7921, uncommitted edits in `.plan34-impl-20261026`, see
`net-platform-*.md`). This slice avoids its claimed files except two
non-overlapping hunks, listed below for the integrator.

## Changed files

- `server/room.hpp`, `server/room.cpp`, `server/sealer.cpp`
- `tests/server_hostile_test.cpp` (new), `tests/net_hostile_ws_test.cpp` (new)
- `tests/server_room_test.cpp` (two assertions, see "Behaviour changes")
- `cmake/net_tests.cmake` (registers `server_hostile`, `net_hostile_ws`)

Overlap with the Task 10 session's uncommitted work: `server/room.cpp`
(their one line is in `on_hello`; mine are in `handle`, `on_join`, `on_hash`,
`on_result`, `step_race`, `health_json`) and `cmake/net_tests.cmake` (my
registrations follow `net_ws` and `server_room`). Both should merge cleanly.

## Gaps found and fixed

Each is a client-visible effect on *other* seats, or unbounded server work,
caused by one hostile connection:

1. **Chat/lobby fan-out flood ends other players' races.** `net::Client` fails
   when 128 events are queued. One connection could send its full 480-message
   burst of `Chat` (or `PickCharacter`/`VoteCourse`/`Ready`, each broadcasting a
   `RoomState`) and every other client received hundreds of events in one frame.
   Fix: a second per-connection token bucket for fan-out messages (`Chat`,
   `PickCharacter`, `VoteCourse`, `Ready`, `RoomCreate`, `RoomJoin`), 4/s with a
   burst of 8 (`ServerConfig::fanout_per_s/fanout_burst`); excess is dropped and
   counted in `fanout_limited`.
2. **A lying duel partner ends the honest player's race.** On a hash mismatch the
   server told the "minority" `Desync`, and the client treats `Desync` as fatal.
   With two seats the tie was broken toward the lowest slot, so a lying slot 0
   always won. Fix: without a strict majority nobody is told; the desync is
   still counted, logged (`strict_majority:false`) and kept in the race record.
3. **Replayed hashes re-trigger whole-log catch-ups.** A hash for a tick already
   decided re-ran the vote and re-sent the entire race log to the "minority" each
   time (amplification), and off-cadence or negative ticks grew the record map.
   Fix: only ticks `>= 0`, `<= sealed`, on the client cadence
   (`(tick + 1) % hash_every == 0`, `hash_every = 60` like `net::Rollback`) and
   not yet decided are accepted; the rest count as `invalid`.
4. **A future `Result` starts the finish timeout for everyone.** Results must now
   name a sealed tick (`0..sealed`), and the 60 s finish timeout starts only once
   at least half of the connected seats have reported. Honest clients all report
   the same deterministic terminal tick, so the timeout is only a backstop.
5. **One report made an "official" table.** `Official.agreed` was cleared only by
   reports that disagreed, so a lone (possibly lying) result with the other
   seat still connected came out official. Now any seat still connected without
   a result makes the table unofficial; departed seats do not count.
6. **Signed overflow in the sealer.** `first_tick + i` near `INT32_MAX` was UB;
   the sealer now computes ticks in 64 bits.
7. **Unbounded spectators.** `ServerConfig::max_spectators = 16` per room; the
   17th gets `Reject{room_full}`.
8. **Silently ignored traffic is now counted.** Server-to-client message types sent
   by a client (`Welcome`, `Reject`, `RoomListReply`, `RoomState`, `ChatRelay`,
   `Start`, `Frames`, `Desync`, `Official`, `LogChunk`) count as `invalid`;
   `Input` from a connection outside any room counts as `wrong_seat_inputs`.
   `/net/health` now also reports `invalid`, `fanout_limited` and
   `wrong_seat_inputs`.

## Behaviour changes to existing tests

`server_room`: the two-seat desync now expects *no* `Desync` to either seat (fix 2),
and uses check tick 59 instead of 60 (fix 3). Its `Result` tick changed from 900
(never sealed in that test) to 90 (fix 4). `net_mano` now runs the race clock past
its result tick before sending results (fix 4). These are deliberate rule
changes, not golden updates.

## Evidence

- `server_hostile` (in-process, virtual clock): lobby fan-out flood (400 messages
  → at most 8 reach others, queues far below 128, flooder stays connected and the
  bucket refills), spectator cap and spectate/leave loop, three-seat race with a
  hostile seat 0 sending wrong-slot inputs (spectator, stranger), extreme ticks,
  forged server messages, oversized/truncated payloads, 1000 fuzzed messages
  under the rate limit, off-cadence/negative/future hashes, a strict-minority lie
  (only the liar is told, replay ignored), future/negative results across the
  finish timeout. Afterwards every sealed word of both honest seats equals what
  they sent, present and on time, over 3700+ ticks. Plus a duel tie (neither
  told), a 5000-message flood (only the flooder closed), and direct sealer checks
  at `INT32_MAX - 3` / `INT32_MIN`.
- `net_hostile_ws` (real WebSocket sockets): text frame, 200 fuzzed binary frames
  and a 128 KB frame from a raw socket without Hello, server-only messages and
  roomless race messages from a hello'd attacker, a 20 000-message flood. Honest
  chat still flows, no forged message reaches an honest client, both seats held.
  In-process run counters: malformed 167, rate_limited 32, invalid 5,
  wrong_seat_inputs 1.
- Same test against the real `build-hostile/server/rr-netd` binary on
  127.0.0.1:18431 (`RR_NETD_URL=ws://127.0.0.1:18431/net`): passed;
  `/net/health` reported malformed 167, rate_limited 481, invalid 5,
  wrong_seat_inputs 1, and 0 connections after the run. Log:
  `/Users/archit/Dev/rr/.plan34-logs/hostile/{external,rr-netd}.log`. The
  process was stopped; no listener remains.

## Residual risks (not fixable without a server-side simulation)

- In a duel, one player can still claim a finish at a sealed tick and start the
  60 s cutoff (one report is half the room). The results are then unofficial
  (`server_hostile` asserts this).
- A colluding majority can outvote an honest minority's hash.
- The server sees Nginx's address, not the client's, so there is no per-IP
  connection or room cap; a client opening many connections can fill
  `max_rooms`. Cloudflare's edge limits are the current mitigation.

## Checks run

- `cmake -S . -B build-hostile -DCMAKE_BUILD_TYPE=Debug`, `cmake --build build-hostile --parallel`:
  pass.
- `ctest --test-dir build-hostile --output-on-failure -j6` (first full run, before
  fixes 4b/5): 120/121; `net_mano` failed on the new sealed-tick result rule (its
  fixture reported tick 100 without running the race clock). Fixture updated.
- After the final edits: `ctest --test-dir build-hostile -R "server_|net_"`: 12/12
  pass (net_protocol, net_ws, net_hostile_ws, server_sealer, server_room,
  server_hostile, net_client, net_mano, net_presentation, net_frontend, net_rtc,
  net_deploy). The remaining 109 tests do not link `rr-netd-core` and passed in
  the full run. Disc tests ran (analysis symlinks to the main checkout).
- ASan+UBSan (`-fno-sanitize-recover=undefined`, `build-hostile-san`):
  server_hostile, server_room, server_sealer, net_mano clean.
  net_hostile_ws aborts in **third-party** libdatachannel v0.24.6
  `src/impl/wstransport.cpp:390` (misaligned `uint64_t` store writing the 64-bit
  length of a > 65535-byte frame; client side, our 128 KB attack frame). Benign on
  ARM64/x86-64; not patched. With only `-fno-sanitize=alignment`
  (`build-hostile-san2`) net_hostile_ws is clean.
- No C++ LSP in this client; compiler and tests were the fallback.

## Remaining work for the integrator

- Tick plan 34 Task 11 Step 2 and copy the evidence into `docs/VERIFICATION.md`,
  `docs/PROGRESS.md`, and the deviations list in `docs/MULTIPLAYER.md` (tie rule,
  fan-out limit, spectator cap). Those shared docs are claimed by the Task 10
  session, so they were not edited here.
- Task 11 Step 1 (soak) and Step 3 (docs) remain.

## Live processes / held resources

None. Ports 18431/18432 were used only during the external run.
