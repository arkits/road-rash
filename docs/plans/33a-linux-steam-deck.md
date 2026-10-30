# Plan 33a: Linux and Steam Deck

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax. Read [00-INDEX.md](00-INDEX.md) and [33-linux-windows-ports.md](33-linux-windows-ports.md) (review findings, Part A, acceptance) first.

**Goal:** `rr-game` runs natively on desktop Linux (x86-64 and ARM64; X11, Wayland) and on the Steam Deck in both Desktop Mode and Game Mode (gamescope):
- the disc imports with a gamepad alone;
- races hold 60 ticks/s at the Deck's 1280×800;
- Steam Input controls, the on-screen keyboard and suspend/resume all work;
- it ships as a portable tarball and an AppImage (plus a Flatpak bundle), with no game data.

**Architecture:** The same C++ sources and CMake build. This plan adds:
- a Linux CMake preset;
- a release build inside the Steam Linux Runtime 3.0 ("sniper") SDK container, so one binary runs on every glibc ≥ 2.31 distribution and on SteamOS;
- statically linked SDL3 that loads its display and audio backends at run time;
- gamepad navigation and an in-game file browser for the import screen;
- runtime Steam Deck defaults (no compile-time `#ifdef`).

**Tech stack:** GCC 14 and Clang 19 (CI), SDL3 static (Part A Task A7), `registry.gitlab.steamos.cloud/steamrt/sniper/sdk` (pinned by digest), `appimagetool` with the static type-2 runtime (no `libfuse2` dependency), `flatpak-builder` with `org.freedesktop.Platform//24.08`.

## Baseline (October 29, 2026)

The first full native build on Linux was this review's: Debian trixie, `linux/arm64` Docker on the Apple-silicon host, GCC 14.2.0, distribution `libsdl3-dev` 3.2.10, OpenSSL 3.5.7, Debug, default options (network transport on), disc mounted read-only.

