# Road Rash native port: plan index

These plans take the port from its current state to a **100% complete, feature-complete native game**: macOS ARM64 first, then Android ARM64, Linux and Windows. Each plan is implemented in full; do not defer tasks to later. Read this file first. It holds the current status, the review findings, the order of the remaining plans, and the conventions every plan assumes.

Last reviewed: **October 26, 2026** (status update below); full review October 14, 2026: plans 25–31 landed (review fixes, one factory, field proxy, race shell, render performance, test/build tooling, this docs pass); plans 21–22 added October 12; plan 21 gained Task 10 (wide menus). See [../PROGRESS.md](../PROGRESS.md) for the per-session timeline.

## Update (October 26, 2026)

The October 14 snapshot below is historical. [../CURRENT_STATUS.md](../CURRENT_STATUS.md) holds the current evidence. Since then:

- **Simulation:** the full Napa level-1 field matches the original for 2400/2400 ticks and a human-input capture for 1431/1431 ticks, with one RNG anchor. Other levels, courses and scenarios remain open (plan 10).
- **Police:** live (periodic and DZHR spawns, chase, siren, arrest, BUSTED flow) and native-tested; comparison with an original capture is open.
- **Crashes:** walls `0x0043a610` and the crash/remount family `0x00403d60` are ported; rider, car and pedestrian contact are ported.
- **Sound:** race MIDI, sirens and car engines play; exact AWE/MIDI timbre is open.
- **Front end:** jukebox, sound mix and controls are implemented; driven-career saves round-trip. Mano-a-mano stays disabled until plan 34.
- **Still not started:** Android hardware validation, Linux/Windows (plan 33) and online multiplayer (plan 34).

## Where things stand (October 14, 2026)

`rr-game` is a playable game on macOS. The same code builds into an asset-free Android ARM64 APK.
- **Front end:** the original menus, career, shop, Schmooze, results and saves lead into races.
- **Napa:** draws with the original scene pipeline: road materials, hills, buildings, shoulders/curbs, static scenery, horizon scroll and the full dashboard.
- **Other courses:** all five draw with the original scene pipeline through the shared race factory (plan 26).
- **Race:** the original bike physics, AI racing state, rider contact, combat and free traffic drive the race (plan 10 oracle replays with a known divergence at frame 207).
- **Audio/video:** all effects, the RRA soundtrack and all 59 videos decode and play.
- **Tests:** 94 native CTests with slow/sim/disc/unit labels (`test-fast`/`test-sim`); `RR_STRICT_WARNINGS` clean. Android debug and release assemble.

### Verified against the original (zero tolerance)
- **Road projection:** 600 CITY frames, 901 moving Napa frames and one Napa start frame.
- **Raster and render:** flat and textured raster, polygon normalisation, view mapping and the display-list renderer, tested on component fixtures.
- **Scene construction:**
  - lane panels and materials;
  - hill strips, bands and projection;
  - shoulders, building facades and curbs;
  - roadside profiles and terrain grounding.
- **Assets:** CEL/mip loading, family/cel-group selection (176 selections), and Napa CESR events (7,949 samples).
- **Display-list replay:** natural Napa frames 0/150/300/750, every pixel. This replays original lists; it does not cover native list construction.
- **Codecs:** all 59 AVIs against FFmpeg.

### Ported but never compared with the original
- Bike physics, AI racing, contact, combat, the rubber band, and traffic timers and spawning. All were translated from decompiled code, but none has been run against an original tick trace.
- Natively built Napa scene lists. They are geometrically safe over 2,509 route views, but no complete natively built frame has been pixel-compared.
- Front-end screens (no pixel comparison) and save files (no original round trip).

### Not started or stand-ins
- **Police:** no cops or arrests.
- **Crashes and animation:**
  - wall collision `0x0043a610` and the crash/remount sequence `0x00403d60` are stand-ins;
  - RRAN-driven rider animation and shadows are not ported;
  - AI overtaking `0x00408e40` and fight states 2–5 are not ported;
  - attack timing is a stand-in.
