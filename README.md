# Road Rash — Native Port

Native C++20 / SDL3 port of the 1996 Windows rerelease of Road Rash.
Runs the game directly on modern systems: no Wine, no x86 emulation at runtime.

Primary target is macOS ARM64, second is Android ARM64. Linux and Windows
are planned (see `docs/plans/33-linux-windows-ports.md`). The port is
incomplete; see Status below.

Reference for reverse engineering is the unmodified disc binary at
`../cdrom/ROADRASH/ROADRASH.EXE`. Oracle captures run it under Wine to
produce comparison traces. It is never executed by the game itself.

## Status

Playable today via `rr-game`: full menu / career / shop / save flow leads
into races on all five courses (Napa, City, Highway, Canyon, Medley) with
the original bike physics, AI field, combat, traffic, pedestrians, engine /
effect audio, soundtrack, and cutscene playback.

What matches the original on receipted captures:

- Napa level 1, full 14-rider offline field: 2400/2400 ticks identical.
- Napa level 1, recorded human-input run: 1431/1431 ticks identical.
- Full native suite: 105/105 CTests, zero skips; sanitizer suites clean.

Still open (see `docs/CURRENT_STATUS.md` and `docs/PROGRESS.md`):

- Simulation parity outside those captures: other courses/levels, human
  paths beyond the receipted run, isolated fight/steal/traffic cases.
- Gameplay: live police (periodic 4096-tick and DZHR spawns, chase, siren,
  arrest, BUSTED flow) is implemented, native-tested, and matches the
  original tick-for-tick: parked (635 pursuit ticks, RNG/field to 4730,
  arrest 4552), crashed (935 ticks, field to 5030, no arrest either side),
  on-foot ×2 (605 ticks each, full parity to 4700, arrests 4616/4544), and
  City (456 ticks, field to 4551; original arrests at 4552, not asserted
  while City AI RNG stays open past 954). Walls (`0x0043a610`), crash entry/motion/recovery
  (`0x00403d60` family) and rider/car/pedestrian contact branches are ported;
  pedestrian pairs never contact in the original (`0x004368f0` `& 3` gate),
  and the traffic draw order approximation is visual-only. Race MIDI
  (`SBKGRNG1..5` + `KLOWN` via `RaceMusicPlayer`, hooked at race start/stop),
  sirens (slots 30/31, event 19) and car engines (slots 4/6, `Engcar11`,
  `0x00435090` Doppler route) play; the player engine switches to the tunnel
  variant (slot 2, `EngTunne`) on terrain type 3 (`0x00434a80`,
  `Race::player_in_tunnel`, pinned). Exact AWE/MIDI timbre remains open.
