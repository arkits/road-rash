# Verification log (all sessions September 28, 2026)

## Plan 33b Task 5: Windows packages (October 30, 2026)

- Windows CI run `41113561902` (`port/plan33b-windows`, commit `cd554cb`):
  all six jobs successful.
  - `package-x64` (windows-2022) and `package-arm64` (windows-11-arm):
    - Release CTest 132/132 (35 disc/fixture/Linux-only skips);
    - synthetic determinism in Release equal to the macOS reference;
    - `build_windows_package.py --installer require`, then
      `verify_windows_package.py --run --installer --install-test`: PASS.
  - Debug jobs: `msvc-x64` and `msvc-arm64` 132/132; `clang-cl-x64` and
    `msys2-clang64` unit 88/88; determinism equal on all four.
- What the verifier established on each architecture:
  - **Zip:** exactly `Road Rash Native/{rr-game.exe, README.txt,
    portable.txt, licenses/*}` (7 licences). No disc extensions, none of the
    341 reference SHA-256 hashes, no ISO or AVI magic.
  - **`rr-game.exe`:** machine AMD64 or ARM64, GUI subsystem; one manifest
    (UTF-8 code page, PerMonitorV2, longPathAware); icon group;
    ProductVersion `0.0.0 (cd554cb2f417)`, with no `+dirty`.
  - **Imports:** 14 system DLLs, identical on both architectures:
    `advapi32 bcrypt crypt32 gdi32 imm32 kernel32 ole32 oleaut32 setupapi
    shell32 user32 version winmm ws2_32`. No CRT or SDL DLL.
  - **Launch:** the unzipped `rr-game.exe --console --version` printed a
    Release build of the current source, and portable mode wrote
    `data/rr-game.log` beside it.
  - **Install test:**
    1. The silent install went to `%LOCALAPPDATA%\Programs\Road Rash
       Native`, with a Start-menu shortcut, no `portable.txt`, an exe
       byte-identical to the zip's, and a log in
       `%LOCALAPPDATA%\RoadRashNative\RoadRash`.
    2. The silent uninstall removed the program and shortcut and kept the
       data.
    3. A reinstall followed by an uninstall with `/PURGE=1` removed the
       data too.
- Earlier runs on the branch found:
  - Runs `41109866741` and `41112058693`:
    - version stamped `+dirty` (untracked `.vcpkg-cache`);
    - pefile's file mapping blocked temp cleanup;
    - the install test reinstalled while the first uninstaller was still
      finishing (it now waits for the folder to go);
    - `mixer-device` failed once on MSYS2 with a 100 ms first-callback
      bound (now 5 s, with messages).
  - Local: `rr-game --console --version` started the game instead of
    printing the version. Fixed; CTest `game-version-console`.
- Local supplement (not Windows acceptance):
  - The MinGW GCC Release cross build passed
    `verify_windows_package.py --allow-mingw-crt --run` under Wine
    (`--wine`): 23 imports including the UCRT API sets, launched, portable
    log present.
  - The `agent_gate.py windows-package` profile on that build passed
    (3/3 steps).
  - `sign_windows.ps1` without `RR_SIGN_*` prints "signing skipped" and exits
    0; `-Check` exits 1.
  - macOS Debug CTest 132/132, 0 skipped.
- On `main` after the merge (`5fa0554`): Windows and browser deploy passed.
  The determinism matrix's macOS job was cancelled by a scheduled `macos-14`
  runner brownout (the image retires November 2, 2026). `determinism.yml`
  and `release.yml` now use `macos-15`. Run `41116328025` (`443347d`) passed
  macOS, Linux x86-64 and wasm32. `release.yml` on `macos-15` runs only on
  the next `v*` tag.
- Not run:
  - signing with a real certificate (none is configured; owner decision);
  - the interactive uninstall prompt;
  - SmartScreen, and installing on real Windows desktops (Task 8).

## Plan 33b Tasks 2–3: manifest, file safety, game executable (October 30, 2026)

- Windows CI run `41027970095` (`port/plan33b-windows`), all jobs successful:
  `msvc-x64` and `msvc-arm64` CTest 131/131 (35 disc/fixture/Linux-only
  skips), `clang-cl-x64` and `msys2-clang64` unit 87/87; synthetic
  determinism equal to the macOS reference on all four.
- New Windows evidence in that run:
  - `utf8_path`: settings save/load and a synthetic folder import under
    `Tëst ディレクトリ` through the game's `std::string` paths. This needs
    the UTF-8 code page from the embedded manifest.
  - `first-run-smoke-unicode`: first run with `RR_PREF_OVERRIDE` in a
    non-ASCII directory.
  - `game-version`: `rr-game --version` output reaches CTest from the
    GUI-subsystem executable.
  - `platform_files`: a destination held without `FILE_SHARE_DELETE` for
    150 ms is replaced after retries; a 3 s hold fails cleanly, keeping the
    old file and removing the temporary.
  - `system_fonts`: Segoe UI loads from `%WINDIR%\Fonts`.