- **Courses:** CITY, HIWAY, CANYN and MEDLY have no original scene. Split stitching and building marking transitions are open on Napa too.
- **Sound and front end:**
  - race MIDI (`GRUNGE1..5.MID` + `.SBK`), sirens and car engines;
  - the jukebox, sound mix and controls screens;
  - the attract demo and the busted/wrecked cutscene selection.
- **Android:** never observed running on hardware; renderer performance on the device is unknown.

## Review findings (October 11, 2026)

1. **The old plans 02–07 describe an architecture that was never built.** They assume a `World` / `Object` scheduler with `think/move/slice` behaviour dispatch, `GameEvent`, `scene.cpp` actor nodes and an `oracle_actors_test`. The real code is `rr::Race` (holding `Rider` values), `rr::CourseScene`, `rr::TrafficSystem` and `SoundEventRecord`. New plans target the real interfaces. The old plans stay as background on recovered addresses only.
2. **Simulation fidelity has no oracle.** `oracle/run.sh` exports `RR_ORACLE_SEED`, `RR_ORACLE_INPUT` and `RR_ORACLE_RECORD`, but `oracle/aweman_proxy.c` never reads them. There is no per-tick rider/bike/car dump. Plan 01 Task 7 was never done, so plan 04's acceptance is unmet and "behaves like the original" cannot yet be claimed for any race system. **Plan 10 closes this, and every later behaviour task depends on it.**
3. **Traffic is uncommitted:** `src/traffic.*` and `tests/traffic_test.cpp`, plus the `Race`/`game_main` hooks. It builds and passes its synthetic and corpus tests. Its random-call interleaving with the race RNG and its car/rider draw order are documented approximations. PROGRESS.md does not mention it yet.
4. **The Napa-first scope** (user, October 10) is tracked in [../NAPA_IMPLEMENTATION.md](../NAPA_IMPLEMENTATION.md). Plan 11 is that milestone's remaining work; plans 12–15 extend it to the whole game.
5. **Two Android approaches exist.** The port is `native-port/android` (native SDL3 APK, runtime disc import). `/Users/archit/Dev/rr/android` holds an earlier Wine-on-Android setup: a GameHub/Winlator controls profile `RoadRash-AirX.icp`, a `.desktop` launcher and a **patched** `ROADRASH.EXE` with a different hash. That setup is a stopgap, not part of the port. Never use it as a reference binary.
6. **PROGRESS.md was stale.** Its header and status table said "no traffic", "road shading is a placeholder" and "race sound triggers open", and its "Next steps" still pointed at the start of plan 02. Refreshed in this review.

## Playtest update (October 11, 2026, evening)

The owner played the Napa race and reported:
- traffic collision missing;
- the right-hand dashboard meter glitching;
- graphical glitches on bikes;
- two apparent versions ("Napa demo" and `rr-native`).

Root causes found:
- **Collision:** `Race` never tests riders against cars.
- **Meter:** the tachometer reads bike `+0x2dc`, which the original's `0x004349c0` updates, but the port only updates a private copy in `sound.cpp`. The centre distance readout reads an unmaintained `+0x44`.
- **Bikes:** native rider, bike and shadow nodes have never been compared with an original display list.
- **Versions:** `run-macos.sh` launches the research viewer `rr-native`, not the game, and the release `.app` dates from October 10.

[Plan 11a](11a-napa-playtest-fixes.md) fixes all four and runs before the rest of plan 11.

Additional review findings from the code that landed since session 23:
- **Plan 10's replay was weakened.** `oracle-sim-test` re-anchors the RNG to the golden on every tick, and the "solo" golden actually runs the player through the attract-demo AI branch (`set_demo_hold`). That is useful for localising divergences, but it **cannot** detect missing or extra `rand()` calls, and it does not exercise player-control physics. Plan 10 Task 5 is complete only when a golden with human-style controls replays with **one** RNG anchor at the green light. Per-tick re-anchoring stays as a diagnostic mode (`--reanchor`), never as acceptance.
- **The race-setup RNG stream** (11,461 calls before the green light) is not reproduced natively. That is acceptable only while setup is anchored once at green; record it as a known limitation until the setup consumers are ported.
- **Police** arrest decisions and actor setup are verified offline, but cops are not yet spawned in the live race.

