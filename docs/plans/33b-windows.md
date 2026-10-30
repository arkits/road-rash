# Plan 33b: Windows

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax. Read [00-INDEX.md](00-INDEX.md) and [33-linux-windows-ports.md](33-linux-windows-ports.md) (review findings, Part A, acceptance) first.

**Goal:** `rr-game.exe` runs natively on Windows 10 (1903+) and Windows 11, x86-64 and ARM64:
- it builds with MSVC (the release toolchain), with clang-cl and MinGW clang64 as build checks;
- every test and golden matches macOS;
- it ships as a portable zip and a per-user installer with code-signing hooks and no game data;
- it plays with keyboard, Xbox and PlayStation controllers.

**Architecture:** The same sources and CMake build. This plan adds:
- MSVC compiler options and presets;
- vcpkg for OpenSSL (plan 34 transport);
- a static CRT and static SDL3, so the zip has no DLLs;
- an application manifest (UTF-8 code page, Per-Monitor-V2 DPI, long paths);
- a resource file (icon, version info);
- `WIN32_EXECUTABLE` for the game (no console window).

`rr-netd` stays Linux-only; its room and sealer libraries and their tests build on Windows.

**Tech stack:** Visual Studio 2022 17.x (MSVC 19.4x, `v143`), clang-cl from the VS LLVM component, MSYS2 clang64, CMake ≥ 3.25 presets with Ninja, vcpkg manifest mode (baseline pinned), Inno Setup 6 for the installer, `signtool` behind a hook. GitHub runners `windows-2022` and `windows-11-arm`.

## Baseline (October 29, 2026)

- MSYS2 clang64 built `rr-game` on October 26 (local `build-win`); CTest was not run (PROGRESS.md).
- MSVC fails on `__int128` (F1). Expected next failures from the audit: `<unistd.h>` in tests (F6), POSIX sockets in `server/main.cpp` (F10), GCC flags under the `RR_*` options (F8).
- No Windows machine is attached to this workspace. Local iteration can cross-compile with llvm-mingw and run CTest under the existing Wine install as a quick check, but **Wine results never count as Windows acceptance**. Acceptance uses GitHub Windows runners for builds and tests, and real hardware for the device checklist.

### Task 1 result (October 30, 2026)

**The Windows native gate is green on GitHub runners** (workflow `Windows`, run `41018820912`, branch `port/plan33b-windows`). No disc is on CI, so disc and fixture tests exit 77.

| Job | Toolchain | Result |
| --- | --- | --- |
| `msvc-x64` | MSVC 19.44 (VS 2022 17.14), `windows-2022` | 128/128, 35 skipped |
| `msvc-arm64` | MSVC 19.51 (VS 18), native ARM64, `windows-11-arm` | 128/128, 35 skipped |
| `clang-cl-x64` | clang-cl 19.1.5 | unit tests 84/84 |
| `msys2-clang64` | Clang 22.1.8, libc++ | unit tests 84/84 |

The synthetic determinism hashes match the macOS reference on all four. The skips are:
- the 28 disc-labelled tests;
- the git-ignored oracle fixtures;
- `oracle-patch` (needs the original EXE);
- `net_deploy` (Linux-only deploy tool).

Four CI rounds found and fixed:
- **Duplicate objects.** `rr-game` re-listed `race_*`/`game_types` sources already in `rr-resource`. `lld-link` rejected the duplicates.
- **Visual Studio version.** The VS generator found no VS 2022 on the ARM image (it has VS 18). The presets now use Ninja Multi-Config inside `ilammy/msvc-dev-cmd`.
- **POSIX-only Python tooling:**
  - `os.getuid` → `user_tag()`;
  - `pid_alive` used `os.kill(pid, 0)`, which **terminates** the process on Windows; it now queries the process instead;
  - `fcntl` in the Linux deploy tool → `net_deploy` skips on Windows;
  - the Wine-prefix capture tool and its tests are macOS/Linux-only;
  - the shebang fake replay gets a `.cmd` wrapper.
- **Missing fixture.** `oracle-sim` now skips (77) without its git-ignored fixture.
- **A real intermittent bug on every platform:** `render-threads-alloc` failed when a render worker first drew a wide quad in a later frame. Four `thread_local` 960-row vectors in `draw_textured_quad_wide` were sized then, plus a 16-byte MSVC debug proxy for each. The workspace now lives in the pool's per-band `RasterScratch64`.