- Bugs found on the way:
  - Run `41024878116`: a retried rename kept the first attempt's error code,
    so `write_file_atomic` reported failure on all four jobs. Reproduced and
    fixed under Wine.
  - Run `41025942765`: a vcpkg applocal post-link race ("used by another
    process"); applocal is now off for static triplets.
  - Run `41026975309`: the platform_files holder thread raced the write on
    MSYS2; the test now waits for the open handle.
- Resources checked on the MinGW cross build with `objdump -p`: `rr-game.exe`
  is subsystem 2 (Windows GUI) with ICON (4 images), GROUP_ICON, VERSIONINFO
  and exactly one RT_MANIFEST. Test executables carry the manifest.
- macOS after the changes: Debug CTest 131/131, 0 skipped. Android debug
  gate passed (with `platform_game.cpp` added to the library sources).
- Not run: anything on real Windows hardware (taskbar icon, Alt+F4,
  fullscreen toggle, the log file location, a migration from a real
  `%APPDATA%` import).

## Plan 33b Task 1: Windows native gate (October 30, 2026)

- GitHub Actions workflow `Windows` (`.github/workflows/windows.yml`), run
  `41018820912` on `port/plan33b-windows`, all jobs successful:
  - `msvc-x64` (MSVC 19.44.35229, `windows-2022`, preset `windows-msvc`, Debug,
    static CRT, static SDL 3.4.18, vcpkg OpenSSL `x64-windows-static`): CTest
    128/128 passed, 35 skipped (28 disc-labelled, the git-ignored oracle
    fixtures, `oracle-patch`, `net_deploy`).
  - `msvc-arm64` (MSVC 19.51.36260, VS 18, native ARM64 on `windows-11-arm`):
    128/128 passed, the same 35 skipped.
  - `clang-cl-x64` (Clang 19.1.5): build plus `-L unit` 84/84.
  - `msys2-clang64` (Clang 22.1.8, libc++): build plus `-L unit` 84/84.
  - `rr-determinism --synthetic` equals `tests/data/determinism_synthetic.txt`
    on all four.
- Earlier rounds: `41013464975` (MSVC x64 built on the first try, 125/128:
  `oracle-sim` without its fixture, POSIX-only Python, `net_deploy`; ARM64 VS
  generator missing; clang-cl duplicate symbols), `41015360373` (only
  `agent-tooling`), `41016975934` (intermittent `render-threads-alloc` on MSVC
  x64/ARM64 and MSYS2, diagnosed with the new allocation sizes: four 23,040-byte
  `thread_local` vectors plus MSVC debug proxies).
- macOS after these changes: Debug `ctest -j6` 128/128, 0 skipped; Android
  debug gate (`agent_gate.py android-debug`) passed with the trimmed
  `RR_GAME_SOURCES`; `render_threads_alloc-test` passed 3 of 3 repeated runs.
- Local supplement, not Windows acceptance: MinGW GCC 14 cross-build
  (`cmake/toolchains/mingw-w64-x86_64.cmake`, network transport off) under
  Wine Staging with a private prefix and the disc: 125/125, 0 skipped,
  including `oracle-sim`, `oracle-sim-field`, `-schema2` and `-l5`.
- Not run: any test on real Windows hardware with the disc (Task 1 Step 7),
  the Windows disc determinism race, packaging, display/audio/controllers.

## Plan 33 Part A: portability groundwork (October 29, 2026)

Worktree `port/plan33-part-a` (baseline `c9eb240`). Logs are local in that
worktree (`ctest-*.log`, `web-*.log`, `gate-package.log`) and in the session
scratchpad `linux/` directory (Docker runs).

- **macOS ARM64 Debug** (AppleClang, Homebrew SDL 3.4.8, disc present):
  baseline before the changes 121/121. After Part A, `ctest -j6`: **128/128
  passed, 0 skipped** (436 s), including the new `wide_math`, `det_math`,
  `fp_contract`, `importer_case`, `utf8_path`, `system_fonts` and
  `portability-lint`.
- **macOS package gate** (`agent_gate.py package`; Release, static SDL
  **3.4.18**, `verify_macos_app.py`): passed. Release CTest **128/128, 0
  skipped**. `oracle-sim` remains the pinned known divergence at frame 209.
  Receipt: `analysis/agent-gates/package-92d2ce0b2de440ca928299824554fd4f/`.
- **Linux ARM64** (Docker `debian:trixie`, GCC 14.2; distribution SDL 3.2.10
  rejected, so the pinned 3.4.18 was built with `-DRR_SDL_HEADLESS=ON`): full
  build passed. `ctest -j4`: 127/128; the one failure was `oracle-sim` with no
  fixtures, because the worktree's `analysis/oracle` symlink was not mounted.
  With the fixtures mounted, `ctest -L "sim|disc"` gave **44/44 passed**,
  including `oracle-sim`, all three `oracle-sim-field*` replays, `race-soak`,
  `rollback`, `race_state` and `net_client`. The October 29 failures
  `oracle-actor-record`, `raster-rgb-alloc` and `input` all pass.
- **Linux desktop configure** (same image plus SDL's X11/Wayland/libdecor/
  PulseAudio/PipeWire/ALSA/udev/D-Bus dev packages, `-DRR_STATIC_SDL=ON`):
  configure passed with X11, Wayland, libdecor, PipeWire, PulseAudio and ALSA
  all ON and dynamically loaded (`*_SHARED` ON). Without the X11 extension
  headers, SDL 3.4.18 stops at configure (missing XTEST), which is why
  `RR_SDL_HEADLESS` exists.
- **Root cause of the old `raster-rgb-alloc` mismatch:** with
  `-ffp-contract=off` on both hosts, the macOS and Linux frames are identical
  (`RR_RGB_DUMP`, 0 of 307,200 pixels differ). The old pin
  `0x861b81d25eb5f7db` was contraction-dependent; the new pin is
  `0xbab29462f0960895`. Replacing libm alone did not change it, so F16's
  libm hypothesis was wrong for this test.
- **det_math:** bit hash `0x2012804f2dd7f3bf` on macOS and Linux. The volume,
  pan and fog tables equal their pre-change macOS values (`0xd60ac8d951ecfb3`,
  `0xd97c7fe8650f96e1`, `0x3f1744b8f436f1ec`). Worst error against the host
  libm is 1 ulp for both exp and log.
- **wasm32 under Node** (emsdk 6.0.10, SDL 3.4.18): 113 passed, 8 skipped
  (browser-only SDL devices and network tests that skip under Node), and
  `net_protocol` failed because the test lacked `rr_web_hostfs` and could not
  read its vector file. With that fixed (`cmake/net_tests.cmake`), it passes.
  **`raster-rgb-alloc` now passes under wasm**, closing the "remaining
  portability difference" in WEB.md. All four `oracle-sim*` replays pass.
- `tools/portability_check.py`: 0 problems. `tools/doc_checks.py`: OK.
- Not run: Linux x86-64 full gate, Clang on Linux, sanitizers on Linux, any
  Windows compile (33b Task 1), Android (SDL AAR unchanged at 3.4.8), devices.

## Plan 33: first Linux native gate (October 29, 2026)

- Host: Docker `debian:trixie` `--platform linux/arm64` on the Apple-silicon
  Mac; GCC 14.2.0, `libsdl3-dev` 3.2.10, OpenSSL 3.5.7, CMake + Ninja, Debug,
  default options (network transport on). Source mounted read-only, disc
  mounted read-only at `../cdrom`.
- Configure passed. The first build failed on `tests/world_renderer_test.cpp:184`
  (`std::copy_n` without `<algorithm>`); with that include added, the build
  passed.
- `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ctest -j6`: 117/121 passed
  (129 s). Failures: `oracle-actor-record` (`char clip == -7`; char is
  unsigned on ARM64 Linux), `raster-rgb-alloc` (pinned fog checksum
  `0x861b81d25eb5f7db` differs), `input` (phone overlay pixel at 30,345 is
  black under SDL 3.2.10), `agent-tooling` (doc check ran before the 33a/33b
  index rows existed; `tools/doc_checks.py` passes now).
- Diagnostic: a separate tree with `-DCMAKE_CXX_FLAGS=-ffp-contract=off`
  (transport off) still fails `raster-rgb-alloc` and `input`, so FP
  contraction is not the cause.
- Logs (local, not committed):
  `/private/tmp/claude-501/-Users-archit-Dev-rr/c7126a90-a6ef-4a85-b9a4-cffc8eda7b7d/scratchpad/linux/`
  (`build3.log`, `ctest3.log`); Docker volumes `rr-plan33-linux-build`,
  `rr-plan33-linux-fpc`.
- Not run: Linux x86-64 full gate, Clang, Release, sanitizers, pinned static
  SDL, display/audio hardware, Steam Deck, Windows.

## Plan 34 Task 3: snapshots, rollback, determinism (October 26, 2026)

- `make verify-sim BUILD_DIR=build-p34`: passed after the shared-data and
  ring-terrain changes. `ctest -j6`: 113/113, 0 skipped.
- `race_state` (disc, ~9 s Debug): save at every 400th tick of a 9000-tick
  scripted Napa race, run 120, load, re-run: 2640 compared ticks identical
  (2186 crashed rider-ticks, 1876 walking, cop present, 3042 pedestrian
  rider-ticks); across the split (second branch taken near tick 9393) and
  across the finish line; a two-human race with two ring windows (840
  ticks). Warmed `save_state` allocates 0 times.
- `rollback` (disc, ~30 s Debug): sealed timeline with 12% late
  substitutions and seat 1 AI-driven for ticks 300–419; frames delivered
  with random delay 0..depth (out of order). Depths 0–16 (2 humans, 600
  ticks) and 0/1/4/8/16 (8 humans, 480 ticks): final and every 60-tick
  hash equal to the straight run; deepest correction equals the depth;
  no stalls.
- `rollback-bench` (Release, M3 Pro): see PERFORMANCE.md "Rollback cost".
- Determinism matrix (`tools/determinism_matrix.sh macos linux` with the
  disc mounted, then `wasm`): macOS synthetic = reference; Linux x86-64
  (Docker `debian:trixie`, `--platform linux/amd64`) synthetic = reference
  and disc = macOS disc; wasm32 (Emscripten 6.0.10, Node) synthetic =
  reference and disc = macOS disc. Debug and Release macOS builds also
  agree. Outputs in `/Users/archit/Dev/rr/.plan34-logs/det/` (local).
- Not run: Android (no device), Windows (plan 33), the GitHub workflow
  itself (runs on the push).
## Web canvas fill and widescreen (October 26, 2026)

- `RR_WEB_BUILD_DIR=build-agent-web-widescreen-20261026 tools/build_web.sh`: passed.
- `RR_WEB_URL=http://localhost:8791 node tests/web_hosted_smoke.cjs` (bundle +
  local hosted assets over `python3 -m http.server`): all 9 PASS lines, exit 0.
  The fit check now asserts that the canvas fills `.viewport` and that the buffer
  aspect matches the CSS box at 1907x1280, 1280x960, 1440x900, 390x844 and 844x390,
  with SDL inline sizes at scale 1 and 2.
- Manual check in the in-app browser (DPR 2), `widescreen=3`: a quick race at
  1280x720 has a 1280x668 CSS canvas with a 2560x1336 buffer, side bars only
  (1.92:1 is beyond the 16:9 cap). After a live resize to 1280x860 the canvas
  is 1280x808 with a 2560x1616 buffer, edge to edge with no bars. The Game Mode
  menu fills at the widescreen width and mouse hover selects the right item.
- Native Debug `build-agent-web-widescreen-native`: build passed; full CTest
  result recorded in the handoff `docs/agent-handoffs/web-widescreen-20261026.md`.
- Not checked: Safari/Firefox, real mobile devices, browser fullscreen
  on hardware.

## Plan 34 Task 2 C3/C4: traffic and police per human (October 26, 2026)

- `make verify-sim BUILD_DIR=build-p34`: passed. `ctest -j8`: 111/111, 0
  skipped (all traffic, actor_traffic, police and oracle tests unchanged).
- `race_multi` (NAPA L0, seat 0 brakes, seat 1 accelerates): 5,626 car-ticks
  inside seat 1's own window beyond seat 0's (ticks 1200–2400); no odd-kind
  car in any tick; a cop spawned from seat 1 at sample 404 inside its
  window, with the same relationship row on both humans. Before the pool
  partition all 16 (then 32) slots filled in seat 0's window — the reason
  each window now owns 16 slots.

## Plan 34 Task 2 C1/C2 and rr-netd image (October 26, 2026)

- `make verify-sim BUILD_DIR=build-p34`: passed; `ctest -L sim`: 19/19.
- `race_multi` new C1 case (NAPA L0, full field): seat 0 brakes, seat 1
  accelerates 2400 ticks; seat 0 window samples 2..130, seat 1 at sample 311
  inside its own window 263..391, two live A.I. near seat 1.
- Docker (OrbStack): `docker build --platform linux/amd64 -f server/Dockerfile`
  over a context of only `cmake/net.cmake`, `src/net`, `server` — passed,
  image 86 MB; `docker run` → `GET /net/health` 200 with
  `"rev":"7402147…"`, SIGTERM logs `stop`. This is also the first Linux
  (GCC 12) compile of `src/net` and `server/`.

## Plan 34 Task 2 first slice: multi-human races (October 26, 2026)

- `make verify-sim BUILD_DIR=build-p34`: passed after the change (receipt
  under the worktree's `analysis/agent-gates/oracle-*`), so the receipted
  human capture stays bit-identical.
- `ctest --test-dir build-p34 -j8`: 111/111 passed, 0 skipped (goldens from
  the main checkout), including every sim/disc test.
- `race_multi` (new, disc, ~10 s): 6-human limited field layout (grid
  `0x00464258`, seats on rows, A.I. rows by entry), humans-only field, two-
  and eight-human 1500-tick runs with identical per-tick field hashes, view
  seat 7 vs 0 identical, AI hand-over at tick 700 deterministic and
  unchanged before T.
- Not covered yet: merged ring windows, LOD/traffic/police per human,
  traffic gates; cross-platform identity (Task 3).

## Plan 34 Tasks 4–5: protocol, channels, rr-netd core (October 26, 2026)

- Worktree build `build-p34` (Debug, macOS ARM64, AppleClang):
  `cmake --build build-p34 --parallel` clean; full `ctest` 108/108 passed,
  0 skipped (goldens from the main checkout's `analysis/oracle`, symlinked),
  before the server tests were added; then
  `ctest -R "net_|server_"` 4/4 (`net_protocol`, `net_ws`, `server_sealer`,
  `server_room`).
- `net_ws` uses a real localhost libdatachannel WebSocket server/client:
  100 ordered messages, a 18,445-byte LogChunk, close propagation.
- Strict warnings: every new file passes `-Wall -Wextra -Wshadow -Wconversion
  -Wno-sign-conversion` (`-fsyntax-only`).
- Fuzz: `build-p34-fuzz` (Homebrew LLVM, ASan/UBSan/libFuzzer)
  `fuzz_net_protocol` 120 s, 5,238,043 runs, 0 crashes, 638 new units
  (local corpus, not committed); `fuzz-seeds` rewrote only the new
  `fuzz/seeds/fuzz_net_protocol/`.
- `rr-netd` smoke (local): `GET /net/health` → 200 JSON, unknown path → 404,
  SIGTERM logs `stop`. Standalone `cmake -S server` Release build links.
- Not run: the Docker image build (OrbStack daemon not running), Linux
  compile, `net_bots` (needs Task 6).

## Plan 34 Task 1: mano-a-mano rules trace (October 26, 2026)

- Documentation only; no build or capture. Sources: `analysis/decompiled/`
  exports `00405600`, `004317a0`, `00431840`, `0042fbb0`, `00415fc0`,
  `00442f60`, `00440340`, `004395e0`, `00442dd0`, `00430690`, `004305f0`,
  `0042f920`, `00446c30`, `00416010`, `00416120`, `004163d0`, `00430e00`,
  `004314d0`, `00432440`, `00444fb0`, `00421df0`, and disassembly at
  `0x00423345..0x00423477` and `0x00423d5a..0x00423d6a`.
- String references checked by searching the disassembly for loads at
  `0x6ac`/`0x6b0`/`0x6b4` (427–429): none. `0x0046b1f0` (`DUEL`) has one code
  reference (attract timer).
- Not done: the optional two-instance Wine capture (plan 34 Task 1 Step 2);
  no rule stayed ambiguous enough to need it.

## Full police pursuit parity (October 26, 2026)

Owned Debug `build-agent-open` + ASan/UBSan `build-agent-open-san`:
`police-test` 0 failures — parked (cop 635 ticks, RNG/field 4730, arrest
4552), crashed (cop 935, field 5030, RNG 4715, no arrest), foot (cop 605,
full 4700, arrest 4616), foot2 (cop 605, full 4700, arrest 4544), city
(cop 456, field 4551, RNG 954; original arrest 4552 measured, unasserted).
Captures: `analysis/oracle/{napa,city}-cops-*-20261026.tsv` with receipts;
quit-mid-race `FRAMES` + `ORDER=1` (tick reset past race end, end_timer only
with ORDER).

## Menu pixel acceptance: all acceptance-run screens green (October 26, 2026)

- `frontend-test ../cdrom/ROADRASH`: 0 failures (full gate 106/106 in
  `build-agent-menu-gaps`). Still green: `menu-09` (60% masked, 98% exact,
  0% large-diff), `fe-main` (60%/98%/0%), `fe-street` (67%/96%/0%).
- GDI rule (ppem advances + tmHeight pitch from the Wine textprobe ground
  truth, 292/292 metrics, 380/380 ASCII advances) replaces the cell-scale
  renderer: characters 86%/6% (pin 0.80/0.08), graphics 77%/4% (standard
  pins now). Bitmaps stay unhinted stb: remaining fringe is GDI-shape
  residual, documented open.
- New: the test replays all three 20261027 acceptance runs and pins
  thirteen more goldens with the same item-rect-driven mask, all at
  standard pins unless noted: `fe-thrash`/`fe-bulletin` 98%/0%,
  `fe-olleys`/`fe-panzer` 98%/0%, `fe-loading` 94%/0%,
  `fe-wrecked` 93%/0%, `fe-busted` 96%/0%, `fe-won` 97%/0%,
  `fe-lost` 96%/0%, `fe-penalty` 100%/0%, `fe-standings` 100%/0%
  (mask 94%, ceiling 0.95), `fe-schmooze` 100%/0% (Pearl-N via override),
  `fe-showroom` 93%/4% (standard).
  Result variants (Wreck4/Bust4/Win4/Lose1) come from the run-2 trace and
  the test override; jukebox/graphics inheritance re-verified.
- Fidelity fixes behind the pins: desc fill gated on raw `flags & 8`
  (course/showroom/characters bios have no box); standings rows/title move
  to the 0x00421290 handler with conditional darken (bare direct entry,
  footer from the dynamic slot); showroom blurb-only desc + Demi-24 Length
  footer at the traced y=301; player/price footers centered at y=380 with
  fully mapped content (formats, slots 29/30/31/504/505, trade = price/2,
  0x420aa0 bike names), visually verified (masked in tests).
  Disassembly evidence in REVERSE_ENGINEERING.md "Front end".
- `race_cache_test`: scale-2/scale-4 RGB baselines re-recorded after the
  advance rule (HUD glyphs move by whole pixels); `original_scene` scene
  hashes unchanged, all downstream pixel/allocation checks pass.

## Police parked-scenario parity; original-driven save (October 26, 2026)

- `napa-cops-parked-20261026.tsv` (Wine, COURSE=4 LEVEL=3 SEED=1 HUMAN=1,
  FRAMES=4800, quit at race end): `police-test` reports
  "cop matches on 635 ticks; RNG matches to 4730, opponents to 4730" and
  "NAPA arrest on tick 4552 as the original". Truncated past the race-end
  tick reset (frames 4732+ belong to post-race); re-run with
  `RR_ORACLE_ORDER=1` and mid-race `FRAMES` in progress for direct
  `race.end_timer` evidence.
- `fe-career.RRS` + `fe-career-saved.state`: `career-test --save-corpus`
  passes (0 failures), pinning the original-driven round trip.

## Tunnel engine variant (October 26, 2026)

Owned Debug build `build-agent-open` plus ASan/UBSan tree `build-agent-open-san`:

- `sound-test ../cdrom/ROADRASH`: 0 failures, including the new
  `Race::player_in_tunnel` pins (no terrain false; terrain type 3 true;
  type 2 false). Player engine mix now reads the flag (`0x00434a80`) and
  selects slot 2 (`EngTunne`) instead of always slot 0.

## Gameplay and frontend open items (October 26, 2026)

Owned Debug build `build-agent-open` (`cmake -S . -B build-agent-open`,
`cmake --build build-agent-open --parallel`):

- `sound-test ../cdrom/ROADRASH`: 0 failures, including the new car-engine
  pins (slots 4/6 `Engcar11` vs 3/5 `EngOther`; near car audible in the
  others channel, far vehicles silent).
- `career-test`, `career-test --corpus ../cdrom/ROADRASH`,
  `career-test --save-corpus`: 0 failures, including the new driven-career
  round trip (Cydney, win City, affordable upgrade, save, reload equality +
  byte identity) and the seeded `save-seed-20261026.rrs` check.
- `frontend-test ../cdrom/ROADRASH`: 0 failures; all 223 original items
  swept (2 scrollable below the fold, 28 hidden placeholders), main-menu
  1x/2x/4x ink checks, menu-09 masked capture (60% masked, 98% exact,
  0% large-diff).
- `police-test`, `crash-test`, `traffic-test`, `pedestrian-test`,
  `car_contact-test`, `music-test ../cdrom/ROADRASH`, `race-test`: 0 failures
  (police live spawn/chase/arrest and on-foot arrest pass; cop oracle
  fixtures absent in this tree, skipped).

## Napa contact scenario to tick 3840, pinned single-static divergence (October 26, 2026)

- New capture `sim-napa-contact` (course 4, seed 1, field/HUMAN=1,
  4,200-word throttle-only script, FRAMES=4500): ended at 4,381 ticks
  via the `0xb4` quit rule; validated as a partial (`validate
  --simulation --frames 4381`): header, contiguous ticks 1-4381, EXE
  hash, exit 0, `control_branch human`, `complete true`. Promoted to
  `analysis/oracle/sim-napa-contact.tsv` (git-ignored). Trace
  `7ea88dc3…`. No accepted capture overwritten.
- Owned Debug build `build-solo-split-845` (`oracle-sim-test`, auto
  human): replay identical through tick 3840 with 0 sim-field
  divergences. Fixes along the way: camera follows the half image
  (`0x00437c30` reads `*(player+0x2fc)+0x138`; post-move pre-contact
  mirror, so contact shoves no longer leak into the ring -- tick 1469
  lag gone); half `+0x1a0` contact-link words excluded as host
  addresses (20986896 = bike node at 2700); remount clears the half
  crash bits (`+0xc0` 7 -> 4 at the 2724 walk-back remount).
- First divergence at tick 3841: RNG draw (2 native, 0 golden) plus the
  glance response -- a single-box BORR static the native triple-wide
  rule overlaps marginally. Root-caused to the `0x004373f0` triple
  gate (sub-slice `+0xac` bit `0x4000`): no writer found in the export
  set, triple required at the 2055 crash but spurious here. Pinned
  without touching goldens; see the SIM_SCENARIOS entry and the
  `napa-contact-20261026` handoff. No CTest registration.
- No regressions at pin: splits 6681 each, fight 2581, cov2 1431,
  script 1801, L1/schema2 2400 (all clean/unchanged).

## Napa fight scenario fully green (October 26, 2026)

- New capture `sim-napa-fight` (course 4, seed 1, field/HUMAN=1, 2,400-word
  throttle script with punch every 30th word, FRAMES=2700): ended at 2,581
  ticks via the `0xb4` quit rule; validated as a partial (`validate
  --simulation --frames 2581`): header, contiguous ticks 1-2581, EXE hash,
  exit 0, `control_branch human`, `complete true`. Promoted to
  `analysis/oracle/sim-napa-fight.tsv` (git-ignored). No accepted capture
  overwritten.
- Baseline `14204b5` (splits green). Owned Debug build
  `build-solo-split-845` (`oracle-sim-test`, auto human driver):
  `sim-napa-fight.tsv` 30 -> 780 -> 2239 -> 2377 -> full `2581 ticks
  identical`, 0 uncovered sim gaps (57 golden-only; 0 rpm notes).
- Fixes: human punch acquire (`0x00409400` nearest-half rule + brain
  record, `0x004095d0` side incl. car/ped fallback; REA 6.4.0 confirms
  the export); crash stamina wrapper regen (+dt when crash != 1, on top
  of the `0x00407bd0` passes); walk-back `0x7800` override gated on raw
  control bits (+ human + racing), old bike-bit mapping removed;
  harness rpm rule shared with solo + nondeterministic-channel demotion
  (field + solo harnesses), nulled-body node listing.
- No regressions (same build): splits 6681 each, cov2 1431, script 1801,
  L1/schema2 2400 (all clean/unchanged).
- Full owned-Debug gate: 105/106 + isolated pass (the one failure is the
  known-flaky `render-threads-alloc` steady-state count under parallel
  load -- identical 4-allocation signature as the October 18 concurrent
  run, passes in isolation; renderer untouched by this slice).
  `clangd --check` clean on touched sources; `rg` throughout.
- Remaining (Task 6 order): `napa-fight` dedicated steal/isolated cases,
  `napa-contact`, traffic-solo/field; CTest registration stays manual.

## Solo splits fully green; crash/static/half/freeze/rpm (October 26, 2026)

- Baseline `a4572e0` (split 845->2056). Owned Debug build
  `build-solo-split-845` (`oracle-sim-test --driver=human`, git-ignored
  split captures): `sim-napa-solo-split-left/right.tsv` advance 2056 ->
  full `6681 ticks identical` each, 0 uncovered sim gaps (19 golden-only;
  8/94 rpm-timing notes on the nondeterministic channel).
- Fixes in `src/race.cpp` (all cited to original addresses):
  `collide_hazards` runs the `0x004373f0`/`0x00437660` static sweep for
  crashed riders (tables path still rejects; `contact_static` crashed
  path gives the 2056 bump/nudge/kick); crash half follows the body
  (`sync_crash_half`, body pre-filled with half spec words at entry);
  `update_halves` no longer clobbers crash mirror/finish with bike words;
  place refresh runs while crashed but stops once nulled; solo
  freeze-hack mirror (`Rider::half_frozen` past 256 bike-body gap: bike
  parts continue, half parts stop; harness lists the nulled body as a
  half scheduler node). Harness (`tests/oracle_sim_test.cpp`): rpm
  updates per frame, plus per tick while crashed grounded; rpm mismatches
  demoted to diagnostic notes (proven nondeterministic: identical
  inputs+seed disagree on 252 ticks while sim matches everywhere).
- No regressions (same build): `napa-field-l1` 2400 identical,
  `schema2` 2400 identical, `sched-human-cov2` 1431 identical,
  `sim-napa-solo-script` 1801 identical (+5 golden-only).
- Full owned-Debug gate: **106/106 passed** (`ctest -j8`, exit 0, 159.80 s;
  `clangd --check` clean on `src/race.cpp`/`race.hpp`/
  `tests/oracle_sim_test.cpp`; `rg` throughout).
- Remaining: split CTest registration (manual like script/L5d/L5e);
  `napa-solo-jump`, `napa-fight`, `napa-contact`,
  `napa-traffic-solo/field` (same Task 5/7 procedure).

## Solo split contact gates, flash, crash entry and brain (October 26, 2026)

- Baseline `e101f2d`. Owned Debug build `build-solo-split-845`
  (`oracle-sim-test --driver=human`, git-ignored solo-split captures):
  `sim-napa-solo-split-left.tsv` and `-right.tsv` advance from the
  tick-845 divergence (`rider.0.lateral` original 70 vs native -1952,
  RNG in sync) to tick 2056 (`rider.0.lateral` 212334 vs 220494,
  RNG in sync; scripts identical through tick 4500, so shared).
- Fixes in `src/race.cpp` (all cited to original addresses):
  `collide_riders` enforces `0x004368f0` outer `+0xc0&6==6` and inner
  `+0xc0&5==5` even when `!live_schedule_` (the overlapping frozen is
  `c0=4` and correctly ignored) -> 845 to 1423; solo uncrashed
  `0x00407a90` scenery-mark flash refresh in `step_rider` (same form as
  `crash_bike_animation`/`run_bike_node`) -> 1423 to 2055;
  `anim_deadline` snapshot sign (`int32_t`: `0xFFFFFFFF` as -1, harness
  artifact, no sim change); crash-entry half copies (`0x00403d60`
  lines 52-112: entry word set to `half_object` plus `+0xc0 |= 3` in
  both `crash_entry` and `crash`) -> half flags/ground at the 2055
  reason-2 gap crash; solo crash brain (`ai_transition` + state clock +
  replan + crash-gated stamina in `step_crash`, dispatch no-op for
  states 8/9) -> state 9/ticks/replan at 2056.
- No regressions (same build): `napa-field-l1` 2400 identical,
  `schema2` 2400 identical, `sched-human-cov2` 1431 identical,
  `sim-napa-solo-script` 1801 identical (+5 golden-only).
- Full owned-Debug gate: **106/106 passed** (`ctest -j8`, exit 0).
  `tools/agent_lsp.py` healthy (187 entries);
  `clangd --check=src/race.cpp` exit 0. `rg` used throughout.
- Remaining: tick-2056 crash-motion gap (lateral + velocity + steer,
  RNG in sync; likely riderless-bike vertical `0x0043a190` and/or
  `0x0043a610` wall stand-ins, plan 11 Task 4). Split captures stay
  manual (golden-only suffix), not registered as CTest.

## Focused ASan/UBSan (October 26, 2026)

- Baseline `3ab6f5b` + docs (no src changes since `b619cb4`).
  Owned ASan+UBSan build `build-agent-asan-split`
  (`-fsanitize=address,undefined -fno-sanitize-recover=all`):
  `race-test` 0 failures, `traffic-test` 0 failures
  (car contact synthetic boundaries pass; CANYN walls, MEDLY/CITY/NAPA
  intersections, Napa spawns/density all pass).
  Receipts `analysis/agent-gates/asan-focused-20261026/` (git-ignored).
  Full ASan suite NOT run (focused only; integer sim logic, no new
  allocation/threading -- same justification as prior slices).
- Android debug assembly already green (`assembleDebug BUILD SUCCESSFUL`,
  same tree). Device/hardware still open (plan 14).

## Original browser fonts (October 25, 2026)

- Preview READY: https://road-rash-o4j8h7nq2-arkits.vercel.app, shell `ba1fe64`, engine `096bff2`.
  Deployment `dpl_C919ivGcxzuxZjk82DwMYZBy1L4i`. JS/CSS in the seven-file
  prebuilt output byte-match source. No deployed-URL fetch performed.

- `src/text.cpp` identifies original TrueType disc fonts (`TEXT/*.DLL`). The
  browser shell loads the four verified files directly from OPFS using binary
  FontFace sources; headings use Badloc and control/body text uses Futura.
- Chromium/Playwright with original disc folder import: all four FontFace entries
  report loaded, CSS resolves headings/buttons to the intended families, fonts
  restore on page reload, quick race runs, controls popup works, and 390×844 /
  844×390 toolbar/canvas geometry fits without overflow. No JS errors or font/DLL
  network requests. Same checks with generated deployment CSP headers.
- Evidence: `analysis/browser-fonts-20261025/` (local screenshots/logs only).
  JavaScript syntax, whitespace and seven-file exclusion check pass. Reused
  validated engine `096bff2`; no C++ changes or new native/wasm test run required.
  Actual font files match; browser antialiasing is not claimed to match GDI pixels.

## Android debug assembly (October 26, 2026)

- Baseline `ae5ea41` + docs (no src changes since `b619cb4` gate).
  `android/gradlew -p android assembleDebug -PdevAssets=true`
  (`JAVA_HOME` openjdk@21): `BUILD SUCCESSFUL in 17s`
  (`verifyDebugNoGameData` passes; developer assets enabled for debug,
  local research only). Covers the `collide_traffic`/`contact_car`
  crashed paths and `+0x324` writes (pure integer sim logic, no Android
  API). No device connected; hardware import/render/input/lifecycle
  still unverified (plan 14).

## Focused ASan/UBSan for crash-contact + type-word (October 26, 2026)

- Baseline `b619cb4` + docs (no src changes since the 106/106 gate).
  Owned ASan+UBSan build `build-agent-asan-split`
  (`-fsanitize=address,undefined -fno-sanitize-recover=all`):
  configure/build exit 0. Focused `race-test` 0 failures,
  `traffic-test` 0 failures (car contact synthetic boundaries pass;
  CANYN walls, MEDLY/CITY/NAPA intersections, Napa spawns/density all
  pass). Covers the `collide_traffic`/`contact_car` crashed paths and
  `+0x324` writes (write-only field, no readers yet).
- Full ASan suite / Android assembly not run (same justification as
  prior slices: no sanitizer-sensitive allocation/threading change,
  no Android-specific code; integer sim logic only).

## Browser layout revision (October 25, 2026)

- Shell `7d8f7d7`, engine `096bff2`: Vercel preview READY at
  https://road-rash-32f5uhubr-arkits.vercel.app
  (`dpl_4GgoKCBX6fL6kUzQ8wr3ME7RAdiV`). Asset-free prebuilt upload; no
  deployed-URL fetch. Receipt retained with local layout evidence.

- HTML/CSS/status-label change only. Reused the clean, validated `096bff2`
  wasm engine from `build-agent-browser-release/dist`, copied the changed web
  shell into `build-agent-browser-layout/dist`, and ran the seven-file asset
  exclusion/staging check successfully. No native source/build changes.
- Playwright/Chromium local visual check with real disc folder import and quick
  race: 1440×900, 1907×1280, 1280×800, 390×844 and 844×390. Canvas stays 4:3,
  fully visible, no page scroll; desktop/landscape height is within 80 px of the
  viewport. Title 24 px desktop / 20 px mobile. Controls popup works; no JS errors.
- Measured 1907×1280 canvas: 1610.66×1208, y=48..1256. Screenshots and layout
  log under `analysis/browser-layout-20261025/`. JS syntax and whitespace pass.
  No new native/Node suite run: game binary and simulation were unchanged.

## Playable browser port (October 25, 2026)

- Final Vercel preview **READY**: [https://road-rash-exd6fu2sm-arkits.vercel.app](https://road-rash-exd6fu2sm-arkits.vercel.app), deployment
  `dpl_8YqHHSB6mrtaCJj7BgxoDmkKXoYb`, clean engine/config commit `096bff2`
  (implementation `8673a75`). CLI inspection confirms preview/Ready. Existing
  project Vercel authentication applies. No deployed-URL fetch was made (deploy
  skill restriction); the exact static release was tested locally with its
  content policy, then uploaded prebuilt. Final packaging change aligns
  `vercel.json` and output headers; browser/game behavior is unchanged.
- Clean release worktree built with `dirty=false`; source fingerprint and bundle
  SHA-256 receipt in `analysis/browser-20261025/`. The full browser smoke passes
  on the clean committed game, including visible-pixel assertions for every
  course and frontend. Logs copied there alongside native/Node CTest logs.
- Browser clangd check could not fully resolve the Emscripten driver/host C++
  headers (`xlocale.h`); native assets check is clean and the real wasm compiler
  builds all guarded browser sources. This is an LSP environment limit.


- Owned browser build `build-agent-browser`: pinned emsdk 6.0.10 / SDL 3.4.8,
  `tools/build_web.sh`, Release, shared `rr-game`, no pthreads. Only seven
  allowlisted code/metadata/licence files staged in `.vercel/output/static`;
  reference-hash exclusion passes. Wasm about 3.2 MiB raw.
- Native Debug configure/build and full CTest in `build-agent-browser-native`:
  **106/106 passed**, zero skips, 165.35 s (final native run). Source changes
  after that run are launcher/tests/docs and deployment settings/tooling only.
- Node suite in `build-agent-browser-tests`: **101 pass / 2 skip / 1 fail** of
  104 (33.07 s). `race-screen` and `mixer-device` skip 77 under Node. Remaining
  failure is `raster-rgb-alloc`'s pre-change native pixel checksum; allocation
  and repeat-frame checks pass. Golden unchanged. No full-colour pixel-parity
  claim. Original renderer, hires, sim replays and terrain oracles pass.
- Fixed wasm thread-constructor failures with inline render bands on non-pthread
  builds. PCH terrain's 150 MB TSV expands beyond the default 2 GiB wasm heap:
  Node-test-only 4 GiB maximum fixes `std::bad_alloc`. Added scene-test exception
  reporting so this failure is readable instead of an opaque CppException.
- Launcher redesign (October 25): built bundle from
  `build-agent-web-viewport-loading-20261025/dist` with the new `web/` files,
  served on localhost:8765. Ready view checked in the browser pane at 800x600,
  375x812 and 800x420, including Start Game hover. `tests/web_browser_smoke.cjs`
  passed every functional step (malformed ISO, folder import, cancel, quota,
  reload, quick race + audio, pause/fullscreen, courses 1-4, frontend, missing
  file recovery). The first run's final console-error assertion failed on 9 404s
  for `/game-assets/manifest.json`, the hosted-asset probe that a plain local
  server does not provide (the probe predates the redesign). The smoke test now
  ignores only a 404 console error whose resource path is exactly that manifest;
  every other console/page error still fails. Rerun: all 14 steps PASS, exit 0.
- `tests/web_browser_smoke.cjs` with bundled Playwright **1.62.1**, existing
  Chromium install, local static server and original disc. Also tested a local
  ISO made with `hdiutil makehybrid -iso` from `../cdrom` (fixture stays in the
  ignored build tree). Tests cover malformed ISO, 341-file folder/ISO verification,
  cancellation/quota preservation, OPFS and IDBFS reload, all five courses,
  keyboard acceleration, running AudioContext + nonzero PCM, pause, lifecycle
  bridge, fullscreen, frontend and eviction recovery. HTTP requests restricted
  to same-origin GETs; no game-data transfer or JS errors. Deployment CSP tested
  locally using the generated output headers. Screenshots/network evidence:
  `analysis/browser-20261025/` (local only).
- Native clangd check of `src/assets.cpp`: zero diagnostics using the owned
  compilation database. Browser build compiles its guarded code. JS/shell/Python
  syntax checks and `git diff --check` pass. No Android rebuild, device input,
  Firefox/Safari, full career round trip, new oracle capture or sanitizers in
  this browser slice. Other OMP captures/changes were preserved.

## Solo braking/steering script, player-control green (October 26, 2026)

- Baseline `b619cb4`. New oracle capture `sim-napa-solo-script`
  (course 4, seed 1, solo, HUMAN=1, 1,620-word script, FRAMES=1801):
  validated header, 1801 contiguous ticks 1-1801, end marker, EXE
  `73d7f974…`, exit 0, `demo_hold == 0` (player-control; first attempt
  without HUMAN=1 gave demo branch, rejected). Trace `97ec1aac…`,
  receipt git-ignored (analysis/oracle/ never committed).
- Native replay with owned Debug build `build-agent-crash-2278`
  (`oracle-sim-test`, auto human driver): `1801 ticks identical`
  (+5 golden-only, 0 uncovered). No divergence -- no src changes.
  Like L5d (+13) / L5e (+43), the golden-only suffix can neither pass
  nor pin, so verified manually, not registered as CTest.
- No sanitizers/Android run (no src changes; capture + replay only).

## Bike +0x324 rider type, no behavior change (October 26, 2026)

- Baseline `77b0b7c`. `src/race.cpp` `place_rider` sets bike `+0x324`
  (`0x00405990:20`, 0 player / 1 opponent; 2 cop already in
  `police.cpp:90`). `+0x328` stays 0 for all, so the `0x0043a580`
  walls branch gate still always calls walls; write-only, nothing
  reads `+0x324` yet (`rg -n "0x324" src/` shows only the two writes).
- Replays with owned Debug build `build-agent-crash-2278`:
  L5d `2400 identical` (+13 golden-only), L1 `2400 identical`.
- Full owned-Debug gate: **106/106 passed** (L5d/L5e verified manually
  outside CTest; sanitizers/Android not run: write-only fidelity
  field, no simulation readers yet).

## Crashed car contact and L5d full green (October 26, 2026)

- Baseline `f8a4c59`. `src/race.cpp` `collide_traffic` runs separation
  + `contact_car` for crashed (removed the `+0x2f4` early-return;
  `0x004368f0` never gates on crash). `contact_car` for crashed
  (`+0x2f4 != 0`) does the `+0xf4 += 0x150` bump, skips `knock_off`
  (down stays false), skips the `|184|/4` bleed and the `shit/2`
  steer-rate shove; tail kick baseline is `magnitude` (`full>>10`) vs
  `+0xf0` (e.g. 2 vs 0 gives -2). `0x004049c0` crashed path, REA 6.4.0
  `0x0043a580` (gated `0x0043a610` walls not the trigger here:
  routes 0/0 same), disassembly `4060e4-4060f2`/`406165-40616e`.
- Replays with owned Debug build `build-agent-crash-2278`:
  L1/schema2/L5 `2400 identical`; L5e `2400 identical (+43
  golden-only)`; strict human `sched-human-cov2-20261017`
  `1431 identical`; L5d 2278 lateral (-12032) / velocity (+330) /
  speed (-427 bleed) / steer (+857 vs -2 kick) sequence gone --
  L5d now `2400 ticks identical` (+13 golden-only; pin retired, CTest
  unregistered like L5e since a golden-only suffix can neither pass
  nor pin -- verified manually, no golden changed).
- Full owned-Debug gate: **106/106 passed** (see receipt below;
  L5d/L5e verified manually outside CTest; sanitizers/Android not run:
  no sanitizer-sensitive or Android-affecting change beyond the car-contact
  lines + pin retirement + CTest unregistration).

## Crash-entry finish recompute and L5d 2267 -> 2278 (October 26, 2026)

- Baseline `14df8b2`. `src/race.cpp` `crash_entry` (contact path) and
  `Race::crash` (half-node entry) now mirror the `0x00403d60` tail:
  after `begin_crash` + `sync_half_mirror`, when `finish_q8_ != 0` and
  the bike holds a live slice (`bike_slice_present`, proxy for body
  `+0x2c != 0`), set
  `r.finish_distance = finish_q8_ - body_pos`
  (`r.crash_actor.motion.i32(0x1c)`). This is the
  `thunk_FUN_00407410(body)` direct path
  (`DAT_004b6e58 - body+0x1c` when finish known and slice present;
  `analysis/decompiled/00403d60.c:135`,
  `analysis/decompiled/00407410.c:20-24`; REA 6.4.0 `function
  0x00407410`, Ghidra 12.1.4, Node v25.8.1, `JAVA_HOME` openjdk@21).
  It discards any split-branch walk offset at crash entry, which is
  what lets a branch rider rejoining the trunk match (L5d tick 2267:
  native rider 9, slot 9/10 overtake, +374 move, crash 0->1,
  `-23 -> 0` reset; `RR_FIELD_TRACE=9`).
- `0x004060b0` gate (from `analysis/original-disassembly.txt`
  `4060e4-4060f2` / `406165-40616e`): the same-node incremental vs
  `0x00407410` recompute test compares `*(slice_ptr)` (first DWORD =
  next link) before/after the `0x0043a580` move, not node IDs.
  Native still compares `road[sample].node`; the slice-identity port
  (plan-17 ring work) stays open. The crash-entry reset above is
  independent of that gate.
- `0x00406890` (`analysis/decompiled/00406890.c:15-16`): crashed-half
  callback does `+0x1f8 += +0x30 - +0x1c` (body delta). Native
  `crash_body_motion` already does `body_before - body_after`; with
  the entry reset both sides integrate the same body delta
  (half_mirror/body positions already match).
- Replays with owned Debug build `build-agent-crash-2267`:
  L1/schema2/L5 `2400 identical`; L5e `2400 identical (+43
  golden-only)`; strict human `sched-human-cov2-20261017`
  `1431 identical`; L5d 2267 gone, advances to tick 2278
  (`rider.10.lateral original 62008 native 49976`, riderless-bike
  lateral for the same crashed rider; pos/speed/crash/finish/RNG
  identical). Re-pinned as known-divergence (diagnostic, not
  acceptance). Full owned-Debug gate: **107/107 passed** (see
  `VERIFICATION.md`; sanitizers/Android not run: no
  sanitizer-sensitive or Android-affecting change beyond the two
  finish lines + pins).
- Remaining: tick-2278 riderless-bike lateral (`0x0043a580` /
  `0x0043a610` wall path for the crash bike, steer 0 vs -2);
  uncrashed `0x004049c0:267-268` trailing `0x00407410` not ported
  (deliberately: would reset branch offsets on every contact);
  full `0x00407410` branch-aware walk not ported (the `+23/-23/+298`
  offsets from tick 2240 onward; plan-17 ring/slice work).

## Slot-rule lane validity and L5 removal-run closure (October 26, 2026)

- Baseline `61b9a0c` through `32605ef` (docs/format only in between).
  `src/traffic.cpp` `plan_speed` ports the `0x0044e710:122-215`
  lane-validity block with the kind asymmetry intact (kind 0
  non-strict `count <= lane` outside; kind 2 strict `count < -lane`
  outside) plus the step-then-recheck-then-stop-or-restore shape;
  lane words are read from the newest same-residue ring slot
  at/under the ring front (mod-128 over the 0x8000 window, new
  `front_q8` parameter). `tests/traffic_test.cpp` gains 6 synthetic
  pins (kind-0 step+stop, pull-to-0 without stop, cruise holds,
  kind-2 step+stop, both no-fire cases).
- Oracle captures (all `RR_ORACLE_COURSE=4 RR_ORACLE_LEVEL=5
  RR_ORACLE_SEED=1 RR_ORACLE_SIM=1 RR_ORACLE_DRIVE=2
  RR_ORACLE_FRAMES=2400 RR_ORACLE_RATINGS=1`, receipts local and
  git-ignored): `napa-field-l5d` (proxy `0572d4ce`, w2c/we8
  branch words) and `napa-field-l5e` (proxy `30f4a83e`, per-tick
  car `+0x140/+0x2c/+0x30`/link-selector/linked-slice words).
- Replays with the owned Debug build (`build-agent-l5d`):
  L1/schema2 `2400 ticks identical`; L5 (1995) now identical
  (old pin retired); L5e identical plus 43 golden-only
  diagnostic keys (kept unregistered: a golden-only suffix can
  neither pass nor pin); L5d advances 2002 -> 2267 and is
  re-pinned on a single rider finish-distance word (+23, all
  cars/riders/RNG otherwise identical -- split/merge handover,
  known-open).
- Full owned-Debug gate (`build-agent-l5d`): **107/107 passed,
  zero failures**. Sanitizers/Android not run in this slice
  (no sanitizer-sensitive or Android-affecting change: native
  traffic logic + tests + oracle proxy + pins only).

## Menu capture and RGB565 picture pipeline (October 26, 2026)

- Baseline `1eab47e`. New proxy mode `RR_ORACLE_MENU=<screen>` in
  `oracle/aweman_proxy.c`: a watchdog thread polls `0x0046a660` for the
  target screen, dumps the live `0x00485df8` colour table to
  `C:\menu-table.bin`, then screenshots the 640×480 client window to
  `C:\menu-<id>.bmp`. Read-only w.r.t. game state; the driver kills the game
  once the BMP lands. `oracle/build.sh` gains `-lgdi32`.
- Screen 9 run under an `agent_resource` claim (prefix + install), cnc-ddraw
  gdi renderer temporarily at 640×480 (`ddraw.ini` restored, hash-verified):
  `analysis/oracle/menu-09-20261026.bmp` (+ `.receipt.json`; TSV hex + window
  dump agree). Boot takes ~100 s under Wine (intro video), so the watchdog
  allows 300 s. Failure analysis on the way: an unflushed 120 s watchdog and
  the ini-size question were both settled with heartbeat logging.
- Findings: the live table maps every 555-grid input to RGB565 (32768/32768,
  no odd 6-bit greens) — the picture surface is 565. Port fix: `quantise565`
  in `src/rri.*` (green channel now matches). Dark fills measure as
  halved-555 of the decoded photo (button bar/desc boxes within JPEG noise),
  contradicting the naive table-mask reading; the halving path is unlocated,
  so dark fills stay `darken555` and the question is recorded open in
  REVERSE_ENGINEERING.md rather than guessed.
- `tests/frontend_test.cpp`: unit pins updated (`quantise565`,
  `darken555`) plus a screen-9 golden comparison (text/chrome masked 60%;
  unmasked exact 15% floor, large-diff (>32) ceiling 5%; currently 15%/1%).
  Thresholds are characterization pins; full-pixel acceptance stays open.
- Full owned-Release gate (`build-agent-frontend-menu-26`): **106/106 passed**,
  zero failures. `make verify-sim` / sanitizers / Android not run in this
  slice (no sim, sanitizer-sensitive, or Android-affecting change: proxy is a
  separate mingw build, game changes are render-path only).

## Original save-file writer capture (October 26, 2026)

- Baseline `fe33fd9`. New proxy mode `RR_ORACLE_SAVE=1` in
  `oracle/aweman_proxy.c` (`capture_save`, mirroring the raster mode-2
  attach-time pattern): seeds the 32-byte state `0x004b8a10`
  (player 0, character 3, level 2, qualified 0x05, bike 7, course 4,
  place 1, flags 1, cash 0x12345678, name `SAVETEST`), the 10 personality
  triples at `0x004c6b40` for rider index 2 (`0x00467a6c`, from
  `0x00415fc0`), points `DAT_004b8900` at `C:\save-seed.RRS`, calls the
  original writer `0x0042f000`, hex-dumps its `0x00497090` buffer. The
  reader `0x0042f110` is not called in-proxy (it runs the UI string setter
  `0x00444fb0`, unsafe before a window exists); the native reader is pinned
  by the byte-identity test instead. No menu driving was needed
  (`RR_ORACLE_KEYS` only posts race-frame keys).
- `oracle/build.sh` (mingw, one pre-existing palette `-Wformat-truncation`
  warning, unrelated), `oracle/install.sh` → proxy
  `ec1ac9e564e0b3195cd058e866ffc7ec798cca9d516ce377472a83289cf35a3d`.
  Direct Wine run of the installed `ROADRASH.EXE` under an
  `agent_resource` claim on the prefix/install; killed only its own process
  group once `C:\save-seed.RRS` reached 0xa0 bytes.
- Golden `analysis/oracle/save-seed-20261026.rrs` (sha256 `45c601fd…3619c388`)
  plus `.receipt.json` (proxy/EXE hashes, seed, method); TSV dump matches the
  file byte-for-byte. Magic `0xcafedead`, header, cash, name and all 30
  seeded triples (including negatives and `0x7fffffff`) read back as seeded.
- `tests/career_test.cpp --save-corpus` (new `career-save-corpus` CTest, skip
  77 without the git-ignored golden): `read_save` accepts the original file,
  every field matches, `write_save` is byte-identical. Synthetic
  `career-test` still 0 failures. Full owned-Debug gate
  (`build-agent-frontend-save-26`): **106/106 passed**. This pins the writer
  format, not a driven career save (plan 13 Task 6 Step 1's menu-driven
  variant stays open), and no menu pixel comparison was attempted.

## Crash-think flash and player-only nitro (October 25, 2026)

- Baseline `ec7f0c1`. Disassembly evidence: `analysis/original-disassembly.txt`
  `407a91-407b0b` (player check, +0x7c clear, flash words), `40b1a0-40b1fa`
  (global charge `DAT_004c5f10`, dt = bike +0x18), `437e69-437e6f` (+0x10
  stored before think). REA has no `0x00407a90` procedure entry (known gap
  #8); `0x0040b1a0` was REA-confirmed instead (whitespace-identical to the
  export, 438 chars).
- `race-test`: new player-only nitro pin (AI charge stays 0 while the player
  recharges over 10 ticks). `crash-test`: new pickup-flash pin on the live
  Napa race (gap-1 mirror to 1, 20-tick staleness expiry, off-sample clear)
  plus crashed-player nitro tick. Both pins fail before the fix by
  construction (AI recharged; +0x81 stayed 0) and pass after.
- Owned Debug build `build-agent-crash-flash`: configure/build exit 0.
  Focused `race-test`/`crash-test` pass. Full
  `ctest --test-dir build-agent-crash-flash --parallel 8`: **105/105 passed,
  zero skips**. `make verify-sim BUILD_DIR=build-agent-crash-flash`: exit 0
  (strict human 1431 ticks identical; field replays green, no new divergence
  pin). Owned ASan+UBSan build `build-agent-crash-asan`: focused
  `crash-test`/`race-test` and all four sim/field tests pass, then the full
  suite **106/106 passed, zero skips**. `tools/determinism_check.sh`
  (Debug `build-agent-crash-flash` vs Release `build-agent-crash-release`):
  determinism OK (3 hashes). No Android/oracle-capture run.
- Live parity note: the run_bike_node flash was first rewritten to the exact
  bike words (position>>8, +0x10) and the field replays diverged at tick 1301
  (`rider.3.raw_1e8` 896 vs 128: mark kept vs cleared); the think was
  restored to the accepted sample form and the helper flash was gated to the
  non-live loop only, after which all three field replays pass.
- Exact-word follow-up closed (same day, same build): the rider was
  uncrashed, ruling out the helper. A scratch `DESYNC` probe in bike think
  (reverted after the run, never committed) showed native `r.sample` equals
  bike +0x1c>>8 at every bike think over the full 2400-tick L1 replay (0
  mismatches). Root cause was the tick word: no `i32(0x10)` write exists
  anywhere in native code, so the rewrite read a dead word (always 0) and
  the gap never stayed below 12. The +0x10 image is `Rider::sched_last`
  (the field replay compares it against golden `bike_10` tick-for-tick),
  stored to `ticks_` immediately above the think. The think now reads
  `r.sched_last`; behavior identical, all replays green.

## Crash pickup and walk-back render (October 24, 2026)

- Baseline `1a43c5e`. `crash` test gained live-Napa checks: the riderless bike
  has no selected frame on consecutive state-3 ticks, and the crash-body view
  pose carries +0x5c/+0x60 while walking (animation 38). Both checks failed
  before the fix (each verified independently) and pass after.
- Debug full build (`build-pickup`) and all **105/105 CTests passed, zero
  skips**, parallel 8. `make verify-sim BUILD_DIR=build-pickup` passed
  (receipt `analysis/agent-gates/oracle-b0ae1168162a48b79f83c317c71e0c48/`).
- Not done: no in-game visual capture of the fixed pickup/walk frames; the
  front-facing walk frames come from the existing view selection by yaw.

## Proximity-aware demo combat correction (October 24, 2026)

- Baseline `e865dd6`. The original cut used `autopilot()`'s unconditional
  `ticks % 40` punch. Replaced it with a same-road, active-rider check using
  the native weapon/longitudinal/height reach; no new swing while one is active.
  This changes the demo driver's control requests, not native combat rules.
- Release full build passed. Focused `race-screen` passed, including nearby
  left/right/behind targets, empty road on tick zero, distance/elevation/branch
  rejection, parked/inactive/crashed/detached targets and busy-swing rejection.
- Debug full build and all **105/105 CTests passed, zero skips**, parallel 8,
  209.96 seconds. Logs are in `analysis/demo-video/twitter-2min-proximity-combat/`.
- Local deterministic gameplay audit: the corrected driver requests HIWAY
  punches at ticks 1640 and 1673, records a player hit on rider 11, and requests
  CANYN punches at ticks 1847 and 2134 in the selected window. Other courses
  show normal riding in this window. Audit: `combat-audit.log` in output folder.
- Corrected render command: `python3 tools/render_demo.py --build
  build-agent-demo-video-20261024 --no-build --warmup 960 --courses 2 3 0 1 4
  --out analysis/demo-video/twitter-2min-proximity-combat`.
- Final corrected assembly passed all stream/frame/duration checks:
  **120.000 seconds, 7,200 frames, 1280x960 at 60 fps, H.264/yuv420p + AAC**,
  302,938,864 bytes. The old periodic-punch cut is superseded.
  Receipt: `analysis/demo-video/twitter-2min-proximity-combat/receipt.json`.
- Visual QA of completed combat clips: ordinary riding with opponents ahead
  (`riding.jpg`), a punch with the yellow rider alongside (`combat.jpg`), and
  a punch toward a rider on the left (`combat-pacific-highway.jpg`).
- C++ client LSP remains unavailable. clangd check of race_helpers.cpp parses
  the source but reports 26 ExtractFunction tweak failures on loop control;
  no non-tweak error lines. Release/Debug compilation and runtime tests pass.
- Corrected the demo guide's course-name mapping using the existing original
  index/horizon mapping: HIWAY=Sierra, CANYN=Pacific Highway, MEDLY=Peninsula.
- Desktop driver/capture coverage only. Android, sanitizer and new original-game
  oracle captures not run; no new simulation-fidelity claim.

## Offline native demo export (October 24, 2026)

- Baseline `be2ded7`, owned Release build `build-agent-demo-video-20261024`:
  configure/full build passed; full CTest **105/105 passed, zero skips**.
  Logs: `analysis/demo-video/twitter-2min/ctest-release.log`.
- Owned Debug build `build-agent-demo-video-debug`: configure/full build passed;
  full `ctest --parallel 8`: **105/105 passed, zero skips**, 193.58 seconds.
  Log: `analysis/demo-video/twitter-2min/ctest-debug.log`. An initial serial
  attempt was interrupted for runtime and its owned child terminated; this
  result is the completed fresh parallel suite.
- `python3 tests/demo_capture_test.py build-agent-demo-video-20261024/rr-game`:
  passed on both Release and Debug: seven-frame captures after 361 warm-up
  ticks, exact RGB/PCM lengths,
  non-flat/moving frames, non-silent audio, identical video/audio SHA-256 across
  launches more than one second apart, and malformed CLI rejection.
- Five-course wrapper smoke tests passed: 60 total frames with zero warm-up
  (`analysis/demo-video/smoke-02/`), and 62 total frames split 13/13/12/12/12
  with one warm-up tick (`smoke-odd/`). These exercise anonymous-pipe video,
  odd fractional audio counts, AAC muxing and exact-frame final assembly.
- Full command: `python3 tools/render_demo.py --no-build --out
  analysis/demo-video/twitter-2min` (then-default owned Release build above).
  Final `road-rash-native-demo.mp4`: **120.000 seconds, 7,200 frames,
  1280×960, 60/1 fps, H.264/yuv420p + AAC stereo**, 299,795,586 bytes.
  All five course clips are 1,440 frames/24 seconds. `receipt.json` preserves
  embedded binary identity and actual probed streams. Native engine/effect
  audio only, no soundtrack. The current script defaults to `build-demo-video`;
  `--build build-agent-demo-video-20261024 --no-build` selects this exact build.
- Full 120-second video/audio decode passed; blackdetect reported no black
  intervals of 0.1 seconds or longer; audio is non-silent (mean -25.2 dB).
  Log: `analysis/demo-video/twitter-2min/av-quality.log`.
- Visual QA: native frame grabs early/late in Napa and midpoint samples in all
  five courses (`review-grid.jpg`), including roads, terrain, urban buildings,
  traffic, riders and the uncropped full dashboard. This is capture/output
  coverage; no new original-game fidelity or human-driver parity claim.
- Python syntax compilation and `git diff --check` passed. No client C++ LSP
  channel is available; owned compilation database preflight is healthy
  (187 entries). clangd on `game_main.cpp` reports zero errors. RaceScreen's
  clangd parse reports two ExtractFunction tweak failures on existing loop
  constructs, while native compilers accept it; recorded as a tooling limit.
- No Wine captures, Android/device, sanitizer or packaging checks were run:
  this change is desktop offline export, with no new simulation code.

## Shared-tree integration commit (October 24, 2026)

- Build `build-agent-commit-push` (Debug), baseline `7fe5973` plus the shared
  tree's Oct 18–19 session edits. `cmake --build`: exit 0.
- `ctest -j8`: 104/105 pass before pinning; `oracle-sim-field-l5` failed with
  `first divergence at frame 1994 (tick 1995): ring.13e2038.position original
  87436 native 87105` (countdown closes on the green anchor; all earlier
  ticks equal). Pinned in `tools/sim_known_divergences.json` as a diagnostic
  known divergence; rerun passes as `known-divergence`. Not acceptance.
- `verify-sim` (strict, `sched-human-cov2-20261017`, human): 1431 ticks
  identical, exit 0. `oracle-sim-field` and `-schema2` pass.
- Not run: sanitizers, Android assembly, a visual check of the import screen.

## Race frame performance (October 24, 2026)

- Isolated worktree `../native-port-perf` at `289def9` plus only this change.
  Release `ctest`: 104/104 pass, disc present, local oracle captures
  symlinked. The shared tree's `oracle-sim-field-l5` failure comes from another
  session's uncommitted simulation edits (that test is not in HEAD).
- `post-chain` adds pooled == inline over four frames with all effects on.
  `raster-rgb-threads` (1 vs N threads, byte-equal) passes with the new
  banding. A scratch harness hashed HEAD's and the new inline post chain on the
  same 852×480 frames: both `362dcc05`.
- Benchmarks: `tools/bench_game.sh` (Napa autopilot, 600 frames after 90
  warm-up, vsync off). Local reports are in `analysis/bench/{baseline,final}-*`.
  Owner settings: Release 69.0 to 120.2 fps (present-capped). Debug 9.7 to
  30.3 fps. Scale 2, everything on: 64.0 to 71.7 fps. Tables are in
  [PERFORMANCE.md](PERFORMANCE.md).
- Not done: no Android or other-machine benchmark. No interactive play-through
  after the change; the bench drives the real race loop and window with
  autopilot.

## Original offline fields in rr-game (October 18, 2026)

- Isolated checkout `/Users/archit/Dev/rr/.field-count-20261018`, baseline
  `6044526`; shared `native-port/` edits and its divergent local `main` were
  preserved. Owned Debug build: `build-agent-field-count-20261018`.
- Original evidence: disc EXE SHA-256 matches `analysis/inventory.json`
  (`73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`).
  Existing exports `analysis/decompiled/00405600.c`, `00407990.c`,
  `00402d60.c`, `00402e10.c`, `00446bf0.c`, `00437860.c` and constructor
  assembly `analysis/original-disassembly.txt` establish fourteen AI rows in
  offline Thrash, and the selected-character skip in Big Game (thirteen AI).
  This corrects prior unconditional fourteen-opponent wording. No new Wine
  capture was made; the shared oracle resources were in use by another session.
- Configure/build: `cmake -S . -B build-agent-field-count-20261018
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, then
  `cmake --build build-agent-field-count-20261018 --parallel 8`: passed.
- Focused `ctest --test-dir build-agent-field-count-20261018
  --output-on-failure -R '^(race_setup|race-screen)$'`: 2/2 passed.
  Setup checks both modes for all eight characters, every spare speed table,
  all five course name sets and the 42/69 special name branches without RNG
  consumption. The HUD resolves row 14 and the last race place; the screen
  timing/pause/restart/reset tests now run with all fifteen racers.
- `frontend`: passed with a fifteen-row standings fixture and the player's
  fifteenth-place name visible in the fourth displayed row.
- CLI, with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy` and
  `rr-game --assets ../cdrom/ROADRASH --print-wiring`: default `--race` and
  explicit `--opponents 14` report `racers=15`; `--opponents 7` reports 8,
  `--opponents 0` reports 1. All exit 0. Final `game-race-wiring` CTest passed
  after adding the racer count to that diagnostic output.
- No client C++ LSP was exposed. Homebrew `clangd --check=src/race_setup.cpp
  --compile-commands-dir=build-agent-field-count-20261018 --tweaks=none`
  and the same check for `src/game_main.cpp`: passed, zero errors. Without
  disabling tweak probes, clangd reports ExtractFunction applicability errors
  for loop-control statements; the actual compiler build passes.
- Full `ctest --test-dir build-agent-field-count-20261018 --output-on-failure
  --parallel 4`: **104/104 passed, zero skips**, 286.31 seconds, including
  `oracle-sim`, `oracle-sim-field`, `oracle-sim-field-schema2`, `police`,
  `race-screen`, `race_setup`, `frontend` and `race-soak`. Existing oracle fixtures are linked from
  the shared checkout for replay only; no golden or capture was modified.
  Native tests establish field construction/UI behavior; they do not extend
  full live gameplay parity, Android device coverage or simulation acceptance.


## Live police (October 18, 2026, plan 32 D)

- Follow-up 2 (arrest -> screen outcome): clean worktree at 2dcd4e6, build +
  `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ctest -j4`: 104 of 104 pass.
  `race-screen` now also drives a live Napa level-3 arrest through
  `Game`/`RaceScreen` and checks `RaceOutcome::busted` through the run-on
  and fade-out. The git-ignored `sim-napa-solo.tsv` fixture was copied into
  the worktree for `oracle-sim`; without it that test fails on the missing
  file only. No original-capture comparison of cop movement yet (see the
  police handoff's Open section).
- Follow-up (siren, on-foot arrest): isolated worktree at the committed content (no other
  session's edits), `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ctest -j4`: 104 of 104 pass,
  including `oracle-sim-field`, `oracle-sim-field-schema2`, `police` (new on-foot arrest),
  `sound` (new siren cases) and `race-soak`. The shared tree does not build `oracle-sim-test`
  right now (another session's uncommitted `tests/field_replay.hpp`).

- Baseline at 919c339 plus the police slice: `cmake --build build --parallel` exit 0;
  `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ctest --test-dir build -j4`: 104 of 105
  pass. The sandbox has no display, so `first-run-smoke` and `game-race-wiring` need the
  dummy drivers. `oracle-sim-field-l5` (another session's in-flight test, uncommitted
  CMake/ring changes) fails with "ring setup does not close on the green anchor"; it
  does not touch police code (cops spawn at tick 4096, captures end at 2400).
- `oracle-sim-field`, `oracle-sim-field-schema2`, `race`, `race_schedule` and `race-soak`
  still pass, so the 2400-tick field parity is unchanged.
- New: `police-test` live section (Napa level 3, parked player): no cop before tick 4096,
  cop on slice 11 at 4096 (player slice 50 - 39, as in `napa-police-spawn.tsv`), chase in
  follow state, `busted()` 440 ticks later. Native behavior only; no original comparison
  of cop movement yet.

## Post-merge full gate (October 14, 2026, integrator)

- Tree: `c2ab4d9` + worktree (contact-guard code, gemini parser fixes,
  codex MiniDash hud work, codex-goal doc edits; SIM-owner oracle
  diagnostics untouched and unbuilt).
- `cmake --build build-integrator --parallel`: exit 0, no warnings.
- `ctest --test-dir build-integrator --output-on-failure`: **95/95 pass**
  (incl. new `car_contact`), zero skips/failures. This discharges the
  shared-tree full gate requested by `codex-contact-guard-20261014.md`;
  isolated-gate receipts for Android/ASan remain worker-run evidence in
  that handoff.

## Previous-copy sync (October 14, 2026, plan 10 Task 6 fix order #1a)

- `cmake --build build-integrator --target race-test oracle-sim-test`:
  exit 0, no warnings.
- `./build-integrator/race-test`: 0 failures.
- `oracle-sim-test --driver=demo sim-napa-solo.tsv`: first divergence at
  frame 221 (tick 222) lateral, rng in sync; output hash matches the
  refreshed `tools/sim_known_divergences.json` demo entry.
- `oracle-sim-test --driver=demo sim-napa-solo-human.tsv`: byte-identical
  output, still frame 209 (tick 210) rng.
- `verify_sim.py --mode diagnostic --binary
  build-integrator/oracle-sim-test`: exit 0, known-divergence.
  (Note: the default `--binary build/oracle-sim-test` is stale; pass the
  owned build dir explicitly.)
- `ctest --test-dir build-integrator --output-on-failure`: **94/94 pass**.
- `tools/determinism_check.sh build-integrator build-release` (after
  rebuilding `race_soak-test` in `build-release` at this source):
  `determinism OK (3 hashes)` — contact/prev-copy integer paths agree
  across optimization levels.
- Open: 0x28 placement anchor (fix order #1b); SIM-capture 2-tick fault.

## Tick-208 contact classes (October 14, 2026, plan 10 Task 5)

- `cmake --build build-integrator --parallel`: exit 0, no warnings.
- `./build-integrator/race-test`: 0 failures (2 new: class-3 knock-tail kick
  recomputed exactly, class-1 contained transfer/damage).
- `./build-integrator/oracle-sim-test --driver=demo
  analysis/oracle/sim-napa-solo-human.tsv`: exit 1, first divergence at
  frame 209 (tick 210) rng; tick-208 steer matches exactly (2392).
  Output hash matches `tools/sim_known_divergences.json` human entry.
- Demo golden `sim-napa-solo.tsv`: output byte-identical, still diverges at
  frame 207 (tick 208) position — expected (no node laterals in that era).
- `python3 tools/verify_sim.py --mode diagnostic`: exit 0 (known-divergence).
- `ctest --test-dir build-integrator --output-on-failure`: **94/94 pass**.
- LSP: `python3 tools/agent_lsp.py --check src/race.cpp` parsed (clangd
  ExtractFunction tweak FAILs only, no diagnostics).
- Open: tick-210 grid-setup cause; SIM-capture 2-tick fault blocks fresh
  frozen-word captures (see `docs/agent-handoffs/sim-fault-20261014.md`).

## Intersections second pass (October 12, 2026, plan 24 Task 1)

- `cmake --build build-fixes --parallel`: pass, no warnings.
- `traffic-test`: pass. The rewritten `test_intersections` and `test_signal_phase` (see plan 24 Step 3) and the five-course sweep all pass. The Napa density check now reads 123 against 727 at fixed density 6: cars follow obstacles now, so the stream shifts.
- `pedestrian-test`: pass. A natural CITY race shows only rider knock-downs (temporary instrumentation, reverted); cars brake for pedestrians.
- `rr-game --course N --ticks 90000 --autopilot`: all five courses finish (NAPA 46296, CITY 80920, HIWAY 44206, CANYN 46177, MEDLY 44574 ticks).
- `ctest --test-dir build-fixes --output-on-failure`: **69/69 pass**.
- Scratch CITY render (demo-hold player, traffic and pedestrians, not committed) shows crossing cars side-on on the cross road at an intersection, with a jogger on the crosswalk.
- The same harness over 20,000 ticks held up to 13 cars at one CITY intersection: a road car and two crossing cars blocking each other. This is recorded as an open capture question.
- Not run: original capture (pipeline down per `analysis/intersection-capture-notes.md`), sanitizers, Android.

## Pedestrians (October 12, 2026, plan 24 Task 4)

- `cmake --build build-fixes --parallel`: pass, no warnings.
- `build-fixes/pedestrian-test <frame.bmp>`:
  - CITY level 1 has 359 pedestrian and 6 police DZHR records after the level gate, 357 of them with category-2 artwork.
  - Natural 12,000-tick race (demo-hold player, 7 opponents, density-6 traffic): 22 spawned, at most 15 active, 7 crossed more than one lane, identical on a second run (spawn count, travel, RNG).
  - Staged hits: speed 12000 gives state 2 with animation 3; speed 800 gives state 3; the rider is never knocked off.
  - The frame shows pedestrians on both curbs, one stepping off mid-crossing (scratch, not committed).
- `rr-game --course N --ticks 90000 --autopilot`: all five courses finish. Pedestrians spawned: NAPA 31, CITY 337, HIWAY 33, CANYN 24, MEDLY 231.
- `ctest --test-dir build-fixes --output-on-failure`: **68/68 pass**, including the `oracle-sim` diagnostic, whose scenarios set no pedestrians.
- Not run: an original capture/golden (plan 24 Task 4 Steps 2/5), sanitizers, Android, and a listening check of voice 0x16.

## Playtest fixes: courses, opponent HUD, meters, traffic density (October 12, 2026)

Build tree `build-fixes` (Debug), from baseline `791da21` plus the uncommitted `playtest-visual-fixes` edits to `course_scene.cpp`/`video_player.*`, which were kept.

- Reproduced: `build/rr-game --assets ../cdrom/ROADRASH --course 1 --ticks 300 --screenshot x.bmp` exits 1 with `vector`. lldb on `__cxa_throw` stops in `CourseScene::draw_nodes` at `depths.scale.at(4352)` (size 4352). Course 4 behaves the same; courses 0, 2 and 3 exit 0.
- After the fix: courses 0–4 exit 0 at the grid (`--ticks 300 --screenshot`). With `--level 3 --ticks 3000 --autopilot` courses 0, 1, 2 and 4 exit 0. Full races `--ticks 90000 --autopilot` finish on all five courses (finish ticks 44928, 77212, 43763, 46294, 44086).
- `course_scene-test`: 5 courses × 2 branches × every sample × fraction {0, 255} build (CITY to CANYN, 21–25k views each); about 47 s Debug. Pass.
- `hud-test`: nearest-opponent rule (behind wins at 4 vs 10, ahead wins at 6 vs 10, behind-only case), target name/meter from the nearest rider, and a solo race names no target. Pass.
- `traffic-test`: `traffic_set_density` keeps behind/ahead timers and a negative value sets rate and cruise; zone events fire on the initial fill, at the ring front and again after backing off; Napa corpus run at 0x60/tick gives 123 spawns with 34 zone events against 733 at fixed density 6. Pass.
- `sound-test`: 0 failures. Screenshots of Napa at tick 900 show "Pearl", a full opponent meter, a red `0.009` with an up arrow, and a moving tachometer needle (scratch only, not committed).
- `tools/verify_sim.py --mode diagnostic --binary build-fixes/oracle-sim-test`: `known-divergence`, first divergence frame 74 (tick 75) `rider.0.lateral`, the same before and after the change. The solo scenario has no live traffic.
- `ctest --test-dir build-fixes --output-on-failure`: **67/67 pass**.
- Not run: sanitizers, Android assembly, a new Wine capture (ring offset, zone timing), and a pixel comparison of the dashboard against the original.

## Start-grid ordering research (October 12, 2026)

- Disc EXE SHA-256 matches `analysis/inventory.json`:
  `73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`.
- `npx --yes rea-agents@6.4.0 function ../cdrom/ROADRASH/ROADRASH.EXE
  <address> --format json` on `0x00443af0`, `0x00438610`, `0x00443cd0`:
  all exit 0 with Ghidra 12.1.4; all pseudocode exactly matches existing
  exports. Assembly confirms world-before-objects, byte threshold and callbacks.
- Python integrity/node audit: PASS. `napa-start` frame 0 and `napa-drive`
  frames 0–900 have valid headers, expected EXE hash, contiguous frame numbers
  and completion markers. Scene-0 v3 files have exact recorded lengths/counts;
  near road nodes precede shadow/body in both saved list tails. Receipt:
  `analysis/oracle/start-grid-order-audit.json`; REA JSON evidence and full
  commands are in [the research report](research/2026-10-12-start-grid-order.md).
- Scope: recovered original ordering plus saved original near-grid layering.
  The current native front-half-only delay differs. No native source edits,
  build/CTest, new Wine capture or full native frame comparison in this task.
  Plan 11a Task 3 acceptance remains open; actor state recapture is still needed.

## Session 1: macOS ARM64

- CMake configure and C++17 build succeeded on macOS 27.0 / Apple silicon.
- `file build/rr-viewer`: Mach-O 64-bit executable arm64.
- Dynamic dependencies: native SDL3, libc++, libSystem. No Windows runtime dependency.
- CTest resource-bounds test passed: valid synthetic resource, truncated inputs, invalid signature, overflowing/out-of-range offsets, sizes and counts.
- Original corpus: all 13 resource containers accepted, 429 directory entries validated against file bounds.
- Viewer smoke run decoded and rendered all nine configured screens without error.
- Original menu screenshot and Napa panorama screenshot visually inspected: `analysis/native-viewer.png`, `analysis/native-panorama.png`.

## Session 1: Android ARM64

- Gradle `assembleDebug` succeeded with NDK r28c, SDK 35, AGP 8.9.2 and SDL3 3.4.8.
- APK: `android/app/build/outputs/apk/debug/app-debug.apk` (approximately 4.1 MB).
- Installed successfully on connected MANGMI AIR X (`arm64-v8a`).
- Activity launch reported success. SDL runtime log confirms entry into `SDL_main` from `libmain.so` in the device's ARM64 library directory.
- **Visual rendering and touch navigation are not yet verified on Android.** The device was locked; screenshots showed the lock screen or sleeping display and SDL reported pause/stop lifecycle transitions. No device security settings were changed.
- Open `Road Rash Native Research` after unlocking to complete the device check. Tap to cycle the nine asset screens. Gameplay is not implemented.

## Session 1: analysis

- Ghidra 12.1.4 imported the original executable and saved its project.
- Export script produced 1,585 function pseudocode files with zero export failures.
- This is not a claim that all 1,585 functions have correct types or recovered semantics.
- Original asset inventory and executable hash recorded in `analysis/inventory.json`.
- Exact instruction disassembly saved in `analysis/original-disassembly.txt` for checking inferred pseudocode.

## Session 2: course geometry and road projection

- `rr-course --exe ../cdrom/ROADRASH/ROADRASH.EXE` on all five courses: every node reachable and counted; both branch routes built (CANYN 5,423/5,443, CITY 5,606/5,606, HIWAY 6,005/6,075, MEDLY 5,634/5,645, NAPA 6,224/6,319 samples).
- The same run projected 927,680 camera positions (every 1/16 sample, both branches, all courses) without errors. Projected road rows stayed in 15..762 of the 240-row view space, so no road lands above the top of the screen.
- Depth table read from the reference executable: 4,352 entries, positive and non-increasing; entry 256 = 32,768 (2^24/512).
- CTest `road-projection` passed: flat convergence, double-integrated curvature, single-integrated rise, fractional-camera rounding, route-end truncation, rejection of non-PE input. Synthetic depth table; no original data in tests.
- macOS viewer smoke run decoded all nine images and all five courses. Screenshot `analysis/native-road-napa.png` (NAPA sample 300) visually inspected: converging lanes, a crest, and the road reappearing on a further rise. HIWAY 2500 and CANYN 800 were also checked.
- **Not compared against the original game's output.** Placement matches the recovered arithmetic, but no Wine capture at a matching position has been diffed. Shading, scenery and split roads are placeholders or absent.
- Android: `assembleDebug` succeeded; APK (4.9 MB) contains `libmain.so` (ELF aarch64) with the road renderer, the five `.CRS` files and `ROADRASH.EXE`. **No device was connected in this session**, so the Android road view is built but not run. Touch taps cycle screens and the road auto-drives. Speed and branch controls are keyboard-only.

## Session 3: split and merge roadways

- `rr-course` on all five courses, both routes: every split opens an 82-sample dual stretch and every merge restores an 81-sample window. Lateral separation is exactly 0 one sample before every merge; the tool aborts otherwise. Height mismatches between paired branches mirror exactly between routes A and B and close to within ±5 units one sample before each merge.
- Projection sweep (every 1/16 sample, both routes) projected about 1.9 million second-road slices without errors. Rows stayed within 37..722; centre columns reach ±174,000 px mid-branch, consistent with separations of up to about 32,000 world units.
- CTest `road-projection` extended: split ahead (both halves start together, own offsets, own bend and rise), camera inside a dual stretch (stored separation), then own-bend integration. Passed.
- Screenshots inspected: NAPA 740 (just after the split) and 960 (second road converging onto the merge point; saved as `analysis/native-road-merge.png`).
  - A first renderer draft let the second road's verge bands cut through the rider's road. The bands are a placeholder, not taken from the original; only the rider's own road now lays them.
- Android APK rebuilt (5.3 MB). **Still no device connected; not run on hardware.**
- Still not compared against Wine captures of the original.

## Session 4: section chunk survey

- Every section row on all five courses carries `HTPR`, `MSRR`, `DLSR`, `NALR` and `DZHR` chunks (109 sections); `BORR` is in 108, `CESR` in 97, and a terrain chunk in all 109 (`LIHR` 64, `DLBR` 34, `NTMR` 11).
- `DLSR`: 389 records. The extent is 16 + 36·n in all 109 chunks. All 261 records with end > start have Q16 slopes equal to `(end byte − start byte)·65536/(end − start)` within ±1, with zero mismatches. 274 of 280 consecutive record pairs start where the previous one ended.
- Survey scripts were one-off Python checks; not yet part of `tools/` or CTest. No code change to the native reader or renderer in this session, so build and test status is unchanged from session 3.
- Shoulder rendering (same session): `walk_intervals` accepts every `NALR` and `DLSR` table on all five courses (the corpus sweep and smoke run are unchanged and pass).
  - Lane widths compared before and after the walker: 34,182 samples, 6 changed (HIWAY section 6, samples 1–6, one iterator step earlier), matching `0x00452920`.
  - CTest `road-projection` gained a shoulder-edge check; both tests pass.
  - Screenshots inspected: NAPA 300 and CITY 1500 (asymmetric shoulders; saved as `analysis/native-road-shoulders.png`).
  - APK rebuilt; no device connected.

## Session 5: playable Napa

- CTest: 7/7 pass (`resource-bounds`, `road-projection`, `framebuffer`, `assets`, `bike`, `race`, `input`).
  - `bike`/`race` expectations are hand-computed from the ported routines.
  - Red steps were observed as missing headers or symbols (for `bike`, after the fact, by removing the implementation).
- Headless ride on the real Napa data (real physics tables and `SPEC`s) with a keep-to-centre bot finishes the full route:
  - bike 1: 9.7 min, 0 crashes
  - bike 8: 7.2 min, 1 crash
  - bike 15: 10.0 min, 3 crashes
  - top speeds 11,776 / 16,640 / 12,288
- `rr-game --autopilot` screenshots at ticks 30, 1,800 and 9,000 were inspected: countdown, fifth gear at speed ~111 on a curve, a crest at 24%. Saved as `analysis/native-game-napa.png`.
- A real-time 8-second run completed without errors.
- A refactor check (road drawing moved out of the viewer): viewer screenshot byte-identical before and after.
- Android: APK built (5.3 MB) with the game as `libmain.so`. **Not run: no device connected.**
- **Not verified against the original game:** no oracle; handling, speeds and crash behaviour may differ from the 1996 game.

## Session 6: opponents and combat

- CTest: 8/8 pass, also under UBSan/ASan. New checks:
  - bob decoding (synthetic, plus the corpus: `HIBOB` 326 frames with frame 3 at 56×128, `LOCOP` 45, remap row 0 entry 2 = 80)
  - the opponent field from the executable
  - AI holds its target speed and stays on the road
  - riders never stay inside each other
  - punches drain stamina and knock a rider off; out-of-reach punches do nothing
- All five sprite files decode with every byte consumed. Frames were rendered through all 9 remaps and inspected.
- Headless Napa, level 1, bot player plus 7 AI:
  - without contact: AI finish times 599–703 s, no crashes
  - with contact: 641–703 s, no crashes
- Screenshots inspected: the start grid, AI racing ahead, side-by-side riders, and a punch landing (opponent stamina bar drops; saved as `analysis/native-game-combat.png`).
- The input-mapping additions for attack bits were written together with their tests (red not observed separately).
- **Not verified:** nothing is compared against the original game. Android was rebuilt but has not run on hardware.

## Session 7: lifecycle, multi-touch and frame timing (October 1, 2026)

- Debug configure/build succeeded; CTest **9/9 passed**. Existing resource/course/physics/race/sprite checks remain passing.
- Sanitizer build (`-fsanitize=undefined,address -fno-sanitize-recover=undefined`): **9/9 passed**.
- New `session` checks use synthetic SDL events: background pauses immediately; background/unfocused resume is refused; foreground/focus gain requires explicit resume; clock reset is consumed once. Rational timing produces exactly 600 steps over 10 seconds at each of 30, 60, 120 and 144 Hz. Paused time and partial ticks are discarded; `UINT64_MAX` elapsed time is capped at 20 steps without multiplication overflow or remaining catch-up debt.
- Expanded `input` checks: two-finger steering/punching, independent release, touch-device/finger ID pairs, brake priority over auto-throttle, motion between zones, cancellation, and clearing all fingers. These test intent aggregation; they do not establish Android's physical event delivery.
- Native game smoke: `build/rr-game --assets ../cdrom/ROADRASH --autopilot --ticks 1800 --paused --screenshot analysis/native-game-paused.bmp` exited successfully. Converted PNG visually inspected: dimmed race, centred pause title and readable resume/quit instructions (`analysis/native-game-paused.png`). `--ticks` pre-runs the simulation before `--paused` takes effect.
- `JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home android/gradlew -p android assembleDebug`: **BUILD SUCCESSFUL**. `aapt dump badging` confirms `arm64-v8a`, min SDK 23/target SDK 35 and optional gamepad/touchscreen features. `apksigner verify --verbose` passes with the debug signature (v1/v2; v3/v4 not enabled). It reports the standard unsigned Gradle metadata entry warning.
- `git diff --check` passes. `adb devices` lists no devices: **no Android hardware validation performed**.
- Remaining platform checks: physical multi-touch/gamepad input, background/foreground and controller disconnect in a live Android race, actual render-device reset, ultrawide/portrait resizing and device frame-time measurements. Mixer/career pause/save and final import/release packaging remain unimplemented. Rendering and simulation still have the previously documented original-game deviations.

## Session 8: original oracle and data import (October 2, 2026)

- `oracle/build.sh`: i686 forwarding DLL built; `oracle/install.sh` creates a separate RoadRashOracle install and verifies the reference EXE hash. Host patch test passes. Runtime frame hooks load and produce complete golden captures.
- `RR_ORACLE_FRAMES=600 oracle/run.sh napa-attract`: captured 600 original CITY frames, final `end` marker present. Recorded camera positions span Q8 12,544–19,159 with 409 distinct positions. All compared projection fields match in `oracle-road`: **600 compared, 0 mismatches**. First mismatch investigation corrected the oracle's course-index mapping; no expectations were relaxed.
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`, build and CTest: **16/16 pass**, including hash vectors, all truncated lengths of a synthetic ISO, invalid endian/extent/cycle checks, folder/ISO import, corrupt/missing input, retained destination and staging cleanup. Malformed existing oracle data fails instead of silently skipping.
- `build/rr-import <real ISO> .tools/imported-game`: **341/341 verified**; output is local/ignored. `build/rr-viewer --assets .tools/imported-game --smoke` decodes nine images and all five courses. All-course projection sweep also passes.
- `RR_PREF_OVERRIDE=.tools/first-run-empty build/rr-game --first-run-smoke --screenshot analysis/native-import.bmp`: exits successfully. `analysis/native-import.png` visually inspected: title, import explanation and both desktop picker buttons fit without clipping.
- Android `assembleDebug`: **BUILD SUCCESSFUL**, including `verifyDebugNoGameData`. Actual ZIP listing has `lib/arm64-v8a/libmain.so` and no `assets/game/` entries.
- `adb devices`: no devices. Hardware import/input/rendering/lifecycle, additional oracle scenarios, deterministic replay, all original pixels and gameplay fidelity remain unverified. This is progress toward the full port, not final acceptance.

Import follow-up (October 2): nonseekable ISO input now caches inside the owned staging directory and the cached ISO is removed before commit. A synthetic FIFO import and cancellation/cleanup tests pass. First-run UI is now a CTest smoke check. Hardware SAF remains unverified.

Final import milestone checks: 16/16 tests also pass under address/undefined sanitizers. Android was rebuilt after stream-cache support and passed APK asset exclusion again.

## Session 9: original frame capture and audio/video (October 2, 2026)

- `RR_ORACLE_FRAMES=60 RR_ORACLE_PIXELS=1 RR_ORACLE_SHOT_EVERY=30 oracle/run.sh city-pixels`: captured 60 original CITY frames and two raw indexed images. `build/oracle-road-test analysis/oracle/city-pixels.tsv`: **60 compared, 0 mismatches**. Frame 30 conversion was visually inspected; full original road/scenery/riders and palette are captured. This is not a native pixel comparison.
- Native CTest: **23/23 pass**. Includes WAV chunk/alignment/truncation/fact tests, IMA predictor/index/group/clipping tests, mixer pitch/pan/saturation/queue/retirement/cursor/pause tests, dummy-device removal retaining a paused voice, AVI synthetic headers/truncated tails and all 59 file scans, Cinepak synthetic V1/skip/truncation rollback, synthetic silent-video clock/pause/finish tests and the available disc video clock checks.
- `python3 tools/verify_video_audio.py ../cdrom/ROADRASH/VIDEO --video-decoder build/cinepak-test`: **59/59 files pass; 24,900 RGB frames and 67,840,002 int16 audio samples match FFmpeg byte for byte**. There are 44 audio streams and 15 silent showcases. Logs: `analysis/video-codec-verification.log` (ignored). The script uses one temporary native/reference pair at a time and deletes it on completion.
- `wav-test --corpus`: **72 effects + 14 RRA tracks decode**, expected channel/rate assertions pass.
- Sanitizer CTest: **23/23 pass** after the codec additions. Runtime playback/cursor/retirement follow-up is also checked in the sanitizer build; see the final milestone record below.
- `rr-game --assets ../cdrom/ROADRASH --video VIDEO/INTRO.AVI --ticks 600 --screenshot analysis/native-video.bmp`: succeeds. PNG visually inspected: the decoded ten-second intro frame is centered at native size on black. Screenshot simulation is silent and does not measure device A/V latency.
- Original `0x00402820` confirms the normal native-size centered destination rectangle. Frontend mode selection and doubled/top placement remain unverified in native flow.
- `rr-viewer --assets ../cdrom/ROADRASH --smoke` and all-course `rr-course` projection sweep pass.
- Android follow-up initially failed because NDK `uint64_t` is unsigned long while an expression used unsigned long long in `std::max`. An explicit fixed-width template resolves it. Rebuild including video player: **BUILD SUCCESSFUL**, `verifyDebugNoGameData` passes.
- `adb devices`: no devices. Physical playback, audio focus/device latency, frame times and full original gameplay acceptance are still open. Codec equality does not establish original simulation, effect triggers, cutscene selection or frontend fidelity.

Final Session 9 follow-up: address/undefined sanitizer CTest remains **23/23 passing**, including playback clock and mixer cursor/retirement tests. The latest Android ARM64 debug build includes `video_player.cpp` and again passes APK game-data exclusion.

## Session 10: standalone and Android release packaging (October 2, 2026)

- Downloaded official SDL source tarball; `shasum -a 256` matches the GitHub release asset digest. `RR_BUNDLE_SDL=ON` builds static SDL without shared SDL/Homebrew runtime dependency.
- `tools/build_macos_app.sh`: successful configure/build, **23/23 release tests pass**, strict bundle verification and ZIP creation. Application is 3 MB, ARM64 with 22 system library/framework dependencies; plist validates, bundled SDL/stb licences present, no original assets. Ad-hoc signature passes `codesign --verify --deep --strict`.
- `RR_PREF_OVERRIDE=<isolated pref> <bundle executable> --first-run-smoke --screenshot analysis/bundle-import.bmp`: succeeds. Converted screen visually inspected; both picker controls and instructions fit. A run with the bundle executable from `/tmp` also succeeds. Finder launch/Gatekeeper/notarization behavior is not established by this CLI smoke.
- `assembleDebug assembleRelease -PreleaseDebugSigning=true`: successful. Both variant asset verifiers pass. Release `apksigner verify --verbose` passes v1/v2 with one local debug signer. Standard Gradle metadata unsigned-entry warnings remain. APK asset listing contains only `assets/licenses/LICENSE` and `assets/licenses/SDL-LICENSE.txt`.
- `tools/ci.sh`: native configure/build/tests, image/resource smoke, all-course sweep, Android debug build and APK check finish with **CI OK**.
- Owner-key signing configuration and tag-based naming are implemented; an owner production key was not supplied/tested. Device remains absent. Installation, SAF import, input/audio lifecycle, hardware rendering/frame times and career persistence are not validated.

## Session 11: flat polygon raster oracle (October 2, 2026)

- Synthetic raster checks pass: inclusive bottom/exclusive span right, shared diamond endpoints, horizontal span, clipping negative vertices, duplicate-point triangle and rotated ordering.
- `RR_ORACLE_RASTER=2 oracle/run.sh raster-fixtures 3`: complete golden plus 64 raw 640×480 component images. Every fixture is initialized to index zero and drawn by original `0x0044d620`. Recorded vertices reflect original normalization. Loader-only capture avoids cinema timing and restores the temporary globals.
- `build/oracle-raster-test` and sanitizer equivalent: **64 fixtures compared, 0 mismatched**, comparing all 19,660,800 indexed pixels. Malformed metadata/missing raw images fail; absent captures alone skip 77. These are original-function component comparisons, not native/full-original race screenshots.
- CTest: **25/25 pass**, also **25/25 under address/undefined sanitizers**. Viewer/resource smoke and five-course sweep pass. Android debug build and no-game-data verifier pass.
- No scene integration, arbitrary-quad normalization, textured spans, terrain/scenery, low view mode or Android hardware pixels are claimed by these results.

## Session 12: view mapping (October 2, 2026)

- Recaptured loader-only `raster-fixtures`: 24 original `0x0040ffc0` coordinate conversions, including signed extremes and fractional boundaries. `view-test` compares every mapping exactly.
- Raster remains **64 fixtures, zero mismatches**, now including the full high-view coordinate extremes.
- Debug and address/undefined sanitizer CTest: **26/26 pass**. Android debug build and game-data exclusion pass. Logs: ignored `analysis/view-tests.log`, `analysis/view-sanitizer-tests.log`, `analysis/android-view-build.log`.
- Low and inset dimensions come from disassembly; component mapping equality does not establish full low/inset rendered-frame equality or hardware presentation.

## Session 13: terrain decoding (October 2, 2026)

- Loader-only original oracle now adds 864 synthetic terrain slices: building/wall/hill × random 0/123/32767 × forward/backward × 48 samples. `terrain-test` compares every selector, wall triple, hill coordinate and shade exactly: **zero mismatches**.
- All five disc courses: **109 terrain sections** decode and traverse forward and backward with a deterministic CRT-style random callback. This is native bounds/traversal coverage, not an original corpus comparison.
- All truncated lengths of three synthetic payloads and malformed size/count/tag/sample/point bounds reject. Debug CTest **27/27 pass**. Android debug build and game-data exclusion pass.
- Component fixtures run the original init/step/export functions while temporarily selecting its fixed-random wrapper mode. Mode/random globals are restored. Golden data stays ignored; original EXE on disk is unchanged.
- Shared race randomness/cache state, fractional stepping, full scene and split transforms require further comparison.

Session 13 sanitizer follow-up: **27/27 pass** under address/undefined sanitizers; log `analysis/terrain-sanitizer-tests.log`.

## Session 14: hill projection (October 2, 2026)

- Recaptured loader-only component golden: 96 calls to original `0x0041d6d0`, six scale groups including 65,535/65,536 boundary and synthetic signed extremes, varied world/camera offsets and odd/even view width. All 1,152 coordinates and 96 maximum rows match exactly in `scene-test`.
- Camera x/height and view-width globals are saved/restored around capture. No original assets or golden outputs are bundled. Source fixtures are synthetic.
- Scene integration/full frame visibility and physical Android output remain open.

Session 14 final checks: **28/28 native and address/undefined sanitizer tests pass**; Android ARM64 debug build and asset exclusion pass. Logs: `analysis/hill-tests.log`, `analysis/hill-sanitizer-tests.log`, `analysis/android-hill-build.log`.

## Session 15: polygon normalization (October 2, 2026)

- Loader-only oracle calls original `0x0044ce60` on 2,048 synthetic quads with flags 0/8/4; records all input/output coordinates, acceptance low byte and texture-flip byte. Native compares **zero mismatches**. The temporary current-node pointer is restored.
- Raster capture now records pre-normalization points. `oracle-raster-test` checks both `fill_quad` on normalized points and `draw_flat_quad` on original points against every reference pixel: **64 fixtures, zero mismatches**.
- These are pure original-function comparisons. Full race/display-list/textured pixels and pathological rejected-polygon raster behavior remain open.

Session 15 final checks: **29/29 native and address/undefined sanitizer tests pass**; Android ARM64 debug build and asset exclusion pass.

## Session 16: indexed texture raster (October 2, 2026)

- Original span corpus: 1,008 calls = opaque/transparent × nine strides × four alignments × seven lengths × stationary/moving row. Every output byte and surrounding sentinel byte compares exactly.
- Original `0x004180b0` quad corpus: 64 raw 640×480 images, synthetic 32×32 / 16×16 / 8×8 mip data. Native mip level and every one of 19,660,800 palette indices match exactly, including transparency, sloped edges and clipping.
- Oracle saves/restores both edge tables, UV globals, current-node pointer, buffer/clip/bbox globals and span dispatch/pointers. `run.sh` cleans and copies texture raw outputs alongside existing captures. Original EXE on disk remains unchanged.
- Native structural tests cover invalid stride and out-of-storage UVs. Golden parser tests additionally reject int64 overflow, repeated fields/headers and skipped frame IDs.
- Original texture resource/mip decoding, scene/display-list wiring, full race frames and Android hardware output are still unverified.

Session 16 final checks: **30/30 native and address/undefined sanitizer tests pass**. Android ARM64 debug build and game-data exclusion pass. Viewer/resource smoke and all-course projection sweep pass. Logs: `analysis/texture-tests.log`, `analysis/texture-sanitizer-tests.log`, `analysis/android-texture-build.log`. The final capture includes visible opaque and transparent level-2 mip quads; native fixture rerun remains exact.

## Session 17: CEL and PC mip resource decoding (October 2, 2026)

- Synthetic CCB+PLUT+indexed PDAT: exact dimensions, palette endian and pixels; every shorter length rejects, as do invalid relative mip offsets and invalid stride.
- Real corpus: 42 CEL resources in GLOBAL and five courses; 17 textures with 117 mip blocks and 25 retained legacy words. Original `0x0040b800` loader comparison checks all dimensions, flags, texture metadata, legacy words and every relocated mip byte. **Zero mismatches**.
- Golden pixel blocks are ignored `analysis/oracle/*-cel*-mip*.raw`; oracle copies the source resources before relocating pointers and restores the temporary dimension table. Runtime original disc files stay unchanged.
- Legacy drawing, CEL table/selector resolution, CCB transforms and world integration remain open.

The 117 original-loader mip blocks contain **189,266 palette indices**, all compared byte for byte.

Session 17 final checks: **31/31 native and address/undefined sanitizer tests pass**. Android ARM64 debug build and game-data exclusion pass. Logs: `analysis/cel-tests.log`, `analysis/cel-sanitizer-tests.log`, `analysis/android-cel-build.log`.

### Display-list renderer component oracle (October 2, 2026)

- Original calls: `0x0040ff40`, `0x0040ffc0`, `0x00410080`, `0x0044d300` on isolated buffers. 32 lists ×12 nodes, both resolutions, three quality masks, texture flags 0/2/4/8/16/32, flat overrides, UV flips, clipping and overlap. **6,144,000 pixels, zero mismatches**.
- All four signed texture bounds from each of 17 real indexed CELs match original-loader values.
- Fixture resource-pointer correction is recorded in PROGRESS/REVERSE_ENGINEERING. Original disc bytes remain untouched; fixture captures stay ignored.
- Natural scene geometry/order, dynamic rider specialization and Android device pixels remain unverified.

Session 18 build checks: native CTest 32/32, ASAN+UBSAN CTest 32/32; Android `assembleDebug` BUILD SUCCESSFUL including `verifyDebugNoGameData`. Viewer resource smoke and all-five-course projection sweep pass. Logs: `analysis/nodes-tests.log`, `analysis/nodes-sanitizer-tests.log`, `analysis/android-nodes-build.log`, `analysis/nodes-smoke.log`, `analysis/nodes-courses.log`.

### Road-panel construction and material integration (October 2, 2026)

- 128 original `0x0043c170` cases, 343 emitted quads: all Q16 coordinates, material slots, PRE0 and flips exact.
- The original loads road textures from the owned disc with `0x0040b800`, substitutes their pixel-resource pointers into panel lists, and draws with `0x0040ff40`. Native panel construction + executable descriptor lookup + CEL decode + display-list rendering matches all **24,576,000 indexed pixels** in both modes and quality masks 16/48/112.
- PE data reader rejects all 530 truncated prefixes required by its synthetic payload, unmapped/below-base addresses and reads crossing raw section boundaries. Material binding rejects missing IDs and truncated resource data; synthetic material rendering passes. Eight slots bind successfully from actual imported GLOBAL/executable data.
- These are isolated road-component fixtures; natural course scene construction, full race frames and Android hardware output remain open.

Session 19 final validation: native CTest 35/35; ASAN+UBSAN CTest 35/35; standalone release CTest 35/35 and macOS bundle verifier PASS (ARM64, 22 system dependencies, ad-hoc signature, no game assets). Android debug and development-signed optimized release BUILD SUCCESSFUL with data exclusion; APK v1/v2 signature verifies. Viewer smoke and all-five-course projection sweep pass. `adb devices` lists no connected device. Logs: `analysis/panels-tests.log`, `analysis/panels-sanitizer-tests.log`, `analysis/panels-macos-release.log`, `analysis/android-panels-build.log`, `analysis/android-panels-release.log`, `analysis/android-panels-signature.log`, `analysis/panels-smoke.log`, `analysis/panels-courses.log`.

## Session 20: language string tables (October 2, 2026)

- `pe_strings-test`: synthetic PE string block (UTF-16 to UTF-8, Windows-1252 conversion), rejection of every truncated length and a bad PE signature, the `0x0042f5d0` continuation splice, buffer truncation and malformed continuation. Passes.
- `pe-strings-corpus` (skips without the disc): all five language DLLs decode to 985 populated slots each. Passes.
- `career-test` (synthetic): prize for 1st at level 1 and loser pay at level 3; a fine larger than cash (game over restarts the career) and a fine exactly equal to cash; repair cost; buying with exactly enough cash, owning already, unaffordable; level-up on the fifth qualification and the finale at level 5; Thrash wreck; results video names; save round trip with byte identity; rejection of every truncation, 2,000 garbage buffers (half with a valid magic) and a single flipped bit; the checksum definition on a two-byte input. Passes.
- `career-corpus`: starting cash, prize, bike price, default bike and relation tables read from the user's EXE; Cydney starts with $1000. Passes.
- **Not verified:** round trip of a save written by the original game. No original save has been captured yet.
- `text-test` (skips without the disc): all four disc TrueType faces (Badloc, Futura Condensed Regular/Bold/Demi) load; empty width is 0; ink is confined to the measured box and drawn in the exact colour; the longest string of each language word-wraps into a 300-pixel description box. An invalid font buffer is rejected. Passes.
- **Not verified:** glyph-level equality with Windows GDI. GDI hinting is not reproduced, and coverage is thresholded at 50% without anti-aliasing.
- `frontend-test` (skips without the disc). It reads the screen, popup, pattern and Schmooze tables from the EXE and drives the original menu flow:
  - credits timer, main menu, Thrash course select, Start video, loading screen, race-start event;
  - win screen and video, standings text, return with the qualified course skipped;
  - wreck and Thrash penalty;
  - Big Game character select (Cydney: $1000), Olley's, Rat bikes, showroom video, refused purchase;
  - Panzer Klub and Schmooze;
  - restroom with Save Game skipped while disabled, Save As, Load;
  - busted game over;
  - the unsaved-game prompt on Go Home.
  
  Every visited screen draws a 640×480 frame. `settings-test` covers settings parse/format/file round trip and rejection of invalid values. All 42 tests pass.
- Visual inspection: native main menu, character select (Slim Jim bio with 6'2"/142 lbs/125cc Perro Pico/$600), showroom, "IN YOUR DREAMS!", Schmooze, winner and race results frames were rendered and inspected (copies in `analysis/frontend/`, git-ignored).
- **Not verified:** pixel or layout equality with the original menus. The original front end could not be captured: the macOS screen capture is unavailable to the sandboxed session, and the oracle hooks only cover the race surface.
- Front-end integration in `rr-game` (default mode; `--race` or the old race options keep the single-race tool). `rr-game --menu-frames 3 --screenshot` renders the credits screen inside the app. `rr-game --course 1 --ticks 900 --autopilot --screenshot` still renders a race. The credits-roll frame (left text with tab stops and colour codes) was inspected. 42/42 tests pass.
- **Not verified:** a complete interactive Big Game played through the app window on macOS or Android. The menu flow is covered by `frontend-test` driving the same `Frontend` class with synthetic input.
- `sound-test`:
  - synthetic dB/pan/frequency conversions;
  - (with disc) the 101-entry volume curve decoded from `0x004362e0`'s code, all 68 effect files present, an audible engine after 120 accelerating ticks, a crash event, the soundtrack playing T_Auto.
  
  Passes. A full-app run with the dummy audio driver starts the intro without errors. 43/43 tests pass.
- **Not verified:** sound timing or levels against the original. The DirectSound oracle hook was not built. Race MIDI plays through the native SF1 sampler (`music-test`: all 11 songs parse, all 5 AWE renders audible with factory presets, sequencer streams through the mixer with pause/stop semantics; October 2026); exact AWE-timbre fidelity and a human listening check remain open (plan 13 Task 2 Step 5).
- **Natural-frame renderer equality.** The oracle gained `dump_scene`, run before `0x00450420` on captured frames. It writes the ring slices, projected entries, scratch blocks and every pending display node, with its resource header and an FNV-1a hash of mip 0. `RR_ORACLE_FRAMES=61 RR_ORACLE_PIXELS=1 RR_ORACLE_SHOT_EVERY=10 oracle/run.sh city-scene` captured 7 CITY attract frames.
- `oracle-scene` (skips without the capture) works as follows:
  - It decodes every texture natively: GLOBAL and course CELs, families exploded and walked including cel groups, and pixel resources in animation/CAR payloads.
  - It identifies each node's resource by hash and specialises rider nodes from HIBOB/remaps.
  - It renders the original node list with the native renderer over the original frame.
  
  Result: **all 98 nodes in each of 7 frames are rendered and 0 of 307,200 pixels differ** (2,150,400 compared). Road, buildings, sky panels, trees, pedestrians and riders are included. With the low-resolution bike sheet 5,233 pixels differ, which is consistent with the attract race using high-resolution bikers.
- Limits:
  - This proves the drawing stage on natural frames. The display list itself still comes from the original; native scene construction from course data is not yet ported or compared.
  - Frames 10–60 of this capture are identical (the attract race had not started moving).
  - The horizon/sky backdrop drawn before the nodes was already present in the original frame used as the background.
- `unpack_resource`: all 163 packed FAM records explode with matching size and CRC-32.

## Session 21: native and Android menu controls (October 10, 2026)

- Native Debug CTest: **44/44 pass**, including existing original-component and natural display-list oracle fixtures. Log: `analysis/menu-input-tests.log`.
- Address/undefined sanitizer CTest: **44/44 pass**, with no sanitizer failures. Log: `analysis/menu-input-sanitizer-tests.log`.
- Extended `input-test` covers stick activation/release boundaries, signed minimum/maximum, dominant-axis diagonals, immediate reversal, 350/100 ms repeat timing, bounded repeat after a stall, and neutral rearming after interruption. These check native menu conveniences; no original gamepad-menu equivalence is claimed.
- Viewer resource smoke, all-five-course projection sweep and `rr-game --menu-frames 3 --screenshot analysis/menu-input-menu.bmp` pass. Logs: `analysis/menu-input-smoke.log`, `analysis/menu-input-courses.log`, `analysis/menu-input-frontend.log`. The frontend smoke renders without errors; no physical gamepad interaction or screenshot fidelity comparison was performed.
- Android ARM64 `assembleDebug assembleRelease -PreleaseDebugSigning=true`: **BUILD SUCCESSFUL**, including both no-game-data checks. Each APK contains only `assets/licenses/LICENSE` and `assets/licenses/SDL-LICENSE.txt`. Development release v1/v2 signatures verify with JDK 21. Logs: `analysis/android-menu-input-build.log`, `analysis/android-menu-input-signature.log`.
- Standalone macOS Release CTest: **44/44 pass**. Refreshed `.app` and ZIP pass ARM64, 22 system-library dependencies, ad-hoc signature and game-data exclusion checks. Log: `analysis/menu-input-macos-release.log`.
- `adb devices` reports no connected device. Android rendering, Storage Access Framework import, physical stick/buttons and controller reconnection remain unverified on hardware. Tests verify stick intent separately from the SDL controller discovery/event path.

### Opening Napa scene work (October 11, 2026, ongoing)

- `family-test`: 163 populated disc families, 577 groups, 786 indexed variants; 176 original group selections matched (`analysis/oracle/napa-groups.tsv`).
- `scene-test`: existing 96 hill projections, 128 new original perspective strip cases (`napa-hill-strips.tsv`), 256 original hill-band cases producing 1,164 nodes (`napa-hill-panels.tsv`); zero differences in points or flips. Input/output fixtures call the original executable's functions; offsets/depth tables are imported from the owned executable.
- `oracle-scene-test analysis/oracle/napa-start.tsv`: 15 nodes rendered, zero unmatched resources and zero pixel differences. This is display-list replay over the captured frame, not independently constructed live Napa scenery.
- Current geometry changes have not yet completed the full native/sanitizer/Android release gate. Full integration remains active.

Additional Session 22 checks: `scene-test` compares 256 shoulder cases / 527 nodes with zero differences (`napa-shoulders.tsv`). `course-test` compares 7,949 original Napa event samples with zero differences (`napa-events.tsv`) and validates source metadata for both roadways in all five course routes. `oracle-road-test analysis/oracle/napa-start.tsv` compares one Napa frame with no mismatches. Logs are `analysis/napa-{shoulders,course}-tests.log`.

Session 22 regression gate: native Debug and address/undefined sanitizer each
pass **46/46 tests** (`analysis/napa-native-tests.log`,
`analysis/napa-sanitizer-tests.log`). Android ARM64 debug and optimized
release with development signing both build successfully and pass the existing
asset exclusion tasks (`analysis/android-napa-scene-build.log`). No physical
Android validation was possible.

The corrected bootstrap completes `napa-moving.tsv` with 121 frames. Camera
position remains 12544 throughout, so this is a stationary capture despite the
filename. Road projection matches all 121 frames. Display-list replay matches
all 48 nodes in five sampled frames, with zero unmatched resources or differing
pixels (`analysis/napa-moving-render.log`). Background remains the captured
original frame; independent scene construction remains unverified.

October 11 playable Napa renderer continuation:

- `build/oracle-scene-test analysis/oracle/napa-drive.tsv`: all four available moving-frame samples match every final indexed pixel (frames 0/150/300/750). Five zero-size shadows in frame 750 are omitted; 125 nonempty nodes render.
- `build/world_renderer-test analysis/oracle/napa-edge-raster.tsv`: 33 original-function display lists, including the exact thin hill quad from the native sweep, match all pixels. This fixture confirms unsigned address wrapping at a fractional UV edge.
- `build/course_scene-test`: 7,949 terrain samples, 7,589 hill samples, zero unresolved hill materials; 2,509 geometry windows across both routes and 503 rasterized windows pass.
- `build/rr-game --assets ../cdrom/ROADRASH --race --ticks 600 --autopilot --screenshot analysis/napa-playable.bmp`: exits successfully with the new playable renderer. This is an early-race smoke check, not a complete race.

Updated live renderer gate: 47/47 native and address/undefined sanitizer tests
pass; Android ARM64 debug/release both build asset-free
(`analysis/napa-live-{native,sanitizer}-tests.log`,
`analysis/android-napa-live-build.log`). This gate precedes dashboard changes.
The denser course sweep now renders all 2,509 tested geometry views; undefined
original reads beyond imported storage use documented native zero guards.
`build/cel-test analysis/oracle/napa-edge-raster.tsv` verifies newly captured
complete partial mip rows with zero failures. The bounded 60,000-tick Napa
autopilot reaches first-place finish at 9:49 (`analysis/napa-finish.bmp`).
`build/hud-test` passes 24 imported dashboard variants and synthetic meter
boundaries; dashboard changes are awaiting the next full regression gate.

Napa animation/scenery gate (October 11): native 50/50 and ASan/UBSan 50/50
pass; both Android variants assemble. Full branch raster sweep: 2,509 views,
624,832 nodes, 6,471 scenery objects with no unresolved frames. Direct roadside
fixtures: 1,024 samples match. These checks do not establish complete race parity.
Logs: `analysis/napa-scenery-native-tests.log`,
`analysis/napa-scenery-sanitizer-tests.log`, `analysis/android-napa-scenery-build.log`,
`analysis/napa-scenery-raster-sweep.log`.

### Android traffic-module build check (October 11, 2026)

- `src/traffic.cpp` (new, uncommitted) compiles cleanly for ARM64 under both
  Gradle CMake configurations: `traffic.cpp.o` builds as ARM aarch64 in
  `android/app/.cxx/Debug` and `android/app/.cxx/RelWithDebInfo`, with
  `VehicleLibrary`/`TrafficSystem` symbols present in each object file.
- The symbols are intentionally absent from `libmain.so`: nothing references
  the traffic module yet (race/game integration is pending), so the static
  archive member is dropped at link time. This is expected, not a missing
  module.
- `assembleDebug` and `assembleRelease -PreleaseDebugSigning=true` both
  succeed with the module present. Debug APK (6.6 MB) and release APK
  (4.4 MB) are arm64-v8a only, signed v1/v2, and contain no `assets/game/`
  entries (`verifyDebugNoGameData`/`verifyReleaseNoGameData` pass).
- The APK `libmain.so` matches the stripped intermediate (SHA-256 identical).
  `adb devices` lists no device, so on-hardware rendering, import and input
  remain unverified.
- REA (`npx rea-agents@latest`) was used for environment checks: adb client
  37.0.0 present with zero attached devices. REA's ELF layout provider covers
  Linux x64 only, so ARM64 `.so` verification was done with local
  `llvm-nm`/`apksigner`/`aapt` instead.

## Session 23: free traffic (October 11, 2026)

- `ctest` (Debug): **53/53 passed** including the new `traffic` suite (synthetic timer/lane/placement/pool/movement/horn checks always run; the CARS corpus part uses the mounted disc).
- Sanitizer build (`-fsanitize=undefined,address -fno-sanitize-recover=undefined`): **53/53 passed** (`analysis/traffic-sanitizer-tests.log`).
- `build/rr-viewer --assets ../cdrom/ROADRASH --smoke` and the five-course `rr-course --exe` sweep pass.
- Live check: `rr-game --autopilot --ticks 2400 --screenshot analysis/napa-traffic.bmp` on Napa renders with traffic active; a byte-diff against the same run from an assets tree without `DATA/CARS` differs (436,231 pixels), confirming the traffic render path executes. The diff is not car-localised: traffic shares the race's rand stream, so rider AI also shifts — expected, since the original uses one CRT stream.
- Geometry probe (headless, real NAPA.CAR + NAPA route): after 2400 autopilot-style steps, 14 active cars with phases 192..319, speeds ramping to the 1,800..3,100 targets, kinds 0/2 mixed, depths spanning the 128-sample ring; visible cars produce textured display nodes within the depth-table span.
- Android: `assembleDebug` and `assembleRelease -PreleaseDebugSigning=true` succeed; the debug APK contains no `assets/game/` entries and `lib/arm64-v8a/libmain.so` includes the traffic code (`analysis/traffic-android-build.log`).
- Not verified: tick-level equality with the original (no traffic oracle capture exists), car animation timing/CCB scaling, rider-car collisions. No Android device was connected.

### Sim-hold replay (October 11, 2026, in progress)

Solo-golden replay (`oracle-sim-test` on `sim-napa-solo.tsv`) runs 124 green
ticks (tick 60 green through tick 183) with zero field divergence, up from an
immediate frame-0 RNG mismatch. Findings, each verified against the golden:
- RNG anchor: the hook dumps post-step state; the race-setup stream
  (11,461 calls, per randlog) is not reproduced, so replay anchors at the
  frame-0 golden rng and re-anchors every tick. Mid-race consumers (scenery
  `0x451xxx`, combat `0x404bxx`, traffic `0x44dexx`, per `RR_ORACLE_RANDLOG=1`
  capture) run in the original tick but are unported; stream-position
  equality is plan 11 work, field comparison is the signal.
- Start-hold: ticks 1-59 frozen (pre-green countdown); replay starts at the
  first moving frame (tick 60) with RNG re-anchored there.
- Demo player: the capture runs the AI branch (`rider.1` think `0x00434660`
  head with mode != 1; only `0x00421430 = 0` writes hold in the tree).
  `Race::set_demo_hold` routes the player through `ai_think` with rider-8
  params (base 95 = personality 8 level-0). Lane shortlist {0,1} (lane 2
  diverges at frame 72); base/lane/bias final fit open.
- SPEC: golden condition 142 = SPEC id 2 (`specs.at(1)`); flags bit 2 set at
  scheduler insert (`0x00437bb0`: `(table & ~8) | 4`).
- Grid lateral: route offset scaled by 256 (Q8); Napa absolute road-base 7
  world units measured from frame 0, traversal mechanism open.
- Steer-state fields (`steer`/`steer_rate`/`lat_vel`/`ground`) added to both
  field tables; golden shows `steer`/`steer_rate` 0 with `latvel` -1..-3
  through tick 181 (bend push only).
- Next gap (tick 184): the demo AI starts steering right (`steer` 0 -> 330
  by tick 184, replan at ~179) while native lane-keeping holds left. Open:
  AI state machine (`0x00408ad0`/`0x00408df0`/table `0x00465980`,
  `0x00409f70` owner check) and overtake gate `0x00408e40`.

Plan 11 Task 1 verification (October 11, 2026): every rule in `src/traffic.cpp` carries its address comment (`0x0044db80`, `0x0044dd00`, `0x0044dd90`, `0x0044df60`, `0x004465f0`, `0x0044efa0`, `0x0040b5f0`/`0x0040b620` SNAC chain, 16-object pool `0x00438b70`); container reads go through bounds-checked `read_resources`/`read_exe_data` plus the SNAC range guard, with truncated-`CARS`/`SNAC` rejection tests in `tests/traffic_test.cpp`. Regression gate: native `ctest` 62/62 pass, sanitizer `traffic`+`actor_traffic` pass. Smoke: `rr-game --race --ticks 3600 --autopilot` renders a car ahead on the Napa road with the rider at speed. Tick order and car/rider draw order remain unverified (plan 10 acceptance input).

## Session 23: simulation oracle, plan 10 tasks 1–4 (October 11, 2026)

- Task 1 (trace): step hook site `0x0044b324` → `0x0044b3b0`; live CRT rand
  state at `getptd()+0x14` (`0x004592b0`); scheduler ring `S=[0x46dcc8]`,
  end `[S]`, first `[[S+4]+4]`; think at `+0xe0`. Committed as docs.
- Task 2 (tables/snapshot): `sim_fields-test` passes (native/oracle table
  agreement + synthetic snapshot). `ctest` 54/54.
- Task 3 (oracle): `oracle-patch-test` preflights the step/controls sites
  against the reference EXE. `sim-napa-solo` (level 1 Napa, seed 1, drive-2
  autopilot, solo): input-driven capture and replay are pairwise identical
  over 2,402 frames (`tools/golden_diff.py`, zero differing fields).
  Controls: `sim-napa-solo.controls` (2,221 words; 181 coast ticks follow
  input exhaustion per the plan's 0xb4 rule).
- Determinism findings: seeding must happen at menu entry (race setup
  consumes rand: grid slots differ run to run otherwise); controls must be
  consumed per step, not per poll (menu polls eat file words); full-field
  captures diverge mid-race (attack landings ride animation frames that
  advance with frame pacing — no rng change), so acceptance goldens are
  solo (`RR_ORACLE_SOLO=1` freezes non-player nodes via NULL think/move and
  nulls the traffic pool). Full-field `sim-napa-solo-straight` kept as
  task 6 input.
- Task 4 (replay test): `oracle-sim-test` builds the same race as
  `Game::restart` (solo when the header says so) and reports the first
  divergence. Current result: diverges at tick 1 on `rng` (original seeded
  stream includes race-setup consumption; native construction consumes
  none yet — plan 10 task 6 item 1). Marked `WILL_FAIL` until then.

Actor animation playback: `build/actor_animation-test` matches all 2,048
original-function steps in `napa-actor-animation-fixtures.tsv`. This validates
selection/queue/timing/loops/retained-frame state, not actor graphics descriptor
construction or live merged actor drawing. REA static evidence is retained in
`analysis/oracle/rea-actor-animation.json`.

Actor graphics fixtures (`analysis/oracle/napa-actor-animation-fixtures.tsv`): 672 CANS/RRAN view cells, 9,312 descriptor view selections, and 256 orientation/projection cases (235 accepted billboards) match original private calls. `actor_descriptors-test` checks selected frame, flags, roll and hotspots; `actor_render-test` checks visibility and all Q16 corners. `actor_nodes-test` exercises owned HI Bob/copy/shadow pages and their stable ownership. Runtime fixture capture restores modified graphics CCBs and projection globals. These module checks do not establish live race ordering or animation-trigger parity.

`actor_traffic-test` checks all ten original Napa car masks (including inherited CANS), 207 graphics cells, 30 playback descriptors and 1,440 yaw/lean view selections against runtime helper captures in the actor fixture. Car live rendering uses the verified `actor_node` projection; per-car movement-derived heading and complete object-chain ordering still require race goldens.

Crash modules now have original-function evidence: 64 local entry cases compare 49 words/byte-containing words on each actor and animation state; 1,024 crash-bike and 2,048 crash-rider animation steps compare state and selected descriptor. The entry fixture is `analysis/oracle/napa-crash-entry-fixtures.tsv`. These tests cover numerical entry and animation only, excluding network ownership, landmark bookkeeping, independent motion, collision callbacks and walking/remount AI. The live native crash path still uses its prior timer stand-in.

`build/crash-test` compares 64 original crash entries (49 fields on each actor), 512 local motion inputs and 256 offline recovery decisions from the private `napa-crash-entry-fixtures.tsv` capture. The separate descriptor test compares 1,024 crash-bike and 2,048 crash-rider animation steps. Fixtures contain scratch pointer values only as opaque field IDs and are ignored/unbundled. These gates prove the scoped routines, not complete crash gameplay.

Crash reset gate additionally compares 64 original `0x00404560` calls, 34 fields per actor and both playback states. The native integration scenario starts with the traction crash flag, verifies both actors have drawable states, detects their positions separate, and requires remount within 4,000 ticks with restored original drag and dimensions (currently 434 ticks). This is an integration regression alongside the individual original-function comparisons; it is not a recorded original live-race tick golden.

Police configuration gate: `build/police-test` compares imported pursuit/relationship fields and rider stats against the explicitly induced level-1 Napa cop constructor in `analysis/oracle/napa-police-spawn.tsv`. It also compares 1,024 local arrest outcomes/delays from `napa-police-arrest.tsv`, requiring both the complete count and successful restoration marker. For that scoped probe, the nearby-cop lookup is temporarily substituted only after a five-byte entry preflight; the actual `0x00407f30` decision and `0x00416010` outcome routine run on scratch actors. This gate excludes actual lookup traversal, spawning cadence and chase movement. Private fixtures/REA data remain unbundled.

`make_police_actor` is checked against 50 original bike fields and 43 valid original rider fields in the bounded level-1 constructor capture (excluding the stats pointer and all bike-only fields beyond rider allocation 0x208). Ground and lane target are caller inputs; dynamic AI/scheduler links are not verified by this snapshot. `crash-test` additionally matches 256 original directed player walking decisions in `napa-crash-walking.tsv`, including resetting the mutable recovery timer. Full checkpoint gate: 62/62 native/sanitizer plus Android debug/release passed.

## Tick-184 AI kink probe (October 12, 2026)

- Extended `dump_sim_tick` in `oracle/aweman_proxy.c`: rider-half raw window
  16→32 words (`+0x180..+0x1fc`), new AI-brain chase (`rider.<slot>.brain_<off>`
  for brain `+0x138..+0x19c` via `rider_half+0x1d8`), bike lateral chain
  (`lat104/lat108/lat10c`, `yaw60`) and steer-rate words
  (`bike_18/2a0/2a4/2a8/2ac/29c/2b0/2d0/2d4`). `oracle-patch-test` preflights
  clean (no new hook sites).
- `sim-kink-probe4.tsv` (Napa level 1, seed 1, drive 2, solo, ticks 2–220):
  demo rider state (`brain_160`) 1→5 at tick 183, `brain_194` (target_x)
  18688→43264, bike `steer` ramps +165/tick from tick 183, `lat10c` −34→+86 at
  tick 184. Native `oracle-sim-test` on `sim-napa-solo.tsv` still diverges at
  frame 183 (tick 184) on `rider.0.lateral` (original 17073, native 16953):
  native `ai_think` has no state machine and never steers here.
- Steer-rate cross-check: live `bike_2a8/bike_2ac` = 165/141; native
  `apply_spec` for SPEC id 2 computes 165/141 exactly (SPEC d62/d63 = 60/70,
  mass 256, either rider value 90/91). No divergence in steer rates.
- Open (plan 10 task 6 input): replay harness must build the 7 frozen
  opponents in solo mode (the original constructs them with nulled think/move
  and replan `0x00408cd0` picks `+0x168` among them); then port `0x00408ad0`
  transitions, state dispatch (`0x00465980`), `0x00408e40` and the
  `0x0040a410`/`0x0040a100` lookahead.

## Fight-target identification (October 12, 2026)

- `dump_sim_tick` now logs every scheduler-ring node's address, think, position
  and kind in the header frame (`nodeaddr.<i>[.think/.position/.kind]`), plus a
  restored readability guard. `oracle-patch-test` preflights clean.
- `sim-kink-probe6.tsv` (same Napa solo settings): the demo rider's
  `brain_168` target `0x1402f60` is ring node 28 with think 0 (a frozen
  opponent node, position 13312, kind 1); the player bike is ring node 32
  (`0x1403c10`). The tick-183 state 1→5 transition targets a frozen opponent
  the rider is pulling away from (gap trips `0x0040a4a0`'s 0xf0 window).
- Consequence for the native replay harness (plan 10 task 6 item 1): solo
  replay must construct the 7 opponents frozen at their grid slots with think
  and move skipped but positions present for the replan/target scans. Port
  order: `0x00408cd0` neighbour pick → `0x00408ad0` transitions → state
  dispatch (`0x00465980`, states 1 and 5 first) → `0x00408e40` →
  `0x0040a410`/`0x0040a100` lookahead.

## Solo grid layout (October 12, 2026)

- `nodeaddr` header entries now carry `type` (+0xc4), `lateral` (+0x28) and
  `half` (+0x2fc). `sim-kink-probe7.tsv` (same Napa solo settings) maps all 38
  ring nodes: 15 bike/half pairs (type 4 bike with `half` pointing at its type 8
  half) plus traffic-pool and scenery nodes.
- Group A (the race field, 8 pairs): player at 12800 plus opponents at
  13056–14592 in consecutive 0x100 steps (samples 50–57). Positions equal the
  `0x00464088` table selectors (level < 4, full 16-rider construction);
  opponent slots by position are 9, 4, 10, 2, 11, 3, 12. Every rider-half
  lateral matches its table lateral exactly (40960/16384).
- Group B (7 pairs at 21248–31488, ahead of the field): extra constructed
  bikes, out of the replan/overtake scan windows at the tick-184 transition;
  relevance for later-race scans is open.
- Anomaly (not explained, mirrored as-is): two group-A bike laterals are wild
  (ring 18: 83712, ring 20: 108032) while their halves are normal; the other
  five equal table lateral plus the local road centre (1792–3584 across
  samples 50–57, consistent with road_base 7 world units at sample 50).
- Native `grid_position`/`grid_lateral` (two rows at 0x3200/0x3300 with four
  fixed offsets) does not match this construction; the replay harness must use
  per-slot table positions/laterals (open).

## Field composition and replan helpers (October 12, 2026)

- `sim-field-probe2.tsv` (full field, solo=0, ticks 1–220) header ring map:
  11 racing opponent pairs (bike think `0x00407a90`, half think `0x00407bd0`,
  bike `net`(+0x324) 1) at 13056–25088; 3 parked pairs (think 0, same net/kind)
  at 27648/30208/31488; player pair (net 0) at 12800; live traffic pool
  (type 32, think `0x00440dd0`) and scenery. Native races 7 opponents; the
  original builds 14 (11 racing + 3 parked). Park mechanism is open.
- Dead end recorded: `rider_half+0x78` reads `0x474bd8` identically for every
  bike, so it is not the per-rider stats row in this path; the stats-dump hunk
  was removed again. Rider identity per node remains open (not needed for the
  solo frozen harness: only position/lateral/scan flags matter).
- Replan helpers fully traced, no oracle needed: `0x00432500` = move-nonnull
  test (+0xe4); `0x00449c20` = absolute position gap; `0x00441750` = CRT rand
  (or fixed `0x0049bce8` in mode 3); `0x00449da0` = nearest ring node by slice
  on each side. Full `0x004082f0` body recovered (replan easing of the 10-entry
  brain table, neighbour pick, overtake-step rating, speed cases).

## Napa playtest fixes: launch paths and tachometer (October 11, 2026, plan 11a)

- **Task 4 (one game, every launch path):**
  - `tools/verify_macos_app.py` now reads `RRBuildCommit` from the app plist
    and fails when it differs from `git rev-parse --short HEAD`. Run against
    the stale October 10 bundle: failed with
    `App build commit None != HEAD '5e40cc5'`.
  - `tools/build_macos_app.sh` stamps `RRBuildCommit` into the built plist and
    re-signs. Full standalone rebuild passed:
    `PASS: ARM64, 22 system libraries/frameworks, valid ad-hoc signature, no
    game data, built from 5e40cc5`.
  - Renamed the research-viewer target `rr-native` → `rr-viewer` (CMake target,
    `src/viewer.cpp` usage string, `tools/ci.sh`, README, RUN_NATIVE, PROGRESS
    tool table, plans 00/01/04/08/15, VERIFICATION). `run-macos.sh` now builds
    and launches `rr-game` (passing `--assets` when the disc is mounted, else
    first-run import); new `run-viewer.sh` launches the viewer. `tools/ci.sh`
    fails a stale `build-release` bundle unless `RR_STANDALONE_APP=1` rebuilds it.
  - `build/rr-viewer --assets ../cdrom/ROADRASH --smoke` passes; `rr-game`
    links.
- **Task 2 (tachometer), rpm part:**
  - `update_engine_rpm(Bike&)` (literal port of `0x004349c0`) added to
    `bike.cpp`; `SoundSystem::update` now takes `Race&`, calls it for the
    player and the two nearest opponents, and reads `+0x2dc` for the engine
    pitch. The private `rpm_`/`rpm_speed_` state is deleted.
  - `bike_test` drives the port over ten synthetic cases (stationary,
    acceleration, gear change at constant speed, airborne ± cap, crashed
    above/below the 0x2001 boundary, unchanged-speed cache, ±0x2000 rate
    limits); `hud_test` asserts `Hud::state(...).rpm_angle` follows `+0x2dc`.
  - Native CTest: **62/62 pass**.
  - The distance readout is traced (see REVERSE_ENGINEERING.md "Dashboard
    distance readout") but not ported: it needs `0x00407410`'s cumulative
    distance in feet and oracle calibration.

## Scan-filter correction (October 12, 2026)

- `0x00449ce0`'s `piVar3[0x31]` is word index `0x31` = byte `+0xC4` (node type),
  not byte `+0x31`. The replan/overtake scans walk rider-half nodes whose type
  8 always passes the `& 8` filter; slice `+0x36` reads 7 on every Napa node
  (`sim-kink-probe8.tsv`, all 15 bikes). A dumped `flags31` byte-`+0x31` field
  turned out to be the previous-position sample byte and was removed again;
  `slice36` stays as the documented constant. No port code reads either.

## Rider-car contact and single-anchor replay (October 12, 2026)

- **11a Task 1 (rider–car collision), trace + fixture + port:**
  - `collide_traffic`/`contact_car`/`crash_car` in `src/race.cpp` with a
    `cars_mut()` accessor in `src/traffic.hpp`; called for player and AI at
    the traced point in `Race::step`. Fixture
    `analysis/oracle/napa-car-contact.tsv` (git-ignored): 256 isolated
    `0x004049c0` calls, 0 faults — 32 fast-contained crashes (reason 2,
    anim 29), 32 slow-contained speed transfers, 64 side shoves, 128
    crashed-rider rejections. Golden comparison and the live
    `--steer-into-traffic` 6000-tick log remain open.
- **Plan 10 Task 5 Step 2a (single-anchor replay):** `oracle-sim-test`
  defaults to one RNG anchor at the green light; `--reanchor` keeps
  per-tick re-anchoring as a diagnostic only. The tick-120 speed gap
  root-caused to the harness (solo replay built `Race` without
  personalities; fixed by loading `0x00464558` rows) — physics now green
  through frame 138 (speed 1919). Next divergence frame 138 (tick 139):
  RNG original 2574079260 vs native 1133891140, fields matching — the
  unported `0x0044dd90` traffic-spawn timer draw. Pinned in
  `tools/sim_known_divergences.json`; `oracle-sim` diagnostic gate passes.
  New `napa-solo-human` scripted-controls scenario (1,431 frames,
  self-check + input replay zero differences). Full Debug suite 63/63.
- **Plan 18 Task 1 (options trace):** findings merged into
  REVERSE_ENGINEERING.md "Options screens and pause"; plan 18's screen map
  corrected (Sound off Restroom as a native dialog look-alike, Pause purely
  native). **11a Task 3 (bike/shadow nodes), Steps 1–2 done October 12:**
  scene dump v4 (`dump_rider_objects` in the proxy; `napa-drive-v4`
  recapture, v4 pixel mode 0 differing pixels) and `--native-actors` mode
  (oracle-scene-test) CONFIRMED FAILING 4/4 frames, isolated to the
  view/subframe selection base (`read_actor_sequence` stores a descriptor
  index where the original resolves a CANS group id). Fix + Step 4 open:
  dispatch-entry hooks, recapture, `select_actor_view` fix, mid-jump frame.

## Native determinism and soak (plan 16 Task 7, October 12, 2026)

`race-soak` (`tests/race_soak_test.cpp`): seed 1, seven opponents,
accelerate held, 108,000 ticks or finish; FNV-1a over every tick
snapshot; same setup twice must hash equal. Rider positions monotonic
within one sample outside crashes (contact separation shoves back a few
Q8 units); pools never exceed 16 cars / 10 controllers / 40 signals.
Default gate runs Napa L0 twice (~30 s Debug). `RR_SOAK_FULL=1` runs all
25 course x level combos (deterministic; ~13 min Debug):

- CITY L0–L4: 108000 ticks each (no finish on accelerate-only)
- HIWAY/MEDLY/NAPA/CANYN L0–L4: 108000 ticks each
- NAPA L0 hash `dec5230d1d4adf99` (representative; per-run hashes printed
  by the test, not pinned — any sim change re-baselines them)

### October 12, 2026 — audio/AI/loading playtest checks

Owned Debug build: `build-agent-playtest-audio-ai-loading`. Source fingerprint
`c231b349b122a14f1c21e0a6dadb31189b89966d863f27ac0aa7131b9a8806db`.

- `cmake -S . -B build-agent-playtest-audio-ai-loading -DCMAKE_BUILD_TYPE=Debug`
  and `cmake --build build-agent-playtest-audio-ai-loading --parallel`: pass.
- Focused `ctest` (`race|pedestrian|sound|music`): 4/4 pass after corrections.
  Engine test renders at native audio cadence for 2,400 ticks and checks that
  the player remains audible beyond the start listener's hearing radius.
  Napa MIDI test pumps/consumes thirty seconds, checks buffer backpressure and
  silence after stream detach. AI tests cover a stale replan target, a distinct
  passing candidate, scalar demo speed surviving replans and all seven riders
  making progress over the last 600 ticks of a 3,600-tick Napa setup.
- `python3 tools/agent_gate.py sanitizers --build-dir build-agent-playtest-audio-ai-loading-asan --tests '^(race|sound|music|mixer|pedestrian)$' --output analysis/agent-gates/playtest-audio-ai-loading-asan-final --session playtest-audio-ai-loading`: **5/5 pass**, no skips.
- `python3 tools/agent_gate.py android-debug --output analysis/agent-gates/playtest-audio-ai-loading-android --session playtest-audio-ai-loading`: **pass**, including APK asset inspection. Hardware not run.
- Normal `build/rr-game` was rebuilt to the same source fingerprint.
- UI smoke: `rr-game --menu-frames 2 --screenshot analysis/playtest-menu.bmp`,
  `rr-game --video VIDEO/INTRO.AVI --ticks 600 --screenshot analysis/playtest-intro.bmp`,
  and interactive-audio `rr-game --race --paused --screenshot analysis/playtest-pause.bmp`
  all exit 0 (with `--assets ../cdrom/ROADRASH`). The pause PNG was inspected at
  `analysis/oracle/playtest-audio-ai-loading/playtest-pause.png`: native dimmed
  frame/debug glyphs, not an original Windows modal dialog. Startup loading is
  exercised by these paths; its animation was not independently screen-captured.

Initial full CTest caught the scalar demo's absent speed table: 68/69, with
CITY pedestrian crossing stalled at the start. The fallback correction restores
the test without changing its assertions. Initial ASan caught an existing
music-test fixture reading 1,024 stereo frames from only 1,024 samples; its
input buffer now holds 2,048 samples. One intermediate native receipt was
invalidated by edits during the gate; use the `-final` receipts only.

No new original runtime capture, human simulation acceptance, crash pixel
comparison, Android device observation, Android release assembly or standalone
macOS packaging was performed. AI changes are assembly-backed and have native
regressions; full AI/scheduler/RNG parity remains open. The existing `oracle-sim`
CTest diagnostic records a known divergence; a passing diagnostic is not strict
simulation acceptance. Visual audit/implementation details are in
[the handoff](agent-handoffs/playtest-audio-ai-loading.md).

Final integration receipt:
`python3 tools/agent_gate.py native --build-dir build-agent-playtest-audio-ai-loading --output analysis/agent-gates/playtest-audio-ai-loading-final --session playtest-audio-ai-loading`: **pass, 69/69 CTests**, no skips, 214.74 seconds. The receipt confirms a stable source fingerprint. The included `oracle-sim` pass is the documented known-divergence diagnostic, not simulation parity.

## Web port Task 2 (October 12, 2026): full suite under Node

Baseline `e2dce9a` plus other workers' uncommitted sim/audio changes
(`src/game_main.cpp`, `src/race.cpp`, `src/mixer.*`, `src/sound.cpp` — untouched).
Owned edits: `src/bike.cpp`, `tests/bike_test.cpp`, `tests/importer_test.cpp`,
`tests/mixer_test.cpp`, `cmake/emscripten.cmake`, `CMakeLists.txt`,
`tools/verify_sim.py`, `tools/ci.sh`, `docs/WEB.md`.

- Configure/build: `source .tools/emsdk/emsdk_env.sh` (with Homebrew python on
  PATH), `.tools/emsdk/upstream/emscripten/emcmake cmake -S . -B build-web
  -DCMAKE_BUILD_TYPE=Release`, `cmake --build build-web --parallel` — exit 0.
- `ctest --test-dir build-web`: **68/68 pass** (67 pass + `mixer-device`
  skipped 77, no audio clock under Node). Former Task 1 failures resolved:
  `race` (wasm 64 KiB stack too small for sim call chains; `-sSTACK_SIZE=1MB`
  for web links), `sim_fields` (real UB: `apply_spec` divided by zero on
  zeroed specs — `src/bike.cpp:104-115`; every divisor now guarded to zero,
  matching ARM64; real specs never hit a zero divisor), `importer` (FIFO test
  gated out under `__EMSCRIPTEN__`; web nonseekable input goes through the
  Task 4 ImportJob), `mixer-device` (binary exits 77 under Emscripten;
  ctest maps 77 to skip), `oracle-sim` (`verify_sim.py` runs `.js` through
  Node). The 12 Task 1 skips are gone: `-sNODERAWFS=1` per test executable
  (`rr_web_hostfs` in `cmake/emscripten.cmake`) exposes the mounted disc and
  `analysis/oracle/` goldens; `oracle-patch/road/scene/raster` and all corpus
  tests pass. `oracle-sim` diagnostic records `known-divergence` (the pinned
  demo-hold trace), same as native — not acceptance.
- Native gate after the `bike.cpp` fix: `ctest --test-dir build` (Debug)
  **69/69 pass** (includes `first-run-smoke`, excluded for EMSCRIPTEN by
  design). ASan/UBSan tree `build-asan`: `bike/race/sim_fields/importer/mixer`
  all exit 0 with **zero** `division by zero` reports (previously 6 in
  `bike.cpp`).
- Step 3 audit: `Iso9660::ReadAt` offsets are `uint64_t` (ISO is 518 MB);
  the only `long`-as-counter uses are `game_main.cpp` tick/frame locals owned
  by the Task 3 refactor (file has another worker's uncommitted changes;
  Task 3 stays blocked on that). `long()` casts elsewhere convert bounded
  in-memory sizes (< 2 GB). No `size_t` file-offset arithmetic above 2 GB.
- Skipped separately: `mixer-device` only (no audio under Node). No known divergences blessed: the `apply_spec` guards reproduce
  native ARM64 values exactly on all previously-valid inputs.
- Evidence: `build-web/ctest-full.log` (pre-skip-mapping run),
  `build-web/oracle-diagnostic.json`, `build-asan-*.log`. Handoff:
  `docs/agent-handoffs/archive/2026-10/2026-10-12-web-task2.md`.

## Web port Task 4 (October 12, 2026): pull-based import job

Owned edits: `src/import_job.hpp/.cpp` (new), `src/sha256.hpp/.cpp`
(`Sha256Stream`; one-shot is now a wrapper), `src/importer.cpp`
(`import_game` reimplemented as a sync driver over the job;
`FilesystemSink` keeps staging/marker/rename semantics),
`tests/import_job_test.cpp` (new, plan's six cases), `CMakeLists.txt`
(`import_job` target + library source).

- TDD: stub ran first — 19 failures (`import_job-test`); implementation
  brings it to 0 failures, `sha256-test` still 0 failures (same vectors).
- `importer_test.cpp` passes **unchanged** (all 8 cases: corrupt import
  leaves no destination, missing file atomic, existing data preserved on
  retry, traversal rejected, ISO/folder/cancel/FIFO paths, staging cleanup).
- Full gates after the rework: native `ctest --test-dir build` (Debug)
  **70/70**; `ctest --test-dir build-web` (emsdk Node 24.19.0) **69/69**
  (incl. the new `import_job` test; `mixer-device` still the single
  environment skip). ASan/UBSan `build-asan`: `importer`, `import_job`,
  `sha256`, `iso9660` all exit 0 with no reports.
- Deliberate interface extension beyond the plan sketch:
  `missing_file(message)` (folder files absent/unreadable record per-file
  `missing` and continue; `fail_source` would abort everything) and lazy
  `mutable` need storage (observing the job stores the offer; transitions
  happen in `supply()`).
- Handoff: `docs/agent-handoffs/archive/2026-10/2026-10-12-web-task4.md`.

## Plan 18 Task 3 Step 6 (October 12, 2026): display fullscreen toggle

- `build/rr-game --assets ../cdrom/ROADRASH --race --ticks 300 --toggle-fullscreen-at 150 --screenshot /tmp/fs-toggle.bmp`: exit 0, screenshot 2560×1920 (fullscreen desktop after the toggle) with 48872/49152 non-black samples.
- `build/rr-game --assets ../cdrom/ROADRASH --video VIDEO/INTRO.AVI --ticks 300 --toggle-fullscreen-at 150 --screenshot /tmp/fs-video.bmp`: exit 0, screenshot 2560×1920 with 39323/49152 non-black samples.
- Both prove the toggle path re-applies window size + logical presentation mid-race and mid-video with no stretch for a frame (screenshots are content, not black). Screenshots in /tmp, not committed.
- Full gate after the Task 3 wiring: native `ctest --test-dir build` (Debug) **72/72** (incl. new `display-test`).
- Note: the race keeps logical 640×480 (dashboard is composed inside the frame); no 510 path exists, so `display_config(..., 480, ...)` is used everywhere. The module supports 510 when a real 510-tall path lands.

## Plan 18 Task 5 Step 4 (October 12, 2026): native Display screen

- `native_screens-test`: 0 failures (slot text, geometry copy, nav chain, Back link, mobile 6-count, 0x13 linkage). `frontend-test`: 0 failures (scripted Restroom → Graphics → Display → Scaling → Integer → Back: settings, file `scale_mode=1`, focus restore).
- `build/rr-game --assets ../cdrom/ROADRASH --menu-script restroom,graphics,display --menu-frames 60 --screenshot /tmp/display-screen.bmp`: exit 0. Display vs Graphics Options screenshots share 5493314/6553628 pixels (84% — same background picture; differences are the buttons/title/focus only). Fonts, colours and arrow come from the shared item draw path. Screenshot in /tmp, not committed (original art).
- Deviations from the plan sketch (all test-covered): the 0x13 Display entry repurposes dead item 2 instead of appending (table size stable, existing 1→2→3 nav chain reused); touch opacity gets a 5-choice popup (`native_popup::touch` + `touch_0..100` slots — the sketch's 5 popups had no touch home); `--menu-script` enters named screens directly (debug-only) rather than synthesizing MenuInput keys.
- Full gate after the wiring: native `ctest --test-dir build` (Debug) **73/73** (incl. new `native_screens-test`).

## Plan 18 Task 6 Step 4 (October 12, 2026): Sound mix with live channel levels

- `frontend-test`: 0 failures (Sound screen via restroom row; Music 80 → 50 via 3× Left; End → 100; Mute toggle; Back to restroom; `apply_mixer` emitted; file round-trips `music_volume=100`; slider bit absent from all original EXE items/popups).
- `mixer-test`: 0 failures (voice `set()` between renders rescales the next block; old level gone — Review Focus 3 at the Mixer layer; `SoundSystem::play_loop` re-voices every update so `set_levels` lands on the next update).
- Preview click: none. Task 1 found no wave playback in the dlgproc (scroll/edit post the live-apply thread message; button 0xBC1 re-posts it) — the `apply_mixer` event is the native equivalent.
- Deviations: mute/swap are direct-toggle rows (dialog has a mute bit; no toggle UI traced — direct toggle is the minimal faithful mapping); swap speakers exposed alongside mute (same bit class).
- Full gate after the wiring: native `ctest --test-dir build` (Debug) **73/73**.

## Plan 18 Task 7 Step 4 (October 13, 2026): Configure Input, gamepad and touch pages

- `frontend-test`: 0 failures (restroom controls row `0x0040fd90` enters 0x12; W binds accelerate low-byte-first per `0x0040e760`, label follows; Escape cancels unchanged and disarms; swap toggle, dead-zone slider 35 → 40 → Home 10; gamepad/touch Back returns to 0x12).
- `input-test`: 0 failures (defaults equal the traced table, rebound key yields its bit, old key silent; left-handed mirrors the touch zones).
- `settings-test`: 0 failures (page fields round-trip; out-of-range dead zone rejected).
- `native_screens-test`: 0 failures (gamepad/touch page rows, desktop/mobile 0x12 link counts, touch link mobile-only).
- Byte order: custom keyboard codes store low-byte-first `(key,0)` per `0x0040e760` (capture store, label read, control-bit read); joystick codes stay high-byte-first.
- Full gate after the wiring: native `ctest --test-dir build` (Debug) **73/73**.

## Plan 18 Task 8 Step 4 (October 13, 2026): in-race pause menu

- `frontend-test`: 0 failures (`open_pause` focuses RESUME; Down×4 → QUIT RACE; quit dialog, No stays paused with no event, Yes emits `quit_race` and closes; Escape emits `resume_race`; sub-screen Back returns to pause; controller-only d-pad + A/B flow).
- `race-test`: 0 failures (two 900-tick runs, second with 120-frame no-tick gaps after ticks 10/450/899: per-tick snapshots and final state identical — Review Focus 4).
- `native_screens-test`: 0 failures (pause rows and SOUND/DISPLAY/CONTROLS targets).
- `draw_pause` deleted; lifecycle freezes open the menu instead. Deviations: CONTROLS rebinds while racing (original limit untraced); sliders apply levels live, no preview click.
- Full gate after the wiring: native `ctest --test-dir build` (Debug) **73/73**; `tools/ci.sh` (native + android-debug) green.

## Plan 18 Task 9 Step 4 (October 13, 2026): FPS counter, docs and gate

- `show_fps` draws `"<fps> FPS <tps> TPS"` top-right (Futura Bold 24, `#DEFFF0`, 2 px shadow, transparent ARGB background, refreshed twice a second).
- Headless proof: `RR_PREF_OVERRIDE=/tmp/rrpref` (`show_fps=1`) `build/rr-game --assets ../cdrom/ROADRASH --ticks 200 --screenshot /tmp/fps.bmp` shows `1 FPS 0 TPS` over the sky (first-frame counts); `show_fps=0` control screenshot is identical minus the text.
- USER_GUIDE Settings section, README/PROGRESS updates. `tools/ci.sh` green; macOS bundle rebuilt and verified (`verify_macos_app.py`: ARM64, ad-hoc signature, no game data, stamp matches HEAD).

## Plan 19 Task 6 Step 4 (October 13, 2026): band-parallel rasterisation

- `hires-render`: 0 failures (golden frame 0 at s=1..4, 1/2/4/7 threads each: byte-identical to the 1-thread image — Review Focus 2).
- Bug fixed by the test: per-band `RasterScratch64` was sized to the band, but 64-bit edge tables index by absolute surface row — bands 1+ read zeroed rows and dropped polygons crossing into them. Scratch is now sized to the full surface height per band job (per-worker allocation, no sharing).
- Frame times (Apple M3 Pro, 12 cores; Debug tree, node raster only, 360 frames over 5 golden display lists):
  | scale | 1 thread (p50/p95/max ms) | 7 threads (p50/p95/max ms) |
  | --- | --- | --- |
  | 1 (640x480) | 0.50/0.59/2.03 | 0.22/0.24/0.27 |
  | 2 (1280x960) | 2.46/2.55/2.65 | 0.99/1.02/1.13 |
  | 3 (1920x1440) | 5.01/5.11/5.24 | 1.91/1.96/2.22 |
  | 4 (2560x1920) | 8.26/8.75/9.61 | 3.22/3.91/5.27 |
- Match-display rule: every scale's 7-thread p95 is under 12 ms on this machine, so no scale is excluded here. Full-frame cost (scene build, horizon, dashboard, presentation) is not included; re-measure end-to-end before shipping the Task 8 default.
- Default thread count (plan interface): `min(8, cores - 1)`, at least 1 — not yet wired into the game (no caller); lands with Task 8's presentation work.
- TSan re-run required once the game calls the threaded path (tsan.sh covers it via the race scenario).

## Plan 19 exit criteria (October 13, 2026): end-to-end frame cost + gate

- End-to-end headless race cost (Apple M3 Pro, 12 cores; Debug tree; `./build/rr-game --race --render-scale <s> --ticks 900 --autopilot`, wall time / 900 ticks, triplicate; includes scene build, horizon, dashboard, threaded raster, presentation — the full-frame cost Task 6's node-only table excluded):
  | scale | run1/run2/run3 (s) | per-tick min–max (ms) |
  | --- | --- | --- |
  | 1 (640x480) | 1.29/1.13/1.10 | 1.22–1.43 |
  | 2 (1280x960) | 1.20/1.19/1.31 | 1.32–1.46 |
  | 3 (1920x1440) | 1.48/1.51/1.47 | 1.63–1.68 |
  | 4 (2560x1920) | 1.79/1.92/1.85 | 1.99–2.13 |
- Match-display rule: every scale's end-to-end p95-equivalent is under 12 ms on this machine, so no scale is excluded. Headless ticks render synchronously (no vsync); on-display frame pacing is governed by VSync, not this table.
- Gate (Debug tree, October 13, 2026): `hires-render`, `view`, `race-soak`, `settings`, `display`, `frontend`, `native-screen` suites all pass (6/6 in the named subset run; `hires-render` re-confirmed separately). UBSan/Release trees: not re-run in this slice — Task 3–8 code paths are shared and allocation-free by construction; CI covers the remaining trees.

## Plan 21 Task 4 Step 4 (October 13, 2026): RGB filter frame times

- End-to-end headless race cost (Apple M3 Pro; Debug tree; 900 autopilot ticks, triplicate; wall/900 per tick):
  | scale | nearest (indexed) | bilinear (RGB) | trilinear (RGB) |
  | --- | --- | --- | --- |
  | 1 (640x480) | 1.22–1.43 ms | 1.10–1.18 ms | 1.14–1.30 ms |
  | 2 (1280x960) | 1.32–1.46 ms | 1.41–1.44 ms | 1.49–1.50 ms |
  | 4 (2560x1920) | 1.99–2.13 ms | 2.57–2.60 ms | 2.87–2.90 ms |
- RGB-nearest equivalence holds exactly (raster-rgb suite); the RGB path only runs when `full_colour` is on or `filter != nearest`.

## Plan 21 Task 9 Step 2 (October 13, 2026): enhancement cost + preset

- Headless race cost at render scale 2 (Apple M3 Pro; 900 autopilot ticks, wall/900 per tick, best of 3; synchronous ticks, relative numbers, not vsync p95):
  | config | ms/tick |
  | --- | --- |
  | all off | 1.22 |
  | +interpolate | 1.25 |
  | +perspective | 1.25 |
  | trilinear (vs bilinear) | 1.32 |
  | fog 60 | 1.28 |
  | dusk grade | 1.39 |
  | bloom | 1.33 |
  | vignette | 1.26 |
  | motion blur | 1.26 |
  | 16:9 wide | 1.30 |
  | ALL ON | 1.67 (+37%) |
- Every single enhancement and the all-on combination are far under the 8.3 ms budget, so the preset includes the largest set: everything except `draw_slices` (Task 6 dropped — stays 33) and `full_colour` (implied by trilinear). Preset: interpolate, perspective, trilinear, fog 40, dusk, bloom, vignette, motion blur, match-display widescreen.

## Plan 20 Task 2 Step 4 + Task 3 (October 13, 2026): MMPX timing + upscale shots

- MMPX 4x generation over all 5 courses' family cells + road materials + rider sheets (Release-equivalent local run): 1,239 textures, ~15 s wall, 223 MB cache (2x + 4x levels). Under the 20 s target.
- Scale-4 Napa off/on: 29,295 sampled pixels differ (indexed path, nearest); road grain and lane edges visibly cleaner. RGB path also routes through upscaled levels.

## Plans 25–28 verification (October 13, 2026)

- Plan 25: fast suites green per handoff (`first-run-smoke` + `race-soak`
  2/2 at Task 1); ASan reproductions for the texture/widescreen overflows
  (Task 7 handoff, exit 134 pre-fix).
- Plan 26: 5/5 courses `--ticks 3600 --autopilot` exit 0 with 9/9 wiring
  hooks; `--print-wiring` specced line + exit 77 without disc; oracle
  diagnostic report byte-identical after harness adoption; full gate green.
  Follow-up: `race_setup_test` career-bike case green; `race-soak` +
  `oracle-sim` green with the same known divergence (frame 207,
  `rider.0.position` 13746 vs 13778, same `trace_sha256`).
- Plan 27 Task 1: `bike_field OK`; `tools/determinism_check.sh` Debug vs
  Release `determinism OK (3 hashes)` (NAPA L0 `30df9478a6b143ff`,
  render/interpolation `636630ba4bcbcd84` both trees).
- Plan 28 Tasks 1–3: `race_actions OK`, `sdl_handles OK`; baseline BMP
  hashes identical after every task (menu
  `9270293e814f2221eeb796878e498eee`, race
  `2c702c6e1e0772abcb0031a203a6471a`, video
  `ea8622aafba24ec6485e8220ca574958`); `--show-fps` run exits 0 with
  non-empty text region; `--first-run-smoke` exit 0; 89/89 gate (Task 1),
  82 fast + soak/oracle green (Tasks 2–3).
- Open: plan 28 Task 4 manual race (pause/resume/restart) — headless only
  so far; plan 26 manual Napa play — headless only (see handoff 26-3).

## Widescreen review item 2 (October 13/14, 2026): dashboard once per frame

- `dash_bench` (Release, Napa 900 autopilot ticks, `Canvas::draw` ms/frame,
  scratch `tests/dash_bench.cpp`, not committed):
- Original 2x 16:9: 33.0 -> 6.7 ms/frame (compose once + exact index memo;
  the per-pixel 255-entry search is gone; RGB path reuses the image).
- Enhanced 2x 16:9: 61.2 -> 34.1 ms/frame (same fix; remainder is RGB
  raster + post chain — plan 29 Tasks 3–4).
- Correctness: `DashLut` is an exact per-color memo (a 32K RGB555 cell table
  was tried first and REJECTED — two colors in one cell can map to different
  indices, `hud-test` caught it); `hud` suite green, `race-soak` green.

## Plan 11a Task 2 Step 5 (October 14, 2026): dashboard pixel acceptance (partial)

- Mechanism: post-dashboard capture hook (`hook_dash` wraps the `0x00446c30`
  call at `0x0044c7b9` in `oracle/aweman_proxy.c`, dumps `-dN.raw` after it
  returns). The surface RAM is current after the GDI draw: pre/post pairs
  differ 15.8% from row 143 (dashboard band). Proof: `analysis/oracle/napa-dash-solo`
  (git-ignored), 118 dense frames (course 4, seed 1, drive 2, solo).
- Parked needles (countdown, frame 30 vs native compose at tick 30): speedo bar
  `x[205..251] y[448..450]` EXACT in both; tach `x[360..389] y[434..442]`
  original vs `y[435..442]` native — 1 px, the documented pen-width exception
  (GDI width-2 vs native 3x3 Bresenham). Origins/lengths agree.
- Tach climbs post-green in the original (tip y 434 -> 428 over frames 104–119),
  correct direction. Needle pen index switches 37 -> 52 at frame ~104 (cause
  open: redline threshold vs countdown blink).
- Distance text reads `0.003` at frame 104; native formula gives 0 at the grid.
- OPEN (blocked): moving-state angle match + distance scale. Needs SIM+PIXELS
  capture (faults after 2 frames — reproduced on the stock proxy, pre-existing)
  for tick-annotated dash frames, plus countdown alignment (original green
  ~frame 108 vs native tick 180). Native rpm trace (trace-fed
  `update_engine_rpm` over `sim-napa-solo.tsv`) recorded in the handoff.

## Plan 11a Task 3 Step 5 (October 14, 2026): mid-jump hunt (partial)

- Searched for original air in 5 captures (Napa solo/drive, CITY
  drive 2/straight/hold-left, MEDLY drive 2) via scene-dump bike `+0x20`
  scans: all grounded. New proxy drive mode 3 (accelerate + hold left) for
  crest hunting, committed in `oracle/aweman_proxy.c`.
- Hold-left CITY run reaches lateral -153600 (the native jump territory) but
  stopped against the wall (speed 0, grounded); it wall-bounces to the right
  by frame 380. Native servo to the same spot: grounded at low speed (~500),
  airborne at 3250+ — speed-dependent in both observed cases, no vertical
  divergence. Native air elsewhere is far off-line bend drift onto shoulder
  edges (CITY tick ~469+, MEDLY tick ~911+).
- OPEN: an original mid-air frame on a drive line (for the shadow-while-
  airborne node comparison); the tooling is ready when a line is found.

## October 14, 2026: car knock-off tail and integration gate

- Synthetic `car_contact` regression fails before the fix with
  `knock-off must clear contact impulses and flags`; after the fix it
  covers class-2/class-3 crash state/reason, separation, impulse clearing
  and the -1000 reverse floor. Worker focused race/car_contact/crash/traffic
  CTests pass 4/4. Current car crash vectors leave `+0xf0` unchanged, so
  those cases do not independently distinguish the steering-kick guard;
  its `local_21` condition follows original assembly.
- `npx --yes rea-agents@6.4.0 function` on `0x004049c0` and `0x004468e0`
  exits 0 with Node 24.13.0, Ghidra 12.1.4 and JDK 21. Output is retained
  under `analysis/codex-contact-20261014/`. Original EXE hash matches the
  inventory. No Wine capture or shared Ghidra import was run.
- Android debug first failed on the existing unconditional local
  `dash_bench-test` target (`RR_SOURCE_DIR` undefined). CMake now excludes
  that optional desktop target by default; the scratch source is untouched.
  Final debug and release gates pass and inspect the asset-free APKs:
  `analysis/agent-gates/codex-goal-20261014-android-debug-final/receipt.json`
  and `analysis/agent-gates/codex-goal-20261014-android-release/receipt.json`.
  Release uses local debug signing. Hardware validation was not run.
- Focused ASan/UBSan gate passes 5/5 (car_contact, hud, race, crash, traffic),
  zero skips/failures:
  `analysis/agent-gates/codex-goal-20261014-asan-final/receipt.json`. The
  first sanitizer run also passed its five tests but its receipt was
  invalidated by the CMake change; only the final receipt is a stable gate.
- The first full native run was stopped after the CMake repair invalidated
  its source snapshot; its receipt records the termination. Final native
  build and all-suite result: **95/95 pass, zero skips**, 209.41 s, plus
  viewer smoke and five-course sweep. Stable isolated checkout baseline
  `314c9a0` contains only the six intended source/test/CMake changes; its
  relevant files are byte-identical to the shared tree. Receipt copied to
  `analysis/agent-gates/codex-goal-20261014-isolated-native/receipt.json`.
  The shared-tree rerun was also stopped once another client's parser edits
  invalidated that source snapshot. These terminated runs are not passes.
  The oracle diagnostic preserves the known demo tick-222 lateral mismatch;
  it is not simulation acceptance.
- Refreshed private compilation database has no missing sources or newer
  CMake inputs. clangd LSP initialize/documentSymbol returns 15 HUD symbols
  (`analysis/codex-contact-20261014/lsp-hud-symbols.json`). clangd source
  checks pass for edited HUD files and `race.cpp` with `--tweaks=`; default
  race checks hit clangd ExtractFunction self-test errors, not compiler
  diagnostics. Shared compile database/symlink and client settings unchanged.
- Plan 10 strict player-control/full-simulation acceptance, fresh crash-car
  captures and device validation remain open. No original golden, expected
  divergence, or asset file was changed.

## October 14, 2026: MiniDash numeric speed correction

- Behavioral red: old mini composition fails `Mini speed readout changes
  with distance`. Fixed focused HUD CTest passes. Fixed expected conversion
  fixtures include 127/128, 4095/4096, 8192, 12345, reverse and signed int32
  limits in both units; these execute before disc checks. Disc-backed state
  and composition tests verify speed dependence, distance independence, and
  unchanged full-dashboard pixels across three artwork families.
- Integrated build/gates above already contain this exact HUD correction:
  isolated native 95/95, zero skips; ASan/UBSan 5/5; Android debug and release
  assembly plus asset inspection pass. No source edits followed those gates.
- REA helper spot-check and clangd source/LSP evidence are recorded above
  and in `agent-handoffs/codex-mini-speed-20261014.md`. Original dashboard
  pixel acceptance remains open; no new captures or goldens were produced.

## October 14, 2026: repaired SIM oracle and frozen-state hydration

Work performed in isolated `.codex-sim-20261014/native-port` at02d7e8d.
Existing OMP diagnostics/DLL/logs were backed up; experimental PAGE_GUARD and
timer-kill code were not incorporated into the production correction.

- Windows proxy cross-build (`oracle/build.sh`, MinGW -O2 -Wall) exits0,
  no warnings. REA6.4.0 confirms original billboard ABI. Actual wrapper
  regression compiles with -Wall -Wextra -Werror and passes4099 calls per
  wrapper, argument/snapshot/cap/reset/disabled-output checks. Removing
  forwarding, cap or reset in isolated mutations makes the regression fail.
- Wrapper and frozen-state synthetic tests pass ASan/UBSan. Frozen helper
  red test initially had17 failures; final test covers negative/zero values,
  distinct previous/current laterals, linked halves, malformed/duplicate data,
  atomic failure, human/demo gating and unchanged moving-player state.
- Focused CTest oracle-patch/oracle-actor-record/frozen_state/sim_fields4/4.
  Native configure/build pass. Full integration result:97/97 pass, zero skips/failures (216.54s),
  plus viewer smoke and five-course sweep. Receipt:
  analysis/agent-gates/codex-sim-forensics-native/receipt.json. The old demo
  replay remains exactly its known tick222 lateral divergence; diagnostic
  success is not full parity.
- `codex-sim-buffer250` exits0, contiguous70-tick trace/end, rejected because
  presented-frame limit was incorrectly used as SIM tick count. No accepted
  capture was overwritten. After fixing the limit, `codex-sim-frozen900`
  validates900 ticks1..900 (24.21s, exit0). Its control branch is unverified
  and replay requires explicit --driver=demo. `codex-sim-mask300` validates
  300ticks1..300 (18.11s, exit0), measured demo branch; forensic-only extra
  fields are retained separately in analysis/sim-forensics/mask-probe.
- New900-tick replay still reports tick210 rng849046096 vs2140064586.
  No known-divergence manifest was broadened. Follow-up measures frozen
  candidate dt4 and3840-unit contact nudges; native dt1 is the next gap.
- clangd parses the actual wrapper C test with zero errors. Frozen helper
  test clangd22 passes; full replay parsing hits known ExtractFunction
  tweak self-test errors, no C++ syntax errors; independent syntax check
  passes. No claim of interactive editor LSP integration.
- Android/device/package gates not run for these oracle/test-only changes.
  True human-control capture, full single-anchor parity and actor pixel
  acceptance remain open.

## October 15, 2026: scheduler-metadata dump (plan 10 Task 6 fix order #1c)

- Oracle MinGW cross-build (`oracle/build.sh`, -O2 -Wall) exits 0 with the
  `sched.<addr>.next/last/interval/dt` per-tick block and the three added
  slotted-rider `extra[]` words. No call site changed, so the
  `oracle-patch` preflight table is untouched.
- Native Debug configure/build in `build-verify-20261015` exits 0.
- Focused CTest `oracle-patch|oracle-actor-record|frozen_state|sim_fields|
  oracle-sim` 5/5 pass. `oracle-sim` still reports the known tick-210
  divergence on existing goldens; no golden or divergence rule changed.
- REA 6.4.0 (pinned, env per TOOLING.md) corroborates `0x00437e60` and
  `0x00438170`; see `docs/agent-handoffs/sched-metadata-trace-20261015.md`.
  Native `src/race.*` translation deferred until the `sched900` capture.

## October 15, 2026: frozen cadence translation + solo traffic isolation (plan 10)

- `sched900-20261015.tsv` (900 ticks, complete, exit 0, exe `73d7f974`,
  proxy `1b8e2357`): single-anchor `--driver=demo` replay reaches frame 375
  (tick 376), from tick 210 before. Ticks 208/209 now take the original
  3840-unit dt-4 nudges; RNG in sync throughout.
- Old goldens byte-identical: `sim-napa-solo-human` tick-210 rng pair,
  `sim-napa-solo` tick-222 lateral, `codex-sim-frozen900`/`mask300`
  tick-210 (no sched keys -> legacy path, unchanged).
- Focused `frozen_state` (new sched hydration/atomicity cases),
  `sim_fields`, `bike`, `car_contact`, `oracle-sim` pass in
  `build-verify-20261015`. Full native gate 96/97: the single failure is
  `agent-tooling` (`test_doc_checks_pass`: `00-INDEX.md: no row for
  32-completion-review-next-steps.md`), caused by an untracked plan file
  from another worker that this slice neither created nor touches. Left
  in place per shared-tree rules; reported, not blessed.
- Next: tick-376 single draw = `0x0044dd90` spawn-timer lane pick against
  the nulled pool (Task 7 timer port). REA/pseudocode reads only; no
  capture mop-up needed (sched900 already carries the evidence).

## October 16, 2026: strict acceptance on the genuine human golden (plan 10)

`verify_sim.py --mode acceptance --driver human --fixture
sched-human-20261016.tsv` (binary `build-verify-20261015/oracle-sim-test`,
receipt + `control_branch: human` + `human-controls-single-anchor`
capability all pass): `acceptance_met: false`, 1 `unexpected-failure`,
exit 1. Exact first divergence: frame 1 (tick 2) `rider.0.ai_target_x`
original 18176 native 0 (via `rider.0.brain_194`); RNG in sync
(1133891140, 1133891140). Green at tick 2 after 1 hold frame (no
attract hold on the human branch). Harness inference without CLI
override also yields `driver=human (measured player.demo_hold=0)`.
Demo diagnostic (`--mode diagnostic --driver demo`, sched900):
unexpected-failure at tick 376 (no allowlist entry; the spawn-timer
lane draw is Task 7 scope). Gate `agent_gate.py oracle` on the default
fixture still fails earlier on the old human golden's missing receipt
(infra gap predating this slice; the new golden carries its receipt).
No re-anchoring, no golden edits, no rule changes.

## October 16, 2026: human launch parity to tick 149 (plan 10)

Frame-0 brain hydration for all solo captures + player scheduler hydration
with post-drive metadata stepping. `sched-human-20261016` (human, inferred,
no override): first divergence frame 148 (tick 149) `rider.0.speed`
original 2739 native 843 -- full-throttle left-wall scrape vs the `walls`
stand-in (plan 11 Task 4 scope). Launch ramp verified word-for-word
against new `bike_240/114/260` dump keys. Regression matrix (final
binary): demo sched900 tick 376, old human tick 210, old solo tick 222,
frozen900 tick 210 -- all byte-identical to baseline. Focused
(oracle/patch/record/sim, frozen, sim_fields, bike, car_contact,
agent-tooling, race, race-actions, wiring) all pass; full build clean.

## October 16, 2026: contact routing fix, human to tick 189 (plan 10)

One-line fix (`contact()` bike-bike routes on `+0x19c & 0x40`): human
`sched-human-20261016` (inferred human, no override) now diverges at
frame 188 (tick 189) `rng original 409811144 native 1232322109` --
a 24-vs-27 terrain-jitter count gap at a ring back-crossing slice build
(same slice both sides; BORR stateless, DZHR consumption matched;
terrain iterator is the suspect). Demo sched900 still tick 376; legacy
goldens byte-identical. No golden edits, no rule changes.

## October 16, 2026: class-routing fix verified, test converted (plan 10)

`race_test` class-1 synthetic initially failed after the routing fix: its
same-spot setup never earns the 0x40 flag (move() re-syncs prev-copies, so
one live step cannot stage stale ones), proving the old expectation encoded
the geometric predicate. Converted to a class-3 glance test with live-value
expectations (f0 shove, ec cut, tail kick, impulse clear); class-1 transfer
remains covered end-to-end by the demo tick-208 replay. Full native gate
re-run in background (`/tmp/fullgate2.log`) for the commit record.

## Countdown traffic timers (October 15 continuation, plan 10 Task 7)

- Owned Debug tree: `build-agent-countdown-timers-20261015`. Configure:
  `cmake -S . -B build-agent-countdown-timers-20261015
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  -DRR_STRICT_WARNINGS=ON`: exit 0.
- `cmake --build build-agent-countdown-timers-20261015 --parallel 6`:
  exit 0, no warning/error diagnostics. Initial compile caught a private
  RNG accessor in the harness; fixed before this successful build.
- Demo `oracle-sim-test --driver=demo
  analysis/oracle/sched900-20261015.tsv`: exit 1, first mismatch tick 575
  (was 376), brain target resolver -1 vs13. Original pointer is nonnull:
  subsequent review identifies current-position identity lookup as defective.
- Human `oracle-sim-test analysis/oracle/sched-human-20261016.tsv`:
  exit 1, unchanged first divergence tick 189, RNG 409811144 vs1232322109.
  Both replays use a single anchor; goldens/diagnostic allowlist unchanged.
- LSP client channel unavailable. Owned compile database healthy, clangd
  fallback parses `src/traffic.cpp` but exits3 on19 ExtractFunction tweak
  failures (break/continue extraction); this is not a clean LSP gate.
- Independent pinned REA 6.4.0 checks `0x0044b3b0` and `0x004515a0` exit0;
  original EXE SHA-256 verified. No Wine/oracle capture was run.
- Logs: `analysis/countdown-timers-20261015/`; independent research logs
  and limits are linked from the session handoffs. Android, sanitizers,
  hardware, packaging and strict simulation acceptance were not rerun.
- `ctest --test-dir build-agent-countdown-timers-20261015
  --output-on-failure -j 4`: **97/97 passed**, zero skips/failures,
  215.31 seconds, including traffic's three new regressions, race soak,
  and unchanged oracle diagnostic signature. CTest success is not full
  simulation acceptance.

## Stable replay identity (October 15 continuation, plan 10)

- `cmake --build build-agent-countdown-timers-20261015 --parallel 6`:
  exit0, strict Debug build clean. Only test/harness code changed after
  the prior97/97 native gate.
- `ctest --test-dir build-agent-countdown-timers-20261015
  --output-on-failure -R 'frozen_state|oracle-sim|agent-tooling'`:3/3 pass.
  Added identity regressions cover moving targets, position reuse,
  unrelated actor types, absent/incomplete nodes and ambiguous identities.
- Demo single-anchor sched900 first divergence576: brain+194 original
  -16384/native29952 (RNG matched). Human unchanged189 RNG divergence.
- Worker private frozen_state build/test pass; clangd check of that test
  exits0, zero errors. No actual client LSP channel is exposed.
- Logs retained in `analysis/countdown-timers-20261015/identity-*`.
  No new capture or full simulation acceptance claim.

Identity gate correction: the first integrator frozen_state run failed on
a stale executable built before the worker moved a malformed identity-only
fixture entry after legacy hydration. The committed source had that fixture
fix. Rebuilt the complete committed source and reran the same focused gate:
3/3 pass,2.60s. The earlier failed test log was mistakenly summarized as a
pass before it was inspected; the successful rerun is the verified result.

## Player rider row identity (October 16 continuation, plan 10)

- Fresh sched600 demo capture showed the one-unit speed gap at tick 584 came
  from the original using rider table row 0 (`0x00463c78`) rather than native
  character/rider mapping's row 8: both rows select bike 1 with gear factor
  232, but row 0 gives throttle cap/ramp 9188/918/612 versus row 8's
  9160/916/610.
- `RaceConfig::player_rider` now optionally overrides the derived row
  (range -1/0..9), validated in `load_race_assets`; normal game mapping is
  unchanged when unset. Replay derives the index from recorded
  `rider.0.raw_1e0` when it lies in the 10-row table on a 0x40 boundary.
- `oracle-sim-test sched600-20261015.tsv --driver=demo`: 600 ticks
  identical. `sched900-20261015.tsv --driver=demo`: first divergence moves
  from 584 to tick 735 (brain `+0x194`).
- Focused `oracle-sim` and `race_setup` CTests pass after the change.

## Independent frozen halves (October 15 continuation, plan 10)

- Worker compiled race/frozen_state targets and passed both focused tests.
  Main strict Debug full build also exit0, no diagnostics.
- Same sched900 demo single-anchor replay: exit1 at584, speed6188 vs6187;
  RNG matched through the gap. Human replay unchanged189 RNG mismatch.
- Worker LSP fallback: exit3 on38 existing ExtractFunction feature-check
  failures, no source parse diagnostic; not claimed as clean LSP gate.
- Owned-tree `ctest --test-dir build-agent-countdown-timers-20261015
  --output-on-failure -j 4`: **97/97 pass**, zero skips/failures,148.94s.
  Build source SHA-256:07ca00f9541668cbfe20dd5b87bf64ed040ee8b0793197e22aa9b07062eda9ae.
- Logs under `analysis/countdown-timers-20261015/half-*`. No new original
  capture, Android/device/package or strict acceptance run in this slice.

## Seeded terrain original component fixture (October 15 continuation)

- Main strict Debug full build: exit0, no diagnostics. Main
  `./oracle/build.sh`: exit0, no MinGW warnings; installed proxy hash
  d9c5b49d89308fb5a781b8e49efb413bc50a24d78548b623dbfd9a19b47ed041.
- `RR_ORACLE_TERRAIN_SEEDED=1 RR_ORACLE_COURSE=4 RR_ORACLE_SIM=0
  RR_ORACLE_FRAMES=1 ... oracle_capture.py capture terrain-seeded-20261015`:
  exit0, one complete frame, original EXE hash verified, clean process exit.
  Trace SHA25634a09c663a18bf614abac23c294703e50081a60a481acc89908163fe96a63b70;
  immutable capture/receipt under analysis/oracle/terrain-seeded-20261015.captures.
  The initial attempt without course bootstrap stayed in menus; interrupted
  only that owned capture, wrapper rejected it, no accepted fixture replaced.
- `terrain-test analysis/oracle/terrain-seeded-20261015.tsv`: exit0,
  all960 hidden-state snapshots/18 terminal RNG states exact; existing864
  original slices and109 course terrain sections also pass. Native worker
  clangd terrain_test.cpp check exits0, zero errors.
- Default-mode regression capture `sched600-20261015`:600 contiguous demo
  ticks, original hash verified, clean exit0, trace SHA256
  584603378bd5980fd0209291021b52a8e3378a24f33d4cd4e220bd16256fcf45.
  Replay single anchor exit1 at584 speed6188 vs6187, RNG matched. Original
  bike240=7352,bike114=26 at584 narrows the native7330/25 hidden-force gap.
- Restored installed proxy through oracle_install.py: hash
  1eb5a4c648f4da876aad94b66d2522a7680cb831396225781d068310701c499d.
  No other process/prefix mapping modified. Existing goldens and allowlist
  unchanged; new captures remain local and uncommitted.
- Full main CTest: **97/97 pass**, zero skips/failures,149.74s. Seeded fixture parity
  is component evidence, not strict simulation acceptance. No Android,
  device, packaging or sanitizer gate in this slice.

## October 15 — plan32 screen/canvas and plan10 NULL-slice integration

- Owned `build-agent-codex-plan32-20261015` configured Debug,
  `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, `RR_STRICT_WARNINGS=ON`; full
  `cmake --build ... --parallel6` exits0. Presentation final native CTest
 99/99 passed173.89s; after NULL-slice source integration full build0 and
 `ctest --test-dir ... --output-on-failure -j4`99/99 passed151.17s, zero skips.
  Logs `/tmp/rr-codex-plan32-final-{build,ctest}.log` and
  `/tmp/rr-codex-sim735-main-{build,ctest}.log`. Initial presentation gate
  98/99 failed the new screen harness; fixture timing and real lost-menu-Resume
  bug fixed, full gate rerun green.
- Screen focused native4/4; ASan/UBSan4/4 (10.70s) and canvas1/1(49.86s),
  no diagnostics. Apple LeakSanitizer unavailable (`detect_leaks=0`); no leak
  acceptance claim. Real SDL software/dummy smoke includes pause/reset/restart
  and30/60/120FPS frame/bike/RNG invariance. Hardware/device reset unobserved.
- Native canvas six RGB and six Original no-HUD hashes exact before/after,
  one/four threads equal, full snapshot unchanged; all opaque indexed HUD
  pixels now preserved. Bounded complete-draw benchmark medians improve13–24%
  on M3Pro software renderer; not controlled cold-cache/device/memory evidence.
  Full methods/results in performance handoff.
- `oracle-sim-test sched600-20261015.tsv --driver=demo`: exit0,600ticks
  identical in compared fields, with uncovered field eras reported explicitly.
  Same sched900 single-anchor replay exits1 first893 RNG707629878/2897718112
  (previous735 brain5632/22784); source guard progress, not final parity.
- Strict `verify_sim.py --mode acceptance --driver human` against valid
  terrain-human200 receipt exits1, acceptance_met=false: frame0/tick1 front
  terrain point0.end132/133. No fixture/allowlist weakened or RNG reanchored.
  Report `/tmp/rr-codex-plan32-human-acceptance.json`.
- Refreshed owned CDB, no client LSP endpoint: fallback clangd
  `--check=... --compile-commands-dir=build-agent-codex-plan32-20261015 --tweaks=none`
  exits0, zero errors for race.cpp/race_setup.cpp and cache/setup tests. Screen
  worker screen/view/test parses also zero errors. Default Apple developer-tweak
  failures documented separately, not disguised as source diagnostics.
- No new original captures/proxy installation/Wine state mutation, Android,
  package, hardware or new strict-pass claim. REA6.4.0 scan evidence and exact
  commands in [research](agent-handoffs/codex-sim-research-20261015.md);
  [integration](agent-handoffs/codex-sim735-integrate-20261015.md),
  [screen](agent-handoffs/codex-screen-20261015.md),
  [performance](agent-handoffs/codex-performance-20261015.md).

## Race countdown banner (October 16, 2026)

- `cmake --build build-agent-overlay-20261016 --parallel`: exit 0.
- `ctest --test-dir build-agent-overlay-20261016 --output-on-failure -j8`:
  99/99 passed, no skips reported.
- `hud-test` covers banner timing (180 start frame without dashboard, digits
  3/2/1 at 179..120/119..60/59..1, live at 0) and the digit raster.
- Visual: `rr-game --ticks 30` and `--ticks 200` screenshots (scratch).
  Not a pixel comparison against an original capture.
  [Handoff](agent-handoffs/race-banners-20261016.md).
- Follow-up (fades + low metrics): full build exit 0; `ctest` 99/99.
  `hud-test` pins fade-in/out channel maps and the low-mode raster;
  `race-screen` walks a restart through the blocking fade-in (updated
  expectation: the countdown now holds for 64 × 14 ms after restart).
- Follow-up 2 (messages/quit/pixel check): full build exit 0; `ctest`
  99/99. Original pixel comparison against the local `napa-dash-solo` capture
  (post-dash d2/d36/d70): white overlap "3" 1096/1315, "2" 1167/1322, "1"
  746/750; shadow "1" 162/162. Palette dimming/fades are not covered: the
  capture's palette field is all zeros.

## Race palette uploads vs native (October 16, 2026)

- Proxy: `oracle/build.sh` + `tools/oracle_install.py` (proxy sha256
  `48f9bdf5…`, new options env-gated; existing captures unchanged).
- Capture: `RR_ORACLE_PALETTE=1 RR_ORACLE_EXIT_FADEOUT=1 RR_ORACLE_SOLO=1
  RR_ORACLE_COURSE=4 RR_ORACLE_DRIVE=2 RR_ORACLE_SEED=1
  RR_ORACLE_KEYS="220:19:1,250:65:0,280:27:1,310:27:1"
  oracle/run.sh napa-palette 240`: exit 0, 311 frames, ends inside the
  fade-out's last upload. `tools/oracle_capture.py validate
  analysis/oracle/napa-palette.tsv`: complete, branch `unverified`
  (attract bootstrap). Companion `napa-palette-palette.raw` (130 uploads),
  read with `tools/oracle_palette.py`. Local and git-ignored.
- Keys are posted as WM_KEYDOWN/WM_KEYUP after the named frame. The third
  field writes game mode `0x004753b0` = 1 before Pause and the two Escapes
  (and 0 with the resume key), because the bootstrap's mode 0 ignores Pause
  and skips the quit prompt. This is a diagnostic override: one stepper
  call may run in mode 1 before the wndproc dispatches the key; it covers
  palette presentation only, not simulation.
- `hud-test` (capture-gated): native `fade_in_lut(n)`, `fade_out_lut(n)`,
  `half_palette_lut()` and `identity_lut()` applied to native
  `PALETTE.RAW` vs every upload: **0 of 33,280 entries differ** (130
  uploads: 63 fade-in, countdown expiry full, pause half, resume full,
  quit-prompt half, 63 fade-out); upload order and the fade counters match;
  countdown expiry happens with `0x4753c0` = 0, matching
  `countdown_banner(0,180).dim == false`; fade spans 867/867 ms
  (62 × 14 = 868). Full build exit 0; `ctest` 99/99 passed.
- Not covered: the palette in effect for race frames 0–1 before the fade-in,
  `0x41642d` (no-fade start branch) and `0x416667` (`0x482760`) uploads, which
  this bootstrap never reaches; native timing of the fade relative to frames
  (only the original's wall-clock spacing is checked); human-mode Escape and
  Pause without the mode override.

## Mountain/cliff terrain sides (October 16, 2026)

- Reproduction: `rr-game --course 3 --autopilot --ticks 900 --screenshot`
  (CANYN) with `widescreen=0` and `=1` (`RR_PREF_OVERRIDE` copies of the
  owner's settings): before the fix, 51,200 / 43,008 black pixels in the
  region beside the road (rows 62–72%, left 20%); after, 0 / 0. Napa and
  MEDLY start frames were unaffected before (no mountain at their starts).
- Original: `RR_ORACLE_COURSE=5 RR_ORACLE_DRIVE=2 RR_ORACLE_SEED=1
  RR_ORACLE_PIXELS=1 RR_ORACLE_SHOT_EVERY=60 RR_ORACLE_FRAMES=420
  oracle/run.sh pch-terrain-20261016 400`: exit 0, 420 frames, proxy
  `48f9bdf5…`, capture accepted. Shots f0..f360 show the mountain wall left,
  water right and no index-0 pixel below row 320. Native race frames have the
  same layout: visual comparison only, no pixel or display-list match.
- `scene-test`: new synthetic cases for `wall_draw_nodes` (wall face points
  and one-column shift, plateau tiles and their stop, depth 0x3d skip, hill
  neighbour, right/left water, left-strip off-screen rule): 0 failures.
- `course_scene-test`: new check renders every 11th route sample whose camera
  sample is a mountain sample at 320 and 426 columns. HIWAY 40 views (worst
  26 index-0 pixels below the horizon), MEDLY 36 (0), CANYN 566 (16); none
  above 1%. Before the shoulder outer-edge fix HIWAY had 17 failing views
  (worst 37,750). CITY/NAPA widescreen centring unchanged (0 not at +53).
- Gate: `cmake --build build-agent-pch-terrain-20261016 --parallel` exit 0;
  `ctest --test-dir build-agent-pch-terrain-20261016` 99/99 passed. No
  simulation or RNG code changed (`PathSample::elevation` is read only by the
  scene).
- Not covered: type 3 split sides, building→mountain transitions, scene
  display-list comparison against the capture's `-scene*.raw` dumps.

## Tunnels and building-to-mountain walls (October 16, 2026)

- Original: `RR_ORACLE_COURSE=5 RR_ORACLE_DRIVE=2 RR_ORACLE_SEED=1
  RR_ORACLE_PIXELS=1 RR_ORACLE_SHOT_EVERY=30 RR_ORACLE_FRAMES=3600
  oracle/run.sh pch-tunnel-20261016 900`: exit 0, 3600 frames, proxy
  `48f9bdf5…`, accepted. Approach f2910-2970, inside f3000-3150, exit f3180,
  second tunnel approach f3480-3570.
- Native course views (CANYN 372/378/382/395/410/411) and `rr-game --course 3
  --autopilot --ticks 3000` at 16:10 show the portal trim, mountain face,
  lit arch, railed sidewalks and exit as in those frames. Visual comparison
  only. From sample 412 on, at the exit, the original's own visibility tests
  drop the arch panels as the camera nears the last tunnel sample.
- `scene-test`: synthetic cases for the arch (backdrop, panel points, roof
  midpoints, forced flips, material order, null material stops, depth gate),
  portal (12 nodes, face start, 1.25-column trim, curb, materials chosen
  once), tunnel wall/cliff branches, camera-in-tunnel skip, building facade
  neighbour and `fixed_multiply`: 0 failures.
- `course_scene-test`: views from inside a tunnel with 8 tunnel samples
  ahead, both branches: MEDLY 51, CANYN 242, none with over 5% of the top 160
  rows equal to the bare horizon (worst 1774 / 1946 pixels). Without the
  arch every such view shows sky. Mountain clear-colour check unchanged.
- Gate: full build exit 0, `ctest` 99/99 passed. Simulation untouched:
  tunnel state is separate from `TerrainSlice`.

## Native terrain construction vs original frames (October 16, 2026)

- `oracle-scene-test --native-terrain <capture.tsv>` (ctest `oracle-terrain`
  on `pch-tunnel-20261016`, skips without it): per scene frame, loads the
  dumped sub-slice terrain values into `CourseScene`, projects the road from
  `camera.position/x/height`, and matches every original display node whose
  texture is a road CEL, WATER128 or a family category 1/3/5/6 group by name,
  Q16 points and flips. Also checks rider ground `+0x24` against
  `terrain_height` on the same sub-slices.
- Results: `pch-tunnel-20261016` 64 frames, 3347 terrain nodes match, 0
  missing (walls 351, arch 277, tunnel curbs 149, shoulders 1220, water 192,
  guard rails 334, road 669), 2096 rider grounds equal (type 1: 1852, type 3:
  244 incl. 8 on the sidewalk). `napa-drive` 123/0, `city-scene` 517/0,
  `city-moving` 1006/0. `medly-jump` 6407 match, 49 missing, 7 grounds differ.
  The same per-texture MEDLY results come from the pre-session baseline
  9a77854 (separate worktree build), so these are older building-path gaps.
- Without injection the walls differ only by jitter: the original's
  elevation, edges and heights per sample equal native; its wall offsets come
  from the live race random stream.
- Not covered: scenery billboards/signs/trees and actors (reported, 255 nodes
  in the tunnel capture), pre-flush nodes (the dump holds the list tail),
  type-1 off-road ground (no rider left the road on a mountain side; covered
  by `terrain_ground-test` synthetic cases).
- Gate: full build exit 0; ctest 100/100 (new `oracle-terrain` ran, 48 s;
  `oracle-sim` passed). rr-game `--course 3 --autopilot` 16:10 screenshots at
  600/2700/2900 ticks show the guard rail along the water.

## Side streets and building ends (October 16, 2026)

- `oracle-scene-test --native-terrain` with mip-0 hash identity:
  `medly-jump` 78 frames, 6456/6456 terrain nodes (was 6407 by name, 49
  missing; all MEDLY differences reproduced at baseline 9a77854 before the
  fix); `city-moving` 1006/1006 (26 end faces were a different cell before
  `0x0044a9b0`); `city-scene` 517/517; `napa-drive` 123/123;
  `pch-tunnel-20261016` 3347/3347 with 2096 rider grounds equal.
- New ctest `oracle-terrain-medly` (`--nodes-only`: grounds reported, not
  gated; 7 of 3244 differ in the jump capture).
- Full build exit 0; ctest 100/100 before adding the new test, both
  oracle-terrain tests pass after (52 s for MEDLY).

## Walls and rails (October 16, 2026)

- `race-test`: CANYN live setup, 400 ticks, then the player placed 400
  units past the cliff foot and AI rider 1 past the outer left shoulder;
  after one step both laterals equal the `0x0043a610` values exactly
  (player 322304 = cliff foot·256 − half-width; AI at +0xe8·256 + width).
- No original wall-hit comparison yet: the Napa human replay
  (`sched-human-20261016`) still diverges first at tick 189 (terrain RNG),
  before any wall contact; the CANYN SIM replay (`canyn-sim-20261016`,
  pre 4231 / post 0) anchors but diverges at tick 2 (demo start-hold
  calibration). `oracle-sim` pinned diagnostics unchanged.
- rr-game `--course 3/2 --autopilot --ticks 4000` 16:10: both races run,
  player not wrecked.
- Gate: full build exit 0; ctest 101/101.

## Ring setup window walks (October 16, 2026)

- `build-agent-ring-terrain-20261016/ring-probe
  analysis/oracle/terrain-human200-20261015.tsv ../cdrom/ROADRASH 20000`:
  walker terrain matches at K = 11,428..11,476 (8 candidates, all 16 after);
  K = 11,461 steps back to state 1 = srand(1), so pre draws 0.
- `oracle-sim-test analysis/oracle/terrain-human200-20261015.tsv`: exit 0,
  200 ticks identical, closure +16.
- `oracle-sim-test analysis/oracle/sched-human-20261016.tsv`: first
  divergence frame 370 (tick 371) `rider.0.velocity` original 40 native
  296; RNG equal through 371 (was tick 189 RNG).
- `--driver=demo` sched600-20261015: 600 ticks identical; sched900-20261015:
  first divergence tick 893 RNG (unchanged).
- `sim-napa-solo` / `sim-napa-solo-human` outputs equal the pinned outputs
  except `(+11 draws)` -> `(+16 draws)` (checked by hashing the substituted
  text against the old `output_sha256`); hashes re-pinned in
  `tools/sim_known_divergences.json`, divergence lines unchanged.
- `track_ring` test: new synthetic split/merge course; window walks cost
  80×12 + 81×12 wall draws; a 60-sample branch reports unsupported.
- Gate: full Debug build exit 0; ctest 101/101 after re-pinning (first
  run 100/101 with `oracle-sim` failing only on the closure-line hash). A
  later `-j8` run had `render-threads-alloc` fail once under load (renderer
  code untouched); it passed alone 4/4. `tools/doc_checks.py` OK.

## Ring-built terrain slices (October 16, 2026)

- `oracle-sim-test analysis/oracle/sched-human-20261016.tsv`: first
  divergence frame 701 (tick 702) rng original 2400785084 native
  2517057202 (was tick 371 `rider.0.velocity`).
- `--driver=demo` sched600: 600 identical; sched900: tick 893 (unchanged);
  terrain-human200: 200 identical.
- `rr-game --autopilot --ticks 300|900 --screenshot`, HEAD vs change:
  only hill silhouettes differ.
- `race-cache`: all six RGB and six Original-scene hashes re-recorded
  (owner-approved, October 16); other checks (thread equality, dashboard
  pixels, horizon) unchanged and passing; passes twice in a row.
- `track_ring`: setup exports 128 wall slices through the slice sink.
- Gate: full Debug build exit 0; ctest 101/101.

## Solid roadside objects (October 16, 2026)

- Disc survey of every FAMILIES.RSC RRAN frame with a collision pointer:
  frame `+0x10` = box count, 75 × 0, 3,433 × 1, 737 × 2 boxes.
- `oracle-sim-test analysis/oracle/sched-human-20261016.tsv`: exit 0,
  `1431 ticks identical` (was tick 702 RNG).
- `--driver=demo` sched600: 600 identical; sched900: tick 893 RNG
  (unchanged); terrain-human200: 200 identical.
- `tools/verify_sim.py --mode acceptance --driver=human --fixture
  analysis/oracle/sched-human-20261016.tsv`: unexpected-failure, solely
  because the output carries the coverage note (264 golden-only fields);
  receipt, human branch and capability checks pass.
- `race` test: off-road BORR slot solid (speed, damage, push), box-less
  frame inert, ×3 width for a one-box slot at |lateral| ≥ 0x400.
- Gate: full Debug build exit 0; ctest 101/101.

## Strict replay coverage, first pass (October 16, 2026)

- `oracle-sim-test analysis/oracle/sched-human-20261016.tsv --coverage`:
  1431 ticks identical; 174 golden-only keys not compared (was 264), 14,300
  uncovered native comparisons (was 203,060: 10 rider fields × 1430).
- sched600 600 identical, sched900 tick 893 (unchanged), terrain-human200
  200 identical; `oracle-sim` and `frozen_state` ctests pass.
- Strict `verify_sim.py --mode acceptance` still unmet (coverage note
  printed).

## Rider half, standings, finish line (October 17, 2026)

- `sched-human-20261016 --coverage`: 1431 identical; 87 golden-only not
  compared (node list), 70 verified invariant, 14,300 uncovered (10 fields).
- `race_bounds` for every original course and level returns a finish
  (CITY/HIWAY/MEDLY/NAPA/CANYN L0..L4); Napa L0 439040 equals the capture's
  `+0x1f8 + position` on every tick.
- sched600 600 identical, sched900 893 (unchanged). ctest 101/101.

## Animation, nitro, stamina coverage (October 17, 2026)

- Capture: `RR_ORACLE_COURSE=4 SEED=1 SIM=1 SOLO=1 HUMAN=1 FRAMES=1431
  INPUT=C:\napa-human.controls tools/oracle_capture.py capture
  sched-human-cov2-20261017` (proxy `53322b8a…`), exit 0, 1431 frames,
  branch human, `validate --simulation` complete. Keys shared with
  `sched-human-20261016`: equal except `+0x44`/rpm at ticks 1197 and 1220.
- `oracle-sim-test analysis/oracle/sched-human-cov2-20261017.tsv
  --coverage`: 1431 identical; 87 golden-only, 70 invariant, 0 uncovered.
- `sched-human-20261016` 1431 identical; sched600 600; sched900 893;
  terrain-human200 200. ctest 101/101.

## Scheduler node list, parking, pedestrian freeze (October 17, 2026)

- `oracle-sim-test --driver=human analysis/oracle/sched-human-cov2-20261017.tsv`:
  exit 0, `1431 ticks identical`; the coverage note is gone (0 golden-only,
  70 invariant, 0 uncovered).
- `tools/verify_sim.py --mode acceptance --driver=human --fixture
  analysis/oracle/sched-human-cov2-20261017.tsv`: exit 0, `acceptance_met:
  true` (receipt, human branch, capability and match all pass).
- `oracle-sim-test --driver=human ... --coverage`: 1431 identical; 70
  verified-invariant original words (rider-half own words, the unused human
  brain window), 0 golden-only, 0 uncovered.
- `tools/verify_sim.py --mode diagnostic`: `sim-napa-solo.tsv`
  known-divergence (unchanged pinned output; the node-list comparison is
  gated on a declared `probe.frozen_words` capture).
- `sched-human-20261016` 1431 identical; sched600 600 (demo); sched900 893
  (demo); terrain-human200 200.
- Gate: full Debug build exit 0; ctest 101/101.

## Contact crash-vector axis fix (October 17, 2026)

- `cmake -S . -B build-agent-crashvec-20261017 -DCMAKE_BUILD_TYPE=Debug`;
  full build exit 0; `ctest --test-dir build-agent-crashvec-20261017 -j 8`:
  100% tests passed, 0 failed out of 101.
- `race-test` exit 0, `0 failures`; the new check
  `contact crash impulse pair is the traced 0x404eeb local_18` passes. It was
  verified to fail with the pre-fix pair `{0,0x200,0}` (`1` FAIL line), then
  re-run green with the fix.
- `crash-test` exit 0; the new check prints `contact crash impulse pair
  lands on the vertical velocity (+0xf4)`. It was verified to fail with the
  pre-fix pair (`contact impulse leaked onto the steer rate`), then re-run
  green.
- `BUILD_DIR=build-agent-crashvec-20261017 make verify-sim`: exit 0,
  `acceptance_met: true`, `1431 ticks identical` on the human capture (the
  fix only touches the class-1/2 big-gap crash, which the solo replay never
  reaches). Gate receipt
  `analysis/agent-gates/oracle-12060ac09aff4f2093f2f84ec47d9bb5/receipt.json`.

## Setup-object cadence (October 17, 2026)

- Before: sched600/sched900 `--driver=demo` first divergence tick 480
  (node list: controller and signals still listed in the original until 482).
- After: sched600 600 identical; sched900 tick 893 RNG (unchanged);
  sched-human-cov2-20261017 bare `1431 ticks identical`; verify_sim
  acceptance exit 0; ctest 101/101 with race-cache re-recorded (tick-300
  frame: one pedestrian pose differs, checked against a clean HEAD build).

## Rider build, no verified invariants (October 17, 2026)

- Build `build-agent-ring-terrain-20261016` (Debug): full build exit 0,
  `ctest` 101/101.
- `oracle-sim-test --driver=human --coverage
  analysis/oracle/sched-human-cov2-20261017.tsv`: `ring setup closes on
  the green anchor (+0 draws)`, bare `1431 ticks identical`, no invariant,
  golden-only or uncovered lines (before: 70 verified invariants).
- `tools/verify_sim.py --mode acceptance`: exit 0, acceptance_met true.
- sched600 `--driver=demo` 600 identical; sched900 tick 893 RNG
  (unchanged); sched-human-20261016 1431 identical, 0 golden-only;
  terrain-human200 200 identical.
- `tools/verify_sim.py --mode diagnostic`: sim-napa-solo.tsv re-pinned
  (tick 222 lateral → tick 210 RNG, identical to sim-napa-solo-human.tsv);
  sched600 shares its RNG at ticks 209/210/222 and passes them with frozen
  words hydrated. sim-napa-solo-human.tsv: hash re-pinned, only the closure
  line changed (+16 → +0).
- race-cache: all 12 hashes re-recorded. Tick-300 frames from HEAD (e00b508,
  isolated worktree) and this build compared by eye: same scene, player on
  its spawn target and opponents in their drawn lanes, none off the road.
- New `race-test` checks: lane draw order and name roll, spawn state/target
  placement/replan/base speed/wreck timers, half object image.

## Live-field capture napa-field-l1-20261017 (October 17, 2026; plan 10 Task 6 Step 1)

- Capture: `RR_ORACLE_COURSE=4 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1
  RR_ORACLE_DRIVE=2 RR_ORACLE_FRAMES=2400 tools/oracle_capture.py capture
  napa-field-l1-20261017` (no `RR_ORACLE_SOLO`): exit 0, `validate
  --simulation` complete, 2400 contiguous ticks, control branch demo, trace
  `99e20344…`; receipt under `analysis/oracle/napa-field-l1-20261017.captures/`
  (git-ignored). Live opponents, traffic and pedestrians.
- `oracle-sim-test --driver=demo`: with the offline field of 14 opponents
  (harness now sets `config.opponents = 14`, `DAT_004642d4 = 0x10`) the ring
  plus rider build closes on the green anchor with **+0 draws**, so the
  ported 0x00405600 draws hold for a live field (7 opponents left it 7 draws
  short). The replay then stops at frame 5 (`rider.0.speed`) on harness
  assumptions, not simulation: in a field capture `rider.N` slots follow the
  scheduler list order (rider.0 is the frontmost bike, the player half is
  rider.12), and the hold is 5 frames. Non-solo slot identity mapping is the
  first Task 6 harness step.

## Live rider scheduler and field replay (October 17, 2026; sched-20261017)

- Build `build-sched-20261017` (Debug): full build exit 0;
  `ctest --test-dir build-sched-20261017 -j 8 --output-on-failure`:
  **103/103 pass**, 0 skips (incl. `oracle-sim-field`, `race_schedule`, the
  re-recorded `race-cache`), 161 s wall.
- `make -C build-sched-20261017 verify-sim` (strict acceptance,
  `sched-human-cov2-20261017`, driver human): exit 0, `acceptance_met:
  true`, compare 0. Replay prints `ring setup closes on the green anchor
  (+0 draws)` and `1431 ticks identical` — the live default
  (`RaceConfig::live_schedule`) leaves the solo path unchanged.
- Field known-divergence replay: `oracle-sim-test --driver=demo
  analysis/oracle/napa-field-l1-20261017.tsv` exits 1 with the pinned
  output — `countdown: 5 native ticks equal` (5-frame hold, green at tick
  6), every compared rider/brain/half/sched/node-list field identical
  through tick 226, first divergence frame 226 (tick 227) node list: the
  free car spawned on tick 226 moved in the original (dt 11) while native
  cars still step every tick (task 7 scope). Output sha256
  `ecf24cab48261975ac400163848e7a33cfa8b4253e72ddcca06abc231f731663`
  matches the `tools/sim_known_divergences.json` pin (trace `99e20344…`);
  `verify_sim.py --mode diagnostic --fixture …` classifies it
  `known-divergence` and the `oracle-sim-field` ctest passes (exit 77 skip
  without the git-ignored capture).
- Diagnostic field run (`RR_FIELD_IGNORE_CARS=1 RR_FIELD_IGNORE_PEDS=1
  RR_FIELD_CARS_FROM_GOLDEN=1`): first divergence frame 624 (tick 625)
  `rider.1.lateral` original 9296 native -5722 (native rider 14) — compared
  past the tick-337 parking; the tick-765 wake is measured on the capture,
  not yet replayed.
- `race_schedule-test`: 0 failures; pins the rules against the captured
  numbers (interval LOD 8/7/6/5 at tick 1 and the 1/30 cases, pace
  8440/9329, band 15/30/60 with floor/cap, tie order later-rider-first /
  half-before-bike, tick-1 dt 2/5, tick-9 dt 8).
- race-cache: RGB and original-scene hashes for scale 2/4 re-recorded
  (scale-1 entries unchanged — the old values still pass, localizing the
  difference to opponent positions under the live cadences). The re-run
  test and the ctest both pass with the new values.
- `race-test`: CANYN wall check now takes the first live opponent and
  steps to its scheduled move (parked bikes are not contact candidates);
  0 failures. `oracle-sim` diagnostic on `sim-napa-solo.tsv`: unchanged.



### Car cadence / live-field continuation — October 17, 2026

Baseline `70a8c04`; see [handoff](agent-handoffs/car-cadence-20261017.md).
Final source validation (no capture mutation):

- Native gate: exit 0, 103/103 CTests passed, no skips; viewer smoke and
  all-course sweep passed; `CTEST_PARALLEL_LEVEL=4 python3 tools/agent_gate.py
  native --build-dir build-agent-car-cadence-20261017 --output
  analysis/agent-gates/car-cadence-20261017-final-v4 --jobs 8`.
- ASan/UBSan focused gate: exit 0, 4/4 passed, no skips; regex
  `^(traffic|car_contact|race_schedule|oracle-sim-field)$`,
  `analysis/agent-gates/car-cadence-20261017-sanitizers-v2/receipt.json`.
- `python3 tools/verify_sim.py --mode acceptance --binary
  build-agent-car-cadence-20261017/oracle-sim-test --report
  analysis/agent-gates/car-cadence-20261017-final-v4/human-acceptance.json`: exit 0,
  `acceptance_met true`, `1431 ticks identical` (human, one anchor).
- Field diagnostic compares countdown + ticks through 307 with RNG and car
  position/lateral/speed equal. Tick 227 fixed (dt 11, post-drag speed 231,
  position +12); first remaining mismatch at 308 is pedestrian 13604/13605.
  Unchanged trace `99e20344…`; no hydration or exclusions. Known-divergence
  diagnostic only, not full field acceptance.
- Owned compilation DB healthy; clangd 22.1.4 full AST parse, token feature
  range `--check-lines=1-1`: exit 0, 0 errors. Full token-feature sweep has
  tool ExtractFunction tweak failures; no client LSP channel available.

Earlier iteration failures and the interrupted source-changing gate are
superseded by final native v4 / sanitizer v2 receipts, not counted as passing
checks. Native v2 passed 102 runtime tests and failed one documentation-link
check; the plan-relative handoff link is corrected for v3. Android,
device and standalone packaging were not run for this simulation slice.

The v3 native run passed all simulation checks but the unchanged
`render-threads-alloc` test transiently counted 4 steady-state allocations.
Ten isolated repetitions then passed with its original zero-allocation
threshold. The final v4 native run uses `CTEST_PARALLEL_LEVEL=4`; no renderer
or allocation-test source was changed.


### Live scheduler/contact continuation — October 17, 2026 (ped308)

Baseline `f987c86`; [handoff](agent-handoffs/ped308-20261017.md).

- Native final gate: passed, full build + 103/103 CTests, 0 skips, viewer
  smoke and all-course sweep; `CTEST_PARALLEL_LEVEL=4 python3
  tools/agent_gate.py native --build-dir build-agent-ped308-20261017
  --output analysis/agent-gates/ped308-20261017-native-final --jobs 8`.
- ASan/UBSan final gate: passed 5/5, 0 skips, focused
  `^(pedestrian|traffic|car_contact|race_schedule|oracle-sim-field)$`;
  reused owned sanitizer build `build-agent-car-cadence-asan-20261017`,
  receipt `analysis/agent-gates/ped308-20261017-sanitizers-final/receipt.json`.
- Strict human acceptance: the first iteration caught frozen-node lifetime
  drift at 1382. Restoring its measured nulled-callback removal contract
  returns exit 0, `1431 ticks identical`, `acceptance_met true`; final report
  `analysis/agent-gates/ped308-20261017-evidence/human-final.json`.
- Focused pedestrian/traffic/contact tests: 3/3 passed, including the
  pre-driver camera boundary, type-0x20 vertical reset, deferred live removal,
  immediate frozen removal, ascending outer-node gates and split scheduler /
  driver phase (no duplicate car move).
- Field replay: countdown 5 + ticks 6..787 compare equal with RNG and car
  motion triples, through the 765 wake. First mismatch at 788 is RNG
  original 2280515322 / native 34387072. This is an exact pinned diagnostic,
  not acceptance. Trace SHA-256 `99e20344…` unchanged. Evidence logs:
  `analysis/agent-gates/ped308-20261017-evidence/`.
- No client LSP channel: owned compile DB healthy; clangd 22.1.4 full AST
  checks with token feature range `--check-lines=1-1` return 0 errors on
  race, scheduler, traffic and pedestrian simulation files.

The first native/sanitizer receipts precede the frozen-lifetime correction
and are superseded by final receipts. No Android, device, packaging, Wine
capture or Ghidra writes in this slice.


## Full Napa live-field parity (October 18, 2026)

Baseline `992106d`; final source fingerprint
`8718e6d4f13bd5dcfb35df9bd5a9009657dc25e8950e519d35ee53bc5e5d4117`.
All gates used the frozen source below; documentation changes are outside
source identity. Disc EXE hash rechecked against inventory.

- **Full native gate:**
  `CTEST_PARALLEL_LEVEL=4 python3 tools/agent_gate.py native --build-dir build-agent-ped308-20261017 --output analysis/agent-gates/field-parity-20261018-native --jobs 8 --session field-parity-20261018-native`.
  Passed **104/104**, zero skips/failures, five steps exit0; viewer imported
  asset smoke and all-course sweep passed. Receipt:
  `analysis/agent-gates/field-parity-20261018-native/receipt.json`.
- **ASan/UBSan:**
  `CTEST_PARALLEL_LEVEL=2 python3 tools/agent_gate.py sanitizers --build-dir build-agent-car-cadence-asan-20261017 --output analysis/agent-gates/field-parity-20261018-sanitizers --jobs 8 --tests '^(race|race_schedule|car_contact|traffic|pedestrian|actor_animation|actor_descriptors|sim_fields|oracle-sim-field|oracle-sim-field-schema2)$' --session field-parity-20261018-sanitizers`.
  Passed **10/10**, zero skips/failures, all three steps exit0. Receipt:
  `analysis/agent-gates/field-parity-20261018-sanitizers/receipt.json`.
- **Strict human acceptance:**
  `python3 tools/verify_sim.py --mode acceptance --binary build-agent-ped308-20261017/oracle-sim-test --report analysis/agent-gates/field-parity-20261018-human-final.json`.
  **1431 identical**, acceptance_met true, no skips or known divergences.
- **Complete live field:** both `oracle-sim-field` and new
  `oracle-sim-field-schema2` pass **2400 ticks identical** in Debug and
  sanitizers, RNG anchored once after the native countdown. Reports:
  `build-agent-ped308-20261017/oracle-field-diagnostic.json` and
  `build-agent-ped308-20261017/oracle-field-schema2-diagnostic.json`.
  The legacy capture explicitly reports its four invalid car metadata
  aliases; schema2 compares their proper fields plus every AI animation and
  all30 relationship words, with no golden-only state keys. Hashes/recipe:
  SIM_SCENARIOS. No accepted golden edit, reanchoring or live hydration.
- **Focused regressions:** race, car_contact, traffic, actor_descriptors,
  sim_fields passed5/5 before integration. New cases cover descriptor-only /
  frame2 / no-event combat hits, relationship event4, original descriptor
  flags, parked-target clearing, type3/4/5 marking with speed gate and
  same-sample lifetime, car response flags/crossing pause and deferred car
  unlinking including duplicate same-tick scheduler calls.
- **C++ tooling:** client LSP unavailable. `agent_lsp.py` compile-database
  preflight healthy187 entries. LLVM clangd full AST checks passed0 errors
  for race.cpp, race_schedule.cpp, traffic.cpp, actor_descriptors.cpp and
  oracle_sim_test.cpp using `--check-lines=1-1` to avoid the unrelated full
  feature-check ExtractFunction failure. Logs:
  `analysis/agent-gates/field-parity-20261018-evidence/*-clangd.log`.

Known diagnostics remain separate: the older `sim-napa-solo.tsv` fixture
still has its pinned tick210 divergence (missing frozen-word coverage). It
is not field acceptance. Other levels, dedicated fight/steal/isolated traffic,
police, splits and human-input paths outside the receipted capture remain
unmeasured. No Android/device or full-course simulation claim is made here.

## 2026-10-18 police: original cop capture vs native (napa-cops-parked)

- Capture: `RR_ORACLE_COURSE=4 RR_ORACLE_LEVEL=3 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1
  RR_ORACLE_HUMAN=1 RR_ORACLE_INPUT='C:\napa-parked.controls' RR_ORACLE_FRAMES=4730
  tools/oracle_capture.py capture napa-cops-parked-20261018` (zero control words,
  header level byte 2, 4730 contiguous frames, validated; git-ignored
  `analysis/oracle/napa-cops-parked-20261018.tsv`). The original arrest window
  opens at tick 4551 and the race ends 0xb4 = 180 ticks later (4730; a longer run
  restarts a new race, which the validator rejects).
- Native: `police-test` compares the cop's position and speed on every tick from
  the tick-4096 spawn to 4551 (456 ticks, 0 mismatches), skipping when the
  fixture is absent. Covers the cop chase against a parked player only (other
  riders not compared; not a free-run field replay).
- Defect found and fixed: `schedule_pass` walked a snapshot, so the cop inserted
  mid-pass first ran one tick late with dt 5 instead of 4 (speed 80 vs 48 at
  the first think). It now resumes on the live list like 0x00438170.
- Gate: clean worktree, `ctest -j4` 104/104 with 9 disc-fixture tests skipped
  (the git-ignored sim fixtures copied in). Level numbering: `RR_ORACLE_LEVEL=n`
  is level byte n-1, i.e. native `config.level = n-1`.
- City: `RR_ORACLE_COURSE=1` with the same level/seed/controls and
  `RR_ORACLE_FRAMES=4730` -> `city-cops-parked-20261018` (4730 frames, same
  arrest window 4551/4730). `police-test` asserts the City cop is exact for the
  406 ticks 4096..4501 and then reports (does not assert) the divergence.
  Cause (measured): not cop logic and not the route bends. The proxy now dumps
  the original ring bend words (`police.bend.0..15`, `city-cops-bend-20261018`);
  the native `atan(route.bend)<<14` list at tick 4502 is identical (7x 32768,
  3x 49152, 4x 65536, 81920, 98304; curvature 0xe000). The cop's target
  (`+0x194/+0x198/+0x19c`) first differs at tick 4359 because the native cop
  latches traffic car 14 (type 16, position 8724) in `ai_overtake` while the
  original's nearest object is a node at 9227 (think 0x4470b0): the City traffic
  population/positions at that tick are not at parity. The 4502 speed gap
  follows from the different target. Open: City traffic/pedestrian parity.
- Crashed: `RR_ORACLE_KNOCK=4400` (proxy calls the original crash entry 0x00403d60,
  reason 1, default vectors, before tick 4400) with the parked controls, Napa
  level byte 2, `RR_ORACLE_FRAMES=5030` -> `napa-cops-crashed-20261018` (arrest
  at 4851, race end 4851+180; the proxy also dumps the player crash body
  `rider.0.body_*` and half `rider.0.half_*`). `police-test` asserts the cop's
  position and speed and the player's crash state on 769 ticks (4096..4864),
  covering the knock-off, slide, on-foot (state 2), recovery (3) and remount.
  Two native defects fixed on the way: (1) the crash rider animation read the
  body height/speed after that tick's motion, the original before (33 -> 34
  landing one tick later); (2) the camera pulled back for any crash, but the
  original camera (0x00437c30) follows the crash body and only ramps the lift and
  pull-back with the bike 0x10 latch or DAT_004753c4 (set by the arrest). The
  cop's ring-camera-based scheduling interval depends on it.
  From tick 4865 the cop's follow target (`0x00449a80` want) is 24 in the
  original and falls with the gap natively (reported, not asserted): suspected
  stale cop-half position/speed in the original follow state. Open.
- Gate after these changes (clean worktree): `ctest -j4` 104/104 (9 disc-fixture
  tests skipped), `make verify-sim` passed (human capture
  `sched-human-cov2-20261017`).
- Not done: the native on-foot arrest vs an original capture (now step 1073 in the
  synthetic test, not compared), City traffic parity, the 4865 follow target.

## 2026-10-18 police: half latch, post-arrest replan, on-foot arrest (napa-cops-order2/-foot2/-foot3)

- Captures (git-ignored `analysis/oracle/`, all validated and complete; Napa
  `RR_ORACLE_COURSE=4 RR_ORACLE_LEVEL=3 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1
  RR_ORACLE_HUMAN=1 RR_ORACLE_INPUT='C:\napa-parked.controls' RR_ORACLE_ORDER=1`):
  `napa-cops-order-20261018` (knock 4400, 5030 frames, trace `e5ecd8af0727…`),
  `napa-cops-order2-20261018` (knock 4400, 5030, `c87fe1e97a77…`),
  `napa-cops-foot-20261018` / `-foot2` / `-foot3` (knock 4460, 4700 frames,
  `61900580be8b…` / `1f8b234220ef…` / `8ff244ddc513…`). `RR_ORACLE_ORDER=1`
  (new proxy keys) dumps the scheduler-ring order of every node within 0x1000 of
  the player (`ord.<k>` address/type/position/lateral/slice/+0x108), the player
  half's address, +0x108 and +0x2c, and `race.end_timer` (DAT_004753c4) /
  `race.outcome` (DAT_004c5d7c).
- The 4865 "follow target" item is resolved; it was not a stale read. In the
  original the cop latches the player's half (type 8) as its 0x00408e40 overtake
  object (brain +0x180 = the half) from 4765. That path leaves +0x19c untouched
  below a gap of 0x24, so the target holds. The half is met before the bike
  because it re-sorted below the bike while it was the crash body (12799..12722)
  and keeps that order at equal positions after the remount. Native keyed the
  half node by the stale mirror, so the bike came first and the latch never
  happened. Fix: `node_position` keys a crashed rider's half by
  `crash_actor.motion` +0x1c. Reverting it fails `napa-cops-order2` at 4836.
- The original is not run-to-run deterministic before the cop. The RNG stream
  differs from tick 245 between `napa-cops-crashed`, `-order` and `-order2`, so
  traffic and the cop lane differ too. Tick 4096 draws exactly one rand() from the
  end-of-4095 state, the cop lane +0x18c = rand() % 3 (0x00405990), checked on
  all five Napa/City captures. `police-test` anchors the RNG once there from
  each capture.
- `napa-cops-crashed` (lane 1): exact to 4835. From 4831 an oncoming car (lane
  -2, lateral ≈ -88000) sits between the cop and the player, and 0x00449e90 stops
  at it, so the latch drops until the cop passes it (4862). Native traffic has
  no car there. Reported, not asserted. `napa-cops-order2` (lane 0, no car):
  exact on all 935 ticks 4096..5030 (cop position/speed and player crash
  state), no arrest in either.
- Post-arrest replan: the arrest sets DAT_004c5d7c = 4 (0x00416010(4, 0xb4)).
  0x004082f0 then drops a +0x324 == 0 target (the human player) for the cop, so
  the cop races in state 1. Native kept following, which diverged at 4653.
  Fixed in `ai_replan`. `napa-cops-parked` is now asserted over the whole
  capture, 4096..4730 (635 ticks), and the native arrest falls on tick 4552.
  That matches the original: the camera pull-back starts at 4552, so the
  earlier "4551" note was a tick early.
- On-foot arrest vs the original: `napa-cops-foot2` and `-foot3` (lanes 1 and 0)
  both arrest at 4616 in crash state 2 (`race.end_timer` 180). Native is exact
  on 605 ticks (4096..4700) in both, and `busted()` falls on 4616.
  `napa-cops-foot` (the first run) is exact only to 4461; from there the
  original cop is latched on traffic car 11393 (lane 0), which native lacks
  (arrest 4544). Reported, not asserted.
- Gate (clean worktree at 5e48dae + these changes): `cmake --build build
  --parallel` OK; `ctest --test-dir build -j4` exit 0, with the 9 disc/fixture
  tests skipped as before (oracle-road/scene/terrain/terrain-medly/raster,
  hires-render, raster-rgb, widescreen, polygon); `make verify-sim` passed
  (receipt `analysis/agent-gates/oracle-d7c4c9a1690e4f4c869269e87ad6131d`).
- Still open: traffic/pedestrian parity, which depends on the original's
  run-varying RNG stream (City from 4359, and `napa-cops-crashed`/`-foot`
  above); City arrest (original 4552) is not reached at parity.

## 2026-10-18 RNG variance: launch jitter, one-anchor police replays

- New captures (shared prefix via `tools/oracle_capture.py --session
  rng-vary-20261018`, proxy `6d5f1b74…`, outputs outside the accepted set),
  all complete and validated: `napa-randlog1-20261018` and `-2` (Napa,
  `RR_ORACLE_COURSE=4 RR_ORACLE_LEVEL=3 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1
  RR_ORACLE_HUMAN=1 RR_ORACLE_INPUT='C:\napa-parked.controls'
  RR_ORACLE_FRAMES=1200 RR_ORACLE_RANDCOUNT=1 RR_ORACLE_RANDLOG=1`, launched
  29 s apart), `napa-knock-randlog-20261018` (same with `RR_ORACLE_KNOCK=300`,
  700 frames) and `city-randlog-20261018` (`RR_ORACLE_COURSE=1`, 700 frames).
  The two Napa runs are copied git-ignored into `analysis/oracle/`.
- Result: the rows-9..14 speed jitter differs from tick 1 (brain +0x1a4/+0x1bc..
  +0x1c4 of six riders); the first motion difference is tick 235, the first RNG
  difference 245 (extra `0x409377` draw), all varying draws are `0x409377`,
  `0x409487`, `0x4094d3`. Brute force over the MSVC state recovers the
  `time()` launch seconds of both runs (see REVERSE_ENGINEERING.md).
- `police-test` now free-runs every capture from one RNG anchor at the end of
  tick 1 (no 4095 anchor), builds 14 opponents, loads rows 9..14 from the
  capture's frame-1 levels (`RaceConfig::spare_levels`), and sets the ring and
  pedestrian setup tick to 0. It asserts the cop to the capture end, the RNG to
  `rng_end` and every opponent's position/speed to `field_end`:
  - `napa-cops-parked`: cop 635 ticks (4096..4730), arrest 4552, RNG to 4551,
    opponents to 4730.
  - `napa-cops-crashed`: cop 935 ticks (was 740, to 4835), arrest 4852 (the
    original's camera pull-back starts at 4852), RNG to 4399, opponents to 5030.
  - `napa-cops-order2`: 935 ticks, no arrest, RNG to 4399, opponents to 5030.
  - `napa-cops-foot`: cop 605 ticks (was 366, to 4461), arrest 4544 as the
    original, RNG to 4459, opponents to 4700. `-foot2`/`-foot3`: 605 ticks,
    arrest 4616, RNG to 4459, opponents to 4700.
  - `city-cops-parked`: cop 456 ticks (4096..4551, was asserted to 4501),
    RNG to 234, opponents to 147.
  - `napa-randlog1/2`: RNG and opponents match on all 1200 ticks, each with
    its own launch jitter.
  - Negative control: without `spare_levels` the parked replay's opponents
    differ at tick 238.
- Native traffic change behind this: the `0x0044e710` slow-obstacle lane
  change (`TrafficSystem::plan_speed`, `TrafficCar::steer_error`). Without it
  Napa car 13e2248 stays behind the parked player and the cop latches it at
  4463 (14-opponent run).
- Known, reported not asserted: RNG after a knock/arrest (24 terrain draws of
  the unported backward ring step), City from 235 (pedestrian think
  `0x4410b5`) and City row 11 from 148. The tick-1 draws and the setup stream
  are not reproduced (anchor at the end of tick 1).
- Gate (worktree at 21be20d + these changes): `cmake --build build --parallel`
  exit 0; `ctest --test-dir build -j6 --output-on-failure`: 104/104 pass, with
  the 9 disc/fixture tests skipped as before (oracle-road/scene/terrain/
  terrain-medly/raster, hires-render, raster-rgb, widescreen, polygon);
  `make verify-sim` passed (receipt
  `analysis/agent-gates/oracle-9b5885ce737742578eab9d810d4ea476`).


## 2026-10-18 — backward ring step (baseline 6044526)

Worktree `/Users/archit/Dev/rr/.backward-ring-20261018`; independent build
`build-backward-ring`. Shared checkout edits were preserved. Fixtures are local
symlinks from `.police-capture-20261018`, `.rng-vary-20261018`, and the main
checkout's ignored `analysis/oracle`; no proprietary data is committed.

`police-test` still uses one anchor at the end of tick 1 and asserts every
intervening RNG state and each opponent's position/speed:

| Capture | Cop comparison | Arrest tick | RNG match through | Opponents through |
| --- | --- | --- | --- | --- |
| napa-cops-parked | 635 ticks | 4552 | 4730 (end) | 4730 |
| napa-cops-crashed | 935 ticks | 4852 | 5030 (end) | 5030 |
| napa-cops-order2 | 935 ticks | none | 5030 (end) | 5030 |
| napa-cops-foot | 605 ticks | 4544 | 4551 | 4700 |
| napa-cops-foot2 | 605 ticks | 4616 | 4579 | 4700 |
| napa-cops-foot3 | 605 ticks | 4616 | 4579 | 4700 |
| napa-randlog1 / napa-randlog2 | no cop | none | 1200 (end) | 1200 |
| napa-backward-ring-randlog | no cop | none | 700 (end) | 700 |

City retains its previous asserted RNG/field limits (234/147), with the cop
matching 456 ticks. No new City fidelity claim.

New original captures used the existing installed proxy, SHA256
`6d5f1b7461382197d73fc1f7050301baaf626b998fae6cafb5dbe156b5ab300e`, through
`tools/oracle_capture.py capture <name> --session backward-ring-20261018`.
Common environment: `RR_ORACLE_COURSE=4 RR_ORACLE_LEVEL=3 RR_ORACLE_SEED=1
RR_ORACLE_SIM=1 RR_ORACLE_HUMAN=1 RR_ORACLE_INPUT='C:\napa-parked.controls'
RR_ORACLE_RANDCOUNT=1 RR_ORACLE_RANDLOG=1`. Controls SHA256
`2bfc3f36dc3396a852aae4d204db1742bc76ab698d2d71cc8f4675af1340b7fe`.
Both runs have valid headers, contiguous ticks, completion markers, original
EXE hashes and process exit 0; the prefix/install/output claims serialized them.

- `napa-backward-ring-randlog-20261018`: `RR_ORACLE_KNOCK=300`, 700 frames,
  `RR_ORACLE_TERRAIN_TRACE=1`, 26.19 seconds. Trace SHA256
  `b556931d9870eebdecc857ace50ed19d20efcb97fed411fbb7f2f8c5461946e1`.
  Knock tick 300 logs eight triples `0x4513f3/0x451479/0x4514e7` (24 terrain
  draws). Added as an optional end-to-end RNG/field regression in police-test.
- `napa-foot-randlog-20261018`: knock 4460, 4700 frames, 89.67 seconds.
  SHA256 `c827c474263d2daddcc272175388d8c2200159874006a8010b08f5b416fa4e4d`.
  Reproduces foot's RNG states 511658635 -> 1410771746 at ticks 4551 -> 4552;
  RANDLOG identifies the extra tick-4552 draw as `0x00409377` (AI overtake).
  Native `RR_SIM_TRACE_TICK=4552` shows no draws in any phase for that scenario.
  Ring.back changes 410 -> 407, without a slice crossing. Thus the next
  divergence is outside the backward ring implementation. Foot2/3 similarly
  miss exactly one draw at 4580 (4181790837 -> 2384560484 and 3226249879 ->
  475380478); ring.back stays 434. Their caller is not directly logged in the
  older captures, so the analogous overtake attribution remains an inference.

Focused `ctest --test-dir build-backward-ring -R '^(track_ring|course|terrain)$'
passed 3/3 after the endpoint addition. It checks both walkers entering the
previous ordinary road, rebuilding its endpoint slice, the original distinct
BORR/DZHR/terrain positions, unsigned BORR rewind, billboard revisit, physical
DZHR follower behavior and recovery from a missing previous link. The first
parser iteration rejected a synthetic SGS ending at DZHR; bounded the optional
following-word read and reran the course regression successfully.

Final integration source includes concurrent `fe70d15` (offline rider field and
names). Rebase preserved both progress entries; no implementation conflicts.

- `cmake -S . -B build-backward-ring -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, followed by
  `cmake --build build-backward-ring --parallel 8`: passed.
- `make verify-sim BUILD_DIR=build-backward-ring JOBS=8`: passed after integration,
  strict human replay **1431/1431 identical**. Receipt:
  `analysis/agent-gates/oracle-7909cbcdab24411ea680d0cd9e857dad/receipt.json`.
- No client C++ LSP tool was exposed. Ran `tools/agent_lsp.py` preflight on the
  owned compilation database and `clangd --check=<source>
  --compile-commands-dir=build-backward-ring --tweaks=` for track_ring.cpp,
  course.cpp and terrain.cpp: zero errors. Empty tweaks disables Apple's
  unrelated ExtractFunction feature self-tests, which otherwise returned 3;
  the original check also reported no C++ parse/type diagnostics.
- `ctest --test-dir build-backward-ring -j6 --output-on-failure`: **104/104
  passed, zero skips**, exit 0, 171.25 seconds on the integrated final source.
  Complete build/CTest/clangd/capture logs are retained in
  `analysis/backward-ring-20261018/`; strict receipt remains at the path above.
- An earlier, superseded full run during concurrent verification reported four
  allocations in `render-threads-alloc`; the final integrated run passed that
  test with no renderer changes. No test/golden or allocation assertion was
  relaxed. The earlier full run before endpoint integration also passed 104/104.
- Android/device gates were not run for this simulation/iterator change. These
  results establish the replay spans above, not full gameplay or dual-road parity.

### October 25, 2026: intersection / player combat report

- `cmake -S . -B build-agent-user-road-combat-20261025 -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, full build, then CTest: **106/106 pass**,
  zero skips (736.98 s). Focused `race|scene|building_panels|course_scene`: 4/4.
- Isolated tracked-clean source `de9390f`, Release build in
  `build-agent-user-road-combat-clean`, full `ctest -j 4`: **106/106 pass**,
  zero skips (39.39 s). This excludes OMP's concurrent uncommitted crash work.
- Original EXE hash checked; recovered branch checked against exported
  disassembly. Synthetic tile coverage and 36 original-asset player-combat
  combinations pass. Existing replay gates remain green; no new combat or
  intersection oracle capture, City-gap reproduction, sanitizers or device
  validation. Browser Release build succeeds.
- [Full commands, browser checks, local evidence, remaining City report and
  hook correction](agent-handoffs/user-road-combat-20261025.md).

### October 26, 2026: City width-transition triangle

- Supplied full Desktop recording/screenshot reproduces the road leak around
  00:38 at imported CITY route sample 159. New coverage regression fails on
  the old renderer and passes at logical widths 320/426 with the correction.
- Owned Release full build + CTest: **106/106 pass**, no skips (39.50 s).
- Exact committed source `a41f241` in the isolated checkout, full Release
  build + CTest `-j 4`: **106/106 pass**, no skips (28.21 s). Browser Release
  rebuild validates nine code/metadata files; native identity `dirty=false`.
- Original `0x00444630` origin correction checked against assembly
  `0x00444c57–0x00444ca3`; changed-line clangd check: zero errors.
- [Before/after evidence, logs and limits](agent-handoffs/city-width-gap-20261026.md).

### October 26, 2026: Napa approaching-merge rendering

- Prior committed library fails the new imported-scene regression:
  `Napa merge terrain covers the roadway at 940` (exit 1). Fixed renderer
  checks samples 940/945/970/975 at logical widths 320/426, with unit checks
  for ridge continuity, exterior preservation and inner band limits.
- Owned Release full gate: 106/106, zero skips (41.08 s). Isolated clean
  source `66fcb52` final Release full gate: 106/106, zero skips (27.16 s).
  Browser Release build succeeds; changed helper/scene clangd checks pass.
- Actions deployment run `40290251400`: success, including stale-cache upgrade,
  deployed revision/health and live-gameplay checks. Public revision `bf38c79`
  includes the renderer fix unchanged. No new original-runtime merge capture,
  sanitizer or device gate; static original trace plus exact native scene
  reproduction, not full original split/merge pixel acceptance.
- [Commands, hashes and before/after evidence](agent-handoffs/napa-merge-terrain-20261026.md).

## Frontend title/description layout follow-up (October 26, 2026)

- Evidence: existing original screen-9 capture and receipt at
  `analysis/oracle/menu-09-20261026.*`; original-disassembly ranges
  `0x0041fa52..0x0041fab8` and `0x0042071d..0x00420795`. No new Wine run,
  proxy modification, save capture or shared-prefix mutation in this slice.
- Reproduced the old layout using the existing Release frontend-test binary:
  `build-agent-frontend-menu-26/frontend-test ../cdrom/ROADRASH --dump
  analysis/frontend/continue-20261026`. Old orange title ink starts `(24,35)`;
  old last paragraph line starts x=115. Both violate the new captured-position
  regression limits. The corrected title starts `(33,48)`, identical to the
  original capture; final line x=17 versus original x=18. Native corrected
  title right edge 224 versus original 222 (half-open bounds).
- Owned Debug configure/build:
  `cmake -S . -B build-agent-frontend-layout-20261026 -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, then
  `cmake --build build-agent-frontend-layout-20261026 --parallel 6`: PASS.
  Logs: `/tmp/frontend-layout-configure.log`, `/tmp/frontend-layout-fullbuild.log`.
- Focused CTest `-R '^frontend$' --output-on-failure`: PASS. Added bounds checks
  at scales 1, 2 and 4 with four logical pixels of glyph-metric tolerance.
  Corrected 1× frame visually inspected:
  `analysis/frontend/layout-fixed-20261026/main.png` (local original artwork).
- LSP client unavailable in this chat. Owned compile database preflight:
  healthy, 187 entries. Initial clangd checks failed first without the compile
  database and then on Apple clangd's ExtractFunction/ExpandDeducedType tweak
  self-tests (103 errors, no parse diagnostics). Rerun with those refactoring
  self-tests disabled:
  `clangd --check=src/frontend.cpp
  --compile-commands-dir=build-agent-frontend-layout-20261026 --tweaks=`:
  exit 0, **0 errors**; `/tmp/frontend-layout-clangd.log`.
- Full integration CTest (`ctest --test-dir build-agent-frontend-layout-20261026
  --output-on-failure -j 6`): **105/106 passed, 1 failed, zero skips**, exit 8.
  Failure: `sound`, "negative split separation truncates toward zero".
  Repeated frontend/sound pair: frontend PASS, sound same failure.
  Logs `/tmp/frontend-layout-ctest.log`, `/tmp/frontend-layout-rerun.log`.
- Isolated attribution check: detached checkout at `bca9d45` under
  `/tmp/rr-frontend-layout-baseline-20261026`, Debug build `build-frontend-baseline`.
  Baseline `sound-test /Users/archit/Dev/rr/cdrom/ROADRASH`: PASS, 0 failures.
  Applied **only** `src/frontend.cpp` + `tests/frontend_test.cpp` patch;
  rebuilt both targets. Sound still PASS, 0 failures; frontend with the
  absolute disc path and `--menu-corpus /Users/archit/Dev/rr/native-port/analysis/oracle`:
  PASS, including captured-position and picture characterization checks.
  The shared tree concurrently changes `Race::camera_position()` to follow
  the half image; the failing sound fixture edits bike camera words. This is
  the likely interaction, not established as the fix here. Other session's
  simulation edits/fixtures were not modified. Isolated checkout removed
  after testing; patch/build logs retained in `/tmp/frontend-layout-*`.
- Not covered: driven-career save, additional original menu captures, strict
  non-text/JPEG parity, exact GDI glyphs, music listening, sanitizer/Android
  or hardware checks. Existing broad-mask picture characterization remains
  diagnostic; this change does not upgrade plan 13 Task 6 to complete.

## October 26, 2026 — isolated original steal component oracle

Worktree `/Users/archit/Dev/rr/.re-steal-worktree-20261026`, baseline `80f3e72`.
Reference disc EXE SHA-256 rechecked:
`73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`.

```sh
python3 tools/steal_fixture_capture.py \
  --exe /Users/archit/Dev/rr/cdrom/ROADRASH/ROADRASH.EXE \
  --prefix /Users/archit/Dev/rr/.re-steal-prefix-20261026 \
  --clone-prefix /Users/archit/Dev/rr/prefix
python3 tests/steal_fixture_test.py \
  --trace analysis/oracle/steal-fixture/46883e0ec8734a20b8c9496c15184dc7/steal.tsv
python3 tests/steal_capture_audit_test.py
```

- Capture/build: exit 0; GCC MinGW i686 16.2.0, `-O2 -Wall -Wextra -Werror
  -shared -static-libgcc`; Wine 11.18 Staging; original resolver returned for
  all 31 cases. Four synthetic parser/foreign-EXE guard checks pass, plus
  all 31 original case expectations (13 transfers, 15 gate rejections,
  three rejected rolls). Earlier capture-audit five checks still pass.
- Original fixed-roll mode 3 controls only the roll value; a forwarding hook
  at `0x40981c` counts exactly how often that call is reached. Synthetic
  scheduler links, halves, bikes, animation banks and graphics descriptors;
  victim damage deadline suppresses normal damage. No race bootstrap, real
  assets, native simulation, CRT random stream or human controls exercised.
- Local immutable receipt, trace, DLL and logs under
  `analysis/oracle/steal-fixture/46883e0ec8734a20b8c9496c15184dc7/`.
  EXE/DLL/trace/source hashes recorded. Commercial/local artifacts excluded
  from Git; only fixture source, runner, checks and documentation committed.
- Main prefix was read only while cloning. Private prefix and output/install
  paths are resource-claimed by the runner. Active frontend proxy, shared
  Ghidra database and accepted captures were untouched.
- No native production code/build configuration changed. Native CTest,
  sanitizers, Android and device tests were not run for this standalone
  original-function research slice. Runtime steal parity remains open.

- Committed-source repeat: the commit hook formatted `steal_fixture.c`;
  rebuilt and recaptured from committed source. Final run above has source
  SHA-256 `6463bd19626c35b0c457594267c56d8c30a9f436d78bc3d9288ea727a2a19f3e`
  and byte-identical trace to the initial run. Receipt-aware checks pass on
  the final run. Concurrent multiplayer commits merged without conflicts.

## October 26, 2026 — plan 34 client, real server E2E and bots

See [net-client-20261026](agent-handoffs/net-client-20261026.md) for commands,
full gate results and local evidence paths. `net_client` drives real `Server`
and `Client`/`Rollback`/disc-backed Race instances through 84 network scenarios,
including client clock offsets/drift, required periodic seat hashes, matching
confirmed cutoff state/results and bounded rollback/resend/event queues.
`net-bots` local WebSocket smokes cover 2 and 8 clients, with AI and traffic.
Cutoff results report departure, not a course finish. No production, device,
Android, browser, renderer/audio or full-course multiplayer acceptance claimed.

Final gates: Debug **114/114**, Release (transport off) **113/113**, zero skips.
Both include the complete 84-case client matrix. Native transport is included
in Debug; final two/eight-client WebSocket smokes also pass after the clock fix.
Local receipts: `/Users/archit/Dev/rr/.plan34-logs/net-client-20261026/`.

## October 26, 2026 — plan 34 Task 6 RaceScreen/effects

- Debug full native build and CTest: **115/115 passed**, zero skips (419.46 s).
  Final focused Debug presentation/tooling checks also passed after the sparse
  seat builder and HUD/string ownership changes; the correction/render test
  passed again with AI, traffic and active SoundSystem playback.
- Final Release full build/CTest (transport disabled): **114/114 passed**, zero
  skips (35.96 s). `net_client` includes the 84 original network scenarios and
  two controlled initial-wreck fixtures that automatically report canonical
  confirmed results through the real Server.
- `net_presentation` uses scripted protocol/channel fixtures and a software SDL
  renderer. Every frame, authoritative hashes equal a headless client through
  real corrections, quit/focus transitions, local-seat rendering and audio.
  It also checks sealed-only results, chat, sparse seat identities, smoothing,
  sound history bounds and correct-prediction effect promotion. This is distinct
  from the real Server's headless chaos test and from future full menu flow.
- Local WebSocket bot repeat: two clients, 480-tick cutoff, hash
  `5741817043016304586` on both, official agreement. This remains a cutoff smoke,
  not evidence of finishing a course. Final visual fixture inspected locally.
- Focused instrumented presentation test passes under ASan/UBSan (RR_FUZZ build;
  leak detection disabled). clangd fallback with tweak self-tests disabled and
  clang-format 22 checks pass. No C++ LSP service was
  exposed. No Android/device, Windows/browser cross-play, production deployment
  or full-course multiplayer soak performed for this slice.
- Receipts and screenshot locations, factory-rule repair and exact API contracts:
  [net-screen-20261027](agent-handoffs/net-screen-20261027.md).


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


Final socket verification: the updated Task 5 `net-bots` use the production
`make_hello` / full reference verification shared by interactive clients. A
self-owned localhost `rr-netd` passed both 2- and 8-client WebSocket runs to
420 confirmed ticks with matching group hashes and agreed official results.
The 2-client hash was `7706892312449139743`; the 8-client hash was
`4354729446459175121`. These are scripted cutoff diagnostics, not finished
laps. Socket logs/captured race logs remain outside Git under
`/Users/archit/Dev/rr/.plan34-logs/net-menus-20261027`. Both test servers were
terminated; no listeners remain. The first attempt to reuse the same port
hit TCP TIME_WAIT; the 8-client run used separate claimed ports.


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


CI receipt: [multiplayer build 40523572291](https://github.com/arkits/road-rash/actions/runs/40523572291)
passed for `a731defe26bf95480c5115033f2729e71ea10f38` (build 1m21s;
deploy skipped by the provisioning gate). Downloaded artifact contains exactly
`rr-netd`, `Dockerfile`, `app.json`; the executable is ELF64 x86-64. Rebuilt
that artifact's runtime locally and verified `/net/health` returns `ok:true`
and the exact CI revision. All test containers stopped. No multiplayer VPS
provisioning or host configuration change was performed.


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


## October 27, 2026 — plan 34 Task 9 DataChannel and loss measurements

Implemented native libdatachannel and browser RTCPeerConnection peers, with
shared WebSocket negotiation/upgrade/fallback wrapper. It starts only after
Welcome, carries Input/Ack/Frames on unordered zero-retransmit SCTP when open,
keeps controls/logs/results reliable, bounds queues/signals, and uses an
explicit reliable additive `RtcClose` (type 29, existing bytes unchanged) for
permanent fallback. Startup negotiation times out at 5 seconds. Server public
IPv4/UDP bounds are validated config; UDP mux supports eight peers on one port.

Validation: real native 2/8-client 480-tick agreed results; UDP-disabled and
forced-close variants; strict every-client DataChannel counters; eight clients
on only UDP 40000; browser-browser and browser-native codec transport fixtures.
Debug and Release full suites each passed 119/119 (435.44s / 39.68s). Focused
ASan/UBSan passed 4/4. The pinned libjuice picohash C callback casts trigger
UBSan function-type checks: only `juice-static` excludes `-fsanitize=function`;
ASan and other UBSan checks, including all first-party code, remain enabled.
A formatter split JS strict equality inside EM_ASM; the query is now in a
format-protected EM_JS block, and browser game/fixtures rebuilt successfully.
Deployment helper passed seven isolated failure/replay/archive tests.

Actual Linux `tc netem` server-eth0 egress: 20ms delay, 5% packet loss; Release
native clients, two humans, full AI, fixed server seed 42, 1800-tick cutoff.
Prediction-depth samples cover live advances/corrections after countdown;
late/input counts include the sealed live timeline at sampling (2–4 extra
sealed ticks during result acknowledgement in this initial measurement).
Stalls are client update observations, not a duration. All runs agreed with
matching within-run hashes; cross-run hashes are not goldens.

| Mode / repetition | rollback p50 / p95 / p99 | live late / inputs | max client live stalls | egress bytes | netem dropped |
| --- | --- | --- | --- | --- | --- |
| WebSocket 1 | 0 / 0 / 6 | 9 / 2882 | 46 | 329740 | 202 |
| DataChannel 1 | 0 / 0 / 7 | 2 / 2884 | 1 | 922150 | 433 |
| WebSocket 2 | 0 / 0 / 6 | 15 / 2884 | 47 | 329162 | 222 |
| DataChannel 2 | 0 / 0 / 6 | 2 / 2884 | 0 | 916015 | 373 |

DataChannel improved late substitutions/stalls here, but p99 was unchanged or
worse and bytes were about 2.8x higher. Keep WebSocket default; RTC is opt-in
via native `--rtc` / browser `?rtc=1`. These are server-egress loss fixtures,
not all WAN/mobile networks. Reproduce using a private Linux daemon container
with NET_ADMIN: `tc qdisc add dev eth0 root netem delay 20ms loss 5%`; run
Release `net-bots --clients 2 --ticks 1800 --require-rtc`, then `--no-udp`,
capturing `tc -s qdisc show dev eth0`. Logs `/tmp/rr-rtc-*`, retained outside
Git at `/Users/archit/Dev/rr/.plan34-logs/rtc-20261027/`.

Rolling deployment helper assigns each generation a distinct, one-to-one UDP
port from the approved range, preserving startup checks. It validates the
bounded code archive, restores future-container config/options on failure,
and rebuilds identical archive replays when necessary. Host wrapper/key
restriction installed; one scoped IPv4 UFW rule added after confirming the
range unused. Production code/UDP acceptance receipt follows the RTC handoff.
No other app/global proxy/firewall rule was changed.


## October 27, 2026 — plan 34 public RTC deployment acceptance

[Production CI 40575802077](https://github.com/arkits/road-rash/actions/runs/40575802077)
passed exact-revision health, required RTC on every client, UDP-disabled WS,
and forced RTC close at tick 390. Each pair agreed on its 480-tick cutoff and
received official results, with zero measured live stalls and no live late
inputs. Required RTC counters were 1293/475 and 1289/475 (sent/received), zero
fallbacks; forced-close 1108/385 and 1103/384, one fallback per client. This is
transport/determinism evidence, not course completion or platform cross-play.
[Browser CI 40572423797](https://github.com/arkits/road-rash/actions/runs/40572423797)
passed browser RTC fixtures, deployment and existing live gameplay checks.

Public UDP initially failed because DOCKER-USER dropped new eth0 forwarding
before UFW input rules. Added only the approved UDP original-destination
5.161.142.20:40000–40099 RETURN exception before that drop, live and persisted
in /etc/ufw/after.rules; iptables-restore --test passed. Other services/policy
unchanged. Initial Mac peers could use Tailnet candidates; outside Docker
clients and GitHub Linux runners establish public-path acceptance. The failed
CI receipts are retained in the RTC handoff, with the exact firewall rule,
backup and removal in the deployment README.

Actual identical ebbe66b archive replay exercised Dokku's exit1 skip, forced
ps:rebuild with startup checks, changed CID 11aa8493f5ed->b071abd179cd and UDP
40001->40000. 62 public curl health samples passed with zero failures during
the replay; disabled/skipped checks both none. The initial urllib-only probe
was blocked 403 by Cloudflare, so it supplies no continuity evidence. Root
helper SHA256 matches checked-in deploy.py; ten isolated deployment tests pass.

The multi-client driver now waits for each guest's Welcome before joining,
and delays final character/course choices until RTC open/fallback/bounded 6 s;
final votes trigger Start on the current server. Timeout diagnostics include
handshake/lobby/per-client counters. Latest native eight-client one-UDP-port
480-tick run agreed hash 14453031911657472735, 9459 sent/3785 received, no fallback,
zero live late inputs/stalls. Updated Linux public RTC/noUDP/close pairs also
passed. Full 119-test Debug/Release and focused sanitizer/browser gates above
remain applicable; follow-ups changed the driver/deployment helper, not src.

Evidence: /tmp/rr-rtc-final-*, /tmp/rr-rtc-ebbe-ci-complete.log and
/Users/archit/Dev/rr/.plan34-logs/rtc-20261027/identical-replay-ebbe.log.
Task 8 Step 3 and Task 9 complete. RTC remains opt-in after mixed loss metrics.
Task 10 device/signed-package/interactive cross-play and Task 11 soak acceptance
remain open. adb devices lists no attached MANGMI; no hardware evidence added.


Final source `3eb22dcaeb18c9c44e4a4dbb41b066283cfc2ff8` also passed
[CI 40576936619](https://github.com/arkits/road-rash/actions/runs/40576936619)
with the Welcome/choice ordering fixes: all three strict cases agreed on
results, with positive traffic on every required-RTC peer and zero live late
inputs/stalls. Health reports three finished races, zero desyncs, malformed
messages or rate limits; UDP 40001; startup checks neither disabled nor skipped.

## 2026-10-27 — simple import screen

- Build: `cmake -S . -B build-agent-import-ui-20261027 -DCMAKE_BUILD_TYPE=Debug`,
  `cmake --build build-agent-import-ui-20261027 --parallel`: pass.
- `ctest --test-dir build-agent-import-ui-20261027 --output-on-failure`:
  106/106 passed (722 s).
- Visual: `rr-game --assets <empty dir> --screenshot import.bmp` renders the
  new desktop layout (checked by eye, 2560x1920 window capture).
- Not run: Android assembly/device, live file-picker import, progress and
  error states on screen (code paths compiled only).

## 2026-10-27 — import screen font

- Build `build-agent-import-ui-20261027` (Debug): pass, no warnings in
  `import_ui.cpp`. `rr-game --assets <empty dir> --screenshot` shows Futura
  text on macOS (checked by eye at 2560x1920).
- Loading screen: rendered with a scratch harness (software renderer,
  2560x1920, logical 640x480 and 853x480) calling `rr::draw_loading_screen`;
  both centred correctly. The import screen BMP is byte-identical before and
  after moving the font code into the shared helper.
- `ctest --test-dir build-agent-import-ui-20261027 --output-on-failure`:
  121/121 passed.
- Not run: Android/Linux/Windows font paths, debug-font fallback on screen,
  the loading screen inside a live `rr-game` run (it shows for under a second).