## Playtest update (October 12, 2026)

The owner reported four faults; all four are fixed (see [../PROGRESS.md](../PROGRESS.md) timeline):
- **Other courses crashed.** HIWAY and CANYN aborted at the start grid. A building in slice 32 read the depth table at index 4352, one past its end. The original reads the two zero words that follow the table there.
- **No opponent name on the dashboard.** The port named only the rider last hit. The original names the nearest opponent (`0x00438550`/`0x00446b80`) and shows the gap with an ahead/behind arrow.
- **Right meter black.** This is the opponent stamina meter, which was empty because there was no target. Separately, the tachometer froze when no sound system was running.
- **Too much traffic.** CESR zones set the traffic density (`0x0041ec70` → `0x0044dd00`). On Napa this means about 6× fewer cars than the port's fixed density 6.

The owner also asked for plans covering pedestrians, oil slicks, police and the crash walk. That is [plan 24](24-road-hazards-police-crash-control.md). The executable has traffic lights and crossing traffic, and **pedestrians**: DZHR actor events spawn them into pool `0x004c1274`, which was first mislabelled as cops. They walk across the road and can be knocked down (family 101 is the woman with a walker). No oil slick was found. The port's auto-walk with no input matches `0x0040a600`; plan 24 Task 3 settles it with a capture.

## Plan status