- Frontend: jukebox, sound mix and controls are implemented; mano-a-mano
  stays disabled — online multiplayer (plan 34) is in scope per the owner's
  October 26 decision and is not yet implemented. Seeded original-writer saves round-trip byte-identical and
  driven-career saves round-trip (win + purchase + save + reload pinned in
  `career-test --corpus`, plus an original-driven save that round-trips
  byte-identical with its dumped game state (`career-test --save-corpus`).
  Menu layout acceptance covers all 223 original
  items plus native screens (2 scrollable, 28 hidden placeholders) with
  main-menu 1x/2x/4x checks; masked pixel checks pass for main menu
  (menu-09 and fe-main goldens: 60% masked, 98% exact, 0% large-diff) and the
  Big Game street screen (fe-career-start golden, item-driven mask: 67%
  masked, 96% exact, 0% large-diff). Characters/restroom/jukebox/graphics
  still diverge in background/portrait art (measured ~1% exact unmasked on
  restroom-class screens); full unmasked pixel equality remains open on
  GDI-hinting text shapes and that art mapping.
- Android builds and packages but hardware validation (render, input,
  import, lifecycle, performance) is pending.
- Linux and Windows build and pass their tests (Linux in Docker; Windows
  x64/ARM64 on GitHub runners, including the zip and installer packages), but
  neither has been played on real hardware yet (plans 33a/33b).

## Browser build

The same game now runs as WebAssembly. Run `tools/build_web.sh`, then serve
`build-agent-browser/dist/` with a local HTTP server. Import your original PC
disc ISO or ROADRASH folder; files stay in private browser storage. Choose
**Quick race** for immediate play or **Start game** for the original frontend.
See [docs/WEB.md](docs/WEB.md) for deployment, tests and current limitations.

## Requirements

- Your own 1997 Windows rerelease disc (ISO or extracted `ROADRASH`
  folder, 341 files, SHA-256 verified on import). No game data is included
  or redistributed.
- CMake 3.22+, a C++20 compiler, SDL3 3.2+ (`brew install cmake sdl3`).
- Android builds additionally need JDK 21, Android SDK 35 / NDK 28, Gradle
  wrapper in `android/`.

## Quick start (macOS)

```sh
./run-macos.sh                       # Release build in build-play/, launch game
./run-macos.sh --debug               # Debug build in build/ (slower renderer)
```

Or with CMake directly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/rr-game --assets ../cdrom/ROADRASH
```

Options:

```sh
build/rr-game --assets ../cdrom/ROADRASH --course 0 --level 1 --bike 0
build/rr-game --assets ../cdrom/ROADRASH --video VIDEO/INTRO.AVI
```

`--course 0-4` (Napa, City, Highway, Canyon, Medley),
`--level 1-5`, `--bike 0-14`, `--character 1-8` (Axle .. Teflon Mike).
An interactive quick race is a Thrash race in the original front end: it
ends on the original results and standings screens, then the Thrash menus.
Omitting race flags enters the menu/career flow. `--ticks N --screenshot
out.bmp` runs a headless verification slice; tool runs (`--ticks`,
`--autopilot`, captures, benchmarks, `--opponents`) keep the standalone race.

## Controls

| Action | Keyboard | Gamepad | Touch |
| --- | --- | --- | --- |
| Accelerate | Up / W | South / RT | Free zones (auto-throttle) |
| Brake | Down / S | East / LT | Bottom-middle zone |
| Steer | Left/Right, A/D | D-pad / left stick | Left/right thirds |
| Punch / Kick / Backhand | Z / X / C | West / North / RB | Top third |
| Pause | P / Esc | Start | System Back / pause tap |
| Restart after finish | Enter | South | Tap |
| Quit (desktop) | Q | — | — |

P/Esc/Start opens the pause menu (Resume, Sound, Display, Controls,
Quit Race with confirmation). Backgrounding, focus loss, or disconnecting
the active gamepad also pauses; resume is always explicit. Keys are
rebindable in Graphics Options → Configure Input; gamepad mapping is fixed
except punch/kick swap and stick dead zone.

## Disc import

First launch without `--assets` shows the import screen: pick the ISO or an
extracted folder. The importer verifies all 341 files by SHA-256 and commits
only a complete match; failures leave any existing install untouched.

Command-line equivalent:

```sh
build/rr-import "/path/to/Road Rash.iso" "/path/to/imported/ROADRASH"
build/rr-game --assets "/path/to/imported/ROADRASH"
```

Default destination is the SDL pref dir `RoadRashNative/RoadRash/game/ROADRASH`.
`RR_PREF_OVERRIDE` redirects it for isolated test runs.

## Tools

| Binary / script | Purpose |
| --- | --- |
| `build/rr-game` | The game (menus, career, races, video playback) |
| `build/rr-viewer` (`./run-viewer.sh`) | Asset research viewer, not the game: screens, course drive-through |
| `build/rr-import` | CLI disc import with hash verification |
| `build/rr-inspect FILE.RSC` | Dump a resource container directory |
| `build/rr-course --exe ../cdrom/ROADRASH/ROADRASH.EXE DATA/COURSES/*.CRS` | Course graph / projection sweep |
| `python3 tools/check_corpus.py <assets>` | Validate all resource containers |
| `python3 tools/render_demo.py` | Render a 2-minute 5-course demo MP4 from the engine |
| `tools/bench_game.sh user original enhanced` | Race frame-time benchmark (`docs/PERFORMANCE.md`) |
| `tools/ci.sh` | Local verification gate (native build, tests, APK assembly) |

Video playback uses native Cinepak / IMA-ADPCM decoders. FFmpeg appears
only in tests as a decode oracle; runtime needs neither FFmpeg nor Wine.

## Android

```sh
export JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home
android/gradlew -p android assembleDebug
```

Output: `android/app/build/outputs/apk/debug/app-debug.apk`.
Normal APKs ship no game data; on first launch pick your own ISO and it is
verified the same way as desktop. SDK location lives in the ignored
`android/local.properties`. Optimized local build:
`assembleRelease -PreleaseDebugSigning=true`. Owner signing and versioning
are documented in `docs/USER_GUIDE.md`. Device behavior is unverified.

## macOS packaging

```sh
tools/build_macos_app.sh
```

Produces `build-release/Road Rash Native.app` and a ZIP with pinned static
SDL3, ARM64 / macOS 13 baseline, bundled licences, ad-hoc signature, and no
game data. Pushing a `v*` tag builds and attaches the same package via
GitHub Actions.

## Windows packaging

From an MSVC developer prompt (`windows-msvc-arm64` for ARM64):

```bat
cmake --preset windows-msvc
cmake --build build-windows-msvc --config Release
python tools/build_windows_package.py --build-dir build-windows-msvc --arch x64
python tools/verify_windows_package.py build-windows-msvc\package\RoadRashNative-<version>-windows-x64.zip --arch x64 --run
```

Produces a portable zip (`portable.txt` keeps data beside the exe) and, with
Inno Setup 6.3+, a per-user installer, both with a static CRT and SDL, so
they need no DLLs beyond Windows' own, and no game data. `tools/sign_windows.ps1` signs them when the owner sets
`RR_SIGN_*`; otherwise they are unsigned and SmartScreen warns. The verifier
needs `pip install pefile`. CI builds and install-tests both architectures.

## Docs map

- `docs/CURRENT_STATUS.md` — evidence matrix and latest gate receipts.
- `docs/PROGRESS.md` — what is ported, approximations, next steps.
- `docs/VERIFICATION.md` — commands run, outcomes, skips, evidence paths.
- `docs/REVERSE_ENGINEERING.md` — recovered structures and addresses.
- `docs/USER_GUIDE.md` — install, import, settings, controls reference.
- `docs/plans/00-INDEX.md` — plan dependency order to completion.
- `docs/AGENT_TOOLING.md` — agent workflow, gates, capture rules.

Commercial assets, disc dumps, and upscaled artwork stay local and out of
the source tree, APKs, and app bundles. Third-party licence texts are in
`third_party/`. No redistribution licence for the game is implied.