- **Configure:** passed.
- **Build:** one error. `tests/world_renderer_test.cpp` used `std::copy_n` without `<algorithm>` (the same class of bug as GitHub issue #1). Fixed in this review, then rebuilt cleanly.
- **CTest:** see "First CTest run" below.

### First CTest run

**117/121 passed in 129 s (`ctest -j6`, `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`).** With the disc mounted, the disc tests ran rather than skipping. All simulation, oracle-replay, network and soak tests passed: `oracle-sim`, `oracle-sim-field`, `-schema2`, `-l5`, `race-soak`, `rollback`, `race_state`, `net_client` and `server_*`.

| Failing test | Cause | Plan |
| --- | --- | --- |
| `oracle-actor-record` | `char clip == -7` assertion; plain `char` is unsigned on ARM64 Linux | 33 F15 / A10 |
| `raster-rgb-alloc` | fog-path RGB checksum differs from the macOS pin; still differs with `-ffp-contract=off`; suspected `libm` `exp`/`log2` | 33 F16 / A11 |
| `input` | phone-overlay pixel check fails with distribution SDL 3.2.10 | 33 F17 / A7 Step 5 |
| `agent-tooling` | doc check ran before this review added the 33a/33b index rows; fixed | — |

Not yet run on Linux: x86-64 (only ARM64 here), Clang, Release, sanitizers, the pinned static SDL, any display or audio hardware. The determinism matrix's earlier Linux x86-64 GCC result (plan 34) still stands.

## Global constraints

- **Release builds run in the sniper SDK container**, never on the developer's distribution. That way the `GLIBC_2.xx` symbol ceiling is 2.31, and the binary runs on SteamOS 3, Debian 11+, Ubuntu 20.04+, Fedora 34+ and Arch.
- **Package contents:** `rr-game`, licences, README, `.desktop` file and icon only. `libstdc++`/`libgcc` are static (`-static-libstdc++ -static-libgcc`), and so are SDL3 and OpenSSL. `NEEDED` entries are limited to an allowlist: `libc.so.6`, `libm.so.6`, `libdl.so.2`, `libpthread.so.0`, `librt.so.1` and `ld-linux-*`. Everything else (X11, Wayland, PipeWire, ALSA, udev, libdecor, Vulkan/GL) is `dlopen`ed by SDL.
- **No game data** in any artefact. The verifier checks every packaged file against `src/reference_hashes.inc`, and checks the size and magic of anything larger than 64 KB.
- **Data locations:** imported disc, saves, settings and caches live in `SDL_GetPrefPath` (`$XDG_DATA_HOME/RoadRashNative/RoadRash`, default `~/.local/share/...`). Flatpak keeps the same layout inside `~/.var/app/<id>/data`. The user guide says where each package keeps data.
- **Steam Deck detection is a run-time check**, using the `SteamDeck=1` environment variable that Steam sets, or `XDG_CURRENT_DESKTOP=gamescope`. It only changes first-run defaults and prompt glyphs, never the simulation.
- **Device access:** the Deck is not attached to this workspace. Device steps are "blocked: no Deck" until the owner provides one (Desktop Mode, `passwd` + `sudo systemctl start sshd`, then `ssh deck@steamdeck`). Software tasks proceed without it.

## Review focus

1. **Game Mode file dialogs.** SDL3 tries the XDG Desktop Portal (D-Bus) and then `zenity`. Whether either appears above a gamescope-composited game is unverified. The import must never depend on it, so Task 5 adds an in-game browser.
2. **Lower-case discs.** A user who mounts the ISO with `mount -o loop` sees `roadrash/data/...`. Part A Task A5 must land before Task 2's acceptance.
3. **Steam on-screen keyboard.** Career alias entry calls `SDL_StartTextInput`. This needs SDL ≥ 3.4.12 and `SDL_HINT_ENABLE_STEAM_SCREEN_KEYBOARD` (Part A Task A7). It is unverified under gamescope, so Task 5 has a fallback.
4. **Suspend in the middle of a race and in the middle of a video.** `TickClock` caps catch-up, but the audio device and controllers re-enumerate after resume.
5. **Performance.** Deck APU: 4 Zen 2 cores / 8 threads. Measure the software rasteriser at render scales 1×/2× and the Enhanced preset at 1280×800, both on battery and plugged in.
6. **Wayland vs X11.** SDL3 prefers Wayland when it is available. Verify fullscreen, scaling, cursor hiding and text input on GNOME (Wayland), KDE Plasma (Wayland and X11) and gamescope (Xwayland).

---

## File structure

| File | Responsibility |
| --- | --- |
| `CMakePresets.json` | `linux-gcc`, `linux-clang`, `linux-release` (static SDL, `-static-libstdc++`), shared with 33b |
| `linux/road-rash-native.desktop.in`, `linux/org.roadrash.nativeport.metainfo.xml` | Desktop entry and AppStream metadata (ID matches macOS/Android `org.roadrash.nativeport`) |
| `linux/org.roadrash.nativeport.yml` | Flatpak manifest |
| `tools/build_linux_package.sh` | Sniper-container release build → tarball + AppImage (+ Flatpak bundle) |
| `tools/verify_linux_package.py` | No game data, `NEEDED` allowlist, `GLIBC_` ceiling, starts headless |
| `src/platform_linux.cpp/.hpp` | Steam Deck / gamescope detection; removable-media roots for the browser |
| `src/file_browser.cpp/.hpp` | Gamepad/keyboard/touch file and folder browser used by the import screen |
| `src/import_ui.cpp` | Gamepad navigation, browser fallback, disc auto-discovery |
| `src/settings.cpp` | First-run defaults for the Deck |
| `tests/file_browser_test.cpp`, `tests/platform_linux_test.cpp` | Synthetic tests |
| `.github/workflows/linux.yml` | Native gate x86-64 + ARM64, package build and verification |
| `docs/USER_GUIDE.md` | Linux and Steam Deck install, import, controls, data locations |

---

### Task 1: Linux native gate

**Files:** Create `CMakePresets.json`. Modify `docs/VERIFICATION.md`, `docs/TOOLING.md`, `README.md`.

- [ ] **Step 1: Presets.** `linux-gcc` and `linux-clang` (Debug, Ninja, `RR_STRICT_WARNINGS=ON`) and `linux-release` (Release, `RR_STATIC_SDL=ON`). Configure presets inherit a shared `base` that 33b also uses.
- [ ] **Step 2: First gate.** Run `ctest` with the disc mounted, on GCC and Clang, x86-64 and ARM64, using Docker on the Mac host (`--platform linux/amd64` under emulation is acceptable for correctness but not for timing). Every failure gets a root cause: a portability bug (fix it), an environment issue (document it), or a real divergence (record it; never re-bless).
- [ ] **Step 3: Goldens.** Every oracle, pixel and determinism golden must equal the macOS result. In particular `oracle-sim*`, `hires-render`, `widescreen`, `raster-rgb`, `world_renderer` and `race-soak`. Part A Tasks A2/A3 are the expected fixes for any pixel mismatch.
- [ ] **Step 4: Sanitizers.** ASan/UBSan and TSan trees on Linux Clang (`tools/tsan.sh` after Part A Task A9).
- [ ] **Step 5:** Record compiler, distribution, SDL version, test counts, skips and evidence paths in VERIFICATION.md. Commit `linux: native gate presets and first green run`.

### Task 2: Portable release build and packages

**Files:** Create `tools/build_linux_package.sh`, `tools/verify_linux_package.py`, `linux/road-rash-native.desktop.in`, `linux/org.roadrash.nativeport.metainfo.xml`. Modify `CMakeLists.txt` (install rules), `tools/agent_gate.py` (a `linux-package` gate).

- [ ] **Step 1: Container build.** `tools/build_linux_package.sh [x86_64|aarch64]` runs `cmake --preset linux-release` in the sniper SDK image, pinned by digest (ARM64 uses the matching sniper arm64 SDK if Valve publishes one; otherwise a Debian 11 container with the same flags, recorded as such). OpenSSL builds static from a pinned tarball.
- [ ] **Step 2: Tarball.** `RoadRashNative-<version>-linux-<arch>.tar.gz` contains `road-rash-native/` with `rr-game`, `LICENSE`, `SDL-LICENSE.txt`, the network licences from `third_party/`, the DejaVu licence, `README.txt`, the `.desktop` file and `icon.png`. `icon.png` is the committed `assets/app-icon/roadrash.png` (owner exception; see its README).
- [ ] **Step 3: AppImage.** `RoadRashNative-<version>-<arch>.AppImage` wraps the same tree with `AppRun`, built with the static type-2 runtime, so it needs no `libfuse2`. It embeds update information only if the owner chooses a release channel (owner decision; default none).
- [ ] **Step 4: Verifier.** `verify_linux_package.py` checks:
  - no file matches a reference hash, and none is an ISO/AVI/RRA/BOB by magic;
  - the `NEEDED` allowlist and the `GLIBC_2.31` ceiling, read with `readelf`/`objdump`;
  - `rr-game --version` and `rr-game --first-run-smoke` exit 0 under `SDL_VIDEODRIVER=dummy`;
  - the AppImage extracts and runs the same smoke.
- [ ] **Step 5: Clean-host smoke.** Run the tarball and the AppImage in fresh `ubuntu:20.04`, `debian:12`, `fedora:latest` and `archlinux:latest` containers with only the packages a desktop already has (`libx11-6`, `libasound2`), headless smoke. Record each.
- [ ] **Step 6:** Commit `linux: portable release tarball and AppImage with verifier`.

### Task 3: Flatpak bundle

**Files:** Create `linux/org.roadrash.nativeport.yml`. Modify `tools/build_linux_package.sh`.

- [ ] **Step 1: Manifest.** Runtime `org.freedesktop.Platform//24.08`. The game builds from source in the manifest, not from the tarball, so it is reproducible. `finish-args`:
  - `--socket=wayland`, `--socket=fallback-x11`, `--share=ipc`, `--socket=pulseaudio`, `--device=all` (controllers and hidraw), `--share=network` (plan 34);
  - no `--filesystem` grants: the disc is chosen through the FileChooser portal (the document portal gives access to the picked ISO or folder);
  - the in-game browser (Task 5) runs inside the sandbox, so it lists only `xdg-download` when the owner adds `--filesystem=xdg-download:ro`. Decide in Step 3.
- [ ] **Step 2: Bundle.** `flatpak build-bundle` produces `RoadRashNative-<version>-<arch>.flatpak`. Install it, then import, play and quit on a Fedora or GNOME host.
- [ ] **Step 3: Decisions to record.** The `xdg-download:ro` grant: recommend yes, because it makes Game Mode import work without the portal. Flathub submission is an owner decision (project name and trademark exposure), recorded in plan 15. The bundle is built and verified either way.
- [ ] **Step 4:** Commit `linux: Flatpak manifest and bundle`.

### Task 4: Desktop integration

**Files:** Create `src/platform_linux.cpp/.hpp`, `tests/platform_linux_test.cpp`. Modify `src/game_main.cpp`, `CMakeLists.txt`.

- [ ] **Step 1: Identity.** Set `SDL_HINT_APP_ID` to `org.roadrash.nativeport` and `SDL_HINT_APP_NAME` to `Road Rash Native` before `SDL_Init`, so Wayland compositors and PipeWire match the `.desktop` file and show the right icon and name.
- [ ] **Step 2: Screensaver and idle.** Keep `SDL_DisableScreenSaver` during races and videos; re-enable it in menus after 5 minutes idle. Verify that the GNOME idle inhibitor (D-Bus) engages.
- [ ] **Step 3: Display.** Verify on X11 and Wayland, at 100%, 150% and 200% scale:
  - fullscreen toggle;
  - render-scale "match display";
  - mouse cursor hidden in races;
  - window restore after exiting fullscreen.

  Fix scaling with SDL's pixel-density APIs, never by detecting the window system.
- [ ] **Step 4: Audio.** PipeWire, PulseAudio and ALSA-only hosts (`SDL_AUDIO_DRIVER=alsa`). Unplugging the default device mid-race switches to the new default without stopping the music.
- [ ] **Step 5:** Commit `linux: desktop identity, idle inhibit, display and audio checks`.

### Task 5: Steam Deck and pad-only import and play

**Files:** Create `src/file_browser.cpp/.hpp`, `tests/file_browser_test.cpp`. Modify `src/import_ui.cpp`, `src/platform_linux.cpp`, `src/settings.cpp`, `src/frontend.cpp` (alias entry fallback), `src/game_main.cpp`.

- [ ] **Step 1: Gamepad navigation on the import screen (F11).** D-pad and left stick move focus between "Choose ISO", "Choose folder" and (new) "Browse". A selects, B cancels. Focus is drawn in the existing button style. Test with synthetic `SDL_Event`s: focus order, activation, and a hot-plugged pad.
- [ ] **Step 2: In-game file browser.** A `FileBrowser` model (pure, testable) lists directories and `*.iso` files, case-insensitively, sorted by name (total order). Its roots, from `platform_linux`, are `$HOME`, `$HOME/Downloads`, `/run/media/$USER/*` (SteamOS SD cards and USB drives), `/media/$USER/*` and `/mnt/*`. A view draws it with the import fonts, and it works with pad, keyboard, mouse and touch. It is used when:
  - the user picks "Browse";
  - `SDL_Show*Dialog` reports an error, or no callback arrives within 3 s;
  - Game Mode is detected (it becomes the default action).
- [ ] **Step 3: Auto-discovery.** On first run, scan those roots two levels deep for `ROADRASH/ROADRASH.EXE` (case-insensitive) and `*.iso` files between 400 and 800 MB. Offer the matches as a list. The scan is name-based only; the import still verifies every hash. Cap the scan at 2 s and stop it if the user acts.
- [ ] **Step 4: Deck first-run defaults.** When `platform_linux::is_steam_deck()` or `is_gamescope()`: fullscreen, widescreen `match` (16:10), the render scale that fits 800 lines, the gamepad control profile and Xbox/Deck button glyphs. Setting prompts use `SDL_GetGamepadButtonLabel`, so a Deck shows A/B/X/Y. Unit-test the defaults with the environment overridden.
- [ ] **Step 5: Text entry.** Set `SDL_HINT_ENABLE_STEAM_SCREEN_KEYBOARD=1` before `SDL_Init`. Fallback: in the career alias field, the pad's up/down cycles the character under the cursor (A–Z, 0–9, space, the original's allowed punctuation), left/right moves, and A confirms. This is a native addition, documented in USER_GUIDE and PROGRESS. It is active whenever a gamepad was the last input device, so it does not depend on the Steam keyboard appearing.
- [ ] **Step 6: Suspend/resume.** Treat a wall-clock jump of more than 2 s between frames as a focus loss: pause the race (an explicit resume is required), reopen the audio stream if SDL reports the device lost, and re-scan gamepads. Test through `TickClock` and `SessionState` with injected timestamps.
- [ ] **Step 7:** Commit `linux: pad-only import, file browser, Deck defaults, text entry, resume`.