| # | Plan | Status (Oct 11) | Remaining work lives in |
| --- | --- | --- | --- |
| 01 | [Foundation and oracle](01-foundation-and-oracle.md) | Tasks 1–6, 8 done (pixels via in-process capture, not cnc-ddraw). **Task 7 (deterministic runs) not done.** | Plan 10 |
| 02 | [Road and world rendering](02-road-and-world-rendering.md) | Napa done except split stitching, marked terrain and building marking transitions; other courses not started | Plans 11, 12 |
| 03 | [Sprites and scenery](03-sprites-and-scenery.md) | Decoders, families, RRAN, static scenery done; rider animation/shadows open | Plan 11 |
| 04 | [Race simulation](04-race-simulation.md) | Physics/movement/camera ported (not in the planned `World` form); unverified; walls/crash stand-ins | Plans 10, 11 |
| 05 | [Opponents, traffic, police, combat](05-opponents-traffic-combat.md) | AI racing, contact and combat partial; traffic in progress; police not started | Plans 10, 11, 12 |
| 06 | [Front end, progression, saves](06-frontend-progression-saves.md) | Mostly done; 3 inert screens, attract demo, pixel and save verification open | Plan 13 |
| 07 | [Audio and video](07-audio-and-video.md) | Codecs, mixer, effects, engines, RRA done; MIDI, sirens, car engines, cutscene selection open | Plans 11, 13 |
| 08 | [Platform, input, packaging](08-platform-input-packaging.md) | Software complete for macOS; Android hardware validation open | Plans 14, 15 |
| 09 | [Multiplayer](09-multiplayer.md) | **Superseded by plan 34** (October 26): its lockstep premise was wrong | Plan 34 |
| **10** | [Simulation oracle and tick verification](10-simulation-oracle.md) | Tasks 1–4 done (`ab8cc79`, `37c4597`, `d3294fa`, `6b2ce6d`). Task 5 in progress: demo-hold replay matches 124 ticks with per-tick RNG re-anchoring (not yet acceptance; see the playtest update) | — |
| **11a** | [Napa playtest fixes](11a-napa-playtest-fixes.md) | Tasks 1 + 4 done. Task 2 Steps 1–5 done (rpm + distance readout; post-dash hook, parked needles match). Task 3 Steps 1–5 done (nodes 25/25 pixel-clean; jump hunt partial, drive-3 mode kept) | — |
| **11** | [Opening Napa race completion](11-napa-race-completion.md) | Task 1 done (`51cb898`). Task 2 partly done: playback, live rider and car nodes (`fda8b2a`, `586169a`); node comparison done (11a Task 3: 25/25, pixel clean). Task 4 partly done: crash entry/motion/recovery/remount verified offline (`63ad4ad`, `be31cdc`); car contact done (11a Task 1). Task 6 partly done: arrest decisions and cop setup (`397691a`, `be31cdc`); live spawning open (plan 24 Task 2). Tasks 3, 5, 7, 8 open | — |
| 12 | [All courses and levels](12-all-courses.md) | **Superseded by plan 17** (October 11) | Plan 17 |
| **13** | [Race music and front-end completion](13-music-and-frontend-completion.md) | New. Task 3 (sound mix, controls) moved to plan 18 Tasks 6–7 | — |
| **14** | [Android port on hardware](14-android-port.md) | New. Task 2 Step 3 (low-resolution view) moved to plan 18 Task 4 | — |
| **15** | [Release and final acceptance](15-release-and-acceptance.md) | New | — |
| **16** | [Gameplay systems: AI, physics, collisions, animation, graphics](16-gameplay-systems.md) | New (session 26). Completion matrix over plans 10/11/11a, the Napa critical path, and new tasks: knock-off and cop contact, roadside objects, weapons, off-road/reverse/finish, per-tick animation fields, dashboard pen/opponent list, determinism soak | — |
| **17** | [All courses port](17-all-courses-port.md) | New (session 26), supersedes 12. Per-level race bounds, one `make_race_setup` (all maps playable early), sweep, then CITY → HIWAY → nitro → MEDLY → CANYN parity, 25-race soak, career, placeholder removal | — |
| **18** | [Settings and options screens](18-settings-and-options.md) | New (session 26). Original Graphics Options/Configure Input/sound mix plus native Display, Sound and Pause screens in the original menu style; fullscreen, scaling, filtering, vsync; low/inset race views | — |
| **19** | [High-resolution rendering](19-high-resolution-rendering.md) | New (session 26). Render scale 1–4× (up to 2560×1920) and match-display for race, dashboard and menus; 640×480 stays bit-identical; band-threaded raster; widescreen decision-gated | — |
| **20** | [Asset upscaling](20-asset-upscaling.md) | New (session 26). Local upscale cache; MMPX 2×/4× for indexed textures/sprites (palette-preserving); Real-ESRGAN for menu artwork via a script; GPU screen filters; AI for in-race art experimental | — |
| **21** | [Enhanced visuals](21-enhanced-visuals.md) | New (October 12). Optional, default-off: high-refresh interpolation, perspective-correct textures, full-colour raster with bilinear/trilinear filtering, fog and colour grading, 48/64-slice draw distance, bloom/vignette/motion blur, Hor+ widescreen; Original/Enhanced/Custom presets. Task 10 (wide menus, blurred side fill) open since the October 13 widescreen review | — |
| **22** | [Toolchain hardening](22-toolchain-hardening.md) | New (October 12). C++20, libFuzzer targets for every parser (in CI), ThreadSanitizer tree, warnings as errors with a ratchet, clang-tidy bounds/lifetime checks. Chosen instead of a Rust rewrite | — |
| **23** | [Browser port (WebAssembly)](23-web-port.md) | Playable browser baseline via Asyncify, verified local import/storage, shared frontend/races/audio, Vercel prebuilt output. See WEB.md for tested flow and remaining cross-browser/performance/enhanced-pixel limits | — |
| **24** | [Road hazards, police and crash recovery](24-road-hazards-police-crash-control.md) | New (October 12). Per-zone traffic density done; traffic lights and crossing traffic, live police on all courses, player-directed run-back (bounds wreck, on-foot contact), and DZHR pedestrians (Task 4; 43–476 events per course). No oil slicks found |
| **25** | [Review correctness fixes](25-review-correctness-fixes.md) | New (October 13, [code review](../agent-review/CODE-REVIEW-2026-10-13.md)). **Done** (Tasks 1–3, 5–7): restart after finish, render-worker exceptions, RGBA mip cache keying, atomic saves, dead `Race` state, ASan overflows | — |
| **26** | [One race construction path](26-one-race-construction-path.md) | New (October 13). **Done** (`e1fd264`, `83890a3`, `7f3b840`, `a72bca0`): game, soak and oracle replay build races through one factory; intentional game behaviour change. Follow-up: career keeps the purchased bike (`94f1490`), bad-CAR fallback + restart guard (`91bd535`), finish-wording + race_action notes (`6c937e7`); see [review](../agent-handoffs/archive/2026-10/review-of-25-28-20261013.md) | — |
| **27** | [Safe object field access](27-safe-object-field-access.md) | New (October 13). Task 1 done (`9b4c019`; Debug/Release determinism confirmed). Tasks 2–3 (named offsets) decision-gated on the owner | — |
| **28** | [Race shell: input actions, RAII, race screen](28-race-shell-input-and-raii.md) | **Done** (`b50dac8`, `a3c1c68`, `5632e71`, `7ca92ba`); plan 23 Task 3 note open (orchestrator scope) | — |
| **29** | [Enhanced render path performance](29-enhanced-path-performance.md) | **Done** (Tasks 1–4): benchmark + counter, cached RGBA levels, band-threaded RGB (bit-identical), persistent band scratch | — |
| **30** | [Test and build infrastructure](30-test-and-build-infrastructure.md) | **Done** (Tasks 1–5): labels/fast targets/guard, Checks, strict clean, ByteReader/ISO parser, build pruner | — |
| **31** | [Docs and status hygiene](31-docs-and-status-hygiene.md) | **Done**: header/index refresh, handoff archive, doc checks in agent-tooling | — |
| **32** | [Completion review follow-through](32-completion-review-next-steps.md) | New (October 14 review, [report](../agent-review/REVIEW-2026-10-14-COMPLETION.md)): R1 restart latch, R2/R3 interpolation, R4 pause texture, R5 side panels | Slice C is the active SIM work (this plan); A/B after it; D–G sequenced after parity/device evidence |
| **33** | [Linux and Windows ports](33-linux-windows-ports.md) | New (October 17). Linux and Windows builds, packaging, CI and acceptance after macOS/Android; portability rules apply now | — |
| **34** | [Mano-a-mano online multiplayer](34-multiplayer.md) | **Active, in the definition of done** (owner, October 26). Modern design: deterministic simulation run by every client, rollback against a server-sealed input timeline, WebSocket and WebRTC DataChannel to `rr-netd` on the owner's VPS `agnee`; room codes and invite links; cross-play across Mac, Android, browser, Linux and Windows. Original trace: [../MULTIPLAYER.md](../MULTIPLAYER.md) | — |

