# quick-race-results-rider-20261027

Task: owner playtest report on the web quick race (road-rash.archit.xyz):
terrain textures, race-end screen, punch difficulty, rider selection.

Baseline: 389e043. Build dirs: `build-agent-quickrace-20261027` (native
Debug), `build-agent-quickrace-web-20261027` (browser Release).

Changed files: `src/game_main.cpp`, `web/index.html`, `web/app.js`,
`web/style.css`, `web/telemetry.js`, `README.md`, `docs/WEB.md`,
`docs/WEB_TELEMETRY.md`, this handoff. Did not touch the in-flight
`CMakeLists.txt`, `oracle/aweman_proxy.c`, `src/import_ui.*`, `src/rri.cpp`,
`docs/PROGRESS.md` or `docs/VERIFICATION.md` changes of other sessions.

## 1. Race-end screen

The native quick race ended on a native stand-in banner (`FINISHED 1ST 3:08`,
"Enter / A / tap: race again", `draw_hud` in `race_helpers.cpp`). The original
has no finish text in a single-player race (`DAT_004c5bd0` is written only by
the pause message `0x004431d0` and the network race end `0x00416010`); after
`0xb4` ticks it fades out and the front end shows the results (`0x1e`/`0x1f`),
penalty and standings (`0x22`) screens.

Change: an interactive quick race (`--race`, `--course`, `--level`, `--bike`,
`--character`, without tool flags) now runs inside `run_game` as a Thrash
race. `QuickStart` performs the same career calls as the original screens:
`select_mode` thrash (`0x00445090`), `choose_character` (`0x0041f5a0`),
`set_level` (`0x004453f0`), optional explicit bike, `start_race`
(`0x004160c0`), then `Frontend::enter(course)` queues the race start. The race
end then follows the career path (`RaceScreen` decision + `0xb4` delay + fade,
`Frontend::race_finished`), so results, standings and the Thrash course menu
are the original screens. It skips the course item's "Start" video and Get
Ready screen (0x004213c0), which is the quick race's purpose.

Tool runs (`--ticks`, `--autopilot`, `--steer-into-traffic`, `--paused`,
`--bench`, captures, `--print-wiring`, `--render-scale`, `--screenshot`,
`--opponents` other than 14) keep the standalone race and its debug banner.

## 2. Rider selection

New `--character 1..8` (validated; bad values exit 2). Standalone tool races
set `RaceConfig::character` and pass it to `SoundSystem::start_race`
(previously 0). The launcher has a Rider select under Course with the
`ENU.DLL` names (Axle, Bose Jefferson, Cydney Bass, Milwaukee Jon, Pearl
McKurdy, Rhonda the Rash, Slim Jim, Teflon Mike) and passes
`--character`. Telemetry `game_start` adds the numeric `character`.
In Thrash the dashboard name stays the alias/"Player" as in the original
(`Frontend::player_name`); the character selects rider stats and the level bike.

## 3. Terrain textures: no divergence found

- The owner's frames (odometer 1.9–2.8 miles, 16:10) were reproduced natively:
  `rr-game --course 0 --autopilot --ticks N --screenshot` with
  `widescreen=1`, N = 6150..8400. Native and web frames show the same hill
  texturing, the same intersection band (the type-4 cross-road tiles from
  `user-road-combat-20261025`) and the same pale far-valley strip. The web
  build does not render differently from native.
- `oracle-scene-test --native-terrain` on every Napa capture with scene dumps
  (`napa-drive`, `-v4`, `-v5`, `-v6`, `napa-moving`, `napa-dash120`,
  `napa-start`): 550 terrain nodes match the original, 0 missing; 338 rider
  grounds equal. The renderer is pixel-matched to original display lists
  (VERIFICATION.md, 0 of 307,200 pixels). Original frame
  `napa-drive-v6-f750` shows the same speckled/streaked affine hill look.
- Not covered: no original capture exists past the start area (~0.3 mile),
  so the 1.9–2.8 mile scenery has no original comparison. The capture farm
  is failing since the `AWEMAN32.DLL` rebuild (REVERSE_ENGINEERING.md
  "Last successes"), so a new capture is blocked on that, not on this task.

Follow-up (October 27, 2026): a native "smooth terrain" path (945e0ff,
continuous road-space hill textures) was tried at the owner's request and then
reverted when the owner asked for the original look. Terrain is the original raster again;
the Enhancements menu is back to 11 rows.

## 4. Punches: traced, matches the original

`Race::fight_reach` matches `0x00409c10` field for field (assembly
0x409c10–0x409c8f): same side test, |Δposition| < 0x40, lateral
< reach·0x1000 (fists/kick 5, club 7, chain 8), |Δheight| < 0x3000. The caller
`0x004096f0` passes the attacker bike (`this`) and the target half
(`[target+0x2fc]`, whose `+0x1d4` is the weapon); native compares attacker
bike against target `half_mirror`, the same pairing. 0x40 is one bike
half-length (`+0x178` = 64), so a hit needs the riders level within half a
bike and lands only on the hit frame (`0x600000` flags or frame 2). This is
the original's tight window, not a port regression. Not measured: an original
human-vs-AI fight capture comparing hit rates.

## Checks

- Native Debug build `build-agent-quickrace-20261027`: rr-game builds.
  `--race --course 0 --character 3 --menu-frames 5`: exit 0, front end
  reaches the race start (race wiring logged). `--race --character 9`: exit 2.
- End-to-end finish (temporary local hook, not committed: env-gated
  `auto_drive` on the career `run_race` plus a frame reset after
  `race_finished`): `--race --course 0 --character 8`, Napa autopilot race
  20:10–20:14, outcome lost, place 13 (0-based), then the original "LOSER!"
  results screen (0x1f) captured 90 frames later. No native banner. The hook
  was removed and the source restored before the gate below.
- Browser Release build `build-agent-quickrace-web-20261027` (9 validated
  code files) served with `tools/web_dev_server.mjs` and local hosted assets
  on :8793 in the in-app browser: the Rider select renders under Course;
  Teflon Mike + Napa starts the race through the front end ("Checking
  multiplayer game data", then the race with "Player 1" on the dash); throttle
  input drives. Not driven to the finish in the browser.
- Full native gate after removing the hook: `cmake --build build-agent-quickrace-20261027 --parallel` exit 0; `ctest --test-dir build-agent-quickrace-20261027 --output-on-failure -j 6`: 121/121 passed, 0 skipped (518.7 s).

## Remaining

- Original capture past mile 1 on Napa to close the terrain question.
- Not deployed to road-rash.archit.xyz.

## Live processes / resources

None held.