### Task 6: Determinism and cross-play

**Files:** Modify `tools/determinism_matrix.py`, `.github/workflows/determinism.yml`, `docs/plans/34-multiplayer.md` (Task 10 Step 4).

- [ ] **Step 1:** Add `linux-arm64` (GCC and Clang) and `linux-x86_64-clang` to the matrix, synthetic and disc. The existing `linux-x86_64` GCC job stays.
- [ ] **Step 2:** Run the disc determinism on the Deck (`rr-determinism --disc`, copied over SSH) once the device is available.
- [ ] **Step 3:** Run plan 34 cross-play: one Linux client (Deck when available, otherwise desktop Linux), one macOS client and one browser in a room on `rr-netd`. Results agree and the hashes match. Record it in plan 34 Task 10 Step 4.
- [ ] **Step 4:** Commit `linux: determinism matrix and cross-play`.

### Task 7: CI and release

**Files:** Create `.github/workflows/linux.yml`. Modify `.github/workflows/release.yml`.

- [ ] **Step 1: `linux.yml`.** On push/PR to `main`:
  - `ubuntu-24.04` and `ubuntu-24.04-arm` run `cmake --preset linux-gcc` + build + `ctest` (no disc, so disc tests skip with 77, and the job asserts that only `disc`-labelled tests skipped);
  - a `linux-clang` build;
  - the package build in the sniper container plus `verify_linux_package.py`.