### Order and dependencies

```
11a playtest fixes ─┐
10 simulation oracle ──► 11 + 16 Napa race ──► 17 all courses ──► 15 release
                            │                                       ▲
13 music/front end ─────────┤                                       │
18 settings ──► 19 high resolution ──► 20 upscaling ────────────────┤
                                    └──► 21 enhanced visuals ───────┤
22 toolchain hardening (any time; before 19 Task 6) ───────────────┤
14 Android (Task 1 now; rest after 11) ─────────────────────────────┤
33 Linux/Windows (after macOS complete) ───────────────────────────┤
34 online mano-a-mano: 1 rules ──► 2 multi-human race ──► 3 rollback ┤
                       4 codecs ─┬► 5 rr-netd ──► 6 client ──► 7 front end
                                 └► 8 VPS ──► 9 WebRTC ──► 10 platforms ──► 11 acceptance
```

- **Plan 34** (online mano-a-mano, definition of done item 9) runs alongside
  the sim plans. Task 2 changes shared simulation files and must keep every
  offline golden bit-identical; Tasks 4 (codecs/channels) and 5 (server
  rooms/sealer) do not depend on the simulation and may proceed while Task 2
  is in progress. Task 3's cross-platform matrix needs plan 33 for Windows.

- **Plan 11a now.** The owner-visible defects come first: Tasks 1 (car collision) and 3 (bike/shadow nodes) remain.
- **Plan 10 next.** Every behaviour change in 11, 16 and 17 is accepted against its tick goldens. Task 5 needs a single-anchor, human-control golden before anything else is called verified.
- **Plans 11 and 16 together finish Napa.** Plan 16's "Napa critical path" gives the exact interleaving of plan 10/11/11a/16 tasks. Plan 11 Task 8 (`napa-acceptance`) closes Napa.
- **Plan 17** (all courses) Tasks 1–3 may start once plan 11 Task 3 (Napa scene construction) lands; Task 2 makes every course playable early (marked unverified). Tasks 4–9 run after `napa-acceptance`, so remaining divergences are course data, not shared bugs.
- **Plan 18** (settings) is independent of the simulation plans and can run in parallel at any time. Its Task 4 (low/inset views) is needed by plan 14 Task 2 on the device.
- **Plan 19** (higher resolutions) needs plan 18 Tasks 2–3 and 5 only for its settings UI; its renderer tasks can start at once, because they change only the final coordinate mapping and keep the 640×480 path bit-identical. **Plan 20** (upscaling) follows plan 19 Task 3. **Plan 21** (enhanced visuals) follows plan 19 Tasks 1–3 and 7.
- **Plan 22** (toolchain hardening) is independent and can run now; its ThreadSanitizer task should land before plan 19's threaded rasteriser.
- **Plan 14 Task 1 (first device run and performance baseline) runs as soon as a device is attached.** It does not wait for the other plans.
- **Plan 13** is independent of 10–12 except for its race-MIDI hook, which uses the race-start point that plan 11 touches.
- **Plan 33** (Linux, Windows) starts once macOS is feature complete.
- **Plan 15** runs last.

