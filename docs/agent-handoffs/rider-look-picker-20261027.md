# rider-look-picker-20261027

Task: owner report on the web Quick Race rider choice: (1) the chosen rider
still looked like the default red/white player in race; (2) replace the
plain rider `<select>` with a picker modelled on the original character
select screen.

Baseline: e3e7fa0. Build dirs: `build-agent-rider-20261027` (native Debug),
`build-agent-rider-web-20261027` (browser Release).

Changed files: `src/race.cpp`, `src/race.hpp`, `src/race_setup.cpp`,
`src/game_main.cpp` (CLI hunks only), `web/index.html`, `web/app.js`,
`web/style.css`, `docs/WEB.md`, `docs/REVERSE_ENGINEERING.md`, this handoff.
Did not touch the in-flight renderer files (`src/world_renderer.*`,
`src/world_texture.*`, `src/render_threads.*`, `src/raster_rgb.*`,
`src/course_scene.cpp`, `src/settings.*`, `src/race_screen.cpp`,
`src/race_caches.hpp`, `src/game_types.hpp`, `CMakeLists.txt`) or
`src/import_ui.*`, `src/rri.cpp`, `oracle/aweman_proxy.c`,
`docs/PROGRESS.md`, `docs/VERIFICATION.md`.

## 1. In-race look: native bug, fixed

The original draws every rider from one sprite set (`HIBOB.DAT`) through a
colour remap `0x0046e158 + n*256`. `0x00446190` (bike, type 4) and
`0x004460c0` (rider half, type 8) pick `n`:

- cop (`DAT_004642dc`): 0xde;
- network race (`0x004c5b00 | 0x004c5af9`): `(s8)bike+0x328`;
- player (`0x004c7f70` / `0x004c7fb0`): byte +5 of the player's rider-table
  row (`half+0x1e0` → `0x00463c78` row);
- opponent in list slot i: i, except the slot equal to the player's byte,
  which gets 0.

Row +5 is 0 on disc. `0x00405990` writes it at `0x405b3a..0x405b57`: the
level bike `0x004641c8[row*5 + level]` (opponents, or any rider in Thrash
mode `0x004753b0 == 1`), else the career bike `0x004b8a14`, and passes the
same byte to `0x00405ea0` as the bike. So the original's player look is the
**bike's** colour scheme. The character changes it only through its bike.
Level-1 bikes: Axle 1, Bose 2, Cydney 2, Jon 1, Pearl 4, Rhonda 1, Jim 4,
Mike 1. So at level 1 there are three looks. The portrait/bio do not change
the sprite shape; there is one rider body for everyone.

Native gave the player remap 0 always (bike 0's red/white) and opponents
`n % 9` in list order. Fix: `Race::set_player_bike(bike)`, called by
`build_race` with the bike index: player = bike, opponent slot i = i or 0
when i == bike. The ctor default is the bike-0 rule (slot). The legacy
sprite path's remap table now loads all 15 (`game_main.cpp`). Not modelled:
the network `+0x328` branch.

Also: a standalone tool race (`--ticks`/`--screenshot` …) given an explicit
`--character` and no `--bike` now rides the character's level bike, as the
interactive Thrash quick race already did.

Evidence (native Debug, `rr-game --assets ../cdrom/ROADRASH --race --course 0
--character N --ticks 240 --screenshot …`): character 1 (bike 1) yellow
helmet/pink jacket on purple; 2 (bike 2) gold/black; 5 (bike 4) red
helmet/pink jacket on red. Before, all were the remap-0 red/white. Disc-derived
screenshots stay local (scratchpad), not committed.

## 2. Web rider picker

See `docs/WEB.md` "Rider picker". The Quick Race card shows a rider chip
(portrait thumbnail, name, level-1 bike); it opens a `<dialog>` laid out like
the original screen (portrait, orange Badloc name box, Futura stats + bio,
three columns of names with the yellow marker) plus a rail with the in-race
sprite (HIBOB frames 3/6/5/8/9 leaning, remapped by the bike, on a small road).
All data is parsed in `web/app.js` from the verified disc files (hosted or
OPFS): PE string table of `TEXT/ENU.DLL`, EXE tables, `.RRI` JPEG portraits,
`HIBOB.DAT` + `PALETTE.RAW`. `#character` stays as a hidden input, so launch
args and telemetry are unchanged; `rider_select` telemetry event added.

The roster parser was checked in Node against the disc: all eight
characters' stats/bikes/cash/bios match the original screen (Axle: 6'1",
183 lbs, 400cc Corsair Swallow, $200).