Local supplement (**not** Windows acceptance): `cmake/toolchains/mingw-w64-x86_64.cmake` cross-builds everything with MinGW GCC 14 on macOS. Under Wine, with the disc, it passed **125/125 with 0 skipped**, including all four oracle-sim replays (`verify_sim.py` honours `RR_SIM_EMULATOR`).

**Open in Task 1:** Step 7, the disc run on a real Windows machine. It is blocked because no Windows machine is attached and CI must not hold game data.

## Global constraints

- **Minimum OS:** Windows 10 1903 (build 18362) x64, because the UTF-8 active code page in the manifest needs it. Windows 11 ARM64 is native, not emulated. Windows 7/8.1 are not supported; record that in USER_GUIDE.
- **Static runtime:** `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>`, so no VC++ redistributable is needed. The packaged exe imports only system DLLs. The verifier holds an allowlist (`KERNEL32`, `USER32`, `GDI32`, `SHELL32`, `OLE32`, `ADVAPI32`, `IMM32`, `WINMM`, `VERSION`, `SETUPAPI`, `CFGMGR32`, `HID`, `WS2_32`, `IPHLPAPI`, `CRYPT32`, `BCRYPT`, `DWMAPI`, `UXTHEME`, plus what SDL3's static build actually imports; generate the list once and review it).
- **No game data** in the zip or the installer, verified the same way as macOS and Linux.
- **Data locations:** `%APPDATA%\RoadRashNative\RoadRash` (`SDL_GetPrefPath`), which roams with the profile. The 518 MB imported disc should not roam, so Task 2 moves the `game/` tree to `%LOCALAPPDATA%` and migrates an existing tree once.
- **Signing:** `tools/sign_windows.ps1` is a hook that does nothing unless the `RR_SIGN_*` environment variables are set. Certificates and passwords stay with the owner, and Claude never handles them. An unsigned build triggers SmartScreen, and USER_GUIDE says so.

## Review focus

1. **Non-ASCII user profiles** (`C:\Users\Jürgen`, `C:\Users\王伟`). Saves, settings, import and the upscale cache must all work. Part A Task A4 plus the UTF-8 manifest. Test it in CI with `RR_PREF_OVERRIDE` pointing at a Unicode directory.
2. **STL differences.** MSVC's `std::sort` orders ties differently from libc++. Part A Task A2 must land before any pixel golden comparison here.
3. **Antivirus file locks.** Defender may scan `settings.txt.tmp` or the staging tree mid-rename. `write_file_atomic` and the import commit retry `ERROR_ACCESS_DENIED` / `ERROR_SHARING_VIOLATION` with backoff, and never leave a half-written file.
4. **Frame pacing.** Check vsync and non-vsync pacing on 60/144/165 Hz displays, and `SDL_DelayNS` precision with SDL3's high-resolution waitable timer. The 60 Hz simulation must not drift or stutter.
5. **Keyboard layouts.** Race controls must use scancodes, so AZERTY, QWERTZ and Dvorak keep the physical layout. Menu text entry uses text events. Audit `input.cpp` / `race_actions.cpp` and the controls screen labels (`SDL_GetKeyName(SDL_GetKeyFromScancode(...))`).
6. **ARM64.** Build natively for ARM64 (not ARM64EC). Run the determinism disc race on the `windows-11-arm` runner.

---

## File structure

| File | Responsibility |
| --- | --- |
| `CMakePresets.json` | `windows-msvc`, `windows-clang-cl`, `windows-msvc-arm64`, `windows-mingw`, `windows-release` (shared `base` with 33a) |
| `vcpkg.json`, `vcpkg-configuration.json` | OpenSSL for libdatachannel, baseline pinned; static triplets `x64-windows-static`, `arm64-windows-static` |
| `cmake/msvc.cmake` | MSVC/clang-cl options: `/utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /bigobj /fp:precise`, `/W4` strict mapping, `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS` |
| `windows/rr-game.manifest` | UTF-8 active code page, Per-Monitor-V2 DPI awareness, `longPathAware`, supportedOS Win10/11 |
| `windows/rr-game.rc.in` | Icon (`assets/app-icon/roadrash.ico`), VERSIONINFO from `build_info.py`; the manifest is `windows/rr-app.manifest` via `rr-app.rc.in` in every executable |
| `windows/installer.iss` | Inno Setup per-user installer (no admin), Start-menu shortcut, uninstaller keeps user data unless asked |
| `src/platform_windows.cpp/.hpp` | Local-app-data path, retry-safe rename, file log, crash dump hook |
| `src/atomic_file.cpp`, `src/importer.cpp` | `_commit` flush; rename retry; game-data location |
| `tools/build_windows_package.ps1`, `tools/verify_windows_package.py`, `tools/sign_windows.ps1` | Build, verify and (optionally) sign the zip and installer |
| `.github/workflows/windows.yml` | Native gate on x64 and ARM64, toolchain checks, package build and verification |
| `docs/USER_GUIDE.md` | Windows install, SmartScreen, import, controls, data locations |

---

### Task 1: Windows native gate

**Files:** Create `cmake/msvc.cmake`, `vcpkg.json`, `vcpkg-configuration.json`; add the Windows presets to `CMakePresets.json`. Modify `CMakeLists.txt`, `cmake/net.cmake`, `server/CMakeLists.txt`.

- [x] **Step 1: Compiler options (F8).** `cmake/msvc.cmake` is included when `MSVC` is set. *(Done. The presets use Ninja Multi-Config in an MSVC developer environment, not the VS generator, so any VS version works.)* Under MSVC, `RR_STRICT_WARNINGS` maps to `/W4 /permissive-`, with the warning numbers that correspond to `-Wshadow`/`-Wconversion` (C4456–C4459, C4244, C4267), and `/WX` follows the plan 22 ratchet. `RR_TSAN` and `RR_FUZZ` fail at configure time under MSVC with a clear message. ASan is available as `/fsanitize=address` behind `RR_ASAN`.
- [x] **Step 2: Server split (F10).** `server/CMakeLists.txt` builds `rr-netd-core` (room, sealer) on every platform and the `rr-netd` executable only `if(NOT WIN32)`. The `server_room`, `server_sealer` and `server_hostile` tests stay on Windows if they use only the core. A test that needs the POSIX HTTP listener exits 77 on Windows with the reason.
- [x] **Step 3: OpenSSL.** Use vcpkg manifest mode with the `x64-windows-static` / `arm64-windows-static` triplets. libdatachannel links the static OpenSSL. `RR_NET_TRANSPORT=OFF` still builds without vcpkg, for quick local work.
- [x] **Step 4: Static CRT and SDL.** `CMAKE_MSVC_RUNTIME_LIBRARY` is static. `RR_STATIC_SDL=ON` (Part A Task A7) is the default in `windows-release` and allowed in Debug.
- [x] **Step 5: Build and test.** Run `cmake --preset windows-msvc`, build, then `ctest` on the `windows-2022` runner (no disc). Fix each failure at its root. Part A Tasks A1/A4/A6 are prerequisites. The `oracle/` C tests (`patch_test.c`, `actor_record_buffer_test.c`) must compile as C under MSVC; fix any GNU C there.
- [x] **Step 6: Toolchain checks.** `windows-clang-cl` and `windows-mingw` (MSYS2 clang64) build everything and run the unit-labelled tests. Neither is a release toolchain; they exist to catch MSVC-only assumptions in both directions.
- [ ] **Step 7: Disc run.** *(Blocked October 30: no Windows machine with the disc.)* On a Windows machine with the disc (owner hardware, or a self-hosted runner if the owner approves), run the full suite with the disc. Every oracle and pixel golden equals macOS.
- [x] **Step 8:** Record everything in VERIFICATION.md. Commit `windows: MSVC native gate`.

### Task 2: Unicode paths, file safety and data location

**Files:** Create `windows/rr-game.manifest`, `src/platform_windows.cpp/.hpp`, `tests/platform_windows_test.cpp`. Modify `src/atomic_file.cpp`, `src/importer.cpp`, `src/game_types.cpp`, `CMakeLists.txt`.

- [ ] **Step 1: Manifest.** Set `<activeCodePage>UTF-8</activeCodePage>`, `<dpiAwareness>PerMonitorV2</dpiAwareness>`, `<longPathAware>true</longPathAware>`, and the Windows 10/11 `supportedOS` GUIDs. Embed it in `rr-game.exe` and in every test executable, so tests see the same code page as the game.
- [ ] **Step 2: Unicode test.** Run `utf8_path_test` (Part A Task A4), plus a full first-run smoke with `RR_PREF_OVERRIDE=%TEMP%\Tëst ディレクトリ`: import a synthetic disc, save settings, save and load a career.
- [ ] **Step 3: Atomic writes.** Flush with `_commit(_fileno(f))` before rename. If the rename fails with access denied or a sharing violation, retry up to 10 times with 10–200 ms backoff, then fail cleanly and keep the old file. Test it by holding the destination open without `FILE_SHARE_DELETE` in a test thread.
- [ ] **Step 4: Game data location.** On Windows, `game_directory()` returns `%LOCALAPPDATA%\RoadRashNative\RoadRash\game` (`SHGetKnownFolderPath(FOLDERID_LocalAppData)`, UTF-16 → UTF-8). If an old `%APPDATA%` tree exists and the new one does not, move it once (rename on the same volume, copy and verify otherwise). Saves and settings stay in `%APPDATA%`, where they roam.
- [ ] **Step 5:** Commit `windows: UTF-8 manifest, retry-safe atomic writes, local game data`.

### Task 3: Game executable and resources

**Files:** Create `windows/rr-game.rc.in`. Modify `CMakeLists.txt`, `src/game_main.cpp`, `src/platform_windows.cpp`, `tools/build_info.py`.

- [ ] **Step 1: No console.** Set `WIN32_EXECUTABLE ON` for `rr-game` only. `SDL_main.h` is already included, so SDL provides `WinMain`. CLI tools and tests stay console programs. `rr-game --console` attaches to the parent console (`AttachConsole(ATTACH_PARENT_PROCESS)`) so `--version`, `--bench` and smoke output print when started from a terminal.
- [ ] **Step 2: Logging.** `SDL_SetLogOutputFunction` writes to `%LOCALAPPDATA%\RoadRashNative\RoadRash\rr-game.log`, keeping the last 2 runs, as well as `OutputDebugString`. USER_GUIDE tells users to attach this log to bug reports.
- [ ] **Step 3: Resources.** The icon is the committed `assets/app-icon/roadrash.ico` (the original 16/32/48 px group; owner exception, see its README). `VERSIONINFO` uses the version and commit from `build_info.py`. The `ProductName` is "Road Rash Native".
- [ ] **Step 4: Window behaviour.** Alt+Enter toggles fullscreen and Alt+F4 quits through the normal quit path (saves flushed). Fullscreen uses SDL3's borderless desktop mode, with `SDL_HINT_WINDOWS_RAW_KEYBOARD` left at the default. Check that the taskbar icon and title match.
- [ ] **Step 5: Fonts.** Part A Task A8 (`%WINDIR%\Fonts`, bundled fallback). Check that the import screen renders with Segoe UI.
- [ ] **Step 6:** Commit `windows: GUI subsystem, log file, icon and version resources`.

### Task 4: Input, audio and pacing

**Files:** Modify `src/input.cpp`, `src/race_actions.cpp`, `src/native_screens.cpp` (controls labels), `docs/REVERSE_ENGINEERING.md` (if any original key semantics are touched).

- [ ] **Step 1: Scancodes.** Audit that race bindings are scancode-based and that labels come from the current layout. Test with a synthetic AZERTY key map via `SDL_GetKeyFromScancode` overrides.
- [ ] **Step 2: Controllers.** Test Xbox (XInput and Windows.Gaming.Input), DualShock 4 / DualSense (SDL's HIDAPI driver) and a generic DirectInput pad. Hot-plug mid-race pauses, and reconnecting resumes input. The button glyphs follow `SDL_GetGamepadButtonLabel`.
- [ ] **Step 3: Audio.** WASAPI shared mode. A default-device change (headset plugged in) mid-race and mid-video moves the output without desynchronising the video clock.
- [ ] **Step 4: Pacing.** Run `rr-game --bench` (plan 14 frame statistics) at 60/144 Hz, with vsync on and off. The race keeps exactly 60 ticks/s (tick counter vs wall clock over 10 minutes, drift < 1 tick), and interpolation (plan 32 R2/R3) is smooth at 144 Hz.
- [ ] **Step 5:** Commit `windows: input layout, controllers, audio device change, pacing checks`.

### Task 5: Packaging

**Files:** Create `windows/installer.iss`, `tools/build_windows_package.ps1`, `tools/verify_windows_package.py`, `tools/sign_windows.ps1`. Modify `tools/agent_gate.py` (a `windows-package` gate).

- [ ] **Step 1: Portable zip.** `RoadRashNative-<version>-windows-<x64|arm64>.zip` contains `Road Rash Native\rr-game.exe`, the licences (SDL, network stack, DejaVu, project), `README.txt` and `portable.txt`. When `portable.txt` sits next to the exe, the pref path is `.\data\` instead of `%APPDATA%`, so the game runs from a USB stick. This is a native addition; document it.
- [ ] **Step 2: Installer.** Inno Setup builds `RoadRashNative-<version>-setup-<arch>.exe`: a per-user install to `%LOCALAPPDATA%\Programs\Road Rash Native`, with no admin prompt. It adds a Start-menu shortcut, an optional desktop shortcut and an uninstaller that keeps saves and the imported disc unless the user ticks "remove game data".
- [ ] **Step 3: Verifier.** `verify_windows_package.py` uses `pefile`, which the icon tool already needs. It checks:
  - the imports are on the allowlist, and the machine type (AMD64/ARM64) is right;
  - the manifest is embedded with the UTF-8 code page;
  - the version resource matches the commit;
  - no packaged file matches a reference hash or has disc-data magic;
  - the zip's `rr-game.exe --console --version` runs on the runner.
- [ ] **Step 4: Signing hook.** `sign_windows.ps1` runs `signtool sign /fd sha256 /tr <timestamp URL> ...` on the exe and the installer when `RR_SIGN_CERT_THUMBPRINT` (or an Azure Trusted Signing profile) is configured, and is skipped otherwise. Which certificate, if any, is an owner decision, recorded in plan 15.
- [ ] **Step 5:** Commit `windows: portable zip, per-user installer, verifier, signing hook`.

### Task 6: Determinism and cross-play

**Files:** Modify `tools/determinism_matrix.py`, `.github/workflows/determinism.yml`, `docs/plans/34-multiplayer.md`.

- [ ] **Step 1:** *(Synthetic part done October 30: every Windows CI job compares `rr-determinism --synthetic` with the reference, and all four match. Disc race open, blocked with Task 1 Step 7.)* Add `windows-msvc-x64`, `windows-clang-cl-x64` and `windows-msvc-arm64` jobs to the matrix, synthetic on CI. Run the disc race wherever a disc is available.
- [ ] **Step 2:** Plan 34 cross-play: a Windows client in a room with macOS, Linux and browser clients. Results agree and hashes match. Confirm that Windows Defender Firewall shows no prompt, because connections are outgoing only. Record it in plan 34 Task 10 Step 4.
- [ ] **Step 3:** Commit `windows: determinism matrix and cross-play`.

### Task 7: CI and release

**Files:** Create `.github/workflows/windows.yml`. Modify `.github/workflows/release.yml`.

- [ ] **Step 1: `windows.yml`.** *(Build and test jobs exist since October 30; the package build and the skip assertion come with Task 5.)* On push/PR to `main`:
  - `windows-2022` runs the `windows-msvc` build and `ctest`, asserting that only `disc`-labelled tests skipped;
  - `windows-clang-cl` and `windows-mingw` build checks;
  - `windows-11-arm` runs the `windows-msvc-arm64` build and `ctest`;
  - the package build plus `verify_windows_package.py`.

  Cache vcpkg binary packages through GitHub Actions cache.
- [ ] **Step 2: `release.yml`.** Tags attach the x64 and ARM64 zips and installers, signed when the owner's signing secrets exist. The icon is committed, so no owner decision is needed for it.
- [ ] **Step 3:** Commit `ci: Windows native gate, toolchain checks, packages`.

### Task 8: Acceptance on hardware

**Files:** Modify `docs/VERIFICATION.md`, `docs/USER_GUIDE.md`, `docs/CURRENT_STATUS.md` (through `agent-status.json`), this plan.

Run plan 33's per-OS acceptance list on:

- [ ] **Step 1: Windows 11 x64** with a discrete or integrated GPU at 100% and 150% display scaling, and a non-ASCII user name, or a profile path containing one.
- [ ] **Step 2: Windows 10 22H2 x64**, a second machine or a VM with GPU passthrough. Record which.
- [ ] **Step 3: Windows 11 ARM64** (Snapdragon X or similar), running the native ARM64 build.
- [ ] **Step 4:** On each one, install from both the zip and the installer, starting with SmartScreen on a downloaded file. Then:
  - import from the ISO and from a folder (including a DVD drive letter if available);
  - play every course and mode with keyboard and Xbox/DualSense;
  - save and reload;
  - change every setting;
  - sleep/resume and alt-tab;
  - uninstall, then reinstall, and confirm the data is kept.
- [ ] **Step 5:** Document install, SmartScreen, import, controls and data locations in USER_GUIDE.md. Commit `docs: Windows acceptance`.

Status: Task 1 done except the disc run on real Windows (Step 7, blocked: no device); Task 6 Step 1 synthetic part done; Tasks 2–5 and 8 open. Tracked in [00-INDEX.md](00-INDEX.md).