## How these plans handle reverse engineering

Most remaining work recovers behaviour from a stripped 1996 binary, so the final native code cannot be written in advance. Each such task has:

- **A named starting point:** function addresses, globals and object offsets already traced (`analysis/decompiled/<address>.c`, `analysis/original-disassembly.txt`, [../REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md)).
- **An oracle test written first:** the original runs under Wine with `oracle/aweman_proxy.c`. Its dump becomes a golden in `analysis/oracle/`, and a failing native test is written before porting.
- **A port rule:** translate the named functions line by line into fixed-width integer C++, keeping shifts, truncating divides, `sar` rounding and 32-bit wraparound. No improvements until the golden passes.
- **Acceptance:** zero tolerance unless the task states a tolerance and why.

Where interfaces and infrastructure are known, the plans give complete code. Where a plan says "trace", the deliverable is a documented finding in REVERSE_ENGINEERING.md plus a test. **If a trace overruns its time box, stop and report rather than guess.**

## Conventions every agent must follow

### Environment
- **Repo:** `/Users/archit/Dev/rr/native-port` (git, branch `port/playable-napa`).
- **Shell:** fish or zsh. There is no `timeout` command. Run oracle captures in the background; each takes about 4 minutes before the race starts.
- **Original game data:** `../cdrom/ROADRASH`. Mount it if missing: `hdiutil attach -readonly -nobrowse -mountpoint /Users/archit/Dev/rr/cdrom "/Users/archit/Dev/rr/Road Rash (USA) (En,Fr,De,Es,It) (Rerelease) (19970930).iso"`.
- **Reference executable:** `../cdrom/ROADRASH/ROADRASH.EXE`, SHA-256 `73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`. Never use `/Users/archit/Dev/rr/android/Download/RoadRash/ROADRASH.EXE` (patched, different hash).
- **Wine:** `$HOME/Applications/Wine Staging.app/Contents/Resources/wine/bin/wine`. Prefix `/Users/archit/Dev/rr/prefix`. The oracle install is `prefix/drive_c/ElectronicArts/RoadRashOracle` (`oracle/install.sh`).
- **Original course order** (`0x0046df58`): 1 CITY, 2 HIWAY, 3 MEDLY, 4 NAPA, 5 CANYN. `rr-game`'s CLI order differs: NAPA, CITY, HIWAY, CANYN, MEDLY. Always convert explicitly.
- **Android:**
  - SDK in `.tools/android-sdk`;
  - `JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home`;
  - device MANGMI AIR X, usually absent; check with `.tools/android-sdk/platform-tools/adb devices`.

### Build and test
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The regression gate after any change to shared code:
- `ctest` (Debug), plus the sanitizer tree `build-ubsan`
- `build/rr-viewer --assets ../cdrom/ROADRASH --smoke`
- `build/rr-course --exe ../cdrom/ROADRASH/ROADRASH.EXE ../cdrom/ROADRASH/DATA/COURSES/*.CRS`
- `tools/ci.sh` before closing a plan, which includes the Android assemble and asset-exclusion checks

