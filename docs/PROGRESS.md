# Port progress

The opening inventory below is historical. See [CURRENT_STATUS.md](CURRENT_STATUS.md)
for the current evidence matrix and the dated continuation entries for updates.

Status of the native macOS ARM64 / Android ARM64 port of Road Rash (1996 Windows rerelease). The port is C++20 with SDL3 and uses no Wine or x86 emulation. It is a **playable game** (`rr-game`). The original front end (menus, Thrash and Big Game, characters, bike shop, Schmooze, restroom options, results, saves and credits) leads into races against seven AI opponents on the original courses. Races use the ported bike physics, AI driving, rider contact, combat and free traffic; the full 2400-tick Napa level-1 field and a 1431-tick human capture now match the original tick by tick, while other scenarios remain open in plan 10. All five courses draw with the original scene pipeline (road, hills, buildings, shoulders, scenery, horizon scroll and full dashboard) through the shared race factory (plan 26); the 640×480 output stays bit-identical at render scale 1. There are no police. Effects, engines, the RRA soundtrack and all videos play; race MIDI is not done. Remaining work is planned in [plans/00-INDEX.md](plans/00-INDEX.md) (plans 10–11, 13–31).

Detailed evidence is in [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) and test results in [VERIFICATION.md](VERIFICATION.md). Build and run instructions are in [../README.md](../README.md).

## Status by area

| Area | Status | Where |
| --- | --- | --- |
| Executable analysis (Ghidra, 1,585 functions exported) | Done; types and names not manually validated | `tools/decompile.sh`, `analysis/decompiled/` |
| Disc inventory (341 files, SHA-256) | Done | `tools/inventory.py`, `analysis/inventory.json` |
| Resource container directory (`CRSR`/`LBTR`) | Done; all 13 containers validated | `src/resource.cpp`, `tests/resource_test.cpp` |
| Menu images (`.RRI` JPEG), dashboard and horizon `.BOB`, palette | Done (display only; no scrolling/cropping) | `src/viewer.cpp` |
| Course graph (`NOD `: roads, splits, merges, ends) | Done; all 5 courses | `src/course.cpp` |
| Course path (`HTPR` bend/rise) and lanes (`NALR`) | Done | `src/course.cpp` |
| Road projection (33 slices, original depth table) | Done; integer port | `src/road.cpp`, `tests/road_test.cpp` |
| Split/merge second roadway | Done, with a documented rider-commit assumption | `make_route`, `project_road` |
| Road sides/shoulders (`DLSR`) | Decoded; shoulder widths rendered, heights carried but not drawn | `src/course.cpp`, `src/road.cpp` |
| Terrain profiles (building / mountain / hill) | All 109 sections decoded; stateful iterator matches 864 original slices; live cache/random integration open | `src/terrain.cpp` |
| Other section chunks (`BORR`, `MSRR`, `DZHR`, `CESR`) | Mapped to iterators only | REVERSE_ENGINEERING.md |
| CEL / PLUT and PC mip textures | 42 CEL wrappers, 117 indexed mip blocks match original loader; legacy packet drawing/table selection open | `src/cel.cpp` |
| World rendering, colours, horizon and scenery (`CEL `) | Napa: original scene construction (road, hills, shoulders, buildings, curbs, static scenery, horizon scroll) in the playable game, verified piecewise; split stitching/marked terrain open. The viewer still uses placeholder shading | `src/course_scene.cpp`, `src/world_renderer.cpp` |
| Bike specs (`BIKESPEC.RSC`) | Decoded and applied (`0x00405ea0`) | `src/bike.cpp` |
| Player bike physics | Ported from the executable: throttle, brake, steering, gears, drag, traction, curve drift, movement along the course, vertical motion/jumps; not oracle-verified | `src/bike.cpp`, `src/race.cpp` |
| Walls, crashes, remount | Simplified stand-ins (plan 11 Task 4) | `src/race.cpp` |
| Playable race (`rr-game`) | All five courses, quick-race and career, keyboard/gamepad/touch, countdown, HUD, finish/restart; race loop as `RaceScreen` (plan 28) | `src/game_main.cpp`, `src/race_screen.cpp` |
| Rider sprites (`BIKERS/*.DAT`, colour remaps) | Decoded; drawn in the game; frame choice by inspection | `src/bob.cpp`, `src/game_main.cpp` |
| AI opponents | Racing state ported (targets, curve speed, steering/speed control, re-plan, rubber band); overtaking and fight states not ported | `src/race.cpp` |
| Rider contact | Detection and separation ported; contact response is the bike branch only | `src/race.cpp` |
| Combat | Original live AI attack selection, descriptor/frame hit gates, relationship reactions and animation queues; measured in the 2400-tick Napa field replay. Steal resolver gates/transfer measured in 31 original component cases (October 26); free-running steal capture remains open | `src/race.cpp` |
| Language strings (`TEXT/*.DLL`) | All five languages, original table assembly and continuations | `src/pe_strings.cpp` |
| Menu text | Disc TrueType fonts (Badloc, Futura); GDI hinting not reproduced | `src/text.cpp` |
| Front end (menus, Thrash/Big Game flow, shop, Schmooze, restroom, results, credits) | Ported from the EXE screen tables and handlers; mano-a-mano, jukebox, sound mix and controls screens not ported; not pixel-compared. Native Display/Sound/Pause/Enhancements screens, visual-style presets, single widescreen setting (October 13) | `src/frontend.cpp` |
| Career rules and save files | Prize, fines, repairs, shop, level-up and the original 0xa0-byte save format; seeded original-writer file round-trips byte-exact (`career-save-corpus`), driven-career saves and menu pixels still open | `src/career.cpp` |
| Settings | Registry values as a preference file | `src/settings.cpp` |
| Traffic, police | Traffic spawning/placement ported with the original timers and vehicle library (not oracle-verified); movement/honk/drawing are documented approximations; collisions, DZHR scripted vehicles and police not started. Career races keep the purchased bike; bad `.CAR` degrades to no-traffic (October 13 review follow-ups) | `src/traffic.cpp`, `docs/ACTORS.md` |
| macOS build | Standalone ARM64 `.app`/ZIP with static pinned SDL, system-only dependencies, ad-hoc signature; 35 release tests pass | `tools/build_macos_app.sh` |
| Android build | Debug and optimized development release APKs build/sign; runtime rendering/import/controls still unverified on hardware | `android/` |
| Platform lifecycle and timing | Pause on background/focus loss/controller removal, explicit resume, texture reset recovery, rational fixed-step clock | `src/session.cpp`, `src/game_main.cpp` |
| Multi-touch | Independent finger tracking, cancellation and brake priority | `src/input.cpp`, `tests/input_test.cpp` |
| Menu gamepad navigation | D-pad/buttons and left stick with dead zone/repeat; existing controllers detected; interruption requires stick neutral; physical Android validation pending | `src/input.cpp`, `src/game_main.cpp` |
| Original road oracle | 660 original CITY frames match compared projection fields; indexed frame/palette capture works; other courses/splits unverified | `oracle/`, `tests/oracle_road_test.cpp` |
| Disc import | ISO/folder reader, SHA-256 verification of 341 files, first-run picker/progress UI | `src/importer.cpp`, `src/import_ui.cpp` |
| Android data packaging | Normal APK excludes game data; guarded developer assets are opt-in | `android/app/build.gradle` |
| Audio decoding/mixing | All 72 effects and 14 RRA tracks decode; queued stereo mixer, resampling, pause and device reopening | `src/wav.cpp`, `src/mixer.cpp` |
| Race sounds and soundtrack | Original effect slots, event codes at ported trigger points, engines with Doppler/pan/rpm, volume curve; RRA soundtrack cycling in menus; race MIDI (plan 13), sirens and car engines (plan 11) not done | `src/sound.cpp` |
| Video | AVI scan, Cinepak and IMA: all 59 videos match FFmpeg RGB/audio; native playback via `--video`; frontend selection and hardware A/V sync open | `src/avi.cpp`, `src/cinepak.cpp`, `src/video_player.cpp` |
| Flat polygon raster | Normalization matches 2,048 original calls; full flat path matches 64 indexed fixtures; scene integration open | `src/raster.cpp`, `src/polygon.cpp` |

## Timeline

### October 26, 2026: plan 34 Task 3, snapshots, rollback, determinism matrix

- `src/race_state.*`: `save_state`/`load_state`/`state_hash` over Race,
  traffic, pedestrians and every ring window. Immutable data is shared and
  the large mutable tables are copy-on-write (`src/cow_vector.hpp`), so a
  Release save is ~6 µs and a warmed save allocates nothing. The ring
  terrain the simulation reads now lives in the race (snapshot-able); the
  scene keeps a copy for rendering, and a restarted race no longer inherits
  the previous race's ring terrain.
- `src/net/rollback.*`: prediction (own input, repeat-last for others),
  correction from the first mispredicted sealed tick, a 16-tick window with
  stalling, AI hand-over on sealed ticks, confirmed hashes every 60 ticks.
- `rr-determinism` + `tools/determinism_matrix.sh` + CI workflow: macOS
  ARM64, Linux x86-64 and wasm32 bit-identical (synthetic and disc runs).
- Open: Android/MANGMI AIR X cost and determinism (no device), Windows
  (plan 33).

### October 26, 2026: web canvas fills the play area; widescreen without letterbox

- The web page pinned `#canvas` to a 4:3 box, and `apply_display` reset the
  SDL window to 640x480 x window scale on every frame. A widescreen frame
  (16:10, 16:9, match) was therefore letterboxed inside a 4:3 element that was
  itself pillarboxed: bars on all four sides, and a 1280x960 buffer however
  large the screen.
- `web/style.css`: the canvas now fills the viewport container (`100cqw` x
  `100cqh`). `src/display.cpp` (web only): the SDL window follows the canvas
  CSS box (`emscripten_get_element_css_size`), so the render buffer is CSS size
  x devicePixelRatio and SDL's mouse mapping keeps the same aspect. SDL's own
  resizable/external-size handler resizes during a race; menus re-apply each frame.
- `Game::logical_width()` passes the window size to `logical_width_for`, so
  `Widescreen::match` picks the window's aspect in the race (clamped
  320..426), as menus already did. It previously always used 426. Applies to
  native too. Rendering only: no simulation input.
- Remaining bars are intentional: 4:3 with widescreen off, or wider than 16:9
  (the cap). Videos keep their own aspect through `VideoPlayer::fit`.

### October 26, 2026: GDI text ground truth; all remaining screens captured

- Wine-GDI probe (`tools/textprobe.c`, i686 mingw, headless memory DC)
  recovers the game's LOGFONTs exactly (0x004239e5 table: positive cell
  heights 72/42/36/32/24/20/18/16/15, weight 0 except New Courier Bold 700,
  OUT_TT_ONLY_PRECIS, DEFAULT_QUALITY) and the layout rules, verified
  against 292/292 TEXTMETRIC points and 380/380 ASCII advances:
  `ppem = round(H*upm/cell)`, step down once when the rounded scaled cell
  exceeds H; advance = `round(design*ppem/upm)`; multiline pitch = tmHeight
  (e.g. 33 at 32px Futura Regular — the characters/graphics drift source).
  `src/text.cpp` implements the ppem pitch + advances (bitmaps stay unhinted
  stb); pins tighten to characters 0.80/0.08 (was 0.70/0.15) and graphics to
  the standard 0.12/0.05 (was 0.65/0.10).
- Two Wine acceptance runs (`analysis/frontend/acceptance-20261027/`,
  new scripts, same harness) capture the remaining scripted screens:
  thrash, loading, olleys, showroom, panzer, bulletin (run 1) and
  wrecked/busted/won/lost/penalty/standings (run 2, variants read from the
  trace: Wreck4/Bust4/Win4/Lose1). All pinned in `frontend_test` with the
  item-rect mask: thrash/bulletin/panzer/olleys/loading/wrecked/busted/won/
  lost at standard pins (98-100% exact, 0% big), penalty 100%/0%,
  standings 100%/0% (cover 0.95), showroom 93%/5% (pin 0.85/0.08).
- Fidelity fixes from the goldens: `0x00420470` desc fill follows raw
  `flags & 8` (characters/course/showroom bios draw on the photo; verified
  per-pixel); standings rows/title/darken move to the 0x00421290 handler
  with a bare-screen direct entry, footer from the dynamic slot at the last
  row; showroom Length footer (Demi 24, y=301) with blurb-only desc.
  Open (masked, documented): price/player footers content mapping
  (0x4208d0 bh&1/bh&8 branches, formats 0x46b058/0x46b09c) and exact
  hinted bitmaps.
- `race_cache_test` RGB baselines (scale 2/4) re-recorded: the GDI advance
  rule shifts HUD glyphs by whole pixels; scene bytes
  (`original_scene` hashes) are unchanged. Full gate 106/106 in
  `build-agent-menu-gaps`.
### October 26, 2026: plan 34 Task 2 C3/C4, traffic and police per human

- `TrafficSystem`: one window per human seat (`set_seat_windows`), each with
  its own spawn timers, slice cursor and 16 pool slots (every original
  machine had its own pool); a slice another window already built places no
  intersection again; cars retire and controllers tear down only outside
  every window; LOD from the nearest window. Networked: density ×4, no
  crossing kinds, and the `0x0044df60` ahead/behind gates per window.
- Police: the cop spawns from the human whose think (periodic) or ring
  window (DZHR) fires, clamped to that human's window; every human gets the
  relationship row. AI replan place/distance and the attack stand-in now
  read the nearest human (the earlier note claimed this already; it landed
  here).
- One human: unchanged. Task 2 complete apart from the cross-platform proof
  in Task 3.

### October 26, 2026: plan 34 Task 2 C1/C2, ring windows per human; rr-netd image

- Each human seat has its own `TrackRing` window (`RaceInstance::seat_rings`,
  `Race::add_human_ring`), advanced in seat order from that seat's camera;
  a slice inside another seat's window fires no DZHR actors or statics.
  Scheduler LOD uses the nearest seat's ring camera; parking, end-slice
  parking and waking test every window. One human: unchanged (verify-sim,
  19/19 sim tests).
- `rr-netd` Docker image builds for linux/amd64 (Debian bookworm, GCC 12)
  from the minimal deploy context and serves `/net/health` with its revision.
- Open in Task 2: per-human traffic windows with the networked gates and
  density ×4 (C3), police targeting the nearest human (C4).

### October 26, 2026: plan 34 Task 2 first slice, multi-human races

- `Race::step(std::span<const uint32_t>)`: one control word per human seat;
  `Rider::human`, `Race::human_rider(seat)`, `set_human_driven` (AI
  hand-over at a tick), `human_outcome`/`all_humans_done` (networked: only
  first place wins), per-seat nitro, per-rider busted.
- The original's "local human" gates that draw `rand()` (contact sounds,
  horn, swish) now run for every human; AI and scheduler reads of "the
  player" (replan place/distance, parked pace, pace band, parking) use the
  nearest human.
- `NetRaceConfig`/`net_race_config`/`net_rider_count` in the race factory:
  seats on grid rows (`0x00464218`, `0x00464258` above four humans), A.I.
  rows by entry, the server seed, cars off; the `0x00442f60` handicap inside
  the simulation.
- One-human races take the old path: `make verify-sim` and every sim/disc
  test unchanged. Open in Task 2: merged ring windows, LOD/traffic/police
  centred on every human, traffic ×4 and spawn gates.

### October 26, 2026: plan 34 Tasks 4–5, wire protocol, channels and rr-netd core

- `src/net/protocol.*`: 28 binary messages, bounds-checked, canonical
  encoding, golden vectors (`tests/data/net_protocol_vectors.txt`), fuzz
  target `fuzz_net_protocol`. `src/net/channel.hpp` + deterministic
  `loopback_pair` network (latency/jitter/loss/duplication/reorder) and the
  libdatachannel WebSocket channel (`ws_channel.cpp`, v0.24.6 pinned).
- `server/`: rr-netd lobby/rooms (create, join by code, public list, seats,
  host hand-over, seat-token reconnect, timeouts, rate limits), the input
  sealer (repeat-last + late flag, AI hand-over at a sealed tick, hand-back),
  race clock, frame delivery (once over WS, repeated until acked over
  unreliable channels), desync detection with log catch-up, official
  results, `/net/health`, JSON logs, 7-day race logs, Dockerfile.
- Remaining in these tasks: `tools/net_bots.cpp` (needs the Task 6 client);
  the Linux Docker image build is unverified locally (Docker daemon off) and
  moves to the Task 8 deploy workflow.

### October 26, 2026: plan 34 Task 1, original mano-a-mano gameplay rules

- Static traces (no capture needed) in [MULTIPLAYER.md](MULTIPLAYER.md)
  "Gameplay rules the port keeps" and REVERSE_ENGINEERING.md: rider count per
  A.I. option (Full 15, Limited 8, None = humans), grid rows by network slot
  and the >4-human grid table, the handicap formula and where it applies,
  the six-line race chat log, the networked finish/wreck/bust flow (won only
  for first place; others keep racing), Escape strings 430/316, restroom
  patterns 5/6/7.
- Corrections: `DUEL` is not a mano-a-mano result video (attract/jukebox
  only), and strings 427–429 are unreferenced by the EXE (the port uses them
  as native announcements).
- Remaining: plan 34 Tasks 2–11.
### October 26, 2026: all-screen menu pixel acceptance pinned (plan 13 Task 6 Step 2)

- `frontend_test` replays the Wine frontend-acceptance script from boot
  (main menu, Big Game to characters, Cydney to the street, then direct
  entry to restroom/jukebox/controls/graphics — the golden game states) and
  pins all five remaining goldens with the item-rect-driven mask:
  characters 82% exact / 13% large-diff, restroom 84%/4%, jukebox 85%/4%,
  controls 80%/2% (92% masked: item rows cover the screen), graphics
  75%/7%. Menu-09, fe-main and fe-street stay 98%/98%/96% exact, 0% big.
- Two fidelity fixes from the gaps: a test-only `Restroom/Rest1..6` pin
  (`set_rest_picture_for_test(3)`; the RNG stream still advances once per
  entry) and the `0x00420470` description-box rule — the dark fill repaints
  only when the focused item's flags take it (`flags & 0x20f08`: characters
  bios at flags `0x5` are text on the photo; restroom/jukebox/graphics at
  `0x8` keep the fill). Static disassembly evidence in
  REVERSE_ENGINEERING.md.
- Jukebox/graphics keep the previous (Restroom/Rest3) background exactly as
  the original does (trace cache 1); the stale-bio label column on the
  direct-enter controls golden is a capture-method artifact inside masked
  item rects (real play always reaches `0x12` through the restroom-row
  handler, which refreshes labels). GDI glyph hinting and its knock-on
  desc-box width deltas stay open; looser pins on characters/graphics are
  characterization, not mask weakening. Full gate 106/106 in
  `build-agent-menu-gaps`.

### October 26, 2026: full police pursuit parity (parked, crashed, on-foot, City)

Six Wine captures (Napa/City level 3, seed 1, human parked, `ORDER=1`,
mid-race `FRAMES`, knock variants), all contiguous, all replayed in
`police_test` with one RNG anchor: parked 635/635 cop ticks with RNG/field
to 4730 and arrest 4552; crashed 935/935 with field to 5030 and no arrest
either side (single RNG-state gap at 4716); on-foot runs 605/605 each with
full parity to 4700 and arrests at 4616/4544; City 456/456 with field to
4551 and RNG to 954 (original arrests at 4552, measured but not asserted
while City AI RNG stays open). Sanitizer trees clean.

### October 26, 2026: pixel acceptance extended; background-art gaps measured

- `frontend_test` gains masked original-vs-native checks for the main menu
  (`fe-main.bmp` second golden: 60% masked, 98% exact, 0% large-diff) and the
  Big Game street screen (`fe-career-start.bmp` from the driven-career run,
  item-rect-driven mask: 67% masked, 96% exact, 0% large-diff). All green.
- Characters/restroom/jukebox/graphics goldens are now pinned too — see the
  all-screen entry above. The gap analysis that led there (variant control,
  portrait reconciliation) is preserved in
  `docs/agent-handoffs/menu-text-art-gaps-20261026.md`.

### October 26, 2026: police parked-scenario parity; original-driven save round trip

- `napa-cops-parked-20261026` (Napa level 3, seed 1, human parked, 4731
  contiguous ticks): `police-test` replays with one RNG anchor — RNG and all
  field rows identical to tick 4730, cop position/speed identical on all 635
  pursuit ticks from the 4096 spawn, native arrest on tick 4552 as captured.
  First-tick spare rows 9..14 feed `RaceConfig::spare_levels`, as the harness
  specifies. Crashed/foot/city captures are running next.
- `fe-career.RRS` (Wine frontend acceptance: original Big Game Cydney through
  a City race, saved via restroom UI): `career-test --save-corpus` loads it
  with the real tables (character 3, level 0, bike 2, cash 651), pins
  byte-identical re-serialization, and checks the payload equals the dumped
  `fe-career-saved.state` game state.

### October 26, 2026: tunnel engine variant wired

- `Race::player_in_tunnel()` ports the `0x00434a80` tunnel flag (sub-slice
  terrain type 3); `SoundSystem::update` reads it instead of forcing false,
  so the player engine selects slot 2 (`EngTunne`) in tunnels. Pinned in
  `sound_test`; Debug and ASan/UBSan trees pass.
### October 26, 2026: explicit `<cstdlib>`/`<algorithm>` includes (issue #1)