## Checks

- Native Debug `build-agent-rider-20261027`: `cmake --build` exit 0;
  `ctest --output-on-failure -j 6`: 121/121 passed, 0 skipped (542 s). Built
  with the shared tree including other sessions' in-flight renderer edits.
- Native screenshots, characters 1, 2, 5 (see §1).
- Browser Release `build-agent-rider-web-20261027` (`tools/build_web.sh`),
  served by `tools/web_dev_server.mjs … --assets .tools/hosted-game-assets
  --port 8811` in the in-app browser:
  - 1280×860: chip shows Axle + portrait + 400cc Corsair Swallow; the picker
    renders the original-screen layout with the leaning sprite; Down ×4
    previews Pearl (stats, bio, red/pink bike-4 sprite); Enter confirms
    (`#character` = 5, focus back on the chip); reopen, Down, Esc keeps 5.
  - Race with Pearl: the player in race is the red/pink bike-4 rider (was
    red/white before).
  - 375×812 (mobile emulation): full-screen picker, two-column roster rows
    ≥44 px, no horizontal overflow (scrollWidth 375); tapping Slim Jim then
    "Ride as" sets `#character` 7 and remembers it in `localStorage`.
  - Console errors only the expected `/net/health` 502s (no local rr-netd).
- clang-format 22 clean on the changed C++ files.

## Follow-ups (owner, same day)

- Portraits did not load on road-rash.archit.xyz: the deployed CSP
  (`vercel.json`, `deploy/nginx.conf`: `img-src 'self' data:`) blocks the
  `blob:` URLs. Portraits are now `data:` URLs (7ef713c); CSP unchanged.
- Course picker in the same style (see `docs/WEB.md` "Course picker"):
  postcard screen, blurb/length from the disc, panning in-race horizon.
- Checks: a fingerprinted bundle of the browser build with the production CSP
  as a `<meta>` tag, served locally. In-app browser: 1280x860 picker matches
  the original City screen ("Length: 5.3 miles"); Down Down Enter picks the
  Peninsula (`--course 4`), Esc keeps it; 375x812 no overflow, 44 px rows,
  tap Napa + Race sets 0; a race on The City via the picker renders the City.
  `NODE_PATH=.tools/browser-tests/node_modules RR_WEB_URL=… node
  tests/web_browser_smoke.cjs`: all steps PASS, exit 0 (courses 1–4 picked
  through the modal). Only console errors: the `/net/health` 502s.

## Deploy fix (owner: "deploy failed")

`Deploy browser game` had failed since 97d365d. Three causes:
- `web_hosted_smoke.cjs` "only fonts are downloaded before launch": the
  pickers read the EXE, ENU.DLL and pictures as soon as the launcher showed.
  Hosted launches now defer them to the first picker open (`pickerReady`).
- `web_recovery.cjs` (fake DOM, no roster buttons): `setCourse` read the
  DOM roster. The course order and names are now constants.
- The `--bike 999` diagnostics step: since a5455c1 the interactive quick race
  truncated an explicit bike into the u8 career bike instead of throwing.
  `game_main.cpp` now rejects an out-of-range `--bike` before `run_game`,
  as the tool race does ("Bike index out of range").
The 03:25 runs failed later in the same test (a TimeoutError after "lazy
fonts"), before this work; later runs stopped earlier, so whether it still
occurs is shown only by the next deploy.

## Remaining

- Network-race colour (`bike+0x328`) not modelled.
- Not deployed to road-rash.archit.xyz.