- [ ] **Step 2: `release.yml`.** Tag builds attach the tarball and AppImage for x86-64 and ARM64, and the x86-64 Flatpak bundle, next to the macOS zip. The icon is committed, so CI needs no disc and no secret for it.
- [ ] **Step 3:** Commit `ci: Linux native gate, packages and release artefacts`.

### Task 8: Acceptance on hardware

**Files:** Modify `docs/VERIFICATION.md`, `docs/USER_GUIDE.md`, `docs/CURRENT_STATUS.md` (through `agent-status.json`), this plan.

Run plan 33's per-OS acceptance list on:

- [ ] **Step 1: Desktop Linux.** One x86-64 machine with a GPU, Ubuntu 24.04 (GNOME Wayland) and Fedora or Arch (KDE). Keyboard plus an Xbox-style pad and a DualSense.
- [ ] **Step 2: Steam Deck Desktop Mode.** AppImage and Flatpak. Import from an SD card (exFAT or ext4) and from a lower-case loop-mounted folder.
- [ ] **Step 3: Steam Deck Game Mode.** Add the AppImage as a Non-Steam Game. Check:
  - pad-only import through the in-game browser;
  - every course and mode, career alias entry (Steam keyboard or the fallback);
  - sleep/wake in a race and in a video;
  - the Steam button overlay and quitting through Steam;
  - the battery benchmark: `rr-game --bench` (plan 14 frame statistics) for a 10-minute Napa race at render scale 1×/2× and the Enhanced preset, recording p50/p95 frame time, dropped ticks and battery drain per hour. The target is p95 ≤ 16.7 ms at the Deck defaults.
- [ ] **Step 4: Legibility.** At 1280×800 on the Deck's 7" screen, check the import screen, menus and HUD text. Steam's Deck Verified guideline asks for at least 9 px text at 1280×800. The original 640×480 art scales by 1.67×, so record any screen that falls short.
- [ ] **Step 5: Document** the install, adding to Steam, import, data locations and the controller layout in USER_GUIDE.md. Commit `docs: Linux and Steam Deck acceptance`.

Status: not started. Part A (plan 33) first. Tracked in [00-INDEX.md](00-INDEX.md).