- Twelve sources used `std::abs`/`std::fill` through transitive includes that
  libc++ 23 (Homebrew LLVM) and MSYS2 clang64 no longer provide, breaking the
  build (GitHub issue #1). Each now includes its own header; no behaviour change.
- Gate: `rr-game` builds with MSYS2 clang64 on Windows (`build-win`, a local
  plan 33 probe); MSVC still fails on `__int128` (plan 33). The full CTest
  suite was not run on this machine.

### October 26, 2026: gameplay and frontend open items closed (police live, car engines, driven saves, menu layout)

- **Live police.** `Race::spawn_cop` runs from the DZHR actor branch and the
  periodic 4096-tick `police_spawn_due` gate; the cop chases through the
  rider think/move path, posts siren events, arrests into `busted_` and the
  BUSTED video/results flow. `police_test` pins arrest boundaries, the
  1024-case original arrest table, pursuit/brain words, the live tick-4096
  spawn/chase/arrest and the on-foot arrest. Original-capture tick parity
  for the cop pursuit remains open (fixtures absent in this tree).
- **Car engines.** `SoundSystem::update` now walks bikes and traffic cars
  together for the two nearest vehicles (`0x004350e0`): bikes keep slots 3/5
  (`EngOther`, rpm route `0x004349c0`/`0x00435030`), cars take slots 4/6
  (`Engcar11`, `0x00435090` Doppler*0x3b route). `Race::traffic()` exposes
  the pool; `sound_test` pins slot names and that a near car is audible in
  the others channel while far vehicles stay silent.
- **Walls/crashes and car/pedestrian branches.** Walls (`0x0043a610`), crash
  entry/motion/recovery (`0x00403d60` family) and rider/car/pedestrian contact
  were already ported; the only `crash()` fallback is the bank-less fixture
  path. Pedestrian pairs are explicitly no-contact, faithful to the
  `0x004368f0` `(cat_a & cat_b & 3) != 0` gate (peds are type `0x20`).
- **Race MIDI, sirens.** `RaceMusicPlayer::play_race` (SBKGRNG1..5 + KLOWN)
  is hooked at race start/stop with silent-no-op fallback; `music_test`
  streams 30 s of SBKGRNG4 audibly. Sirens (slots 30/31, event 19) were
  already wired and stay pinned by `sound_test`.
- **Frontend.** Jukebox, sound mix and controls were already implemented and
  stay pinned by `frontend_test`. Mano-a-mano stays disabled; online
  multiplayer (plan 34) is in scope per the owner's October 26 decision.
- **Driven-career saves.** `career-test --corpus` now drives a real-table
  career (choose Cydney, win City, buy an affordable upgrade, save) and pins
  reload state equality plus byte identity, alongside the seeded
  original-writer `save-seed-20261026.rrs` check.
- **Menu layout.** `frontend_test` now sweeps all 223 original items (2
  scrollable below the fold, 28 hidden placeholders) plus the native screens,
  with main-menu 1x/2x/4x ink checks and the menu-09 masked capture
  (60% masked, 98% exact, 0% large-diff). Full unmasked pixel equality stays
  open on GDI-hinting text shapes.
- Gate: `sound-test`, `career-test` (+`--corpus`, `--save-corpus`),
  `frontend-test`, `police-test`, `crash-test`, `traffic-test`,
  `pedestrian-test`, `car_contact-test`, `music-test`, `race-test` all pass
  in `build-agent-open`.

### October 26, 2026: plan 34 server stays C++

A Rust `rr-netd` was planned and then reverted the same day (owner decision).
The server stays C++20 in `server/`, sharing `src/net/` codecs and
libdatachannel with the clients, as plan 34 Decision 4 says. `00-INDEX.md`
code rules now say C++20, matching `CMakeLists.txt`.

### October 26, 2026: online multiplayer joins the definition of done; plan 34 redesigned

The owner decided two things:
- mano-a-mano multiplayer is part of the definition of done;
- it should be modern internet play through the owner's VPS, not a faithful
  port of the original's LAN/modem netcode.

Plan 34 is rewritten around that decision.

- **Simulation.** Every client runs the whole deterministic race. Only one
  16-bit input word per human per tick crosses the network.
- **Server.** `rr-netd` on `agnee` holds no game data and runs no simulation.
  It runs a 60 Hz clock per race and seals one authoritative input timeline.
- **Clients.** Each client predicts other players' inputs and rolls back to a
  saved snapshot when a sealed frame differs.
- **Transport.**
  - WebSocket over 443 through the existing Cloudflare/Nginx setup carries the
    lobby and fallback race traffic.
  - An unreliable WebRTC DataChannel to the server's public IP carries race
    traffic, using libdatachannel natively and the browser's own APIs on the
    web.
- **Rooms.** Players join by room code or invite link, using the original mano
  menus and gameplay rules.

The hardest piece is turning the player-centred simulation (track ring, LOD,
traffic, police) into a shared one with several centres, without changing
single-player results. Snapshot/rollback cost on Android and cross-platform
bit-identity are measured early, in Task 3. This entry is planning only;
nothing was built.

### October 26, 2026: mano-a-mano traced; multiplayer plan 34

This is a documentation-only session. It covers the original's mano-a-mano
mode, using static analysis of the disc EXE (hash verified), its imports, the
Ghidra exports and the ENU.DLL strings.

- **What it is.** Mano-a-mano is networked multiplayer for up to eight humans
  over TCP/IP (UDP 6996/6995) or IPX, or two over a TAPI modem.
- **How it plays.**
  - Every peer simulates its own rider and the AI riders it owns, and sends
    0x4c-byte snapshot/event frames to the host.
  - The host relays those frames and owns the AI-owner, interest and handicap
    tables.
  - It is not lockstep.
- **New documents.** [MULTIPLAYER.md](MULTIPLAYER.md) holds the findings, each
  labelled traced or inferred. [Plan 34](plans/34-multiplayer.md) replaces plan
  09.
- **Plan 34's approach.**
  - Port that model with the original's wire format.
  - Verify it with a two-instance Wine capture (WSOCK32 hooks in the oracle
    proxy) and with native↔original interop.
  - Ship TCP/IP as "Internet/LAN" on macOS, Android, Linux and Windows; drop
    Modem and IPX.
  - Leave NAT relay and browser play owner-gated.
- **Remaining.** All of plan 34. Whether multiplayer joins the definition of
  done is the owner's decision (plan 15 Task 4). Nothing was built or tested.

### October 25, 2026: Road Rash title-screen launcher

Redesigned the browser launcher (`web/index.html`, `web/style.css`) as a title
screen: skewed orange/yellow Badloc "Road Rash" heading, hazard-tape panel edge,
an animated receding road with grain/scanlines behind it, and the two modes side
by side. Start Game is the dominant yellow slab (about 1.75x the width of Quick
Race) with the new copy "Play original game". Quick Race is a narrower card with
course select and an orange Race button. The hosted-only note "Game files load
automatically as you play and are cached on this device." now sits at the bottom
of the ready view (`#cache-note`, shown by `restoreHostedAssets`). The generic
"Ready to play." status is hidden while the ready view is up; errors still show.
Layout stacks on phones and compacts below 500 px height; reduced-motion disables
the road, entrance and chevron animations. Presentation only; no engine change.

### October 25, 2026: original disc fonts in the browser shell

Replaced the Impact approximation with the actual Badloc and Futura font files
used by `TextRenderer`. After verified disc import/restore, the page loads
`TEXT/BADLOC.DLL`, `FUTR.DLL`, `FUTB.DLL` and `FUTD.DLL` from OPFS into browser
FontFace objects. Badloc headings, Futura Bold controls and Futura body text use
the same local data as the game; no font files are bundled or requested over
the network. Readable system fallbacks remain before disc import or if a browser
cannot load the legacy fonts. Browser smoothing differs from native thresholded
GDI-style rasterisation; this is a font-file match, not a pixel-identical text claim.


### October 25, 2026: browser layout revised around gameplay

Removed the oversized title, promotional slogans, decorative disc and large
below-game sections. The page now uses a compact orange/yellow/charcoal menu
style based on the game's frontend, a 48 px toolbar, on-demand controls and a
viewport-sized game canvas. The 4:3 frame expands to available height/width
without the previous 960 px cap or pointer-coordinate distortion. At
1907×1280 the game measures 1611×1208, with no scrolling or HUD crop.
Only web HTML/CSS and status labels changed; the validated `096bff2` wasm
engine is reused, excluding concurrent OMP simulation probes.


### October 25, 2026: playable browser port and Vercel output

The previous web scaffold was a placeholder. `RR_WEB_BROWSER` now builds the
current native frontend/races/audio/video with Asyncify and an OPFS-backed
`Assets` boundary; the existing ImportJob verifies ISO ranges/folder files before
committing a local disc generation. IDBFS persists settings/saves. The page offers
all five quick-race courses, keyboard controls, fullscreen and recovery from
bad discs, cancellation, quota errors and evicted data. Single-thread wasm
rendering safely falls back to inline bands. The deploy bundle contains only
code, metadata and licences; `tools/build_web.sh` stages validated Vercel
Build Output API files for `vercel deploy --prebuilt`.

Native gate 106/106 green; wasm 101 pass, 2 expected device skips, 1 optional
full-colour checksum failure (golden unchanged). Browser tests cover real local
disc import, all courses, driving/audio, persistence and recovery. Callback
refactoring is superseded as a prerequisite by SDL's Asyncify support. See
[WEB.md](WEB.md) and the browser handoff for evidence and limits.

Preview deployment **READY**: [https://road-rash-exd6fu2sm-arkits.vercel.app](https://road-rash-exd6fu2sm-arkits.vercel.app), engine/config `096bff2`.
Existing Vercel preview authentication applies. Disc data is imported locally.
The release was built from a clean detached worktree, excluding OMP scratch
probes; committed implementation `8673a75` is pushed to main.



### October 26, 2026: solo split contact gates, flash, crash entry, brain (845 -> 2056)

- `sim-napa-solo-split-left/right` (plan 10 Task 5, 6,681-tick human
  captures, scripts identical through tick 4,500) diverged at tick 845:
  player lateral 70 vs -1,952 with RNG in sync. Root causes found and
  fixed in `src/race.cpp`: the solo path skipped the `0x004368f0`
  `+0xc0` contact gates (the overlapping frozen is `c0=4`, correctly
  ignored by the original); the solo uncrashed path never ran the
  `0x00407a90` scenery-mark flash (`+0x1e9` stuck at 1 vs 2 at 1,423);
  `anim_deadline` compared `0xFFFFFFFF` vs -1 (snapshot sign artifact);
  crash entry dropped the `0x00403d60` half copies and `+0xc0 |= 3`
  (half flags/ground at the 2,055 reason-2 gap crash); the solo crash
  path never ran the `0x00407bd0` brain (state 0 vs 9 at 2,056).
  Both splits now replay identically through tick 2,055; the next gap
  at 2,056 (crash-motion lateral/velocity/steer, RNG in sync) is the
  known riderless-bike vertical (`0x0043a190`) / wall (`0x0043a610`)
  stand-in area (plan 11 Task 4). No regressions: L1/schema2 2,400,
  strict human 1,431, solo-script 1,801; full gate 106/106.
  Remaining (same Task 5): crash-motion gap, split branch commits past
  the crash, jump, fight/contact, traffic-solo/field (separate captures).

### October 26, 2026: solo braking/steering script green, no porting needed

- New capture `sim-napa-solo-script` (plan 10 Task 5): 1,620-word
  script, 3 cycles `[1*300, 5*60, 2*120, 201*60]`, course 4/level 1/
  seed 1/solo/HUMAN=1/FRAMES=1801. First attempt without HUMAN=1 gave
  1801 demo-branch ticks (INPUT ignored, correctly rejected for player
  physics); re-ran with HUMAN=1 for player-control (`demo_hold == 0`).
  Validated: header, 1801 contiguous ticks, end marker, EXE hash, exit 0.
  Native replay `1801 ticks identical` (+5 golden-only, 0 uncovered).
  Player throttle/brake/steer/gears hold; no divergence, no src changes.
  Like L5d/L5e, golden-only suffix => verified manually, not registered.
  Remaining (same Task 5): split-left/right, jump, fight/contact,
  traffic-solo/field (separate captures).

### October 26, 2026: bike +0x324 rider type (walls groundwork, no behavior change)

- `0x00405990` sets bike `+0x324` 0/1/2 (player/opponent/cop; cop
  already set in `police.cpp`). `place_rider` now sets
  `+0x324 = (slot == 0 ? 0 : 1)`; `+0x328` stays 0 for all
  (`DAT_004c4fb0` mapping open, plan-17), so the walls branch gate
  still always takes the walls path. Write-only (nothing reads
  `+0x324` yet): L5d/L1 still 2400/2400; full owned-Debug gate 106/106.
  Remaining: `+0x328` + `DAT_004c4fb0` mapping for real branch gating.

### October 26, 2026: crashed car contact (L5d 2278 -> 2400 green)

- `collide_traffic` no longer skips crashed riders (`0x004368f0` gates
  only on tick/flags/geometry, never on `+0x2f4`; separation runs for
  crashed too). `contact_car` for crashed now mirrors bike-bike
  `contact()`'s crashed path: `+0xf4 += 0x150` bump, no `knock_off`
  (down stays false), no `|184|/4` speed bleed, no `shit/2` steer-rate
  shove; tail kick baseline is `magnitude` (`full>>10`) vs `+0xf0`
  (e.g. 2 vs 0 gives -2). Nudge, impulse/flag clear and clamps
  unchanged. L5d 2278 lateral/velocity/speed/steer sequence gone;
  L5d now 2400/2400 (pin retired, CTest unregistered like L5e since
  golden-only suffix can neither pass nor pin; verified manually).
  L1/schema2/L5/L5e still 2400/2400;
  strict human still 1431/1431; full owned-Debug gate 106/106.
  Remaining: `+0x324` type-word never set (branch gates unported);
  uncrashed trailing `0x00407410` (deliberately open); full
  `0x00407410` walk (plan-17); class-2-contained crashed speed path
  (untested).

### October 26, 2026: crash-entry finish recompute (L5d 2267 -> 2278)

- `0x00403d60` tail ported in `Race::crash_entry` (contact) and
  `Race::crash` (half-node entry): after the body copy + mirror sync,
  `finish_distance = finish_q8_ - body_pos` when the finish is known
  and the bike holds a live slice. This is the `0x00407410` direct
  path on the crash body, discarding split-branch walk offsets at
  crash entry. L5d advances 2267 -> 2278 (single riderless-bike
  lateral word, everything else identical; re-pinned as a known
  divergence). L1/schema2/L5/L5e still 2400/2400; strict human still
  1431/1431; full owned-Debug gate 107/107.
  Remaining: tick-2278 crash-bike lateral (`0x0043a580`/`0x0043a610`
  path); uncrashed contact trailing `0x00407410` (deliberately open);
  full branch-aware `0x00407410` walk (plan-17).

### October 26, 2026: lane-validity stop port closes the L5 removal runs

- `0x0044e710:122-215` ported into `TrafficSystem::plan_speed` with
  the kind asymmetry intact (kind 0 non-strict, kind 2 strict) and
  the step-then-recheck-then-stop-or-restore shape; lane words
  come from the newest same-residue ring slot at/under the ring
  front (mod-128 over the 0x8000 window, `+0x140`/`+0x2c` slot
  identity, `+0x08`/`+0x18` linked slots, `+0x28` slot always 0 in
  every dumped tick). Verified end to end against dumped
  per-tick slice words (`napa-field-l5e`): kind-0 lane 1 with
  1-lane count steps to 0 and stops (+70, full stop).
  `traffic_test` pins the block plus both no-fire cases.
- New captures (same L5 recipe, git-ignored goldens): `napa-field-l5d`
  (car `+0x2c/+0xe8` branch words, proxy `0572d4ce`) isolates the
  tick-2002 removal; `napa-field-l5e` (per-tick slice words, proxy
  `30f4a83e`) isolates the tick-2399 removal. Both replay to the
  removal tick with every compared field identical, then diverge
  on the removal run alone.
- Results: L5 (1995) and L5d (2002) and L5e (2399) divergences all
  gone with the slot-rule port -- L5 and L5e replay 2400/2400, L5d
  advances to a tick-2267 split/merge finish-distance diff (single
  rider word, everything else identical; re-pinned as a known
  divergence). L1/schema2 still 2400/2400; full owned-Debug gate
  107/107. Remaining: behind-back slot repurposing beyond the
  newest-in-window rule needs the ring allocator (plan-17 work).

### October 25, 2026: Crash-think flash and player-only nitro from disassembly

- `0x00407a90` head (disassembly `407a91-407aa7`): nitro `0x0040b1a0` runs
  only when the bike pointer equals `DAT_004642d8` (the player). The native
  every-tick loop called it for every rider and skipped it for a crashed
  player; `step_rider` now ticks nitro for the player only, including on the
  crash branch. A `race_test` pin fails before (AI recharges) and passes after.
- State-3 flash (disassembly `407abc-407b0b`): outside crash states 1/2 the
  think clears bike +0x7c, then refreshes the scenery mark from the half
  (+0x1e9 survives only while bike +0x1c>>8 equals half +0x1ec and the
  unsigned bike +0x10 minus half +0x1f0 stays below 12, advancing as
  (gap>>2)+1; bike +0x81 copies the half). `0x00437e60` stores the current
  tick into +0x10 just before think (`437e69-437e6f`), so +0x10 already equals
  the tick there. The live scheduler already did this copy in bike think;
  `crash_bike_animation` now does it for the every-tick loop only (gated on
  `!live_schedule_`), keeping live field parity bit-identical. A `crash_test`
  pin covers mirror (gap 1 -> 1), 12-tick expiry and sample mismatch.
- REA limit recorded: `0x00407a90` has no REA procedure entry (known gap #8,
  scheduler-dispatched); the port rests on `analysis/decompiled/00407a90.c`
  plus the disassembly above, not on a fresh REA fetch. `0x0040b1a0` was read
  from the project export (global `DAT_004c5f10` charge, dt = bike +0x18).
- Live parity preserved: full Debug `ctest` 105/105 and `make verify-sim`
  (strict human 1431 ticks identical) pass with these changes; the three
  field replays are green with no new known-divergence pin.
- Remaining: the uncrashed every-tick path still has no flash copy
  (live-only concern, game uses the scheduler).
- Follow-up closed the same day: rewriting the think to the literal bike
  words first diverged field L1 at tick 1301 (`rider.3.raw_1e8` 896 vs 128:
  mark kept vs cleared). The rider was uncrashed, ruling out the helper
  double-run. A scratch probe showed native `r.sample` equals bike +0x1c>>8
  at every bike think over the full 2400 ticks (0 mismatches), ruling out
  the sample half. Root cause was the tick half: the raw Bike word +0x10
  is never stored in native (no `i32(0x10)` write exists anywhere); the
  +0x10 image lives in `Rider::sched_last`, stored to `ticks_` just above
  the think. The rewrite had read the dead word (always 0), so the gap was
  always huge and the mark always cleared. The think now reads
  `r.sched_last` with that lineage documented; behavior is identical to the
  accepted form and all replays stay green. No exact-word gap remains.

### October 24, 2026: Crash pickup draws one bike; walk-back faces the bike

- Pickup double bike: `0x00407a90` clears the riderless bike's frame pointer
  (+0x7c) and skips its advance outside crash states 1/2, so during the
  state-3 pickup only rider animation 41 (which carries the bike) is drawn.
  `Race::crash_bike_animation` now does the same; before, the lying bike kept
  drawing beside the lifted one.
- Walk-back facing: `capture_view` never copied the crash body's heading
  (+0x5c) or roll into its interpolated pose, so with interpolation on the
  walking rider always drew heading 0 (back to camera) even when walking
  toward a bike behind. The sim heading from `0x0040a600` was already right.
- On-foot control (no change needed): `0x0040a600` auto-walks toward the bike
  at speed 1200 with no keys; accelerate/brake step only toward the bike
  longitudinally, left/right move sideways, and any key resets the 1200-tick
  timeout. The native port already matches this (`recover_crash_actor`).
- Remaining (October 25 update): the state-3 flash copy and the player-only
  `0x0040b1a0` call are now ported (flash in scheduler think plus the
  every-tick crash helper; nitro gated to the player in both loops, including
  while crashed). A same-day follow-up also closed the exact-word lineage:
  the think reads `Rider::sched_last` (the maintained +0x10 image) with the
  sample/position equivalence proven (0 mismatches over 2400 ticks). Open is
  only flash on the uncrashed every-tick path (live-only concern).

### October 14, 2026: Plan10 SIM captures restored; frozen-state replay

- Fixed oracle billboard forwarding and a 512-record overflow that overwrote
  frame/pixel globals in SIM mode; reset buffers per present. SIM capture
  faults were instrumentation corruption, not established Wine timer faults.
- SIM frame bounds now count dumped ticks. Validated900-tick and300-tick
  forensic captures finish cleanly. Measured frozen fields hydrate once in
  solo replay with strict complete-state/identity validation; legacy captures
  keep their old fallback. No golden/expected-divergence rule changed.
- Replay still diverges at tick210, but the new evidence resolves its cause:
  original frozen scheduler dt4 nudges opponents3840, native dt1 only960.
  Original scheduler metadata advances despite NULL callbacks. Next planned
  work is exact scheduler-state capture/cadence, rather than guessed grid or
  velocity changes. Full player-control/parity and scene acceptance remain open.



### October 14, 2026: MiniDash numeric speed (plan 11 Task 7 / plan 18)

- MiniDash now displays speed from bike `+0xec`, replacing travelled
  distance. Imperial and metric use the original `0x004468e0` signed
  shifts/masks and reverse clamp. The full dashboard keeps its distance
  readout. `HudState::speed` is appended to preserve aggregate positions.
- Conversion thresholds, reverse/int32 bounds, state wiring and composition
  are covered; the old composition fails the regression. REA independently
  corroborates the helper. Integrated native 95/95, focused ASan/UBSan 5/5
  and Android debug/release assembly pass before this commit.
- Original MiniDash/list pixel acceptance and the needle pen remain open.


### October 14, 2026: car knock-off cleanup (plan 10 / 11a)

- `Race::contact_car` now follows `0x00404f4d`/`0x00404feb` after a
  knock-off: separate the actors, clear contact impulses/flags, and clamp
  speed to the original -1000 floor. `local_21` suppresses the steering
  kick only. The previous `+0x2f4` return skipped that cleanup.
- Synthetic class-2/class-3 regressions fail before the change and pass
  afterward. REA 6.4.0 corroborates the original assembly. This supersedes
  the implicit-guard explanation in `agent-handoffs/sim-fault-2.md`:
  `begin_crash` sets `+0x2f4=1`, but the original still runs the tail.
- CMake now registers the synthetic `car_contact` test and makes the local
  dashboard benchmark opt-in (`RR_BENCH_DASH`) on desktop. Its previous
  unconditional target broke Android and required an untracked source in
  clean checkouts. The existing local benchmark source remains untouched.
- Full original car-contact capture acceptance and the tick-210 grid
  mismatch remain open; no golden or known-divergence signature changed.


### October 14, 2026: previous-copy sync (plan 10 Task 6 fix order #1a)

- `place_rider` now copies the five placed words `0x1c..0x2c` to the
  previous-position copies `0x30..0x40`, mirroring the `0x00438930` loop
  (grid trace's "0x30..0x44" corrected to five words). Demo golden
  improved 14 ticks: first divergence moved frame 207 (tick 208) to
  frame 221 (tick 222, lateral 43537 vs 43777, rng in sync); the old
  tick-208 contact is culled by the `0x00436a40` lateral gate with true
  prev-laterals. Human golden byte-identical (still tick 210; the 0x28
  placement anchor is next, fix order #1b). The knock-kick fixture's
  contact had relied on the zero-prev bug; it now seeds `0x30 = -20000`
  explicitly. Full gate 94/94.

### October 14, 2026: tick-208 contact classes (plan 10 Task 5)

- Ported the missing class-3 `knock_off(0, swing, 3)` call plus routing
  corrections into `Race::contact`/`contact_car` (`src/race.cpp`,
  `0x004049c0` lines 77–93, `0x004052c0`): contained bike-bike hits are
  class 1 (other `+0x17c 0x2300 < 0x2800`, `+0xc4 & 0x10 == 0`), sounds and
  knock magnitudes read unscaled impulses with elasticity applied after
  (including previously missing `0x18c` scaling), and gap crashes use the
  handler's `local_18` vectors (`{0,0x200,0}`/`{0,0,0x100}`).
  `sim-napa-solo-human.tsv` (`--driver=demo`, single green anchor) now
  matches every field through tick 209 (tick-208 steer exact at 2392);
  first divergence moved to tick 210 (native-only contact: one 960-unit
  nudge + 424 speed + 2 RNG draws; frozen-word/grid-setup cause, fix
  order #1). Demo golden byte-identical. `race-test` gains class-3
  knock-tail and class-1 transfer cases. Full gate 94/94.
- Still open: grid-setup port (Task 6 fix order #1; trace in
  `docs/agent-handoffs/grid-setup-20261014.md`) and working SIM captures
  (2-tick fault, `docs/agent-handoffs/sim-fault-20261014.md`).

### October 12, 2026: web port Tasks 2 and 4 (plan 23)

- **Task 2 (full suite under Node):** `ctest --test-dir build-web` is 68/68
  (67 pass + `mixer-device` environment skip). Fixed a genuine UB
  (`apply_spec` divided by zero on zeroed specs, `src/bike.cpp`, guarded to
  the ARM64 result with a `bike_test` pin), the 64 KiB wasm stack
  (`-sSTACK_SIZE=1MB` for web links), the FIFO/pthread gate
  (`importer_test`), the audio-hardware gate (`mixer_test --device` exits
  77), and the `verify_sim.py` Node prefix; `-sNODERAWFS=1` per test
  executable un-skipped all 12 corpus/oracle tests, which pass.
- **Task 4 (pull-based import):** new `ImportJob`/`ImportSink`
  (`src/import_job.*`, `tests/import_job_test.cpp` six cases) plus streaming
  `Sha256Stream`; `import_game` reimplemented as a sync driver with
  unchanged semantics (`importer_test` untouched, green). Native 70/70, web
  69/69, ASan/UBSan clean.
- **Remaining gaps:** Task 3 (SDL main callbacks) is blocked on another
  worker's uncommitted `game_main.cpp` changes; Task 5 touches dirty
  `src/sound.cpp` (coordinate); Tasks 6–9 open (Vercel placeholder scaffold
  in `vercel.json`/`tools/build_web.sh` only).

### October 12, 2026: traffic lights and crossing traffic, second pass (plan 24 Task 1, `6eaab29`)

A review of the first intersection port against the decompile found it placed crossing cars past the intersection (end-based base), always used kind 1 with the wrong lane range, thought every tick, and moved crossing cars 0x78000 per tick.

The port now follows `0x0041eca0`/`0x0041ef60`/`0x0041f040`/`0x0044df60`/`0x0044e710`:
- **Controllers:** placed from the event start, with the type and lane-span bytes and the short phase table on dual hill slices.
- **Signals:** slots 0..3 by index, at the corner slice + 0x50/0xb0 on the shoulder middle, with type `0x40 << index`.
- **Controller think:** every 30 ticks.
- **Crossing cars:** start 1920 units to the side and drive across at ±90°. They stop at the road edge on red, follow each other, and are parked off intersection slices.
- **Free traffic:** cars latch the index-0/2 signal, stop at the line on red (hurrying when already past it) and follow any obstacle in their lane: riders, riders on foot, pedestrians, cars.
- **Pedestrians:** consult a signal only on crosswalk edge slices.
- **Collisions:** crossing cars are 0xa000 wide and collide only near the camera.

Still open:
- kind-0 lane changes around non-car obstacles;
- the split-roadway parks;
- the sub-slice link for signal laterals;
- the capture golden.

A road car and a crossing car can block each other inside a zone (both follow at 0); the original has no lane change against car obstacles either, so this may be faithful (capture check open).


### October 12, 2026: pedestrians (plan 24 Task 4)

Pedestrians are live on every course.
- **Spawning:** the ring build walks each slice's DZHR actor records (`0x004520d0`). Records below type 0xb spawn into a 15-object pool (`0x004409e0`, template `0x0046dde0`) with the original lateral jitter and trigger `rand()` calls. Police records consume their two draws without spawning (police are not live yet).
- **Behaviour:** the think `0x00440dd0` waits for the player, crosses when lanes are clear (`0x00441500` against bikes and cars), hesitates, recovers and turns back at the far shoulder (`0x004414a0`). Motion is `0x00440990` (crash-actor motion, held within 0x1ff of the spawn point) on the `0x00437e60` distance cadence.
- **Hits:** `0x00440760` knocks a pedestrian down above speed 1000 or when hit by an attacking rider (animation 3, voice 0x16), and staggers it otherwise. The rider's short-object branch of `0x004049c0` nudges below speed 2000 and rides up over the pedestrian above it, with no knock-off. Cars knock pedestrians down too.
- **Drawing:** category-2 RRAN/CANS assets with animation codes 0–6 from the EXE tables, through the actor billboard at size 0x200.
- **Native differences:** listed in `race_pedestrians.cpp`. Not verified against an original capture.


### October 12, 2026: playtest fixes (other courses, opponent HUD, meters, traffic density)

- **HIWAY and CANYN crashed at the grid** (`vector` out of range). Building heights read the depth table at `depth+256` and `depth+shoulder`. At slice 32 (depth up to 8448) that is index 4352, one past the table. The original reads the two zero words after `0x0046f098`'s 4352 entries there, so the scale is 0. `depth_scale` in `course_scene.cpp` does the same and throws beyond the padding. `course_scene-test` now builds every route sample of all five courses at fractions 0 and 255 (it was Napa at fraction 127 only, which never reaches the padding).
- **Dashboard opponent.** `nearest_opponent` ports `0x00438550`: the nearest behind wins only if it is less than half the nearest ahead. `Hud::state` names that rider and meters their stamina (`0x00446b80`). It replaces "last rider the player hit". The dashboard draws the gap at layout entry 10 as `%d.%03d` miles (or km) and colours it red/green for ahead/behind (`0x00446c30`). The ahead/behind glyph is Wingdings 0xe9/0xea in the original. Wingdings is not on the disc, so a native arrow stands in for it (labelled in `hud.hpp`). Quick races (`--course`) now resolve opponent names too.
- **Tachometer.** `0x00434980` refreshes the player's rpm in both of its branches. The port only did so inside `SoundSystem::update` once a race had started, and quick races have no sound system, so the needle stayed at 0x2000. Now the rpm is always refreshed, and the race tick calls `update_engine_rpm` when there is no sound system.
- **Traffic density.** CESR events of types 1–3 (and other types except 4, 5 and 7) call `0x0041ec70` when their end sample enters the ring front. That applies `0x0044dd00(byte +10)`: a positive value sets the density, so the reloads scale with it; 0 restores 6; a negative value sets the rate and the cruise scale `DAT_004b7e8c`. The initial ring fill applies zones in the first 128 slices after `0x0044db80`'s default. `PathSample::zone_end`, `traffic_set_density` and `TrafficSystem::build_slice` port this. Napa at 0x60/tick: 123 spawns instead of 733. The ring front moves from view camera + 0x5000 to + 0x5100 (ring camera starts at 0x3200 against the view camera's 0x3100). This is a static trace, not capture-checked.
- **Plans.** [Plan 24](plans/24-road-hazards-police-crash-control.md) covers traffic lights and crossing traffic (`0x0041eca0`/`0x0041f040`, not ported), live police, the run-back under player control and an evidence gate for pedestrians/oil slicks. (Corrected the same day after the owner reported a pedestrian on crutches: pedestrians exist. They are the DZHR actor pool `0x004c1274`, earlier mislabelled as cops, with category-2 artwork such as family 101. Plan 24 Task 4 now ports them.) The port's auto-walk with no keys held matches `0x0040a600`.
- Remaining gaps: intersections and crossing cars; the `0x00407410` route-walk form of `+0x1f8` on split routes (the port uses finish − position, which is exact once the finish is known); the Wingdings glyph; ring-offset capture check.


### October 12, 2026: original start-grid ordering recovered with REA

Used pinned `npx rea-agents@6.4.0` on the hash-verified disc EXE to check
`0x00443af0`, `0x00443cd0` and `0x00438610`, including their assembly;
all three pseudocode outputs match the prior exports exactly. The original
emits the complete next nearer world strip before the farther object pass.
The `<0x80` fraction threshold inserts scenery between actors, rather than
delaying only front-half actors. Two saved original frame-0 tails confirm
the near road precedes shadow/body. The current native workaround remains
approximate and was left intact during this research request. See
[the trace and correction sequence](research/2026-10-12-start-grid-order.md).
Plan 11a Task 3 stays open pending actor-state capture and native node/frame parity.

Sessions 1–6: September 28, 2026. Later dates are listed per session.

### Session 1: foundations
- Imported and analysed `ROADRASH.EXE` in Ghidra and exported address-keyed pseudocode, disassembly and a string index.
- Inventoried the disc and wrote a bounds-checked resource-directory reader with a malformed-input test.
- Traced the image and palette loaders and built the shared SDL3 viewer: 6 menu screens, dashboard, 2 horizons.
- Set up the Android Gradle/NDK project with the SDL3 AAR (hash-verified). The APK installed on a MANGMI AIR X and reached `SDL_main`; the device was locked, so rendering and touch were not checked.
- Started course decoding (`src/course.cpp`): section rows, `HTPR`, `NALR` and the node graph, validated on all five courses.

### Session 2: road geometry
- Recovered the semantics of the path bytes: bend is curvature (integrated twice), rise is a height delta (integrated once).
- Traced the road pipeline: slice ring (`0x0049e820`) → per-slice fill (`0x00454240`) → 33-slice projector (`0x00443d70`) → per-slice projection (`0x00444630`). Found the static non-linear depth table (`0x0046f098`), the fixed 320×240 view with horizon on row 90, and the camera height of 150.
- Ported the single-road projection in integer arithmetic (`src/road.cpp`). The depth table is read from the user's executable at runtime and never copied into the source.
- Added five course screens to the viewer, with auto-drive, speed and branch controls, `--position` and `--screenshot`.
- Added a synthetic projection test and a full-corpus sweep in `rr-course --exe`.
- Android now packages the 5 courses and `ROADRASH.EXE`; APK rebuilt.

### Session 3: splits and merges
- Mapped split/merge node fields and the lookahead traversal state machine (`0x00453030`): two cursors, separation slope/offset, and events.
- Also mapped the load-time 81-sample walk (`0x00453b60`) that rewrites window lengths and stores merge snapshots, and the collapse rule (`0x00452fd0`) with its rider-commit flag (`0x00476cd4`, set by `0x00439390`).
- Replayed that traversal along each route in `make_route` and ported the dual-road paths of `0x00443d70`. Verified that separation closes to zero at every merge on every course.
- Found that branches have independent height profiles and the original never tracks vertical separation. As a result, only the rider's own road lays ground bands in the viewer.
- Extended the tests and the corpus sweep; APK rebuilt.

### Session 4: section chunks and shoulders
- Mapped all eight per-section row chunks to their iterator init/step functions (table in REVERSE_ENGINEERING.md).
- Decoded the `DLSR` sides layout: pair A is shoulder width and pair B is shoulder height, both in world units with Q16 interpolation. Validated on all 389 records.
- Consolidated documentation (this file).
- Replaced the closed-form lane lookup with `walk_intervals`, an exact simulation of the original iterator step (`0x00452920`/`0x00452b70`), shared by lanes and sides. Lane widths changed on 6 samples (HIWAY section 6), now matching the original stepping.
- Shoulders are carried through projection (`RoadSurface::shoulder_left/right`) and drawn by the viewer; a projection test was added.
- Traced the terrain-type dispatch: building → type 0, mountain → type 1, hill → type 4. Only type 0 applies shoulder heights.
- APK rebuilt (5.3 MB).

### Session 5: playable Napa
- Initialised git (`main` holds the baseline; work on `port/playable-napa`) and added `rr::Assets` and `rr::Framebuffer` (plan 01 Task 2).
- Recovered 56 functions Ghidra had missed because they are only reached through behaviour tables (`tools/ghidra/ExportPointerTargets.java`), among them the bike think `0x00407a90` and the player rider think `0x00434660`.
- Traced the rider objects:
  - `0x00405990` builds a bike + rider pair from tables `0x00463ba8`/`0x00463c10`. Table words 3–6 become `+0xdc`/`+0xe0` (think)/`+0xe4` (move)/`+0xe8` (slice change).
  - The player's control word is `0x004b7ed0`, from `0x0044f800` (keyboard + joystick).
  - The simulation runs at 60 ticks/s (`0x004163d0`: counter frequency/`0x3c`), at most 20 steps per frame.
- Ported the riding physics and the race movement (`src/bike.cpp`, `src/race.cpp`), with tests (`bike_test`, `race_test`). A headless keep-to-centre bot finishes Napa in 7.2–10.0 minutes depending on the bike.
- Added `rr-game`, a playable race on any course, and switched the Android build to it.

### Session 6: opponents and combat
- Decoded the rider sprites. `0x004436a0` reads `Data/Bikers/{Hi|Lo}<name>.DAT`: per frame a width byte, a height byte, then indices. There are 326 bike frames and 45 cop frames; shadows use the same layout. Each rider's colours come from a 256-byte remap table at `0x0046e158` (one per rider), applied by `0x00443820`. The player now rides the original sprite with lean frames.
- **Opponents:**
  - Ported the AI racing state: `0x00407bd0` → `0x00409000` → `0x004090a0`/`0x004091f0` target choice, the `0x00409020` curve speed limit, `0x00409f70` steering/speed control, and the once-a-second `0x004082f0` re-plan with the `0x004659d4` rubber band.
  - The field comes from personalities at `0x00464558`, bikes from `0x004641c8`, rider values and stats from `0x00463c78`, and the level throttle scale from `0x004642a0`.
  - Headless: 7 opponents finish Napa at level 1 in 599–703 s.
- **Rider contact:** detection from `0x004368f0` and separation/impulses from `0x00436a40`, with the bike-against-bike branch of the contact handler `0x004049c0`.
- **Combat:**
  - Ported the hit test (`0x00409c10`), damage (`0x00409ad0`/`0x004052c0`), stamina (`0x004050c0`) and bike condition (`0x004051e0`), plus knock-offs, steering knocks and the backhand shove (`0x004096f0`).
  - Weapons: fists, club, chain and kick, with reach 5/7/8/5 × `0x1000` and damage multipliers 256/320/384/64.
- The game now has a 7-rider field, placings, attack keys (Z/X/C, gamepad X/Y/RB, touch top third) and stamina bars.

### Session 7: Android lifecycle and multi-touch (October 1, 2026)

- Added shared `SessionState` and `TickClock` for desktop and Android. SDL's lifecycle event watch updates atomic state immediately; renderer and race work stay on the game thread.
- Background/focus loss pauses the race, discards accumulated time and clears touch controls. Foreground/focus gain leaves a visible pause prompt; Enter, Start or a tap resumes. Android Back opens pause. P/Esc/Start toggles pause; Q quits on desktop.
- Active gamepad removal pauses. Render-device/target reset invalidates the sprite cache so sprites are recreated on demand. An unrecoverable render-device loss exits with an error.
- Replaced the single touch point with per-device/per-finger tracking: simultaneous steering and punching, independent releases, finger cancellation, and braking priority over touch auto-throttle.
- Replaced truncated nanosecond tick intervals with a rational 60 Hz accumulator and retained the 20-step stall cap. Tests cover ten seconds at 30/60/120/144 Hz, pauses and very large elapsed times.
- Android manifest declares optional gamepad/touch hardware and a resizable landscape activity.
- Native and sanitizer tests pass (9/9); Android ARM64 debug APK rebuilt. Pause screenshot inspected at `analysis/native-game-paused.png`.
- No Android device connected. Physical input, lifecycle and graphics reset recovery still need hardware validation. Plan 08 remains partial: import, settings/rebinding, saves/audio lifecycle and release packaging are not implemented.

### Session 8: original oracle and verified data import (October 2, 2026)

- Completed AWEMAN32 forwarding and in-memory frame hooks at four verified original call sites. The reference EXE stays unchanged in a separate oracle install. All sites are preflighted before any write; projected pointers are checked before logging.
- Captured 600 original frames. The scenario is named `napa-attract`, but selector 0 is **CITY** in the original (`0x0046df58`), unlike native CLI ordering. The initial apparent shoulder mismatch was a comparison-course mapping error; no road-code change was needed.
- `oracle-road` compares slice kind/sample, depth, scale, centre, surface row, road edges and outer shoulder edges. All 600 frames match exactly. Camera Q8 position spans 12,544–19,159 (samples 49–74). This does not verify other courses, splits, terrain, AI, physics or pixels.
- Added bounds-checked ISO 9660 parsing, SHA-256 and a transactionally staged importer. Path/hash metadata comes from the existing inventory; original file bytes are never embedded. Missing/mismatching files prevent commit; existing nonempty destinations are preserved.
- Imported all 341 files from the real ISO into `.tools/imported-game`; viewer smoke passed using that copy.
- Added first-run ISO/folder selection and asynchronous progress/error display. Android uses SDL's content-URI picker; physical SAF operation remains unverified. Screenshot/`--first-run-smoke` verifies the empty-data screen without opening a picker.
- Default APKs exclude research assets. Assembly checks actual APK entries. `-PdevAssets=true` permits local debug research only; release checks always forbid game data.
- 16/16 native tests pass. Android ARM64 debug builds and passes the no-game-data check. No device connected. Plans 01–08 remain active.

### Session 20: front end, career and saves (October 2, 2026)

- Language DLL string tables: native PE resource reader and the `0x0042f5d0` table assembly (descending load, `0xff` continuations). All five languages give 985 slots. Language follows the original rule (`0x0042f340`), mapped from the SDL locale.
- The `TEXT/BADLOC|FUTB|FUTD|FUTR.DLL` files are TrueType fonts. Menu text now uses them through stb_truetype (pinned, unmodified, MIT/public domain).
- Career rules ported:
  - game state block `0x004b8a10`, mode `0x004753b0`, characters and starting cash;
  - default bikes; prize money for every finisher by place × (level+1);
  - fines (400 × (level+1)), repairs (price/10), game over when unpaid;
  - qualification bits and level-up/finale; bike trade-in at half price.
- Original save files ported: 0xa0 bytes with the `0x0042efc0` checksum and the relationship triples.
- Front end, driven by the EXE's own screen/item/popup tables:
  - navigation bytes, handlers, focus pictures, title/description/price boxes, darkened surfaces, button colours;
  - restroom Y/N enabling and popups (mode, level, name, resolution, biker detail, videos);
  - character bios with original units, Olley's showroom with bike videos, Schmooze lines from the relationship values;
  - results, penalties, standings rows, level-up, finale, and the scrolling credit rolls with colour codes and tab stops.
- `rr-game` now runs menus → videos (Intro, Start, results, bikes) → career races → results. The race ends 0xb4 ticks after it is decided (`0x00416010`). Quitting a race counts as wrecked, and opponents are named from `0x00464188`.
- Tests: 42/42 pass, including `career`, `frontend` (full scripted menu flow, rendered frames), `text`, `settings`. Android ARM64 debug APK builds without game data.
- Not done or not verified:
  - no pixel comparison with the original menus;
  - no original save round trip;
  - mano-a-mano disabled;
  - jukebox/sound mix/controls screens inert;
  - no attract demo;
  - native file list instead of Windows file dialogs;
  - interactive play-through on hardware not performed.

### Session 20 (continued): sound, resource unpacking, natural-frame renderer equality

- Race sound and soundtrack added (see the Audio rows in the status table and REVERSE_ENGINEERING.md).
- PKWARE-implode resource unpacking with CRC checks covers all 163 FAM records. Cel groups (CLGP) and dynamic rider textures are decoded.
- The oracle `dump_scene` writes the original's display list for each captured frame. Rendering that list natively reproduces 7 natural CITY frames with 0 differing pixels.
- The oracle DLL now has dump format v3: a memory snapshot before `0x00443d70`, one after the build, and the node list. It is built but **not yet captured with**. A `city-moving` capture (v1 DLL, 900 frames) was still running when the session ended.
- **Next:** port the scene build (`0x00443af0` subtree, `0x00443d70`/`0x00444630`, ring fill `0x00454240`) as a literal port over an emulated address space. Families need to load and relocate as `0x0044aa80` does, with templates resolved through `0x0044acd0`/`0x0044a870`. Compare emitted nodes against v3 dumps, then replace the placeholder race renderer. After that: HUD/dashboard, traffic/police (plan 05), race MIDI, controls/sound-mix screens, and device validation.

## Assumptions and approximations in current code

- **Rider commit:** after a split the preview assumes the rider has already committed to the selected branch, so the other road is dropped 82 samples in. In the game this depends on the bike's lateral position (`0x00439390`).
- **Collapse onto half 1:** the next bend comes from the kept road; the original reads a stale ring sub-slice.
- **Merge snapshot:** lane alignment uses the last sample's width instead of the terminal `NALR` record.
- **Camera:** fixed at height 150 on the centre of the route's own road. The original follows the bike.
- **Oracle scope:** all captured state and RNG match the complete 2400-tick Napa level-1 live field and the receipted 1431-tick human input capture. Other levels, courses and human sequences need their own comparisons.
- **Walls and crashes:** the wall collision (`0x0043a610`) and the crash/remount sequence (`0x00403d60`) are simplified stand-ins: the rider is clamped at the shoulder edge, crashes above speed 4000, and remounts one second after stopping.
- **Terrain:** terrain type and flags are treated as 0 (building-style sides), because the terrain chunks are not decoded yet.
- **Not ported yet:** nitro (`0x0040b110`), attacks, the joystick analog path, and the original key bindings (`0x0040e5f0`). The defaults are Up/Down/Left/Right = accelerate/brake/`0x4`/`0x200`.
- **Race layout:** the player starts in grid slot 0 (`0x00464218`), and the race runs the whole route to the course end node.
- **HUD:** the speed shown is the internal speed ÷ 100; its real unit is not established. The original dashboard is not drawn yet.
- **Race field:** seven opponents (personalities 1..7) regardless of the player's character; the original standings hold 15 rows (`0x004642d4` = 16) and the field composition is plan 05 work.
- **Sprite frames** are chosen by inspection: lean frames 3–11 by steering; attack wind-up/strike frames per weapon (fists 44/45 and 51/52, kick 60/66, club 140–143, chain 101/102 and 105/106); crash frame 21. The original picks frames through the `RRAN`/`RRFD` animation system (`0x00445a60`, `0x0043ae90`), which is not ported.
- **Sprite scale:** opponents scale by depth relative to depth 512, capped at the player's size and baseline. The original sprite scale law and raster are not ported.
- **AI:** original replan, avoidance, follow and state-3 attack dispatch now free-run through the full 2400-tick Napa field capture. The 45-tick attack rule remains only in bank-less synthetic component fixtures; police, other levels and additional combat scenarios remain open.
- **Attack timing:** live bank-backed AI uses original animation-table predicates and descriptor flags `0x600000` OR frame byte 2, with cooldown and repeat/taunt queues. The 18/tick-7 fallback remains only for bank-less fixtures. Human attack-control sequences need their separate captures.
- **Weapon grabs** follow the traced `0x004096f0` rule (`resolve_hit`: victim immunity expired, victim frame < 2 and mid-attack, attacker fists vs armed non-kick victim, `(rand&3)==3`, atomic transfer, attacker immune 300 ticks, no damage). No weapons drop to the road or carry in career (weapon trace, October 15, 2026). The attack predicate and steal animation queues are ported; dedicated capture confirmation (`napa-weapon-steal`) remains open.
- **Contact response:** the bike-against-bike and bike-against-car branches of `0x004049c0` are ported with the `0x004052c0` knock-off decision applied on contact. Remaining: ped/static branches, wall/crash-sequence stand-ins (plan 11 Task 4, plan 16 Task 1, plan 24).
- **Shading:** sky, verge, shoulder, road and lane-marking colours and the 4-sample stripe are placeholders. Painter's back-to-front drawing replaces the original's occlusion row (`0x4c10c8`).

## Not verified

- **No pixel comparison with the original's output.** Numeric road fields match the recorded starting stretch of CITY; frame pixels and other systems remain unverified.
- **Android has never been observed running on hardware.** Session 1's device was locked, and no device was connected in sessions 2–5. The APK contains native code; default builds import disc assets at runtime.

## Next steps

The plans to finish the port are indexed in [plans/00-INDEX.md](plans/00-INDEX.md), which was reviewed and rewritten on October 11, 2026. In order:

1. **Plan 10:** seeded per-tick simulation dumps from the original, and a native replay test; then fix physics, AI, combat and traffic until they match. Tasks 1–2 are done; Task 3 is in progress.
2. **Plan 11:** finish the opening Napa race:
   - actor animation and merged actor nodes;
   - remaining scene items;
   - crashes;
   - AI fight states;
   - police;
   - car engines and the mini dashboard;
   - a full-race acceptance test.
3. **Plan 12:** the other four courses and all levels; delete the placeholder renderer.
4. **Plan 13:** race MIDI with SBK banks, the sound mix/controls/jukebox screens, the attract demo, cutscene audit, save and menu verification.
5. **Plan 14:** Android on the MANGMI AIR X: device check script, frame-time benchmark, the `HiRes` setting, touch overlay, import/controls/lifecycle checklist, owner signing. Task 1 runs as soon as a device is attached.
6. **Plan 15:** acceptance script from a clean clone, macOS distribution, upgrade path, multiplayer decision, final docs.

## Tool and command reference

| Command | Purpose |
| --- | --- |
| `./run-macos.sh` | Build and launch the viewer on macOS |
| `ctest --test-dir build --output-on-failure` | `resource-bounds` and `road-projection` tests |
| `build/rr-viewer --assets ../cdrom/ROADRASH --smoke` | Decode and render every screen once |
| `build/rr-viewer --screen 9 --position 960 --screenshot out.bmp` | Capture one course frame (screens 9–13: NAPA, CITY, HIWAY, CANYN, MEDLY) |
| `build/rr-course --exe ../cdrom/ROADRASH/ROADRASH.EXE ../cdrom/ROADRASH/DATA/COURSES/*.CRS` | Course graph, split/merge windows and positions, full projection sweep |
| `build/rr-inspect FILE.RSC` | Dump a resource directory |
| `python3 tools/check_corpus.py ../cdrom/ROADRASH` | Validate all resource containers |
| `python3 tools/prepare_android_assets.py ../cdrom/ROADRASH` then `android/gradlew -p android assembleDebug` | Build the APK (needs `JAVA_HOME` set to JDK 21) |
| `npx rea-agents@latest function "../cdrom/ROADRASH/ROADRASH.EXE" <address> --format md` | Independent Ghidra cross-check of one function (needs Node 24 and `GHIDRA_INSTALL_DIR`; setup and limits in [TOOLING.md](TOOLING.md)) |

Import follow-up (October 2): nonseekable ISO input now caches inside the owned staging directory and the cached ISO is removed before commit. A synthetic FIFO import and cancellation/cleanup tests pass. First-run UI is now a CTest smoke check. Hardware SAF remains unverified.

### Session 9: original pixels and native audio/video (October 2, 2026)

- Extended the isolated original oracle with a verified hook at `0x004439f5` → `0x00450420`. Capture occurs while the 8-bit surface is locked, before presentation/dashboard. It writes raw view pixels, the active palette, coordinate shift and sub-slice fields. Original EXE bytes remain unchanged.
- `city-pixels` captures 60 frames plus raw frames 0 and 30. Both are 640×480 with pitch 640 and coordinate shift 15. Frame 30 was converted and visually inspected. All 60 compared road projections match exactly; this establishes capture, not native pixel equality.
- Corrected renderer tasks: `0x0044b630` and related functions prepare scenery; polygon fill is `0x0044d620`, spans `0x0044db30`, traversal/dispatch `0x0044d300`. `0x0040ffc0` converts coordinates rather than dispatching node types.
- Added strict PCM8/PCM16 and WAV IMA readers, including chunk padding, fact trimming, block/group validation and malformed/truncated input tests. All 72 effects and 14 RRA files decode.
- Added an SPSC queued stereo mixer with Q32 linear rate conversion, pan/volume/pitch, saturating sums, pause-preserved cursors, safe sound retirement and SDL audio stream/device reopening. Oldest-voice stealing remains provisional until original priorities are traced.
- Added AVI physical packet scanning, bounded RGB Cinepak codebooks/V1/V4/intra/inter decoding and WAV IMA audio. Missing or corrupt indexes do not affect sequential scanning. A truncated INTRO retains its complete packets. The 15 bike showcases are silent 320×172, unlike the 44 cutscenes at 320×192 with audio.
- Full disc codec comparison against FFmpeg passed for every RGB frame in all 59 videos and all 67,840,002 int16 samples in 44 audio streams. FFmpeg is a test oracle only and is never linked.
- Added native video playback via `rr-game --video VIDEO/INTRO.AVI`, SDL RGB texture output, skip, pause, lifecycle muting, audio cursor timing and silent-video wall-clock fallback. Native intro screenshot at ten seconds was visually inspected. Playback buffers are released only after their mixer references are retired.
- Original MCI presentation `0x00402820`: normal native-size mode uses x=160, y=240-height/2; doubled mode uses x=0, y=240-height, or y=20 when flag bit 0 is set. The current explicit player uses native-size mode. Frontend mode/clip selection and hardware A/V latency remain unverified.
- This milestone does not implement game sound triggers, original voice stealing priorities, streamed RRA selection/fades, MIDI, frontend cutscene selection, scenery/terrain raster, full AI/traffic/police or career/save behavior. The whole-port goal remains active.

### Session 10: standalone and release packaging (October 2, 2026)

- Added opt-in `RR_BUNDLE_SDL`: official SDL 3.4.8 source tarball pinned to SHA-256 `e9fff7467fb60f037e6708da18b25560649e4c63edc2a69bb871b960d9cbfbba`, static SDL, ARM64/macOS 13 deployment target and application plist.
- Built `build-release/Road Rash Native.app` (3 MB) and ZIP. Post-build copies licences/notices and ad-hoc signs the app. Verifier confirms ARM64, 22 Apple system libraries/frameworks only, strict signature validity and no game data. All 23 release tests pass. Bundle first-run screen was captured and inspected; executable also runs from `/tmp`.
- Added Android release signing configuration from owner-supplied properties/environment, tag-derived version name with research fallback, explicit `-PreleaseDebugSigning=true` for local optimized development APKs, and always-packaged SDL/stb licence notices. Default owner release remains unsigned without key configuration.
- Debug and optimized release builds pass APK game-data exclusion. Local development release signature verifies (v1/v2); only two licence assets are present. No production signing key was created or claimed, and no device is connected.
- Added `tools/ci.sh`, standalone build/ZIP script, bundle verifier and current-development `docs/USER_GUIDE.md`. Local CI reports `CI OK`.
- Video gamepad Start now pauses/resumes; active controller disconnect pauses, and desktop Q quits. Physical gamepad behavior remains unverified.
- Packaging is software-verified. Hardware testing, Developer ID/notarization, full original frontend/gameplay/audio behavior and final whole-port acceptance remain open.

### Session 11: original flat polygon filler (October 2, 2026)

- Ported the normalized edge/span stage of `0x0044d620` and the byte-fill behavior of `0x0044db30` to an indexed framebuffer. Q16 slopes, signed floor, wrap, inclusive bottom/exclusive span right, shared endpoints and degenerate points follow measured original behavior.
- Added synthetic coverage and an isolated original-function oracle mode. Loader mode `RR_ORACLE_RASTER=2` runs the pure original filler on a private buffer before graphics setup, records the original-normalized vertices, restores modified globals and closes a complete golden. Live mode 1 can run during the first locked draw. These fixtures are component tests, not full race images.
- 64 original fixtures compare with **zero pixel mismatches**: rectangles, diamonds, horizontal spans, negative clipping, fractional/rotated trapezoids, duplicate-point triangles and large ±16,384 coordinates. Golden images stay in ignored `analysis/oracle/`.
- Native and address/undefined sanitizer suites pass **25/25**. Resource/viewer smoke and all-course sweep pass; Android ARM64 debug builds with asset exclusion.
- Timed oracle cleanup now targets only the process it launched, without killing every Wine process in the prefix. Old scenario raw outputs are removed before capture to prevent stale fixtures from being mistaken for new output.
- Full polygon normalization, original scene/texture/scenery/terrain drawing and full-frame equality remain open. The game still uses the placeholder scene renderer.

### Session 12: original view coordinate mapping (October 2, 2026)

- Added `ViewMode` from `0x00448870`: 320×240 low, 640×480 high, or a low view centered at (160,120) in a 640×480 display.
- Recovered `0x00410160` / `0x0040ffc0`: high mode shifts Q16 by 15 and masks with `0xfffffffe`. Vertices therefore lie on even pixels; simply shifting by 15 was insufficient. Low mode uses signed floor by 16 without masking.
- The isolated oracle calls the original conversion helper for twelve signed/boundary coordinates in each mode. All 24 mappings match. This is helper evidence; low-resolution race pixels remain unmeasured.
- Expanded raster input bounds to the complete converted high-mode range −65,536..65,535. A new extreme rectangle remains exact against the original; all 64 raster fixtures still match.
- Native and address/undefined sanitizer suites pass 26/26. Android ARM64 debug builds and asset exclusion pass. Scene integration remains open; the whole-port goal continues.

### Session 13: stateful terrain decoding (October 2, 2026)

- Added bounded `DLBR`, `NTMR` / `DWDR`, and `LIHR` payload decoding. All 109 sections in five real courses decode and traverse in both directions. `CourseSection` owns immutable terrain definitions rather than precomputing random samples.
- Ported original terrain initializer/step/export behavior: building selector pairs, two mountain wall profiles with three jitter values each, eight hill control points with interval slope generation, and six exported points/shading indices. Randomness is supplied by the caller, preserving state across steps.
- Added 864 original-function component comparisons: three terrain types, three fixed random values, forward and backward traversal over 48 samples. All exported selectors, wall values, hill points and gradients match without tolerance.
- Disc records require preserving physical order. CITY contains negative hill boundaries and an inverted initial 0..−6 interval; HIWAY/MEDLY contain out-of-order wall boundaries. The original advances only one boundary per call. Sorting or rejecting those records would alter the course.
- Structural tests reject truncated payloads, invalid extents/counts/tags, mismatched path samples and invalid hill parameter bounds. Native CTest passes 27/27; Android ARM64 debug build and asset exclusion pass. Sanitizer follow-up is recorded in VERIFICATION.md.
- Terrain is decoded and independently validated, but original traversal/cache random-stream integration, split road transforms, scene projection/texturing and full race pixel equality remain open. No complete terrain-renderer claim is made.

### Session 14: original hill projection (October 2, 2026)

- Added `project_hills` from `0x0041d6d0`: six Q16 vertices and maximum projected row, using explicit signed 32-bit wrap. At scale ≥65,536 the original applies ±0x40000000 x sentinels when wrapped signs indicate overflow; lower scales use direct wrapped projection.
- Added 96 original-helper fixtures covering six scales (including the branch boundary and synthetic signed extremes), signed world/camera offsets, odd/even view widths and six varied points per case. Every coordinate and maximum row matches exactly.
- Synthetic tests cover ordinary projection and the near overflow sentinels. This pure module is the start of scene projection, not an integrated world renderer. Original terrain state, occlusion, split transforms and texturing still require integration.

### Session 15: polygon normalization and full flat path (October 2, 2026)

- Ported `0x0044cd70` signed/wrapped cross classification, degenerate-edge propagation, and `0x0044ce60` vertex repairs, orientation, rotation, axis sorting and texture-flip mode. The original adjacency lookup is implemented as cyclic-edge logic rather than embedded table bytes.
- All 2,048 original normalization fixtures match vertices, acceptance byte and flip state. Coverage includes arbitrary quads, duplicates, collinear points, large wrapped cross products and flag modes 0/8/4.
- Added `draw_flat_quad` that normalizes before the verified filler (horizontal spans bypass normalization as in the original). All 64 raw fixture images match from both original input vertices and already normalized vertices.
- Hill projection milestone passes 28/28 native/sanitizer tests and Android build. Combined normalization verification is recorded in VERIFICATION.md. Textured spans, pathological rejected polygons and world/display-list integration remain open.

### Session 16: original indexed texture scan conversion (October 2, 2026)

- Ported opaque and index-zero-transparent spans for texture strides 1..256. All 1,008 original fixtures match, including all four output alignments, short/unrolled lengths, fixed/moving row coordinates and signed fractional increments.
- Ported `0x004184a0` edge interpolation and `0x00418760` scan conversion: reciprocal truncation, wrapped Q16 products, inclusive edge rows, negative/top/bottom/right clipping, endpoint replacement, per-row UV interpolation and transparency. Added the original two-axis minimum mip selection.
- All 64 original textured quad images match exactly, covering sloped edges, offscreen clipping, transparency and three distinct mip levels. This compares 19,660,800 indexed pixels. Mip data/UV selection is caller-owned; original resource/display-list integration remains open.
- Direct disassembly resolved a decompiler argument-order ambiguity: spans consume V as row and U as column. No golden expectation was changed to hide this mismatch.
- Golden reader now rejects integer overflow, duplicate fields/headers and noncontiguous frame IDs, with malformed-input checks.
- Polygon milestone passed 29/29 native/sanitizer tests and Android asset-free build. Combined texture checks are recorded in VERIFICATION.md. The full world renderer, original scene/resource preparation and sprite integration remain open.

Session 16 final validation: 30/30 native and sanitizer tests pass, Android builds asset-free, and viewer/resource smoke plus five-course sweep pass. The whole-port goal remains active.

### Session 17: CEL wrappers and indexed mip resources (October 2, 2026)

- Added bounded CCB/PLUT/chunk readers and the PC indexed `PDAT` / `RPDT` / `RRPD` resource layout. Dimension flags are interpreted using original `0x0044fb60`; mip pointers use the +28 relative base from `0x0040b7b0`.
- All 42 CEL resources decode: 17 indexed textures, 117 mip blocks, 25 retained legacy four-byte pixel words. Pixels in the PC texture blocks are already palette indices; a hypothetical 3DO packed-pixel conversion would be incorrect for these blocks.
- The original loader `0x0040b800` now runs on copied owned-disc resources in the isolated oracle. All CCB dimensions/flags, texture flags/levels and every pixel in all 117 relocated mip blocks match native output. Small legacy words match exactly but their final drawing semantics remain open.
- Tests reject every truncated length of a synthetic CEL plus invalid mip offsets and non-power-of-two stride. The loader oracle restores the original dimension table; it never patches disc bytes.
- Original course CEL selector tables, legacy packet interpretation, CCB transform semantics, palette use outside indexed mip blocks, animation and scene/resource integration are still pending. The whole-port goal remains active.

Session 17 final validation: 31/31 native and sanitizer tests pass; Android builds asset-free. Whole-port completion is not yet achieved.

### Session 18: original display-list rendering (October 2, 2026)

- Added `render_nodes`: original Q16 view conversion, ordered node traversal, bounding-box rejection, quality-mask flat dispatch, normalization/UV flips, inset UV coordinates, mip selection and indexed scan conversion. Callers own texture storage and display-list construction.
- CEL texture bounds are now decoded as signed shorts and verified against the original loader for all 17 indexed resources.
- The isolated oracle runs the actual `0x0040ff40` → `0x0044d300` path on 32 synthetic lists with 12 overlapping nodes each, both resolutions and quality masks 16/48/112. All **6,144,000 pixels match exactly**.
- An initial fixture incorrectly pointed nodes at a CCB wrapper. Disassembly shows `0x00410080` accepts the pixel-resource pointer directly. Correcting the fixture enabled actual textured dispatch; native expectations were not adjusted to conceal a renderer difference.
- Dynamic rider frame specialization (`0x00443820`), original scene construction/selector resolution, sky, natural full-frame equality and replacing the game's placeholder renderer remain open. Whole-port completion has not been achieved.

Session 18 validation: 32/32 native and address/undefined sanitizer tests pass. Synthetic dispatch tests also run without oracle captures. Android ARM64 debug build and asset exclusion pass; resource/viewer smoke and five-course projection sweep pass. No Android device pixel comparison was performed.

### Session 19: original road panels and imported materials (October 2, 2026)

- Ported `0x0043c170` lane-panel construction and `0x0043c0b0` material selection: byte lane counts, fractional Q8 lane widths, four-pixel outer expansion, Q16 row masking, marking direction, visibility/clipping, terrain-dependent PRE0 and quad UV flips.
- 128 original-function cases emit 343 panels. Every coordinate, material slot, PRE0 and flip matches exactly, including fractional lanes, reversed direction, negative steps, offscreen panels and hidden slices.
- Added bounded PE virtual-address data extraction. Road material IDs are read from the user's executable descriptor table at `0x0046dfb0`, then decoded from imported `GLOBAL.RSC`. The eight logical slots include the original `0x00444e30` marked-middle alias. No game tables/pixels are embedded in source.
- Combined original panel construction and actual imported CEL rendering match **24,576,000 indexed pixels** across 128 fixtures in both modes and all three quality masks. These are isolated synthetic road panels, not natural race-frame comparisons.
- Synthetic PE truncation/address checks and resource-binding/rendering checks run without game assets. Original scene cache/occlusion, terrain, scenery, horizon, player/sprite specialization and complete game renderer integration remain open. The whole-port goal remains active.

Session 19 final checks: 35/35 native, sanitizer and standalone release tests pass; Android debug and optimized development release build asset-free. Development APK v1/v2 signature verifies. Refreshed macOS `.app`/ZIP passes ARM64, system-library, signature and game-data checks. Viewer smoke and five-course projection sweep pass. No Android device is connected.

### Session 21: native and Android menu controls (October 10, 2026)

- Added left-stick menu navigation in the shared native/Android frontend: dominant-axis selection, 12,000 activation / 8,000 release thresholds, immediate direction changes, 350 ms initial repeat and 100 ms subsequent repeat. These are native usability choices, not recovered original behavior.
- Stick input requires neutral after lifecycle pause, controller removal, video playback or a race. Long frame stalls emit at most one repeat, avoiding a burst of menu navigation.
- Menus and videos now discover already-connected controllers, including controllers whose initial SDL added event was consumed by another screen. Menu button events are filtered to the active controller.
- Menu controller removal pauses and discovers a remaining controller. Returning from nested race/video loops also checks whether their consumed events disconnected the menu controller.
- Extended synthetic input coverage for signed axis limits, dead zones/hysteresis, diagonal dominance, reversal, repeat boundaries, stalls and neutral rearming. Updated the user guide to reflect implemented menu/career/audio behavior.
- Native Debug, address/undefined sanitizer and standalone Release suites each pass 44/44 tests. Viewer smoke, all-five-course sweep and frontend rendering smoke pass. Refreshed standalone macOS `.app`/ZIP passes packaging checks. Android debug and optimized development release APKs build with no game assets; release v1/v2 signatures verify. Details/logs are in VERIFICATION.md.
- Original disc mounted read-only for corpus verification. No Android device is connected; physical controls and picker/import behavior remain unverified. Full native scene construction, traffic/police and race MIDI remain open.

### Session 22: opening Napa family selection and hillside construction (October 10–11, 2026)

- User clarified the requested first level is the opening Napa race. Acceptance tracking lives in `NAPA_IMPLEMENTATION.md`; the complete race remains in progress.
- Added bounded native family hierarchy / CLGP decoding, preserving original resource offsets rather than relocated pointers. All 163 populated families decode into 577 groups and 786 indexed variants. Empty family records remain excluded. Terrain groups with the original unused-axis exponent sentinel decode without inventing an axis size.
- Captured the original Napa start through a test-only menu bootstrap, with an unmodified original executable. All 176 selections from 22 loaded groups match dimensions and mip-zero pixels. One-frame scene replay also matches all 15 emitted nodes and every final pixel; this replay does not prove live scene construction.
- Added the original near hill perspective subdivision (`0x0044bd40`) and hill-band construction / material-size calculations (`0x0041c390`, `0x0041cf60`, `0x0041d060`). All 128 subdivision fixtures match, and all 256 hill-band fixtures match 1,164 emitted quads and UV flips. Cases cover both sides, distance boundaries, terrain transitions, offscreen rejection, occlusion, seam corrections and imported overlap offsets.
- The bootstrap currently faults in the original surface-restoration path when attempting a multi-frame capture. Completed one-frame golden files are used for verification; the incomplete multi-frame capture is not accepted as evidence.
- Hill geometry and family selection are isolated shared modules. Live integration, shoulder/wall/building geometry, original scenery/traffic/police/music and full-race verification remain open. No Android device is attached.

Session 22 continuation:

- Ported shoulder construction (`0x0041d260`) and its distinct near subdivision (`0x0044c0e0`). All 256 original cases match all 527 quads and UV flips, including hill/wall transitions, edge expansion and quarter-pixel row masking.
- Added bounded CESR course events and MSRR family load/unload record decoding. All 7,949 Napa path samples match original event types, parameters and marking flags. Corpus decoding preserves empty event tables and signed negative starts used by other courses.
- Both branch routes retain the other road's source node, section and local sample. The five-course sweep validates 57,980 route samples and 3,586 dual-road references against their source sections; 1,200 family request records decode.
- The existing road projector also matches all 33 projected slices of the original Napa start (one frame, no mismatches). This still does not establish complete moving Napa scene equality.

- Corrected the original-game bootstrap to take the actual course menu exit. This removes the stale credits timer that caused the prior surface fault. The completed 121-frame stationary capture matches road projection and five display-list replays. A subsequent 901-frame driven capture advances camera position 12544→29004; all 901 road projections match. Moving display-list replay exposed an empty shadow frame producing invalid native UV storage; that integration regression is under repair.
- Added an owned `CourseScene` composition module: imported horizon, road materials, structural family resolution and deterministic native terrain generation. A full Napa branch sweep emits geometry for 2,509 views and resolves every hill material over all 7,589 hill samples. Its independent indexed preview renders the original road textures and Napa hills/horizon. Building/marked terrain, split stitching, resource timing and full-race random-call interleaving are still incomplete; this renderer is not yet substituted for the playable renderer.

Session 22 playable renderer continuation:

- Moving Napa display-list replay now matches every pixel in frames 0, 150, 300 and 750. Frame 750 has 130 original nodes; five empty shadow frames are safely omitted. Its original static scenery image `GRVP2SM2` revealed that non-power-of-two mip heights retain the final partial row. The decoder now rounds mip heights upward; a synthetic truncated-row check covers this boundary. Older CEL oracle files captured only floored rows and continue to verify that prefix; the oracle exporter now captures complete rows.
- A native route raster sweep exposed fractional UV underflow on a thin `HILL13A` quad at sample 825. A new original-function fixture reproduces its exact coordinates and confirms the original unsigned span address wraps to byte 65,535. Native mip objects retain available bytes from their owning resource (up to the original 16-bit address extent); no invented padding or coordinate clamping is used. The 33-list original raster fixture matches every pixel. This preserves reads within the imported backing resource; it cannot reproduce reads outside that owned resource or process-specific relocated pointer bytes.
- Both Napa branches now pass 2,509 geometry views; 503 sampled views also render fully without storage errors. All 7,589 hill samples resolve their materials. This validates safety/material coverage, not full original scene equality or complete building/scenery support.
- The playable Napa race now uses the imported indexed road/hill/horizon composition, uploaded through a nearest-filtered SDL streaming texture. Career starts and quick races load owned scene resources; renderer reset events recreate the texture. Other courses retain their previous renderer. The scene's fixed horizon scroll, buildings/marked terrain, split stitching, scenery placement, terrain collision integration, rider animation/dashboard and traffic/police/music still require work.
- A 600-tick autopilot gameplay screenshot succeeds with the new renderer (`analysis/napa-playable.bmp`). Full race acceptance remains unchecked. User scope remains the complete opening Napa race; no completion is claimed.

Further full-route raster coverage and dashboard work:

- Increased raster coverage from every 25th sample to every 5th sample: all 2,509 Napa branch views render. The denser sweep found an original-style unsigned read beyond the end of one imported family's allocation. Native backing storage now defines unavailable bytes as zero guards, while preserving available imported bytes. This is an explicit native safety behavior for process-dependent original heap reads; it is not claimed as pixel-identical outside owned resource storage. The earlier 'no invented padding' statement applies only to the verified thin-quad fixture whose addressed byte is present.
- A 60,000-tick bounded autopilot run reaches Napa's finish in first place at 9:49 with the live renderer. This covers simulation completion and a final rendered frame; it does not cover rendering every simulation frame, scenery/traffic or original completion parity.
- Added the original high-resolution full dashboard: actual imported RAT/SUPER/SPORT mph/metric artwork, complete-row height returned by the original image loader (RAT is 119 rows, despite the loader's 122-row maximum), imported layout table, player/place/distance text, stamina/condition/target meter selections, speed/RPM angle formulas and needle geometry. Font height/face are recovered from the original constructor (32-pixel Badloc). Font hinting and pen rasterization retain known native differences. Mini dashboard/opponent-distance indicator and original post-dashboard frame comparison remain open.
- The shared playable loop composites the transparent dashboard after riders and recreates its texture on renderer reset. Career units/player/opponent names feed the dashboard. Quick race defaults to mph. The player sprite now follows its projected course sample rather than a fixed screen-bottom baseline; full original animation/hotspot projection remains separate work.
- Synthetic meter boundaries and 24 imported dashboard combinations (three bike classes, two units, four meter levels) pass. The full Napa acceptance list remains unchecked while remaining systems are ported.

Animation and Napa scenery continuation:

- Added bounded RRAN/RRFD animation decoding, including indexed frames, legacy references, hotspots and conditional collision metadata. The imported family corpus decodes 1,663 animations and 6,503 frames (6,066 indexed frames); stale unused original collision pointers are retained without dereferencing them.
- Decoded BORR roadside profiles and DZHR placed-scenery records. Native roadside generation matches all 1,024 samples from 32 direct original-function cases. Static scenery uses the imported category-0/category-4 animation selectors, perspective scaling, hotspots and mirror flags. Moving DZHR records still require traffic integration.
- All 6,471 generated Napa scenery objects resolve indexed frames. Both branch routes render all 2,509 sampled windows, emitting 624,832 nodes without raster errors. Ground interpolation and isolated terrain/scenery random ordering remain integration approximations requiring original comparison.
- The playable screenshot `analysis/napa-scenery.bmp` shows imported trees/fences, course terrain, projected player and dashboard. Buildings, marked terrain, split stitching, horizon motion and remaining race systems are still open.
- Verification: 50/50 native tests and 50/50 address/undefined-sanitizer tests pass; Android debug and release assemble successfully. Logs: `analysis/napa-scenery-native-tests.log`, `analysis/napa-scenery-sanitizer-tests.log`, `analysis/android-napa-scenery-build.log`. Android hardware validation remains unavailable.

Napa buildings and ground continuation:

- Imported building material groups now provide their original nominal heights and roof-cut test. Added original facade, flat roof and exposed end geometry: all 128 direct original cases match 145 emitted nodes, including UV flags and offscreen/absent-building cases.
- Ported ordinary raised building shoulders and curbs using their original category-5 frames and perspective subdivisions. All 128 direct original cases match 429 nodes. Building marking transitions remain open; ordinary faces are connected to the playable Napa scene.
- Added building/hill terrain ground lookup and connected the imported terrain source to race physics and scenery grounding. Direct original ground fixtures are being checked; full race behavior and random-call ordering still require verification.

- Terrain grounding matches all 2,048 direct original samples, including hill-band interpolation, signed slopes, shoulder boundaries, absent buildings and marked flat ground. Both Napa branches still rasterize all 2,509 views after integration (642,765 emitted nodes).
- Added original camera heading accumulation and signed horizon scroll formulas to the live race. The horizon now moves as the course bends; original page-copy scroll direction is preserved. Integrated trajectory comparison remains open.

### Session 23: free traffic on Napa (October 11, 2026)

- Plan 05 task 2 (traffic): actor census in `docs/ACTORS.md` and the traffic foundation ported in `src/traffic.hpp/.cpp`:
  - `VehicleLibrary` loads the course's `DATA/CARS/<name>.CAR` exactly as `0x004465f0` does (ANIM/CANS pairing, sequential CANS, mask from the SNAC cel block). NAPA decodes 10 vehicles with 12 textured RRAN frames each; masks measured from the real cel flags.
  - Spawn timers `0x0044dd00`/`0x0044dd90` with the original integer arithmetic (rate 2500, density 6, reload 3515), ring cursors front/back (camera ±0x5000/0x3000), signed-char lane counts, exact placement `0x0044df60` (lateral, mask filter with the last-vehicle exclusion, four random bytes, serial) and the 16-object pool `0x00438b70(0x210,0x10)`.
  - Movement is a documented approximation of `0x0044e710`: cruise target `((25·phase)>>8)·100`, rate `phase·0x12>>8`, backward kind 2, lane-centre steering with the per-kind drift table, despawn outside `[back, front)`; honk `0x12` when the player blocks the car's path (`0x0044efa0` approximation). Obstacle following/lane changes are not ported.
  - Two original-arithmetic bugs found and fixed during verification: the cruise constant is 25 (`DAT_004b7e8c`), not the timer rate 2500, and the phase byte is a full int slot (192..319) whose uint8 truncation froze cars.
  - `Race::set_traffic` steps traffic inside the tick with the per-step camera advance; `rr-game` draws cars through the verified scenery projection between the composed scene and the rider sprites. Traffic runs on every course with a CARS container; composed drawing only on Napa.
- Verification: 53/53 native tests (`traffic` suite added: exact timer/lane/placement math, pool limit, movement, horn, corpus decode of the real NAPA.CAR and live spawn/draw on the Napa route) and 53/53 address/undefined-sanitizer tests pass. Autopilot screenshot `analysis/napa-traffic.bmp`; a frame diff against a CARS-less assets tree confirms the traffic path changes the render (the shared rand stream also shifts rider AI, as in the original). Android debug and optimized development release APKs build asset-free. No Android device is connected; hardware checks remain open.
- Traffic is **not** oracle-verified (no tick-level object dump exists yet); the census is static. Rider-car collisions, DZHR scripted vehicles/police, and the original car animation/scaling laws remain open.

### Session 23: port review and replanning (October 11, 2026)

- Reviewed the native and Android ports against the plans. Native Debug CTest passes 53/53, including the `traffic` test (traffic was uncommitted when the review started and was committed as `51cb898` by a parallel session during it). No Android device is attached (`adb devices` is empty).
- Findings:
  - Plans 02–07 assumed a `World`/`Object` architecture that was never built.
  - The oracle could not seed, replay inputs or dump objects per tick, so no simulation system is verified against the original. Plan 10 addresses this; a parallel session completed its Tasks 1–2 (`ab8cc79`, `37c4597`) during the review.
  - Only Napa uses the original renderer.
  - The race view ignores the `HiRes` setting.
  - Android has never run on hardware.
  - `/Users/archit/Dev/rr/android` is an older Wine-on-Android setup with a patched EXE, not part of the port.
- Rewrote [plans/00-INDEX.md](plans/00-INDEX.md) with current status, findings, task status per old plan and a new order. Added status banners to plans 01–09.
- Wrote new plans 10–15: simulation oracle, Napa completion, all courses, music and front end, Android on hardware, release and acceptance. Refreshed this file's header, status table and next steps.

### Session 23 (tooling addendum): REA setup and analysis notes

- `npx rea-agents@latest` (REA 6.4.0) now works on this Mac: Node 24 via nvm (the system Node 25.8.1 line is unsupported) and `GHIDRA_INSTALL_DIR=/opt/homebrew/Cellar/ghidra/12.1.4/libexec` with JDK 21. Setup steps, commands and evidence limits are in [TOOLING.md](TOOLING.md); `rea doctor` stays `healthy: false` overall because Hopper/IDA and agent registrations are optional and unconfigured, which does not block the Ghidra path.
- REA's Ghidra backend re-analyzed `0x0044dd90` and produced pseudocode identical to the project's own export — an independent confirmation of the `tools/decompile.sh` pipeline. REA is for spot-checks (one to two minutes per function); the project pipeline remains the bulk source.
- TOOLING.md also collects the session's transferable analysis lessons: Ghidra word-scaled pointer arithmetic, mis-decompiled helpers needing the raw disassembly (`0x0040b5f0`), int-sized object slots (the car phase wrap bug), reversed resource tags, the `CRSR`/`LBTR` fixture layout, runtime-initialised EXE words versus embeddable static tables, the single shared MSVC rand stream (and why feature-diff pixel comparisons diverge globally), and tracing constants to their exact address (`DAT_004b7e8c` = 25 versus the 2500 spawn rate).
- Workflow lesson recorded for concurrent sessions: stage with explicit pathspecs, not `git add -A` (two interleaved commits already happened: `37c4597` and `51cb898`), and check file mtimes for active writers before editing shared docs.

### Napa actor animation continuation with REA (October 11, 2026)

- At the user's request, used `npx rea-agents@6.4.0` with supported Node 24, JDK 21 and the Ghidra provider to analyze `0x0043ae90`. The canonical evidence file is `analysis/oracle/rea-actor-animation.json`; its pseudocode matches the existing original-function export. REA confirms static control flow, not runtime parity by itself.
- Added the owned actor animation playback module, separate from RRAN graphics decoding. It ports direct/fallback selection, queued animation, signed frame durations, unsigned accumulated deadlines, loop frame/count and the pre-advance frame retained for drawing. Missing/invalid native sequence references are bounded.
- Captured 64 private original-function scenarios of 32 steps each. All 2,048 animation states match, including deadline, current/queued sequence, current frame, loop count and retained draw frame. Fixture: `analysis/oracle/napa-actor-animation-fixtures.tsv`; verification: `build/actor_animation-test`.
- Graphics descriptor construction from CANS/EXE tables and live actor node integration remain open; hand-picked rider animation is not claimed replaced yet. Existing in-flight simulation replay corrections were preserved.

- Added bounded CANS descriptor construction (`0x0040b200` / `0x0040b6a0`) and imported rider/cop animation banks. All 194 descriptors in 59 original initializer bindings match every non-pointer metadata byte. The two uninitialized rider slots remain unavailable, as their original BSS descriptor draw pointers are zero. Graphics RRAN data is owned alongside the banks.
- The parser checks complete chunk extents and descriptor tables; truncated CANS and malformed source descriptors fail. Runtime animation/bank integration and merged actor nodes remain the next step.
- Animation playback regression gate: 56/56 native and 56/56 sanitizer tests passed, and asset-free Android debug/release assembled. Logs: `analysis/napa-actor-animation-native-tests.log`, `analysis/napa-actor-animation-sanitizer-tests.log`, `analysis/android-napa-actor-animation-build.log`. Those gate logs precede the added descriptor suite.

Actor graphics follow-up: the owned CANS decoder resolves all 672 view cells from the two rider/cop graphics banks to imported RRAN frames and flags. Captured `0x0040b6f0` calls match every cell. Native `select_actor_view` ports the yaw/elevation selection in `0x0043af60`; 9,312 original calls across all 194 descriptors and 48 angle combinations match the selected image, flags, roll correction and hotspots. Synthetic oracle calls restore shared CCB metadata. Actor playback remains awaiting live dispatch integration. Added the axis-aligned `0x0044ba40` actor projection implementation and a 256-case oracle probe; validation is underway. The previous descriptor gate passed 57/57 tests in native and ASAN/UBSAN builds, plus Android debug/release builds; it predates the view selector and projection additions.

Actor projection/orientation gate: 256 original `0x0044ba40` cases yield 235 accepted nodes with every corner matching. The same fixtures verify `0x00413ae0` camera-relative angles across lateral positions, negative Q8 division, road headings and rider depth doubling. Added `ActorNodes`, which owns 451 populated rider/cop/shadow Bob pages and skips 291 original empty frames; cached texture pointers remain stable across remap insertions. It selects the imported descriptor view, emits terrain-ground shadows first, then vertically displaced bodies, retaining RRAN logical dimensions while sampling HI Bob textures. Corpus exercise emits 311 nodes from all rider descriptors. This is a reusable node builder, not yet the live race draw pass; original triggers, render interpolation and merged scene ordering remain outstanding.

The actor-node regression gate passed 59/59 tests in native and ASAN/UBSAN builds; `tools/ci.sh` completed imported-resource smoke/course checks and asset-free Android debug, and Android release assembled. Logs: `analysis/napa-actor-nodes-ci.log`, `analysis/napa-actor-nodes-sanitizer-tests.log`, `analysis/android-napa-actor-nodes-release.log`. That gate predates normal riding trigger additions. The normal-riding branch of `0x00407f30` and queued fallback `0x0040aca0` are now implemented; 1,024 original player animation steps across speed/throttle/lean/boost thresholds and state transitions match state and selected descriptors. Crash-specific dispatch, combat event timing, and live integration remain open.

Live Napa rider integration: Game owns the imported actor library and simulation-tick animation states. Normal riders now render through indexed `ActorNodes` within each roadway slice, sorted by longitudinal position and inserted before/after midpoint scenery according to their Q8 fraction. Road projection now retains the original tangent angle used for view selection. Imported EXE attack code tables replace inspected attack frame numbers for normal riders. The existing combat timer currently queues return-to-riding through the original playback mechanism; original combat event timing remains unported. Crashed riders retain the old draw path pending crash actor dispatch. Traffic remains a separate pass pending a unified object chain, and split-road dispatch/interpolation still need original full-scene validation. Visually inspected grid and 3,600-tick moving captures. A 50,000-tick bounded run reaches FINISHED 1ST at 9:48 (`analysis/oracle/napa-live-actors-finish.png`), which proves the native course can finish, not overall original parity.

Live actor initializer correction: `0x00446060` sets AND queues riding code 2 before its initial advance. Preserved that queued state in Game so the first attack can return through the original descriptor control rather than loop indefinitely. The existing combat timer bridge remains for subsequent attacks until hit/event scheduling is ported. No original art or golden snapshots are bundled into the game.

Live rider checkpoint verification: 59/59 native and ASAN/UBSAN tests passed, CI imported-asset checks and Android debug passed, and Android release assembled. After the combat-return bridge and queued initializer correction, native/ASAN 3,600-tick rendered smoke runs and both Android variants rebuilt successfully. Final logs: `analysis/napa-live-actors-ci.log`, `analysis/napa-live-actors-sanitizer-tests.log`, `analysis/napa-live-actors-final-sanitizer-smoke.log`, `analysis/android-napa-live-actors-final-build.log`. No Android device is attached; hardware behavior remains unverified.

Actor checkpoint committed as `fda8b2a` (original playback and live Napa normal rider display nodes). Preserved the other session's pending simulation changes. Vehicle follow-up: `0x004465f0` missing-CANS fallback reads the previous animation pair's CANS pointer (`0x474db0 + vehicle*8`), rather than leaving a zero mask. Added that inheritance and optional imported EXE construction of the three vehicle descriptors (`Cars`, `Car1`, `Car2` at `0x474da0/8/b0`) plus owned CANS view groups. A new private original capture checks all ten Napa vehicle masks, view cells and descriptors; validation is underway. Existing synthetic traffic tests retain the lightweight mask-only loader, while the live game supplies the imported executable for full descriptor validation.

Vehicle graphics runtime validation completed: all 207 imported CANS cells, all 30 descriptors, all ten masks and 1,440 original view selections match. Live Napa cars now use those descriptors, original camera-distance code selection (0/1/2), and `0x00446830`'s size-0x500 actor billboard path. Game merges normal riders and cars by longitudinal position within each road slice and around midpoint scenery; the separate traffic draw pass is removed from Napa presentation. Inspected `analysis/oracle/napa-live-car-nodes.png`. Current traffic movement still approximates obstacle braking/lane changes; heading derives from its current direction rather than verified full movement/interpolation. Crashed riders and split-road object dispatch still need integration. Legacy `traffic_draw_nodes` remains for existing standalone inspection/tests that construct a mask-only library; the live game loads full EXE descriptors.

Live car checkpoint verification: 60/60 native and ASAN/UBSAN tests passed; imported-asset CI smoke/course checks and Android debug passed; Android release assembled; sanitizer 3,600-tick live rendered smoke passed. Logs: `analysis/napa-live-cars-ci.log`, `analysis/napa-live-cars-sanitizer-tests.log`, `analysis/napa-live-cars-sanitizer-smoke.log`, `analysis/android-napa-live-cars-release.log`. Original movement, crash/recovery and whole-scene parity remain acceptance work.

Car actor checkpoint committed as `586169a` (imported car views and merged Napa actor nodes). REA follow-up completed: `npx rea-agents@6.4.0 function ... 0x00403d60 --format json` produced `analysis/oracle/rea-crash-entry.json`; its pseudocode exactly matches the existing Ghidra export. Traced crash entry's separate rider/bike motion objects, reason-dependent start animations, collision dimensions, impulses, and later recovery. Added the crash-bike branch of `0x00407a90` to the playback module: stopped bike code 48, random initial 46/47 and queued rolling code 44. Runtime fixtures cover 32 scenarios of 32 steps using the original deterministic replay-random mode, restoring those globals afterwards. Validation is underway; this animation module is not yet live crash physics/recovery.

Crash animation probe correction: the first scratch bike used owner -1, which falls through to original `0x0040a410` and expects a linked rider; the isolated oracle process faulted. Corrected the scratch ownership to the local non-cop path, regenerated a complete fixture, and all 1,024 crash-bike animation steps now match. Added the separate crash-rider branch of `0x00407f30` (fall/roll/stand animation queues and crash state 1→2 when stopped), with a 2,048-step original probe; validation is underway. Full crash entry, independent motion and remount AI are still remaining work.

Crash entry numeric gate: 64 isolated original `0x00403d60` cases now match all 49 tracked fields on both bike and rider, including existing-crash rejection, reason-specific animation, optional independent impulses, launch heights, collision dimensions, cleared motion/control fields and byte flags. The fixture supplies a scratch linked bike/rider and a nonzero landmark to avoid traversing the live course; route/network bookkeeping is outside the native numeric entry routine. Native `CrashConfig` imports the original rider behavior template and crash bike dimensions. All 2,048 crash-rider and 1,024 crash-bike animation steps also match. No live crash path is claimed replaced until independent motion and remount behavior are integrated.

Independent crash actor motion now matches 512 original `0x00439920` cases: momentum transfer, velocity projection, previous-position copy, longitudinal integration and lateral integration. The route callback is tested against a flat private ring; full terrain/wall/contact physics is separate. Offline `0x0040a600` recovery decisions match 256 original cases, including walking direction, strict bike proximity thresholds, delayed automatic remount, animation queues and AI condition restoration after wrecking. Player input-directed walking, route-bound wreck checks, police ownership and the remount reset remain integration work. Private fixtures are regenerated in `analysis/oracle/napa-crash-entry-fixtures.tsv` and never bundled.

Crash module checkpoint before the motion/recovery additions: 61/61 native and ASAN/UBSAN tests passed, imported-asset CI and Android debug passed, Android release assembled. Logs: `analysis/napa-crash-modules-ci.log`, `analysis/napa-crash-modules-sanitizer-tests.log`, `analysis/android-napa-crash-modules-release.log`. Motion/recovery have passed their original-case native comparisons; broader gates will run after live integration.

Remount reset (`0x00404560` / riding copy `0x004067d0`) matches 64 private original cases across 34 fields on each actor and both animation states. The first scratch probe missed the rider's linked bike pointer and faulted in landmark bookkeeping; corrected that link and regenerated the complete fixture. Native reset restores imported normal collision dimensions, resets drag/control fields and queues both actors' idle code; Race restores current stamina separately.

Live Napa crash integration now owns a separate rider motion object and bike/rider playback state, imports the original crash constants, steps independent motion, runs the verified recovery decisions and remount reset, and emits both actors through the merged scene chain. A seeded native Napa scenario separates the actor positions and remounts after 434 ticks, checking restored drag and collision dimensions. Standalone Race clients without an imported actor library retain their synthetic fallback. Full original terrain walls/contact physics, player directed walking/route-bound wreck checks, police interaction, interpolation and split crash tick goldens remain open; this is not full crash parity. Broader native/sanitizer/Android gates are running.

Live crash checkpoint verification completed: 61/61 native and ASAN/UBSAN tests pass, imported-asset CI passes, asset-free Android debug and release assemble, and the sanitizer 3,600-tick rendered run passes. Inspected `analysis/oracle/napa-live-crash.png` (a normal moving frame; the separate crash integration test supplies crash/remount coverage). Logs: `analysis/napa-live-crash-ci.log`, `analysis/napa-live-crash-sanitizer-tests.log`, `analysis/napa-live-crash-sanitizer-smoke.log`, `analysis/android-napa-live-crash-release.log`. Other session's pending simulation/grid corrections remain separate.

Crash checkpoint committed as `63ad4ad`. Police follow-up: actual `npx rea-agents@6.4.0 function ... 0x00406b80 --format json` completed (`analysis/oracle/rea-police-spawn.json`); its pseudocode exactly matches the prior Ghidra export. `0x00407f30` checks offline cop spawning every 4096 ticks (including a zero tick if called), without a difficulty restriction. An explicit isolated original race-ring constructor capture (`RR_ORACLE_POLICE=1`, Napa level 1) successfully creates a cop: bike index 4, class 4 bike/class 8 rider, strength 6, stamina 2200, rider value 171/2, pursuit base speed 120 and target relationship 48000 for the captured player place/personality. New bounded `PoliceConfig` imports the templates, profile 9, rider row 15, bike indices and eight speech IDs; `police_pursuit` matches selected original relationship/speed fields and stats. Spawn and arrest threshold boundary tests pass. This is configuration/decision work; cop chase, collisions, sirens, rendering and busted outcome integration are not yet live.

An original arrest-decision probe is being repaired after an isolated Wine fault. It temporarily substitutes only the nearby-cop lookup after verifying its five original entry bytes, then calls the actual decision/ending routines. Its output is not accepted until a complete capture finishes and the temporary entry/global state is restored. Valid crash fixtures were preserved separately.

Original arrest-decision capture completed with all 1,024 cases and explicit restoration markers for the substituted lookup bytes, instruction cache and game globals. Native `police-test` matches every local arrest outcome and the original 180-tick delay, including normal/crashed speed boundaries, cop/rider longitudinal distance, relative speed, rider exclusion flag, missing lookup result and owner -1 skipping the local outcome. This validates the decision after cop lookup; it does not validate the lookup traversal or live cops. Added restoration checkpoints after earlier isolated faults; a repeat capture is running to check probe stability before accepting further cop work.

Police arrest repeat capture also completed and restored its hook; all recorded decision/restoration rows are identical to the first successful capture. Police and crash scoped tests pass under ASAN/UBSAN (`analysis/napa-police-sanitizer-test.log`), including the live native crash/remount regression. Cop runtime integration and the broader gate remain subsequent work.

Police actor local constructor now matches the original captured numeric bike fields and valid rider fields, using imported behavior callbacks/constants, level bike/throttle selection, reduced AI low gears, condition, stamina, weapon and cop-bank animation initialization. Road ground and initial lane target are supplied by the caller; scheduler links and AI replan/search remain outside this helper. Corrected the capture to honor the original rider pool's 0x208 allocation (`0x00405490`) rather than reading the larger bike layout past it; regenerated the level-1 cop constructor fixture. The full 62-test/native/sanitizer/Android gate is running. No live cop integration is claimed yet.

Directed crash recovery now matches 256 original player walking cases, covering all 16 direction-bit combinations, signed longitudinal targets, strict proximity thresholds, heading/speed selection and resetting the recovery timer when walking input applies. Race maps shared throttle/brake/steering inputs to the original walking directions only during crash recovery, so keyboard/controller/touch use the same implementation. This does not yet add the route-bound wreck branch. Existing 256 offline recovery cases, 512 motion cases, 64 entries/resets and the live separation/remount scenario still pass.

The police actor full native gate found a stale synthetic grid assertion from the parallel simulation edits: the EXE's slot-0 offset is 16384, while 18176 is the measured Napa initial lateral including road-base 1792. Corrected the flat synthetic race test to use the raw 16384 offset. The simulation session's road-base/demo changes remain separate. Full gate will rerun after this correction and the walking additions.

Police local actor/player walking checkpoint gate completed: 62/62 native and ASAN/UBSAN tests pass; imported-asset CI passes; asset-free Android debug/release assemble. Logs: `analysis/napa-police-walking-ci.log`, `analysis/napa-police-walking-sanitizer-tests.log`, `analysis/android-napa-police-walking-release.log`. The native integration test still separates/remounts after 434 ticks. These gates include compatible in-flight simulation/demo changes in the workspace; those changes are kept outside this checkpoint's commit. Next: original combat animation event timing and remaining cop/race integration.

Build fix (October 11, 2026): `src/game_main.cpp` called `Race::set_traffic` but the `TrafficSystem*` setter was missing from `src/race.hpp` (only the `traffic_` member and its use in `race.cpp` remained). Restored the one-line setter. Verified: full `build/rr-game` links, `race`/`traffic`/`crash`/`police`/`actor_traffic` ctests pass, `--ticks 120 --screenshot` headless race renders (Napa GET READY frame), `--menu-frames 120 --screenshot` renders the menu disclaimer. Game left running via nohup (`./build/rr-game --assets /Users/archit/Dev/rr/cdrom/ROADRASH`).

UI cleanup (October 11, 2026): removed the bottom keyboard-instructions overlay from the race HUD in `src/game_main.cpp` (`draw_hud`). The two `SDL_RenderDebugText` lines at the bottom of the view ("Up/Down throttle/brake, Left/Right steer, Z punch, X kick, C backhand" and "P/Esc/Back/Start pauses. Enter/Start/tap resumes. Q quits") are no longer drawn.

### Session 24: Napa playtest review and replanning (October 11, 2026)

- The owner played the Napa race and reported:
  - no traffic collision;
  - the right dashboard meter glitching;
  - bike graphical glitches;
  - an apparent split between the "Napa demo" and `rr-native`.
- Root causes:
  - `Race` has no rider–car contact pass.
  - The tachometer reads bike `+0x2dc`, which only `bike.cpp` initialises; the original `0x004349c0` updates it, but the port keeps that logic privately in `sound.cpp`. The distance readout reads an unmaintained `+0x44` (shown as a constant `25.5` in screenshots 1,500 ticks apart).
  - Native rider/bike/shadow nodes have not been compared with original display lists. Autopilot screenshots show offset or doubled bike parts and dithered blocks.
  - `./run-macos.sh` launches the research viewer (`rr-native`), not `rr-game`, and `build-release/Road Rash Native.app` was built October 10, before the Napa scene and actor work.
- Review of newly landed work: plan 10's replay currently re-anchors the RNG every tick and replays the attract-demo AI branch for the player. This localises divergences but is not acceptance; plan 10 Task 5 now requires a single anchor and one human-control scenario.
- Added [plans/11a-napa-playtest-fixes.md](plans/11a-napa-playtest-fixes.md). Updated the [plans/00-INDEX.md](plans/00-INDEX.md) status table, order and findings, plan 10 Task 5's acceptance rule and plan 11's status. Added `analysis/playtest/` to `.gitignore`. No source code was changed in this session. A parallel session has uncommitted edits in `src/game_main.cpp` and `src/race.cpp`; they were left untouched.

### Session 25: Napa playtest fixes — launch paths and tachometer (October 11, 2026)

- Reviewed [plans/11a-napa-playtest-fixes.md](plans/11a-napa-playtest-fixes.md) and began executing it.
- **Task 4 (one game, every launch path)** done: `run-macos.sh` now builds and
  launches `rr-game` (with `--assets` when the disc is mounted, else the
  first-run import); new `run-viewer.sh` launches the research viewer, which is
  renamed `rr-native` → `rr-viewer` in CMake, the viewer usage string,
  `tools/ci.sh`, README/RUN_NATIVE and plans 00/01/04/08/15. `tools/build_macos_app.sh`
  stamps `RRBuildCommit` (from `git rev-parse --short HEAD`) into the bundle
  plist and re-signs; `tools/verify_macos_app.py` fails a bundle whose stamp
  differs from HEAD, and `tools/ci.sh` fails a stale `build-release` bundle
  (or rebuilds it under `RR_STANDALONE_APP=1`). Rebuilt the standalone bundle
  and confirmed the verifier passes; the stale October 10 bundle fails as
  expected.
- **Task 2 (tachometer)** rpm part done: `update_engine_rpm(Bike&)` is a
  literal port of `0x004349c0` (gear-ratio/speed → `+0x2dc`, `±0x2000` slew,
  `+0x1000` airborne capped at `0xffff`, `−0x1000` after a crash, `+0x2e8`
  speed cache, `+0x2e0 = 0x9ffe`). `SoundSystem::update` now takes `Race&`,
  calls it for the player and the two nearest opponents, and reads `+0x2dc`
  for the engine pitch, deleting the private `rpm_`/`rpm_speed_`. `bike_test`
  and `hud_test` cover the port and the needle-vs-`+0x2dc` relationship; the
  native CTest suite is 62/62.
- The distance readout is traced to the global `DAT_004b8a34`
  (target-opponent distance, `0x00438550`, in feet) but not yet ported (needs
  the `0x00407410` cumulative-distance walk and oracle calibration). Details in
  REVERSE_ENGINEERING.md and VERIFICATION.md.
- Tasks 1 (rider–car collision) and 3 (bike/shadow nodes) remain: both need
  new original captures/fixtures.

### Session 26: replan for Napa completion, all courses and settings screens (October 11, 2026)

- The owner asked for plans to: finish Napa, then port all remaining courses; complete AI, physics, collisions, animation and graphics; and add game-style settings screens (resolution, sound, etc.).
- Reviewed the code and plans against that request. Findings:
  - Napa's remaining work was spread over plans 10, 11 and 11a with no single view per system. Several gaps had no owner: the knock-off decision `0x004052c0`, rider–cop contact, roadside-object collision, the weapon lifecycle, off-road drag and reverse riding, per-tick animation verification, the original needle pen and opponent list, and a native determinism/soak gate.
  - Plan 12 (all courses) was too thin: it missed per-level race start/finish (the course-length table `0x00468f58` differs by level, but the port runs the whole route), the `make_race_setup` extraction that plan 10 Task 4 specified but never landed (`src/race_setup.*` does not exist), and per-course trackers.
  - Settings: `Settings` already stores the original registry values and the Graphics Options popups work, but the sound mix, controls and jukebox handlers are stubs (`frontend.cpp`, `return 0`), the race ignores `HiRes`/`Inset`/`HiResBikers`, the window is fixed (960×760, letterbox, vsync on), and the pause screen is SDL debug text.
- Added [plans/16-gameplay-systems.md](plans/16-gameplay-systems.md) (completion matrix, Napa critical path, 7 new tasks), [plans/17-all-courses-port.md](plans/17-all-courses-port.md) (supersedes plan 12; all maps playable early via `make_race_setup`, then CITY → HIWAY → nitro → MEDLY → CANYN parity) and [plans/18-settings-and-options.md](plans/18-settings-and-options.md) (original options screens plus native Display/Sound/Pause screens built from original menu geometry, fonts and colours; fullscreen, scaling, filtering, vsync; low/inset race views).
- Updated [plans/00-INDEX.md](plans/00-INDEX.md) (status table, order, definition of done), and added pointers in plans 11, 12 (superseded), 13 (Task 3 moved to plan 18) and 14 (Task 2 Step 3 moved to plan 18).
- No source code changed. Uncommitted work from a parallel session (`src/race.*`, `src/game_main.cpp`, `oracle/aweman_proxy.c`) was left untouched.
- Follow-up in the same session: the owner clarified that "graphic settings" means the game itself must render at higher resolutions, and asked for plans for the recommended upscaling approach. Finding: the scene pipeline emits resolution-independent Q16 view coordinates (`DrawNode`), and only `ViewMode::map` fixes the 640×480 surface (shift 15, bit 0 cleared), so higher render scales need a scaled final mapping plus surface-size fixes (`TextureEdge` rows fixed at 480, `int16_t` vertices, horizon/HUD/menu images) rather than a new renderer. Added [plans/19-high-resolution-rendering.md](plans/19-high-resolution-rendering.md) (render scale 1–4× and match-display, 640×480 kept bit-identical, band-threaded raster, widescreen decision-gated) and [plans/20-asset-upscaling.md](plans/20-asset-upscaling.md) (local upscale cache, MMPX for indexed textures and sprites, Real-ESRGAN for menu artwork via a script, GPU screen filters, AI for in-race art experimental). Plan 18's "non-goal" section now points to them; the index lists both.

### Session 27: enhanced visuals and toolchain plans (October 12, 2026)

- The owner asked whether C++ was the right language (answer: yes; keep C++, harden the toolchain instead of a Rust rewrite) and for plans for the graphics enhancements and the hardening steps discussed.
- Code facts used: the race loop is fixed-step (`TickClock`, 60 ticks/s) and redraws from live `Race` state, so interpolation needs a captured per-tick view state; `DrawNode` carries no depth, so perspective texturing and fog add per-vertex depth from the slice projection; textures are affine (`TextureVertex` has no depth term); the draw distance is the constant `view_slices = 33`. Homebrew LLVM 22 is installed with libFuzzer and clang-tidy; Apple clang has neither. Threads today: the import worker, the SDL audio callback and SDL event watches.
- Added [plans/21-enhanced-visuals.md](plans/21-enhanced-visuals.md) and [plans/22-toolchain-hardening.md](plans/22-toolchain-hardening.md). Plan 19 Task 9 (widescreen) now hands implementation to plan 21 Task 8. The index lists both plans and their order.

## Makefile

`native-port/Makefile` wraps the README workflows: `make build|run|viewer|video|test|smoke|app|apk|apk-release|ci|clean`. `make help` lists targets; `COURSE=`, `BIKE=`, `ASSETS=`, `BUILD_TYPE=` override defaults. `run` omits `--course/--bike` unless set, since those flags force quick-race mode.

### Session 28: rider-car contact, single-anchor replay, tooling gates (October 12, 2026)

- **11a Task 1 (rider–car collision)** trace, fixture and port landed:
  `collide_traffic`/`contact_car`/`crash_car` in `src/race.cpp` (player and
  AI, traced point in `Race::step`), `cars_mut()` in `src/traffic.hpp`.
  Fixture `analysis/oracle/napa-car-contact.tsv`: 256 isolated `0x004049c0`
  calls, 0 faults. Golden comparison and the live `--steer-into-traffic`
  log remain open; stamina-rise in crash cases is an open trace question.
- **Plan 10 Task 5 Step 2a (single-anchor replay)** landed: default one RNG
  anchor at green, `--reanchor` diagnostic only, `napa-solo-human`
  scripted-controls scenario (1,431 frames). The tick-120 speed gap
  root-caused to the harness (missing personalities) and fixed — physics
  green through frame 138; next divergence is the unported traffic-spawn
  timer RNG at tick 139 (pinned in `tools/sim_known_divergences.json`).
- **11a Task 3 (bike/shadow nodes), Steps 1–2 done October 12:** scene dump
  v4 + `napa-drive-v4` recapture, `--native-actors` CONFIRMED FAILING 4/4
  (root cause isolated to the view/subframe selection base); fix + Step 4
  open (dispatch hooks, recapture, mid-jump frame).
- **Agent tooling P0/P1** landed separately: workspace ownership, gate
  receipts, capture validator, build provenance, CI/packaging wrappers.
- Gate: full Debug suite **63/63 pass** (62 + agent-tooling), UBSan affected
  suites pass (pre-existing `sim_fields` div-by-zero on a zeroed spec
  confirmed on pristine HEAD, documented, not fixed here), viewer smoke,
  course sweep and headless race render exit 0. The release bundle is stale
  by design until the next `tools/build_macos_app.sh` run (verifier fails
  it as intended).

### Session 29: NAPA doc refresh — four landed slice families (October 12, 2026)

- Gate this turn: full native suite **66/66 pass** (`ctest -N` enumerates 66
  tests, up from the session-28 63).
- Landed since session 28:
  - Combat (`ba7e127`, `42e3e4d`): shared `knock_off` routing, descriptor-frame
    weapon hits, nitro, roadside hazards, in-tick animation, crash-impulse order.
    Tests: `race`, `crash`, `sim_fields`.
  - Courses (`e7dc0a6`): 26-combo census with level-tagged finish table. Test: `course`.
  - Music (`fcaf29b`, `7edce12`): MIDS parser, SF1 banks, factory presets,
    streaming PCM queue, race hooks, jukebox triggers. Test: `music`.
  - Render (`cea33a2`): Q16-preserving wide path, signed mips, MMPX cache,
    band raster. Tests: `mmpx`, `upscale_cache`, `texture`, `raster`, `view`.
  - Replay (`ce8e1eb`, `e4b32a8`): driver-axis harness with shared factory
    prerequisites and strict comparator. Tests: `oracle-sim`, `sim_fields`, `golden`.
- `NAPA_IMPLEMENTATION.md` acceptance boxes stay open per plan 11 Task 8
  Step 4 (no full-race `napa-acceptance` golden yet); per-slice evidence
  lines recorded under the checklist.
- Remaining open work: finish-crossing golden, 15-rider golden, proxy captures
  of car/police/animation fields, live `game_main.cpp` music wiring (owner:
  playtest-visual-fixes session), visual-session-owned files untouched.
- No pixel/frame equality with the original is claimed beyond the measured
  fixtures cited above.

### October 12, 2026 — audio, AI and launch playtest fixes

Fixed the engine disappearing as the camera left the start: the audio listener
now follows the camera. Race MIDI is now pumped from the live race render loop
(independent of the effects channel), buffered with backpressure, paused with
the mixer and safely detached before its stream is freed. Normal menus and
interactive `--race` both play race audio; deterministic `--ticks` runs remain
silent. Quick races now honor saved engine/effects/music levels and mute.

Corrected the original overtake gate's AL return and implemented its passing
point writes, so a selected replan target no longer suppresses a fresh racing
line. Added a scalar demo-speed fallback exposed by this correction. Napa's
seven opponents all advance during the final ten seconds of a one-minute
regression. This does not establish original simulation parity.

Launch now presents a loading label and moving activity bar before disc-data,
menu-music and video decoding. Native data/video/menu-music decoding runs on
a worker while SDL rendering/event pumping stays on the main thread; the
non-threaded web build presents a static loading label. Menu/sound preparation
also presents a stage label.

Pause and wipeout visuals are **not exact matches**: the in-race pause menu
(plan 18 Task 8: RESUME/SOUND/DISPLAY/CONTROLS/QUIT RACE over the darkened
race frame) is native, not the original Windows modal message; `WIPEOUT!` is
still a debug-text overlay. Original crash-frame pixel acceptance remains
open. Details/checks: [playtest handoff](agent-handoffs/playtest-audio-ai-loading.md).

### October 13, 2026 — settings screens done (plan 18 Tasks 7–9)

Configure Input keeps the original nine rebind rows with live key labels;
press-a-key binds through the traced `0x0040fbf0` conflict rule (Escape
cancels), storing keyboard codes low-byte-first per `0x0040e760`. Native
Gamepad (swap punch+kick, 10–40 % dead zone) and Touch (opacity, left-handed
mirror) pages hang off it; the race honors custom tables, swap, zone and
handedness. The in-race pause menu (RESUME/SOUND/DISPLAY/CONTROLS/QUIT RACE
with confirmation) opens over the darkened captured frame from
P/Esc/Start/Android Back and lifecycle freezes; `draw_pause` is gone. The
`show_fps` row draws FPS + ticks/sec top-right (Futura Bold 24, `#DEFFF0`,
shadowed, transparent background, twice a second). USER_GUIDE has a Settings
section. `tools/ci.sh` (native + android-debug) and 73/73 ctests pass; the
macOS bundle is refreshed. Deviations: rebinding stays available while
racing; sliders apply levels live with no preview click.

### October 13, 2026 — review correctness fixes, one factory, field proxy, race shell (plans 25–28)

Plans 25 (review correctness fixes), 26 (one race construction path), 27
Task 1 (safe field proxy) and 28 Tasks 1–3 (race shell) landed on main.
Evidence: handoffs `25-1/2/3/5/6/7`, `26-1/2/3`, review
`agent-handoffs/archive/2026-10/review-of-25-28-20261013.md`.

- **Plan 25 (done: Tasks 1–3, 5–7):** restart-after-finish restored plus a
  shared `reset_clock` that also resets view history; render-worker
  exceptions propagate (lost-wakeup fix); RGBA mip cache is content-keyed;
  saves are atomic; dead `Race` state removed; ASan `texture_edge_wide`
  overflow fix + widescreen test fix with reproductions.
- **Plan 26 (done):** game, race-soak and oracle replay build races through
  one factory (`load_race_assets` + `build_race`); construction guard test
  pins the single path. Intentional game behaviour change: personalities,
  ride bank, nitro, ring and finish hooks now attached (wiring report).
  Correction (review finding 3): `wiring().finish == 1` means the finish
  hook is attached at the route-end placeholder — not the original finish
  (plan 17 Task 1 still open).
- **Plan 26 follow-up (review findings 1–2, this turn):** career races keep
  the shop's purchased bike (`bike_from_table` defaults false; oracle
  harness and sweeps opt in; `race_setup_test` pins it); a bad `.CAR`
  degrades to no-traffic (`optional_traffic`) and `restart()` refuses
  without course data instead of crashing. Soak + oracle-sim identical
  (known divergence frame 207 unchanged, same `trace_sha256`).
- **Plan 27 Task 1 (done):** `Bike::i32` is a memcpy-backed proxy (no
  aliasing UB, x86 wraparound defined); Debug/Release determinism
  confirmed (`determinism OK (3 hashes)`); `bike_field-test` pins
  semantics. Tasks 2–3 (named offsets) decision-gated on the owner.
- **Plan 28 Tasks 1–3 (done):** pure `race_action()` with tests (two
  intentional changes vs the old loop, kept: no-menu Back toggles pause;
  paused finger MOTION/UP reach touch — finding 4); RAII SDL handles
  (`sdl_handles.hpp`, counting-deleter throw test); FPS overlay + quit
  prompt as units (`race_overlay.cpp`). Baseline BMP hashes
  (menu/race/video) identical after every task; 89/89 gate at Task 1,
  82 fast + soak/oracle green at Tasks 2–3. Task 4 (`RaceScreen`) in
  progress (shelved for the review findings; stash
  `plan28-task4-wip-shelved-for-review-findings`).

### October 15, 2026: frozen scheduler cadence translated, replay 210 -> 376 (plan 10)

- New `sched900` SIM capture (900 ticks, new scheduler-word dump) proves
  frozen interval 4, lasts 1,5,...,209, dt 4. Native `Rider` models it
  (`sched_*` + `sched_live` gate, hydrated from the capture; legacy
  captures keep dt 1 with identical signatures). Solo replay detaches
  native traffic to match the oracle-nulled pool (first native spawn had
  been a 6-draw isolation break at tick 319).
- Single-anchor demo replay now diverges at tick 376 (was 210): a single
  `0x0044dd90` spawn-timer lane draw against the nulled pool places no
  car. Timer state/rates/taps are Task 7 work. Old goldens unchanged.

### October 16, 2026: genuine human-branch proof + exact acceptance (plan 10)

- The proxy dumps per-tick `player.demo_hold` and `RR_ORACLE_HUMAN=1`
  selects the player-control branch at race start (bootstrap stays on
  attract otherwise). New `sched-human-20261016`: 1431 contiguous ticks,
  hold 0 throughout, real recorded human inputs. Previous "human" goldens
  are demo-branch and prove no player control.
- Harness infers `driver=human` with no override (branch proof by
  instrument, not name). Position responds to human throttle identically;
  first divergence is tick 2 `ai_target_x` 18176 vs 0 (native human path
  skips target selection). RNG in sync. Five more human gates are
  demo-tuned natively (steer, skid, move-push, sched-copy, wreck).
- Strict `verify_sim` acceptance on the human golden: `unexpected-failure`
  at the tick-2 gap (`acceptance_met: false`; infra checks now pass:
  receipt, `control_branch: human`, capability). Demo solo diagnostic
  stands at tick 376 (spawn-timer lane draw). Nothing re-anchored, no
  golden altered, no allowlist expanded.

### October 16, 2026: human launch matches, first gap is wall scrape (plan 10)

- Frame-0 brain hydration extended to all solo captures (spawn-frozen
  targets) plus player scheduler hydration and post-drive metadata
  stepping: human replay now matches through tick 148 (was tick 2).
  Launch ramp root-caused to tick-2 dt ordering with oracle ramp words as
  ground truth; native spec words verified equal (table 248; cap/rate
  micro-deltas truncation-absorbed).
- Tick 149: full-throttle left-wall scrape (speed 3351 -> 2739, no crash)
  vs native 843 -- the `0x0043a610` wall port (plan 11 Task 4). Demo at
  tick 376; all legacy goldens byte-identical. No regressions.

### October 16, 2026: contact class routing fixed, human to tick 189 (plan 10)

- Bike-bike contact now routes on separate()'s `+0x19c & 0x40` width flag
  per `0x004049c0` (the geometric `contained` predicate was a port
  invention; the car path already used the flag). Human replay advances
  tick 149 -> 189 with identical contact sounds/push; demo and legacy
  goldens byte-identical.
- Tick 189: 24-vs-27 terrain-jitter draws at a ring slice build (same
  slice both sides) -- terrain iterator integration is the next slice.

### October 16, 2026: spawn gating landed, timer schedule deferred (plan 10)

- Nulled-pool spawn gating (draw lane pick, skip placement) is implemented
  and order-verified, but attaching it moves demo 376 -> 319: the native
  behind-timer schedule fires where the original is silent. Harness keeps
  traffic detached (best replay); the gate waits for Task-7 timer inputs.
  All goldens and focused suites at baseline.

### Countdown traffic-timer continuation (October 15 session, plan 10 Task 7)

- Integrated the unfinished `traffic.*` timer extraction after review.
  `Race::countdown_step` now banks zero-distance traffic timers, applying
  initial ring zone events first without car/controller movement. Replay
  banks the hydrated first tick explicitly and keeps solo traffic attached
  with placement disabled, preserving the original lane-pick draws.
- Single-anchor `sched900-20261015` demo replay moves first divergence
  **376 -> 575** (brain target resolver reports -1/native 13; the
  original pointer is nonnull, so identity mapping needs correction).
  Human `sched-human-20261016` remains at tick 189 (terrain RNG).
  Old diagnostic goldens and allowlist are unchanged. This advances Task 7;
  traffic-field and full human acceptance remain open.
- Synthetic regressions cover 59 stationary hold ticks, density-event order,
  initial-fill reuse, and disabled-pool expiry draws. See
  [session handoff](agent-handoffs/countdown-timers-20261015.md).

### Stable replay actor identities (October 15 continuation, plan 10)

The target pointer mismatch at demo tick 575 was a harness bug: original
node address 20976340 still identified native rider13, which had moved
away from its starting position. Replay now freezes address-to-rider
identity after initial hydration. Types4/8 resolve to rider owners;
unrelated actor types cannot alias a rider merely by sharing position.
Duplicate addresses and ambiguous rider matches fail explicitly.

The same single-anchor demo trace now reaches tick576: original target
lateral `brain+0x194`=-16384, native29952, with RNG still matched. Human
remains at189. New synthetic tests reproduce both movement and reuse of
the initial position by a different rider. No simulation behavior,
golden or known-divergence expectation changed in this correction.

### Independent frozen rider-half state (October 15 continuation, plan 10)

Frozen type8 halves now retain captured position/speed and select their
own route slice for AI scans, independently of a bike moved by contact.
Live attached halves keep their existing behavior. Hydration reads the
linked half speed from the bike node's half108; older schemas without
that word retain zero and do not gain a new required field.

Demo sched900 advances576 ->584, where speed is6188 original/6187 native
with RNG still aligned. Human remains189. Synthetic tests cover detached
position/speed, slice-dependent passing side, linked-half field identity,
legacy compatibility and atomic validation. This is partial component and
demo-trace progress, not complete human or traffic-field acceptance.

### Seeded all-eight-point terrain oracle (October 15 continuation, plan 10)

Added opt-in original component capture `RR_ORACLE_TERRAIN_SEEDED=1` and
a separate seeded fixture comparator. Captures call the original terrain
functions using six CRT seeds across forward/backward/roundtrip paths,
record all8 point windows/coordinates/slopes plus iterator/CRT state,
and exit cleanly from a frame hook outside the loader lock. Existing
fixed-random fixtures and default simulation behavior remain unchanged.

New validated one-frame original capture `terrain-seeded-20261015`:
**960 hidden-state snapshots +18 terminal RNG states match exactly**.
Existing864 original slice comparisons also pass. This proves the tested
synthetic component traversals, not the race's terrain interleaving.
Next: compare actual front/back race-walker hidden state from setup.

A fresh600-tick demo capture reproduces speed gap584 and exposes original
throttle7352/force26 vs native7330/25; original throttle drops612 per tick
at582–585. RNG/earlier compared fields remain matched. No one-unit bias
was added; trace imported spec/ramp inputs next. Original proxy installation
was restored to its pre-session hash after both captures.

### October 15, 2026 — plan32 screen/canvas and plan10 frozen slice scans

- Plan32 A/B complete in109c640: actual screen restart/pause/resume/reset
  harness, consistent supplied timestamps, lost Resume event fix, FPS cache
  recovery and explicit modular interpolation. Original pixels match at
  30/60/120FPS; full bike state/RNG agree with interpolation on/off. Native
  focused4/4, ASan/UBSan4/4 plus canvas1/1 pass; injected SDL resets do not
  establish GPU/hardware recovery.
- E canvas slice109c640: indexed HUD/panels survive horizon clear; RGB skips
  unused indexed HUD pass and retains horizon storage.6 RGB and6 Original
  no-HUD frame hashes remain exact; warmed horizon allocates zero, complete
  RGB draw one fewer allocation. Bounded M3Pro software-renderer Release
  benchmark RGB medians13–24% faster. Memory/device and cancellable first
  upscale attachment remain open; no broad frame-performance acceptance.
- Player-row regression60bae61 validates the only unset sentinel-1 and
  original row0/8 mass-scaled ramps. Frozen scan slice guards now retain
  independent NULL bike/half memberships from measured metadata. REA and
  assembly confirm00449ce0 gates. Same single-anchor demo first735→893;
  600ticks match compared fields. Human declared terrain trace exposes
  setup mismatch at tick1, earlier than coarser RNG189. Strict acceptance
  remains unmet; fixtures/allowlist unchanged.
- Main strict Debug full build and99/99 CTests pass after each integration
  (presentation173.89s; frozen scan151.17s), zero skips. Shared source changes
  integrated serially from detached worker with clean affected main files;
  old retained integrator claims not reaped. Corrected clean-checkout doc
  dependency by tracking the delivered countdown-driver handoff.
- Next: contact flags at893, human terrain setup order, cancellable upscale
  loading. [Integration evidence](agent-handoffs/codex-sim735-integrate-20261015.md).

### October 16, 2026 — original countdown banner, no wipeout text

- Replaced the native SDL debug banners `GET READY n` and `WIPEOUT!` with the
  original presentation: half-brightness palette (`DAT_004b8030`) during the
  0xb4-tick countdown, Badloc 0x80 digit `t/60+1` with black shadow 120 rows
  below the view top (`0x00446c30`/`0x00447b20`), no dashboard on the
  start-value frame (`0x0044c780`). The original has no wipeout or finish
  banner; native `FINISHED` stays only in the quick-race shell.
- Gaps: start fade-in not ported, no pixel capture comparison, low-resolution
  metrics. [Handoff](agent-handoffs/race-banners-20261016.md).
- Follow-up: race-start fade-in (`0x00415d90`, 63 × 14 ms, `trunc(half ×
  0.96^(n−1))`, blocks the race, clock starts after) and race-end fade-out
  (`0x00415e90`, cumulative `trunc(x × 0.96)` from the half palette) ported
  as channel maps driven by `RaceScreen`; low-resolution countdown metrics
  (0x48, +60, shadow 1, pixel-doubled). Remaining: pixel capture comparison,
  quit-confirm fade/delay, pause message.
- Follow-up 2: original in-race pump messages (quit prompt 316 on career
  Escape, "Game paused." 317 on the Pause key) drawn in small Badloc over
  the half palette; quit-confirm ends the race at once (`WM_QUIT`, no 0xb4
  run-on: corrects the earlier claim) with the fade-out; fade-in holds the
  previous screen until its first upload; fixed catch-up ticks on the
  fade-out release frame. Countdown digits compared with the original
  `napa-dash-solo` post-dash capture: "1" 746/750 white pixels, "3"/"2" one-row
  GDI hinting offsets. Open: palette-state capture (proxy `SetEntries` hook).

### October 16, 2026: mountain/cliff terrain sides (Pacific Highway black wedges)

- Cause: terrain type 1 (`NTMR`/`DWDR` mountain, CANYN's first section, also
  HIWAY/MEDLY) was decoded but never drawn. `0x0043bef0` draws types 1-3 with
  shoulder `0x0041d260` then `0x0040dfd0` (height >= 1) or `0x0040d030`
  (height < 1), and the native scene handled only types 0 and 4. The wedges
  were the clear colour below the 320-row horizon. Not caused by cb44f8b
  (4:3 showed them too).
- Ported: `wall_draw_nodes` (`terrain_panels.cpp`): wall face with the near
  edge one column out, plateau tiles four lanes wide to the view edge, cliff
  face to sea level, WATER128 (`GLOBAL.RSC` CEL 0x10, `0x00441870`) for the
  right side. The left water strip is emitted only fully off-screen, as in
  `0x0040d2f2`. `course_scene.cpp` applies `0x00454240`'s wall transform
  (outer shoulder edge ∓ exported offsets, height < 1 becomes -elevation),
  projects base/top like `0x00444630` (+0x70/+0x80), ends mountain shoulders
  at the wall base (`0x0041d260`) and feeds the points to hill transitions.
  `PathSample::elevation` carries the HTPR absolute height (`0x00451970`,
  `0x004519f0`). Type-1 sides drop pair-B shoulder heights and marked ground.
- Remaining: type 3 split sides (`0x0040dac0`/`0x0040d420`), building-to-
  mountain transitions (skipped), node flags 0x20000/0x40000, no pixel
  comparison with the original frames. [Handoff](agent-handoffs/pch-terrain-20261016.md).

### October 16, 2026: tunnels and building-to-mountain transition

- Correction: the "type 3 split sides" listed as remaining above are
  tunnels. `0x00454240` makes a sub-slice type 3 for marking flag 0x40; on
  the disc that occurs only on mountain sections (MEDLY s13, CANYN s0/s7/s14,
  runs of 25-33 samples).
- Ported: `tunnel_arch_nodes` (`0x0040dac0`: colour-0xfd backdrop on the
  farthest strip, six panels from the `0x00465a54` table, category-3
  materials of the CESR selector, lit set every fourth sample),
  `tunnel_portal_nodes` (`0x0040d420`: mountain face, trim, curb faces, lean
  from `0x0044f5a0`/`0x0044f770`), tunnel branches of `0x0040dfd0` and
  `0x0040d030`, raised sidewalks via `building_shoulder_nodes` (`0x0040bde0`
  with category-3 frames 2/3), and the type-3 transform (mountain side,
  height >= 800, doubled open-side edge, halved shoulders, 16-unit curbs).
  Tunnels are a render-only flag in `CourseScene`; `TerrainSlice::type`,
  which the race reads, is unchanged.
- Building neighbour of a mountain pair: the wall/cliff builders now read the
  building's facade offsets/heights as `0x00444630` leaves them at
  +0x70/+0x80. No route on the disc has that adjacency (0 on all courses,
  both branches); covered by a synthetic case only.
- Remaining: sub-slice type 3 in simulation (`0x00439b10`), no pixel or
  display-list comparison, scenery guard rails at the CANYN portal are a
  separate scenery question. [Handoff](agent-handoffs/pch-tunnel-20261016.md).

### October 16, 2026: guard rails, tunnel ground, native terrain vs original

- Guard rails: `0x0041c200` from `0x00443c00` for CESR event 5 (selector
  param 0, signed shoulder offset param 1, left/right bits param 2; height
  `((1<<+0x1b)<<+0x19)*4` from the category-6 group header, new
  `CelGroup::row_shift`). Emitted after the next nearer pair's terrain, in
  `0x00443c00`'s side order.
- Occlusion: mountain/tunnel strips take `0x00444630`'s maximum row from the
  wall tops (a cliff's sea-level foot), not the road row.
- Tunnel roof: the original never draws `0x0040dac0`'s lit set; its
  `0x0040ccf0` 'CLGP' test at pointer−8 fails because group pointers address
  the chunk tag itself. All 88 captured tunnel strips hold the plain set.
- Simulation: sub-slice type 3 is now set for marking flag 0x40, as
  `0x00454240` does, so the race sees tunnels (`surface_class` wall class,
  `road_panels`' type-3 path). `terrain_height` ports `0x00439b10` cases 1
  (wall ramps, the drop to height 0 beyond a cliff, absolute-top limit) and
  3 (sidewalk heights, 16 when empty). This changes CANYN/HIWAY/MEDLY ground
  off the road; Napa is unchanged.
- New `oracle-terrain` ctest: `oracle-scene-test --native-terrain` builds
  the native scene for each captured frame, with the dump's live-RNG terrain
  values injected, and requires every original terrain node to match.
- Remaining: CourseScene's terrain jitter still comes from an isolated
  random stream (live race RNG integration, plan 10 TrackRing); wall/rail
  collision `0x0043a610` is still the plan 11 stand-in (it reads the rail
  height `0x0041c200` stores); billboards, signs and trees differ from the
  original on every course; MEDLY building curbs/facades with an off-centre
  camera (36 nodes, 7 grounds in `medly-jump`) differ, unchanged since before
  this session. [Handoff](agent-handoffs/pch-terrain-parity-20261016.md).

### October 16, 2026: side streets and building ends (Peninsula)

- Ported `0x0040ba10` (side-street pavement tiles stepping out from the
  transition point; flag 16 curb faces, flag 8 colour-0x4d gap fill to the
  nearer strip), `0x0040bde0`'s flag-16 curb face and `0x0040c450`
  (side-street facades; `0x00441730` always returns 0, so runs are
  `max(4, 0x00465a40[0])` then 2, alternating selector and entry 0, with
  `0x004ba180` choosing 8 or 16). `0x0040cbb0` end faces and the side
  facades draw the group's first cell: `0x0040c3f0` -> `0x0044a9b0` resets
  the selected template. `0x004504f0` copies drop the flip bytes.
- Comparator: textures are now identified by the dump's mip-0 hash, not by
  name (several disc resources share pixels under other names). This
  exposed and fixed the City end-face cell choice (26 nodes).
- Result: every terrain node in `medly-jump` (6456), `city-moving` (1006),
  `city-scene`, `napa-drive` and the CANYN tunnel capture matches. New ctest
  `oracle-terrain-medly`. The 7 MEDLY rider grounds that differ are riders
  whose +0x24 does not follow their own sub-slice (jump scenario), not a
  terrain-path difference.

### October 16, 2026: walls and rails (0x0043a610), bounds wreck, ring terrain findings

- `Race::walls` now transcribes `0x0043a610` on sub-slice values
  (`Race::sub_slice`): CESR-event-5 guard rails (left/right bits, offset
  +0x3a, height from +0x3b bits 2..7, set by `0x0041c200` for strips up to
  31 ahead of the camera, else 0x40), type 0 buildings (shoulder edge, empty
  selectors widened by 250·frac/3, angled entry), type 1 mountains (the
  player is held at the wall bases or, on a cliff side, at the cliff foot;
  other riders at the outer shoulder edges), type 3 tunnels (angled entry),
  types 2/4/5 no walls. Outcomes: lateral bounce `+0xf0 *= -100/256`,
  angled stop and step back (`0x00439270`), reason-3 rail crash with null
  impulses, reason-6 crash above 0x9c3 lateral speed with event-5 sound.
  The synthetic-route fixtures keep the old stand-in.
- `step_crash`: the walking player (animation 38) on a mountain sample is
  wrecked outside the wall bases (`0x0040a6d5`, outcome 5).
- `wall_profile()` (terrain_ground) now holds the 0x00454240 wall transform
  for the scene, the ground and the walls.
- Ring terrain from the live race stream (plan 10, not wired): CANYN
  `ring-probe` on the new `canyn-sim-20261016` finds the ring setup K=1137
  draws before green (125/125 in-race crossings match), i.e. srand(1) +
  4231 pre-draws, 0 post. Native `TrackRing` seeded there does not
  reproduce the frame-600 sub-slice wall jitter; the iterator, walker step
  order (`0x00452e10`) and entry (`0x00452f50`/`0x00452cb0`) match the
  decompiled code, so the setup draw positions remain open (same family as
  the Napa human replay's tick-189 terrain draw gap).
- Scenery: roadside objects are scheduler nodes made at slice build
  (`0x00438930`, ground from `0x00439b10` at fraction 0x80 then) with their
  own position fraction; native `SceneryObject` has no fraction and uses
  the isolated placement stream, so trees match in x but drift in y and
  posts/signs land elsewhere. Open.

### October 16, 2026: ring setup window walks, human replay to tick 371 (plan 10)

- `TrackRing` now runs the `0x00453b60` split/merge window walks inside
  the DZHR pass (`0x00453e60`): a scratch walker 0x51 slices through both
  halves, whose terrain iterators draw from the shared stream. Napa's three
  splits and three merges added 9,089 draws (from the old start state) that the fitted 9,098
  `ring.pre_draws` had absorbed. `CourseNode::primary` reads the
  split/merge byte +0x19 (half stepped first).
- Recalibrated with `ring-probe` (new walker-terrain scoring): the ring
  setup starts at srand(1) with 0 pre draws and ends 16 draws before the
  green anchor; frame-0 walker terrain matches exactly.
- `terrain-human200-20261015`: 200 ticks identical, including the walker
  terrain trace (was tick 1). `sched-human-20261016`: first divergence
  **189 -> 371** (`rider.0.velocity` 40 vs 296, RNG in sync). Demo sched600
  600 identical, sched900 893 unchanged. Legacy pinned diagnostics keep the
  same divergence lines; only the closure line changed (re-pinned).
- Remaining: tick-371 human velocity gap; live split/merge ring walking
  (the real ring copies the merge snapshot, `0x00453030` event 6); a branch
  shorter than 81 slices is reported unsupported.
  [Handoff](agent-handoffs/ring-terrain-20261016.md).

### October 16, 2026: ring-built terrain slices, human replay to tick 702 (plan 10)

- Tick 371 (`+0xf4` 296 vs 40, then the original's −218 at 373): the race
  read off-road ground from `CourseScene` terrain built with its own random
  stream. `TrackRing` now exports the front walker's terrain slice at every
  slice build (`0x00451910` in `0x00454240`) and the factory writes it into
  the scene (`CourseScene::apply_ring_terrain`: hill/wall geometry only,
  same `0x00454240` point-height rule; building/empty samples unchanged).
  Physics and rendering now share the race stream's terrain.
- `sched-human-20261016`: first divergence **371 -> 702** (RNG: the
  original draws as speed drops 3956 -> 992 at lateral −271123, likely a
  wall/hill contact). sched600 600 identical, sched900 893, terrain-human200
  200 identical: unchanged.
- `race-cache` native hash baselines (12 entries) re-recorded,
  owner-approved: hill shapes changed (screenshots compared, riders/HUD
  identical). [Handoff](agent-handoffs/human371-20261016.md).

### October 16, 2026: solid roadside objects, human replay 1431/1431 (plans 10, 16)

- Tick 702: the player rode into a roadside tree (BORR slot at −980
  units, off-road). The original sweeps every BORR slot of the rider's
  slice and its billboards (`0x004373f0`) with the collision boxes the
  renderer copies from the object's frame 2 (`0x0044b630`: frame `+0x10`
  count, `+0xc` boxes); a hit builds the static object `0x00437660`
  (type 1, mass 0x1000000) and runs `0x00436a40` plus `0x004049c0`.
- Ported: RRAN frames keep every collision box (disc: 0/1/2 per frame);
  `TrackRing` exports each slice build's BORR slots (second `0x00451cb0`
  walk) and billboards (`0x004520d0`); `Race` keeps them per route sample,
  sweeps them (incl. the ×3 width rule for one-box far slots), and runs a
  literal type-1 `0x004049c0` (classes 0–3, kick and tail). `0x00436a40`
  mass products now wrap like `imul`. Without a scene/frames the legacy
  span tables still apply.
- `sched-human-20261016` (genuine human branch, single anchor): **1431 of
  1431 ticks identical** in every compared field. Demo sched600 600
  identical, sched900 893, terrain-human200 200: unchanged; all pinned
  diagnostics unchanged. Strict `verify_sim.py --mode acceptance` on it
  still fails on coverage only (264 golden-only fields not in the
  comparison set).
- Open: billboard types 3+ (marking branch), crashed-rider static contact
  (`+0xf4 += 0x150` path), the render-window gate on box loading.
  [Handoff](agent-handoffs/human702-20261016.md).

### October 16, 2026: strict replay coverage, first pass (plan 10)

- `oracle-sim-test` now keeps a ledger of the golden keys it compares and
  prints the bare `N ticks identical` (what `verify_sim.py` acceptance
  needs) only when every native field is compared and every golden key is
  either compared or a listed non-state key (inputs, probes, code/object
  pointers). `--coverage` lists what is left.
- Newly compared on `sched-human-20261016`, all 1431 ticks equal: rider-half
  weapon/steal/hit/attack words (`raw_1c8..1d4`), every dumped player
  bike-image word (`bike_*`, `lat104..10c`, `yaw60`; scheduler words via
  `Rider::sched_*`), player rpm `+0x2dc`/`+0x2e8` through the per-frame
  `0x00434980` hook (frame schedule from the capture's `+0x44` render-prep
  copy: ticks 13, 493 and 1197 share a frame), ring cursors, frozen-node
  scheduler words (`sched.<addr>.*`) and the empty solo car pool.
- Remaining for strict acceptance: 174 golden-only keys (rider-half object
  `rider.1.*`, rider-half raw window AI words, 28 brain words, the
  non-rider node list) and 10 native fields the proxy never dumped
  (riding animation block, nitro globals, stamina in the rider table row
  `*(half+0x1e0)+0x38/0x3c`), which need a proxy extension and a new
  capture. [Handoff](agent-handoffs/coverage-20261016.md).

### October 17, 2026: rider half, standings, original finish line (plans 10, 17, 32 D)

- Rider-half mirror (`Race::update_halves`): after the scheduler pass and
  before contacts the half carries the bike's motion words; compared as
  `rider.1.*` on `sched-human-20261016`, equal on all 1431 ticks (it differs
  from the bike exactly at the contact ticks 149–151 and 702–704).
- Standings words of `0x004060b0`'s tail: `+0x1f8` distance to the finish,
  `+0x1fc` place (`0x00407550`), `+0x1f4` next refresh (`0x004075f0`, every 4
  ticks for the player, 60 for others), also during the countdown.
- The race factory now uses the original level-tagged finish
  (`race_bounds`, all 25 course/level pairs; Napa level 0 = sample 1715)
  instead of the route end. Start stays 0x3200 (level-4 start 0x3300 waits
  for the grid port). Game races end at the original finish line.
- Constant original words the port does not model (rider-half own words,
  the brain window on a human run) count as verified invariants only while
  constant on every captured tick.
- Coverage now: 87 golden-only keys (the non-rider scheduler node list) and
  10 undumped native fields (riding animation, nitro, stamina) remain.

### October 17, 2026: animation, nitro and stamina compared; spec nitro words fixed (plans 10, 16)

- Proxy now dumps the rider half's riding/attack animation block
  (`half+0x80..+0x88`; the riding bike's own block only mirrors `+0x81`),
  stamina from the rider-table row `*(half+0x1e0)+0x38/0x3c`, and the nitro
  globals `DAT_004c5f10`/`DAT_00465a34`. New capture
  `sched-human-cov2-20261017` (same recipe): every simulation word equals
  `sched-human-20261016`; only the wall-clock frame schedule (`+0x44`, rpm)
  differs at ticks 1197/1220.
- Riding-animation selection/advance (`0x00407f30`) now runs at the end of
  the think, before physics, as in `0x00434660`/`0x00407bd0` (it read the
  current tick's speed and switched one tick early).
- Nitro config came from the wrong spec words: `0x0040b0e0` copies
  `spec+0x14c` = words 83..88 (only bikes 7–10 carry nitro); the port read
  77..82 and gave every bike 127 nitro. The factory now copies the words
  literally (zero words = no nitro).
- Native-only `nitro_throttle` (mid-tick copy, no original offset) left the
  snapshot; bike `+0x114`/`+0x240` are compared through the bike image.
- `sched-human-cov2-20261017`: 1431 identical, **0 uncovered native
  comparisons**; 87 golden-only keys (the scheduler node list) remain.

### October 17, 2026: scheduler node list, rider parking, pedestrian freeze (plan 10)

- The non-rider scheduler node list (`node.N.*`, the 0x00437e60 object list
  minus the two player slots) is now compared as a per-frame multiset of
  (draw callback `+0xdc`, position): 14 frozen opponent bike/half pairs, the
  three pedestrians (`0x00440680`) and the intersection controller/signals
  (no draw). `+0x73` is the top byte of the `+0x70` pointer, not state.
- Parking (`0x00406940`, reached from the `0x00438108` window test):
  `front - pos < 0x1e00 || pos - back < 0x800` on a scheduled run backs up
  `+0xdc`/`+0xe0` and nulls draw/think, both `+0x2c` slices and `+0xc0 & 0xfc`
  on the bike and its half. Native `Rider::parked` reproduces it for frozen
  solo riders; the capture's NULL-slice frozen riders are parked at frame 0.
- Frozen pedestrians never think, move or recompute `+0x14`: the proxy zeroes
  `+0xe0`/`+0xe4` at the start of every step, so `0x00437e60` jumps to
  `0x00438108` (NULL move). The scheduler metadata and the window test still
  run, which is why the frozen nodes hold position and leave the list on a
  scheduled run once the ring passes them (the two behind leave at tick
  1382 = 2 + 23×60; the ahead one stays).
- `sched-human-cov2-20261017`: 1431 identical, **0 uncovered native
  comparisons and 0 golden-only keys**; 70 unmodeled-but-constant original
  words remain accepted only as verified invariants. Strips the node-list
  gate on a declared `probe.frozen_words` capture (legacy demo captures lack
  the frozen slice flags parking needs).
- Open: live-AI packing/waking scheduling (the `0x00438108` window for
  stepped opponents, plan 10 Task 6) is still unported; only frozen solo
  riders park. [Handoff](agent-handoffs/coverage-20261017.md).

### October 17, 2026: contact crash-vector axis fixed; full 0x00403d60 audit (plans 10, 16)

- The class-1 (bike/bike) and class-2 (bike/car) big-speed-gap crash handed
  `Race::crash_entry` the rider impulse `{0,0x200,0}`; the original's
  `0x404eeb` local_18 is `{0,0,0x200}` (rider) and `{0,0,0x100}` (bike). The
  components add in `+0xec`/`+0xf0`/`+0xf4` order, so the port was putting
  the whole 0x200 on the half's steer rate instead of its vertical velocity.
  `Race::contact_static` already had the correct axis; `contact_car`
  (`crash_car`) and `contact` did not.
- The pair is now one traced constant,
  `rr::kContactCrashRiderImpulse`/`kContactCrashBikeImpulse` (`src/crash.hpp`),
  used by all three sites, and pinned by `race-test` and `crash-test`.
- Every call to `0x00403d60` was re-read from the disassembly; the table and
  the remaining unmodeled sites (`0x434825`, `0x438e34`, the `0x0043a190`
  high-jump wreck `0x43a50b`) are recorded in
  [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md).
- This does not change the strict acceptance replay: `sched-human-cov2`
  remains 1431/1431 identical. New test log and gate receipt in
  [VERIFICATION.md](VERIFICATION.md). [Handoff](agent-handoffs/crash-vector-20261017.md).

### October 17, 2026: setup-object cadence, demo replays restored (plan 10)

- After the node-list comparison landed (84166fb/851e795), the demo
  captures sched600/sched900 failed at tick 480: the intersection controller
  left one tick-phase early. Measured on both solo captures: the controller
  tears down on ticks 2 + 30k and setup pedestrians unlink on 2 + 60k, so
  objects built by the 0x004552e0 ring setup start with +0xc next = 2
  (`TrafficSystem::setup_tick`; the player and grid, built later by
  0x00405600, see 0). The scheduler also runs controllers and pedestrians
  during the countdown hold (native `countdown_step` now thinks them).
- Result: sched600 600 identical, sched900 tick 893 (as before),
  sched-human-cov2 strict pass, sched-human-20261016 and terrain-human200
  identical. race-cache hashes re-recorded (one pedestrian pose at tick 300).

### October 17, 2026: rider build ported; no verified-invariant fields left (plan 10)

- Rider build after the ring setup (`Race::build_draws`, `spawn_brain`):
  0x00405600 builds the player and then each opponent through 0x00405990,
  which draws the brain lane `rand()%3` for **every** rider (the player
  too). 0x00405c80 then clears the brain (state 0), places the bike on the
  0x004091f0 straight target (`+0x28 = target x`), sets the wreck timers
  +0x30c/+0x310/+0x314 and replans (0x004082f0) against the riders built so
  far. The tail draws `rand()%100` for 0x00402e10's rider-name set (42 and
  69 pick the alternates; kept as `Race::name_roll`). These are exactly the
  16 draws the harness used to skip as `ring.post_draws`: every capture now
  closes with +0 post draws.
- Evidence: all 15 spawn laterals (player and 14 opponents on their drawn
  lanes) equal the captured node laterals on sched-human-cov2-20261017,
  now checked before frozen-state hydration. The player brain is no longer
  copied from frame 0 on human runs; its whole window (+0x138..+0x1c4)
  compares against native spawn state on every tick.
- `0x004082f0` reads the player half's place word (+0x1fc), not a computed
  place; native `ai_replan` now does the same. The brain speed words
  +0x1a0/+0x1a4 start at the record value 0 at spawn (the 0x004c6b40
  copies persist across races in the original; only an unstepped human
  brain keeps that difference).
- Rider-half object image (`Rider::half_object`): 0x004387a0 from table
  0x00463c10 (`PhysicsTables::half_object`); the generic `init_object` also
  writes +0x1a4/+0x1a8/+0x1ac, which the bike build skipped. Parking masks
  +0xc0 on bike and half (0x00406940). Brain +0x184 has no writer in the
  EXE and keeps the record word (`AiState::word_184`).
- Riding-animation queue: 0x00405990 calls 0x00446060 on both blocks
  (0x0043ae10 animation 2, 0x0043ae60 queue 2, one 0x0043ae90 advance);
  ported in `Race::set_ride_library`, so the queued 2 is native, not
  hydrated.
- `oracle-sim-test`: the "verified invariant" category is removed. All 70
  words it waved through now compare against native state; sched-human-cov2
  prints the bare `1431 ticks identical`.
- Correction: the Oct 12 rider-build audit said the player path skips the
  lane draw; the capture shows it draws (first post-ring draw %3 = 1 = the
  player brain lane).
- Remaining: live-AI parking/waking (next slice); the legacy
  sim-napa-solo.tsv stops at tick 210, which is the same native-only contact
  as its sibling capture. Its frozen-word coverage is missing, and sched600,
  which runs the same stream, passes it.

### October 17, 2026: live rider scheduler and field replay (plan 10 Task 6)

- `src/race_schedule.cpp` ports the live scheduler. `0x00438170(0)` walks the
  position-sorted node list and runs every due bike (type 4) and rider-half
  (type 8) node through `0x00437e60`, each with its own `+0x0c` next, `+0x10`
  last, `+0x14` interval and dt; the interval is the `0x00437e60` LOD tail
  (1..8 by distance from the ring camera, 30 without a slice) with the
  pre-tick-300 slow-object bonus; `0x004383e0` re-sorts after the pass and
  after the contacts, and the AI scans walk the last sort's link order (a
  pass does not reorder links).
- Parking `0x00406940`: pace `+0x314 = ((+0x310 << 8) / +0x30c)` at 95% for
  a rider ahead of the player or 105% behind, the `0x00407610` 60-tick band
  (nearest three ahead lose 60/30/15 with floor 3000, nearest three behind
  gain 60/30/15 with cap 30000), crash delay `+0x318`, live byte `+0x300`
  (0x00431960), callbacks and slices nulled. Parked walk `0x004060b0`:
  `dt * pace >> 8` over the virtual position `+0x304/+0x308` (route node
  walk; end nodes clamp and zero the pace), a virtual finish adds
  `rand()%0x701 + 0x100`; waking needs the ring tracker window (back + 14 ..
  back + 91 slices), lands the bike mid-slice on the road centre, retakes the
  `0x004091f0`/`0x00408e40` targets, sets speed = pace and `+0xc0 |= 3`.
- The AI reads the target half's words (`+0x2fc`): follow `0x00409ca0`,
  can-overtake `0x004092e0`, plan clock `0x00407bd0` (half dt). `0x004368f0`
  contacts only riders that moved this tick (`+0xc0 & 6 == 6`). The countdown
  hold runs the whole pass, so tick 1 uses dt 5 (opponents) and 2 (player)
  and the three front riders park on tick 1 (pace 8440, band 15/30/60; the
  tick-9 advances 263/262/261 match the capture).
- `RaceConfig::live_schedule` (default true) selects the live path for every
  non-solo race, so the native game rides the original cadences; solo oracle
  replays keep the frozen path. Field harness `tests/field_replay.hpp`
  replays non-solo captures with slot identity by scheduler list order
  (parked bikes move to `node.*`/`sched.<addr>` keys) and loads the captured
  row 9..14 brain levels (the `0x004078f0` jitter draws precede the anchor).
- Evidence: `napa-field-l1-20261017` — countdown and ticks 6..226 compare
  identical on every rider, brain, half, sched and node-list field with rng
  in sync. First divergence tick 227: the free car spawned on tick 226 ran
  its first scheduled think (dt 11, +12) in the original while native cars
  still move every tick (plan 10 Task 7). Diagnostics that leave cars and
  pedestrians out and load the captured car states compare to frame 623
  (tick 624) and first diverge at frame 624 (tick 625): `rider.1.lateral`,
  past the tick-337 parking; the tick-765 wake is measured on the capture,
  not yet replayed. Pinned in `tools/sim_known_divergences.json` (`ecf24cab…`).
- `race_schedule` unit tests pin the measured rules; `race` CANYN wall check
  follows the live scheduler (first live opponent, its scheduled move);
  race-cache scene hashes re-recorded at scale 2/4 (scale-1 frames unchanged).
- Known limits, labelled in `race_schedule.cpp`: pedestrians/cars keep their
  own passes instead of interleaving by list position; the tracker split/
  merge hand-over is not modelled; a crashed bike keeps the native crash
  path in its move slot; the parked virtual walk follows the rider's own
  route at splits; the pace-band split-branch exclusion is not modelled
  (every parked rider qualifies while the field is on the trunk).

### Car scheduler cadence and dt motion — October 17, 2026

Plan 10 Task 7 continuation from `70a8c04`: cars now retain the original
scheduler last/next/interval/dt words (template interval 10) and choose the
`0x00437e60` type-0x10 distance/relative-velocity LOD after moving. The race
passes the player's forward velocity, and scheduling uses the ring window
before the driver's camera advance. Timer-spawned cars first run on the
following tick. Think `0x0044e710` / `0x00449880` / `0x004498c0` uses dt for
acceleration and stop-line lookahead; move reuses `integrate_speed`
(`0x004390c0`) and `advance_actor_motion` (`0x00439920`) with the car template,
surface drag and sine table. The proximity-based per-move horn RNG draw was
removed: `0x0044efa0` is a contact callback. The drift table is four signed
bytes, corrected from the former word indexing.

On the unchanged `napa-field-l1-20261017` trace, countdown (5) and ticks
6..307 match every compared rider/brain/half/scheduler/node-list word and RNG.
The harness additionally compares captured free-car position/lateral/speed
triples as a multiset, without hydrating cars or excluding fields. Tick 227
now matches: dt 11, think speed 341, post-drag speed 231, position 34828
(+12). Tick 308 first differs on a pedestrian node, original 13604/native
13605; car position 35240, lateral 32000, speed 1431 still match.

This completes the requested tick-227 unblock, not Task 7 or live-field
acceptance. Interleaved car/pedestrian scheduler nodes, the pedestrian
boundary, car lane changes, split-road teardown, curved-route heading
transport, wall/contact response, and later scenarios remain open. No new
capture or golden edit. See [handoff](agent-handoffs/car-cadence-20261017.md).

Final validation for this slice: native full build, 103/103 CTests with no
skips, viewer smoke and all-course sweep passed; ASan/UBSan focused 4/4;
strict human acceptance 1431/1431 identical. Receipts are in VERIFICATION.


### Live scheduler/contact continuation — October 17, 2026

Continued from `f987c86`. Pedestrian cadence now uses the pre-driver ring
camera, type-0x20 vertical motion stays grounded, and live teardown marks
then unlinks on the following tick. Frozen solo node lifetimes remain at
their measured behavior. Cars run in position order between rider nodes,
before contacts and driver timers. Car contact eligibility follows the
ascending outer node's due tick; live separation uses the shared recovered
separator and persistent car motion words.

`napa-field-l1-20261017` now matches through tick **787**, including the
337 parking and 765 wake, RNG and car motion, with no hydration or comparison
exclusions. The 308, 625, 647, 685 and 761 blockers are resolved. The next
failure at 788 is an AI state-3 combat mismatch (two missing RNG draws and
rider-12 steering), consistent with the still-omitted `0x00409460` /
`0x004095d0` branch. Tasks 6/7 and full field acceptance remain open;
pedestrian/controller node interleaving, full mixed contact-list ordering,
car lane/split/wall behavior and combat scenarios are not established by
this prefix. See [handoff](agent-handoffs/ped308-20261017.md).

Final gates for this continuation: full native build, 103/103 CTests (no
skips), viewer smoke and course sweep passed; focused ASan/UBSan 5/5; strict
human acceptance 1431/1431 identical. Receipts are recorded in VERIFICATION.


### Full live-field replay — October 18, 2026

Continued from `992106d` through every remaining divergence. The unchanged
`napa-field-l1-20261017` now matches **all 2400 ticks**, including shared RNG,
riders/brains/halves, scheduler/node lists and car motion. The old first-fail
rule is removed. Legacy car kind/vehicle/lane/phase probe offsets were invalid;
the replay prints that schema defect explicitly instead of treating those
aliases as native state.

A separate corrected-schema capture, `napa-field-l1-schema2-20261018`, also
matches **2400 ticks**, adding valid car metadata, every AI animation block
and all 30 relationship words. It configures the six extra riders' recorded
pre-race speed jitters before original race construction; the receipt records
those startup inputs. Native replay still anchors once and free-runs, without
live hydration, per-tick reanchoring or state exclusions.

The fixes port state-3 attack selection/hits and relationship reactions,
car scan list keys and signed closing velocities, car contact horn/response
and deferred retirement, demo-wrapper/AI stamina regeneration, marking
billboards and their first-draw collision-box lifetime, parked-target clearing
and pre-driver camera timing for AI animation LOD. Tests cover combat event
gates, reactions, marking gates/lifetime, target parking and car callbacks.

This establishes the full captured Napa level-1 live field. Tasks 6/7 retain
other scenarios: level 5, isolated traffic/fights/steals, police, splits and
mixed pedestrian/controller ordering. It does not establish an entire race,
all courses, or full human-input parity outside the 1431-tick human capture.
Final gate receipts are recorded in VERIFICATION and the session handoff.

## 2026-10-18: tag-triggered GitHub release

Added `.github/workflows/release.yml`. Pushing a `v*` tag builds and verifies
the standalone macOS ARM64 app with `tools/build_macos_app.sh`, then attaches
the ZIP and generated notes to a GitHub Release. No branch-push or manual
workflow trigger is configured. The workflow itself has not been run in GitHub.

## 2026-10-18: police half latch, post-arrest replan, on-foot arrest parity

- The schedule sort keys a crashed rider's half node by the crash body's
  +0x1c (0x004383e0 sorts the live list). The half then stays ahead of its
  bike after the remount, and the cop's overtake scan (0x00449e90) latches it
  as in the original. This resolves the "4865 follow target" item.
- After the arrest (DAT_004c5d7c = 4) the cop's replan drops the human player
  as its target (0x004082f0).
- `police-test` anchors the RNG at the end of tick 4095 from each capture (the
  cop lane draw), asserts the arrest tick, and adds `napa-cops-order2`,
  `-foot2` and `-foot3`. Parked Napa is exact through the race end (4730),
  with the arrest at 4552. The on-foot arrest (4616) is exact in two original
  runs.
- The oracle proxy has `RR_ORACLE_ORDER=1` diagnostics (node order, half
  words, race-end timer and outcome).
- Remaining: traffic/pedestrian parity. The original's pre-cop RNG stream
  varies between runs, so its traffic does too (City, `napa-cops-crashed`
  from 4831, `napa-cops-foot` from 4462). The arrest-to-BUSTED video playtest
  in `rr-game` is also still open.

## 2026-10-18: run-to-run RNG variance explained; traffic lane change

- The original's run-varying RNG stream comes from `srand(time(NULL))` at
  launch (`0x00448200`): `0x004078f0` jitters the five level speeds of rider
  rows 9..14 from it before any race seed. Native now models this: `Career`
  holds the CRT state (`Career::rng`, seeded from `time()` at launch in
  `rr-game`, shared with the front end's `rand()` uses), `reset_relations`
  performs the 30-draw jitter (`Career::spare_levels`) at all four original
  call sites (launch, mode change, character restart, valid save load), and
  `RaceConfig::spare_levels` builds rows 9..14 from it.
- Traffic: cars now change lane past a slower non-car obstacle (`0x0044e710`),
  with the halved steering error of that think.
- `police-test` replays every police capture from one RNG anchor at tick 1
  with 14 opponents and the capture's spare-row speeds. All six Napa cop
  captures match over their whole length (cop, arrest tick, every opponent),
  and the RNG to the knock/arrest. City's cop matches to its end.
- Remaining: the backward ring step after a crash/arrest (24 terrain draws),
  the `0x0044e710` lane-validity block, City pedestrian/AI parity (RNG from 235,
  row 11 from 148), and native career races still build 7 opponents where the
  offline original builds 14 (so rows 8..14 never race in `rr-game`).

## 2026-10-18: rr-game uses the original offline field

- Corrected the earlier blanket fourteen-opponent claim: original constructor
  assembly proves Thrash/attract build rows 1..14 (15 racers), while Big Game
  skips the selected character's AI row (13 opponents / 14 racers). See
  `REVERSE_ENGINEERING.md` for addresses, reference hash and evidence limits.
- Career race setup now requests all fourteen rows and passes the mode. The
  shared factory omits only Big Game's selected row, preserving all remaining
  row identities/grid slots. Existing `Career::spare_levels` wiring now reaches
  the six live spare opponents. Quick race defaults to fourteen opponents;
  explicit `--opponents 0..14` remains available for diagnostics.
- HUD and finish standings resolve names from the built riders' personality
  indices, including the six course names and the original 42/69 name-roll
  tables. No name lookup consumes RNG. Cops are excluded from finish standings.
  The dynamic place calculation already supports fifteenth place.
- Regression coverage: both modes/all eight characters, all spare speed tables,
  course and special names, row-14 HUD target, last-place HUD/standings, and the
  real race screen with all fifteen racers. Native gate details are recorded
  in `VERIFICATION.md`; this closes the seven-opponent rr-game gap above.
- Simulation/traffic/pedestrian and backward-ring gaps from prior sessions
  remain separate; this change does not claim broader live gameplay parity.

## 2026-10-18 — backward ring step and police RNG acceptance

Ported the negative `0x00454120` crossing: step back walker, rebuild new back
slice, step front walker. Added reverse BORR/DZHR traversal and previous-road
entry, preserving the original distinct iterator origins and DZHR following-word
read. Synthetic regression covers rewind ordering/draw counts, BORR boundaries,
billboard revisit, exhausted DZHR tables, missing links and previous-road entry,
including the back walker's endpoint slice. Endpoint road geometry and terrain
selectors are recovered from the original end initializers and physical SGS bytes.

One RNG anchor at tick 1: Napa parked reaches 4730/4730, crashed and order2
5030/5030. Foot now reaches 4551 (was 4459); foot2/3 reach 4579 (was 4459).
All cops, arrest outcomes and opponents still match their entire capture spans.
A new 700-tick knock-at-300 RANDLOG replay matches throughout. The next foot
mismatch is an original AI overtake draw at caller `0x00409377`, not a ring
crossing. Foot2/3 each next miss one draw at stationary-camera tick 4580;
their old captures do not log that caller. Initial setup/tick-1 RNG remains
anchored rather than reproduced, and dual-road split/merge ring traversal remains
open. See VERIFICATION.md and agent-handoffs/backward-ring-20261018.md for gates.

## 2026-10-24: race frame performance and benchmark tooling

- `./run-macos.sh` now builds Release in `build-play/` by default; `--debug`
  keeps the Debug `build/` tree. The owner's 10 fps came mostly from the Debug
  build of the CPU renderer.
- New `rr-game --bench FRAMES` (plus `--bench-warmup`, `--bench-out`,
  `--bench-label` and `--bench-vsync`) runs a phase-timed autopilot race with the
  settings file. `tools/bench_game.sh` runs presets in isolated preference dirs.
  `tools/bench_report.py` prints tables, compares runs and gates a minimum fps.
  See [PERFORMANCE.md](PERFORMANCE.md).
- Made the RGB post-processing passes row-parallel on the render pool: grade,
  horizon conversion, remap, motion blur, bloom and vignette. Cached the grade
  table and the vignette gain. Rasterizer bands are now 4x workers with
  per-band node culling and clipped edge walks. All of it is bit-identical to
  the previous output.
- Measured with the owner's settings, Release, 852×480: 69 to 120 fps
  (display present cap). Debug: 9.7 to 30 fps. Scale 2 (1704×960) with
  everything on: 72 fps.
- Gaps: double-precision trilinear and grade math remains the per-pixel cost at
  scale 3+; no Android device benchmark; `draw_slices` does not reach the race
  fog (recorded in PERFORMANCE.md).

## 2026-10-24: integration of the Oct 18–19 shared-tree sessions

Uncommitted work from the codex-siren, codex-police-wrap, codex-arrest-flow,
codex-l5-integration/sim-l5 and import-hud-terrain sessions, reviewed and
committed together (their handoffs are in `agent-handoffs/`):

- Siren: the `0x00434f40` term is lateral branch separation (ring `+0x24`,
  `x_sep`), applied when cop and player halves sit on disjoint road masks; a
  cop half with no slice is silent (`SoundSystem::siren`).
- Police: arrest thresholds use the x86 `CDQ; XOR; SUB` magnitude, so
  `INT32_MIN` stays negative in the signed comparisons.
- Crash split across scheduler nodes like the original: the half node runs
  the crash phases, body motion (`0x0043adc0`) and recovery, the bike node
  the riderless bike animation/motion and the `0x500` speed pull toward the
  half. The half mirror follows the crash body; `0x00404560` cleanup on park;
  crashed-half static contact `0x004046b0`; bike contact class ladder 0–3
  of `0x004049c0`; `0x004394c0` heading sweep into `+0x128`.
- Pedestrian nodes run inside the position-sorted scheduler walk; quiet
  intersection signals carry their `0x0041eca0` flag.
- Level 5: `RR_ORACLE_LEVEL` in the proxy, brain dump through `+0x1cc`, field
  replay defers the green closure past the countdown, new diagnostic test
  `oracle-sim-field-l5`.
- Import screen: arcade restyle, input geometry unchanged.

Gaps: `napa-field-l5` diverges at tick 1995 (ring node position, cause not
traced) and is pinned as a known divergence; the `0x0043c7e0` standings call
in the crashed-bike pull is not ported; the import screen has no screenshot
check (no display in that session).

## Offline native gameplay demo export (October 24, 2026)

Added `--capture-video/--capture-audio/--capture-frames/--capture-warmup`
to the playable race driver. Capture shares `RaceScreen` and the native mixer,
with exactly one 60 Hz tick per frame, fixed launch jitter, no physical audio
device and no real-time input. Original interactive execution keeps its clock,
input and audio-device path. Export preserves 4:3/HUD framing at 1280×960,
uses original visual settings, and omits soundtrack playback.

`tools/render_demo.py` builds the engine and streams RGB to FFmpeg, exports
native engine/effect PCM, and produces H.264/AAC course clips plus a two-minute
five-course assembly. Original assets and outputs remain local/ignored.
See [DEMO_VIDEO.md](DEMO_VIDEO.md) for parameters and repeatable commands.
The standalone disc-backed `tests/demo_capture_test.py` verifies deterministic
pixels/audio, moving scenes, sample arithmetic and invalid CLI rejection.
This is native export coverage, not additional original-game parity evidence.

## Demo combat correction (October 24, 2026)

Removed the autopilot's unconditional punch every 40 ticks. It now initiates
swings only beside an active opponent inside the native weapon, longitudinal
and height reach on the same road; current swings complete through normal
combat. This changes the demo input driver, not simulation or hit rules.
`race-screen` covers nearby riders on either side, riders slightly behind,
empty road and distant/invalid/crashed targets, plus swing retrigger gating.
The corrected two-minute cut uses `--warmup 960 --courses 2 3 0 1 4`, retaining
actual close-range combat and avoiding shadowboxing. See DEMO_VIDEO.md.

### October 25, 2026: reported intersection and combat defects

- Fixed player descriptor-backed attack completion: the legacy 18-tick cap
  restarted held club swings before damage. Real-asset/live-scheduler tests now
  cover 36 tap/hold, weapon, side and action combinations.
- Restored the missing type-4 rural intersection pavement branch
  (`0x0043bef0` → `0x0041d110`); marked rural side roads now use their pavement
  tiles instead of ordinary hillside bands.
- Full Debug and isolated committed-source Release gates: 106/106 each, no skips.
  Browser Release build succeeds. The supplied City recording's specific road
  gap has not been isolated; these fixes do not close City intersection pixel
  acceptance or full combat parity. [Evidence and limits](agent-handoffs/user-road-combat-20261025.md).

### October 26, 2026: exact City road-gap reproduction and fix

The screenshot and full Desktop recording locate the triangle near 00:38.
Imported CITY route sample 159 reproduces it: the road widens, but the far
end of the lane grid used the next sample's rounded lane origin. Both ends
now use the strip's own origin, matching `0x00444630`'s previous-record update.
The triangle closes and the displaced centre stripe is corrected. Added
pixel coverage regression at 4:3 and Hor+ widths; it fails on the old code.
This closes the specific City report left open October 25; full City parity
remains separate. [Evidence](agent-handoffs/city-width-gap-20261026.md).

### October 26, 2026: Napa merge terrain overdraw (reported web screenshots)

Reproduced the grass bands, truncated road and green curtain around 2.6–2.8
miles at Napa route-A samples 940/945/970/975. The inward-facing hill profiles
were rendered like exterior terrain and extended across the viewport. Ported
the approaching-merge profile join, band limits and terminal ridge connection
from `0x00454890` / `0x0041d500` / `0x0041d620`, using per-view copies without
changing simulation or RNG. New sky/road coverage checks at widths 320/426
fail before the fix and pass afterward. Clean committed-source native gate:
106/106, no skips. Browser deployment and live-gameplay checks pass; correction
is live. [Evidence and remaining fidelity scope](agent-handoffs/napa-merge-terrain-20261026.md).

## Frontend layout acceptance follow-up — October 26, 2026

- Continued plan 13 Task 6 using the existing original screen-9 capture.
  Traced title/description DrawText flags and RECT offsets in the unmodified
  disc disassembly; fixed banner bottom/right alignment, paragraph caller
  alignment and the fixed description rectangle bottom (420, not 720).
- Shared layout rules serve 1× and scaled rendering. Main-menu title ink now
  starts at the original `(33,48)` instead of `(24,35)`; the last description
  line starts at x=17 versus original x=18 (formerly centered at x=115).
  Regression checks use captured positions at 1×/2×/4× and permit four logical
  pixels for the documented font metric difference.
- README now distinguishes implemented jukebox/sound/controls from acceptance
  gaps. Mano-a-mano remains behind plan 09's multiplayer decision. Seeded
  writer round-trip coverage exists; driven-career round trip, all-screen
  captures and strict non-text pixel acceptance remain open. The existing
  broad-mask menu characterization pin is not an acceptance claim.
- Verification and limitations: see the matching VERIFICATION entry and
  `docs/agent-handoffs/frontend-layout-20261026.md`.

### October 26, 2026 — original steal resolver measured in an isolated worktree

- Followed the worktree request: all changes in `.re-steal-worktree-20261026`,
  with a private APFS-cloned Wine prefix and standalone fixture DLL. No edits
  to the main checkout's active frontend proxy or simulation.
- `0x004096f0` now has 31 measured original component cases: 13 transfers,
  15 eligibility rejections before the steal roll, three rejected rolls.
  Signed victim frame, strict unsigned victim cooldown, attacker cooldown
  write, inventory transfer, reach boundaries and animation queues all agree
  with the recovered trace. Four synthetic parser/hash guard checks pass.
- Added `oracle/steal_fixture.c`, `tools/steal_fixture_capture.py` and
  `tests/steal_fixture_test.py`; results and exact scope in
  [the handoff](agent-handoffs/steal-original-fixture-20261026.md).
  This closes the original resolver gate/transfer component measurement;
  plan 10/16 free-running steal parity remains open. Controlled original
  mode-3 rolls do not establish CRT RNG or full-race fidelity.

## October 26, 2026 — plan 34 client and bots

Task 6 Steps 1–2 now have a real `net::Client`: clock sync/time dilation,
adaptive capped input delay, authoritative frame delivery into Rollback,
bounded resend/events and confirmed hash reporting. The E2E matrix covers all
2–8 player counts across the requested latency/loss settings (84 scenarios).
Task 5's `net-bots` runs actual disc-backed clients over WebSockets; local two-
and eight-client cutoff smokes agree on state hashes and official results.
RaceScreen/audio/smoothing/HUD integration remains open in Task 6 Steps 3–5.
Scope, commands, limitations and gate receipts:
[net-client-20261026](agent-handoffs/net-client-20261026.md).

## October 26, 2026 — plan 34 Task 6 race presentation

The online RaceScreen now renders a detached local-seat view of the client's
rollback race, rather than stepping or modifying authoritative state. It keeps
networking active through quit/focus/background events, relays F1–F8 chat,
shows confirmed outcome announcements and waits for Official results before
returning a result. Sound replay is deduplicated by tick/emitter/event, with
bounded history. Remote-only correction smoothing defaults on; the network
HUD defaults off. Both options persist in settings.

Rollback now publishes confirmed per-tick effects even when predictions were
correct and no re-simulation was needed. Automatic Result reports use a common
confirmed terminal tick/hash. The shared Start builder handles sparse room
seats. Native tests cover the original offline goldens, 84 chaos/cutoff rooms,
two automatic terminal rooms and a rendered/audio screen matched against a
headless peer. Host/Join launch and rematch UI remain Task 7.
[Evidence and remaining scope](agent-handoffs/net-screen-20261027.md).


## October 27, 2026 — plan 34 Task 7 online menus

Host/Join now use the original mano menu handlers with native room controls:
level/AI/cars/visibility, public rooms and Refresh, pasted code/link, spectator
join, room-code copy, character/ping roster, course vote, and eight saved chat
phrases. Main-menu online play is enabled only when a transport and production
compatibility gate are attached. Startup verifies all 341 reference file hashes;
the fingerprint includes source content and a synthetic multi-human simulation.
A failed data check leaves offline play available and logs the reason.

The lobby hands its existing channel to `net::Client` at Start and freezes its
polling immediately, preserving queued Frames/LogChunks. RaceScreen forwards
room/chat/official events back to the lobby. Official standings and WIN/LOSE/
BUSTED/WRECK result videos return to the same room for rematch without changing
either career save or its RNG. Sparse room seats map to compact simulation and
audio rider indices.

`net_frontend` drives real frontend handlers through Host, Join, vote, two real
rollback clients, an agreed scripted 420-tick cutoff (including live simulation),
results and rematch against in-process `rr-netd::Server`. It also covers chat
persistence, visible standings Back, unofficial/busted routing and both careers.
`net_mano` covers handshake ordering, compatibility rejection, room control,
chat, reconnect tokens and handover. Original-asset menu captures at 1x/2x stay
outside Git. The browser WebSocket/`?room=` path compiles; browser/native device
cross-play and Android transport/app links remain Task 10 acceptance.

See [the Task 7 handoff](agent-handoffs/net-frontend-20261027.md) for checks.

Task 7 checks: Debug 117/117; Release 116/116; ASan/UBSan focused 2/2; final native game build/invite smoke and browser Release compile. Task 5 bots use the identical production Hello gate.


## October 27, 2026 — plan 34 Task 8 deployment preparation

Prepared `.github/workflows/net-deploy.yml` and `deploy/road-rash-net/`: a
Linux amd64 Release build, code-only Dokku archive/runtime, startup and exact
revision checks, loopback Nginx WebSocket/health routes, and a separate Linux
bot image. The enabled deploy job uses a dedicated forced-command key and
verifies two production clients with hash-checked ephemeral disc data. The
workflow is gated by `RR_NET_DEPLOY_ENABLED`; that variable is absent, so only
its build job runs until provisioning is complete.

Local checks passed: amd64 build and stripped binary package; exact revision
health; macOS bots against the Linux server directly and through the Nginx
proxy; Linux CI-image bots through that proxy, all reaching 420 confirmed ticks
with matching hashes/agreed official results. Actionlint 1.7.7 and Nginx syntax
checks passed. The data fetcher verified all 341 actual-disc fixtures and
rejected a hostile manifest before any write; the live hosted manifest matches
all references and one hosted object verified. Default Python user-agent got
HTTP 403; an explicit `RoadRash-NetSmoke/1.0` client identity succeeds.

Production is **not deployed**. Operator SSH is blocked: 1Password RSA signing
fails, and the local Ed25519 key is rejected for `arkits@agnee` / the known
public IP. No app, deploy key, Nginx, firewall or TLS change was made on the VPS.
An SSH unlock request is pending. Task 8 remains open, including its production
smoke and TLS renewal-hook check. Its DataChannel/blocked-UDP acceptance also
depends on the unimplemented Task 9 peer/negotiation/fallback; no unused UDP
range was opened. [Resume details](agent-handoffs/net-deploy-20261027.md).


## October 27, 2026 — plan 34 live WebSocket deployment

Provisioned only Dokku `road-rash-net`, its persistent `/data`, dedicated
restricted CI archive key and `/net` routes in the existing TLS server.
Explicit app-only `proxy:set road-rash-net nginx` is required because agnee's
global Dokku proxy is Caddy. Nginx bindings are loopback-only 18080/18081 on
IPv4 and IPv6. Existing web health/revision and 341-file manifest passed; the
Certbot deploy hook validated/reloaded Nginx. Other apps/global proxy settings
and all firewall rules were preserved.

`wss://road-rash.archit.xyz/net` passed two native clients with matched hash
`13426816110484682456` at 420 confirmed ticks and agreed official results.
The dedicated deploy key was verified by the initial archive deployment,
stored only in the repository secret, and its temporary local private file
removed. `RR_NET_DEPLOY_ENABLED=true`; the enabled CI run deployed source
`0adb9a9533ae0577dd8ae31efa8bc552be2dab9e` and its public exact-revision health
check passed. Full CI production smoke receipt follows in the live handoff.

No DataChannel or UDP fallback acceptance is claimed: Task 9 must implement
that transport before Task 8 Step 3 can be measured. No unused UDP ports were
published/opened. This is a scripted transport/determinism cutoff, not a full
course or interactive cross-play acceptance. [Live handoff](agent-handoffs/net-live-20261027.md).


Live production CI receipt: [40534691545](https://github.com/arkits/road-rash/actions/runs/40534691545)
passed the enabled build + deployment + Linux two-client production smoke.
All 341 hosted files verified in ephemeral storage; temporary CI credential/data
cleanup passed. Source revision `0adb9a9533ae0577dd8ae31efa8bc552be2dab9e`
matched public health. Both CI clients reached the scripted 420-tick cutoff
with matching hashes and agreed official results. Public counters reported
one finished race and zero desyncs after the run. Game assets remain outside
Git, images and artifacts. Task 8 Steps 1/2/4/5 complete; Step 3 remains for
Task 9's DataChannel implementation and measurements.