### Code rules
- C++20 (`CMakeLists.txt`; this includes the plan 34 server `rr-netd`), SDL3 only, plus pinned single-header libraries already in `third_party/`. A new dependency needs its licence file in `third_party/` and a mention in the user guide.
- All parsing of original data is bounds-checked and throws `std::runtime_error`. Every parser gets a truncation/garbage test.
- Use fixed-width integers for anything ported, emulate wraparound with `uint32_t`, and cite original addresses in comments (`// 0x00444630`).
- **Never embed original game data** (tables, images, text, audio) in source or tests. Read it at runtime from the user's files. Goldens live in git-ignored `analysis/oracle/`. Tests that need them exit 77 (`rr::golden::skip`) and are registered with `SKIP_RETURN_CODE 77`.
- Tests that use only synthetic data must always run.
- Simulation code has no wall clock, no floats and no allocation in the tick path. All randomness goes through `Race::random()` (the MSVC `rand` stream), in the original call order.

### Evidence and documentation
Every task that recovers a format or behaviour updates, in the same commit:
- `docs/REVERSE_ENGINEERING.md`: the finding, the addresses, and how it was validated;
- `docs/VERIFICATION.md`: what was run and the result;
- `docs/PROGRESS.md`: the status table and the timeline;
- `docs/NAPA_IMPLEMENTATION.md`, while plan 11 is active.

Never claim a match with the original that was not measured. Write "not verified" where it applies.

### Commits
One commit per task, message `<area>: <what>`, ending with:
```
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```
Never commit `analysis/frontend`, `analysis/oracle`, screenshots of original art, or anything from the disc.

## Definition of done for the whole port

1. From a clean checkout plus the user's own Road Rash disc image, a documented import step produces a runnable game on:
   - macOS ARM64: a standalone `.app` with no Homebrew dependency;
   - Android ARM64: an installable APK, with disc data imported at runtime and never bundled.
2. All five courses at all five levels are playable in the original single-player modes: Big Game (career) and Thrash (plan 17).
   - Opponents, traffic, police, weapons, crashes, cash, the bike shop and saves behave like the original.
3. Oracle tick goldens (plan 10 format) pass for the scenarios listed in plans 10, 11, 16 and 17, and natural-frame pixel goldens pass for the scenarios listed in plans 11, 16, 17 and 18.
4. All audio plays: effects, the RRA soundtrack, race MIDI, engines and sirens. All cutscenes play at their original trigger points.
5. Controls work with keyboard, gamepad (including the MANGMI AIR X built-in controls) and touch. Plan 14's hardware checklist has been run on the device.
6. Every original options screen works, and the native Display, Sound and Pause screens (plan 18) are usable with a controller only. The game renders at up to 2560×1920 (plan 19), with optional upscaled textures and artwork generated locally (plan 20).
7. The docs describe every recovered format and every known deviation from the original.

8. Linux and Windows builds pass the same gates (plan 33).
9. Mano-a-mano is playable online by two to eight players across every shipped platform. It uses the original menus and gameplay rules on the plan 34 architecture, and passes plan 34's acceptance.

Plan 34 (mano-a-mano online multiplayer, superseding plan 09) is part of the definition of done (owner decision, October 26, 2026; item 9). The owner also chose a modern internet design over a faithful port of the original netcode.

## Global risks

- **Wine oracle stability:** long driven captures have faulted before (credits timer, fixed in session 22). Keep captures short and scenario-specific, and use the course-menu bootstrap.
- **Random-stream interleaving:** every system shares one `rand()` stream. A single missing or extra call desynchronises all later ticks. Plan 10's per-tick RNG-state field exists to find the first divergence.
- **Android performance:** the CPU indexed rasteriser at 640×480 has never been profiled on ARM64 phone hardware.
- **MIDI/SBK:** `.SBK` is an EMU SoundFont 1 bank. Plan 13 Task 1 is time-boxed and has a fallback.
- **Licensing:** original assets are never shipped. Android imports from the user's disc (plan 08).
