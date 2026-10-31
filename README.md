# Road Rash — Native Port

Native C++20 / SDL3 port of the 1996 Windows rerelease of Road Rash.
Runs the game directly on modern systems: no Wine, no x86 emulation at runtime.

Platforms: macOS ARM64 (primary), Android ARM64, Linux x86-64/ARM64
(including Steam Deck) and Windows x64/ARM64, plus a WebAssembly browser
build. Linux and Windows builds, tests and packages are done (plans 33a and
33b); Android, Linux, Steam Deck and Windows still await testing on real
hardware. The port is incomplete; see Status below.

Reference for reverse engineering is the unmodified disc binary at
`../cdrom/ROADRASH/ROADRASH.EXE`. Oracle captures run it under Wine to
produce comparison traces. It is never executed by the game itself.

## Status

The port is playable but not finished. `rr-game` runs the full original
front end (menus, Thrash and Big Game careers, shop, Schmooze, results,
saves, credits) into races on all five courses (Napa, City, Highway,
Canyon, Medley). Races have the original bike physics, AI field, combat,
traffic, pedestrians, police, engine and effect audio, soundtrack and
cutscenes. Online mano-a-mano for two to eight players (plan 34) works over
the internet through the `rr-netd` server, with room codes and invite links.

Measured against the original:

- **Simulation:** Napa level 1 with the full 14-rider field matches for
  2400/2400 ticks; a recorded human-input run matches for 1431/1431 ticks.
  Both use one RNG anchor and then run freely.
- **Police:** pursuit, siren and arrest match tick for tick on parked,
  crashed and on-foot captures, and on City up to the arrest.
- **Front end:** all 223 original menu items pass the layout sweep; every
  captured screen passes masked pixel checks (98–100% exact on most, 75–85%
  on screens with portrait or background art). Seeded original-writer saves
  round-trip byte for byte, as do saves from a driven career.
- **Tests:** 134/134 CTests with the disc and zero skips on macOS and on
  Linux (GCC and Clang, x86-64 and ARM64); ASan/UBSan and TSan clean.
- **Determinism:** the determinism matrix gives equal race results on
  macOS, Linux (GCC/Clang, x86-64/ARM64), Windows (MSVC, clang-cl, ARM64)
  and WebAssembly. In CI, bots on six platforms (macOS, Linux x86-64/ARM64,
  Windows x64/ARM64, WebAssembly under Node) finish one live online room
  with equal hashes and an official result. A real browser tab in a mixed
  room has not been tested yet.

Platforms:

| Platform | State |
| --- | --- |
| macOS ARM64 | Main development target; app bundle and zip |
| Android ARM64 | APK builds; not yet tested on a device |
| Linux x86-64/ARM64, Steam Deck | Tarball, AppImage and Flatpak built and smoke-tested on clean distributions; not yet tested on a real desktop or a Deck (plan 33a) |
| Windows x64/ARM64 | Zip and per-user installer built and install-tested on CI; unsigned; not yet tested on real hardware (plan 33b) |
| Browser (WebAssembly) | Playable; see [docs/WEB.md](docs/WEB.md) |

Still open (see `docs/CURRENT_STATUS.md` and `docs/PROGRESS.md`):

- **Simulation parity** outside the captured runs: other courses and
  levels, City AI RNG past tick 954, human paths beyond the recorded run,
  and isolated fight, steal and traffic cases (plans 10, 16, 17).
- **Front end:** exact GDI-hinted glyph bitmaps and the portrait and
  background art on the characters, restroom, jukebox and graphics screens;
  exact AWE/MIDI music timbre.
- **Online:** device and cross-play acceptance on Android and real
  desktops, and the long soak tests (plan 34 Tasks 10–11).
- **Hardware:** Android, desktop Linux, Steam Deck and Windows (plans 14,
  33a, 33b). Owner decisions remain on signing, the licence and Flathub
  (plan 15).

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
  Linux: GCC 14 or Clang 19 with `cmake --preset linux-gcc` (or
  `linux-clang`); the pinned SDL is built from source, and
  `linux/install-gate-deps.sh` lists the Debian packages. Windows: Visual
  Studio 2022 with `cmake --preset windows-msvc` (see Windows packaging).
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

## Linux packaging

Needs Docker (on a Mac, OrbStack or Docker Desktop builds both
architectures; x86-64 runs under emulation):

```sh
tools/build_linux_package.sh x86_64       # or aarch64
tools/linux_clean_host_smoke.sh build-linux-package/RoadRashNative-<version>-linux-x86_64.tar.gz \
    build-linux-package/RoadRashNative-<version>-x86_64.AppImage x86_64
tools/build_linux_flatpak.sh x86_64       # committed HEAD only
```

The tarball and AppImage are built in the Steam Runtime "sniper" SDK
container (glibc 2.31 baseline) with static SDL, OpenSSL and libstdc++, so
they need only glibc and whatever display and audio libraries the system
has; `tools/verify_linux_package.py` checks that, the absence of game data
and (on Linux) that both launch. The Flatpak builds from source against the
freedesktop 24.08 runtime. Linux native gate: `tools/linux_gate.sh linux-gcc`
inside the `linux/gate.Dockerfile` image (see `docs/TOOLING.md`).

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
