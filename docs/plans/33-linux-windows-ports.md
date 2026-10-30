# Plan 33: Linux, Steam Deck and Windows ports

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax. Read [00-INDEX.md](00-INDEX.md) first.

**Goal:** the same game, built from the same sources, runs natively on Linux (x86-64 and ARM64, including the Steam Deck) and Windows (x86-64 and ARM64). Every platform shares one simulation, the same goldens and the same disc import. No platform ships game data.

**Structure.** This file holds the review findings, the shared portability work (Part A) and the per-OS acceptance rules. The per-OS work is in two child plans:

- [33a — Linux and Steam Deck](33a-linux-steam-deck.md)
- [33b — Windows](33b-windows.md)

**Order.** Part A starts now. It is small, it fixes latent bugs on every platform, and none of it waits for macOS feature completion. 33a Tasks 1–3 and 33b Tasks 1–3 follow Part A and can run in parallel with the simulation plans. Device acceptance (33a Task 8, 33b Task 8) runs after plan 17, so it covers every course and mode. Plan 34 Task 10 Step 4 (online cross-play) depends on 33a Task 6 and 33b Task 6.

**Tech stack:** C++20, SDL3 (pinned source build for packages), CMake ≥ 3.22 with `CMakePresets.json`, GCC 14 / Clang 19 on Linux, MSVC 19.4x (VS 2022 17.x) / clang-cl on Windows, libdatachannel + OpenSSL for plan 34.

---

## Review findings (October 29, 2026)

The audit covered `CMakeLists.txt`, `cmake/`, every `src/` file, `tests/`, `tools/`, `server/` and the CI workflows. It found the following.

### Already portable

- **Platform layer.** All windowing, input, audio, file dialogs, threads and paths go through SDL3 and `std::filesystem`. Platform `#if` blocks are limited to `import_ui.cpp` (font paths, Android buttons), `settings.cpp` / `race_setup.cpp` / `lifecycle.hpp` (Android defaults), `atomic_file.cpp` (already guards `fsync` on `_WIN32`) and the Emscripten branches.
- **Simulation.** No `float` or `double` in `race.cpp`, `bike.cpp`, `traffic.cpp`, `police.cpp`, `crash.cpp`, `pedestrians.cpp`, `race_schedule.cpp` or `track_ring.cpp`. It uses no `long` and no `<random>`. It uses no `unordered_*` iteration (only `hud.hpp`, which is presentation).
- **Cross-platform determinism already measured.** `tools/determinism_matrix.sh` and `.github/workflows/determinism.yml` show bit-identical per-60-tick hashes on macOS ARM64 (AppleClang), Linux x86-64 (Debian trixie GCC, Docker) and wasm32. That covers the 8-human synthetic race and the disc race (plan 34 Task 3 Step 5).
- **Frame clock.** `TickClock::advance` (`src/session.cpp`) caps catch-up at `max_steps_per_frame`, so a suspend/resume (Steam Deck sleep, laptop lid) does not fast-forward the race.
- **Pref paths.** `SDL_GetPrefPath("RoadRashNative","RoadRash")` resolves to `$XDG_DATA_HOME/RoadRashNative/RoadRash` on Linux and `%APPDATA%\RoadRashNative\RoadRash` on Windows.
- **Windows probe.** `rr-game` built with MSYS2 clang64 on October 26 (`PROGRESS.md`); CTest was not run there.

### Blockers and defects found

| # | Finding | Where | Affects | Fix |
| --- | --- | --- | --- | --- |
| F1 | `__int128` in the RGB rasteriser (`floor64`, `prod`) | `src/raster_rgb.cpp:412,426` | MSVC, clang-cl (does not compile) | A1 |
| F2 | `std::sort` with non-total comparators on equal depth/distance; libc++, libstdc++ and MSVC STL order ties differently | `src/traffic.cpp:1136` (car draw order), `src/race_helpers.cpp:261` (rider items), `src/sound.cpp:381` (nearest-engine slots) | pixel/audio goldens across platforms | A2 |
| F3 | Floating-point contraction differs by compiler: GCC defaults to `-ffp-contract=fast`, Clang to `on`, MSVC to off. FMA fusion changes float results in the presentation and enhanced paths. | build flags | cross-platform pixel goldens for scaled/enhanced output | A3 |
| F4 | UTF-8 strings from SDL (`SDL_GetPrefPath`, dialog results) are turned into `std::filesystem::path` from `const char*`, and back with `.string()`. On Windows both use the ANSI code page. A non-ASCII user name breaks saves, settings and import. | `src/game_types.cpp:275`, `src/importer.cpp:102–106`, `src/game_main.cpp:323,343`, `src/race_caches.hpp:186`, `src/importer.cpp:33` | Windows | A4 + 33b Task 2 |
| F5 | Folder import looks up exact upper-case names (`source_root / "DATA/COURSES/NAPA.CRS"`). The Linux iso9660 driver lowercases plain ISO9660 names by default (`map=normal`), and users copy discs with arbitrary case. On macOS the case-insensitive filesystem hides this. | `src/importer.cpp:146,193` | Linux, Steam Deck | A5 |
| F6 | Tests include `<unistd.h>` / `<sys/stat.h>` for temp dirs and permissions | `tests/{assets,frontend,settings,upscale_import,upscale_cache,import_job,importer,atomic_file}_test.cpp` | MSVC | A6 |
| F7 | `#pragma GCC diagnostic` without a compiler guard (C4068 on MSVC) | `src/import_ui.cpp:16–24` | MSVC warnings | A6 |
| F8 | `RR_STRICT_WARNINGS`, `RR_TSAN` and `RR_FUZZ` add GCC-style flags unconditionally | `CMakeLists.txt` | MSVC | 33b Task 1 |
| F9 | Static SDL is macOS-only (`RR_BUNDLE_SDL` fails on any other OS). SDL is pinned at 3.4.8, which is older than `SDL_HINT_ENABLE_STEAM_SCREEN_KEYBOARD` (3.4.12). | `CMakeLists.txt`, `cmake/sdl3_static.cmake` | Linux/Windows packages, Steam Deck text entry | A7 |
| F10 | `rr-netd` (`server/main.cpp`) uses POSIX sockets and signals, and is always added | `cmake/net.cmake` | Windows build | 33b Task 1 (server stays Linux-only; room/sealer libraries and their tests stay portable) |
| F11 | Import screen has no gamepad navigation (only `I`/`F` keys, mouse and touch) | `src/import_ui.cpp:265–293` | Steam Deck Game Mode, any pad-only setup | 33a Task 5 |
| F12 | Import-screen fonts are hard-coded paths. `C:/Windows/Fonts` ignores `%WINDIR%`. The Linux DejaVu paths miss Fedora (`/usr/share/fonts/dejavu-sans-fonts`) and any system without DejaVu. When nothing loads, the screen falls back to the 8×8 debug font. | `src/import_ui.cpp:59–80` | Linux, Steam Deck, Windows | A8 |
| F13 | Tool scripts assume macOS: `sysctl` and `/opt/homebrew` in `Makefile`, `tools/tsan.sh`, `tools/fuzz.sh`, `tools/agent_gate.py`, `tools/agent_doctor.py`; `tools/determinism_matrix.sh` is POSIX `sh` only | tools | Linux dev hosts, Windows CI | A9 |
| F14 | The full native gate (`rr-game`, all CTests) has never run on Linux; only `rr-determinism` was built there | — | Linux | 33a Task 1 (first run recorded below) |
| F15 | Plain `char` is unsigned on Linux/Android ARM64 and signed on macOS/Windows/x86. `oracle/actor_record_buffer_test.c:43` (`char clip == -7`) fails on Linux ARM64; any `src/` byte read through plain `char` (for example `race_setup.cpp:219`) has the same exposure, **including the shipping Android build** | oracle hooks, `src/` audit | Linux ARM64, Android ARM64 | A10 |
| F16 | Transcendental `libm` calls differ in the last ulp between Apple libm, glibc, MSVC CRT and Bionic: `std::exp` in `fog.cpp:16`, `std::log2` (mip LOD) in `raster_rgb.cpp:650` and `std::pow` in `sound.cpp:157,163`. **Corrected October 29 (A11 Step 1):** the `raster-rgb-alloc` mismatch was *not* libm. With contraction off on both hosts, macOS and Linux render identical frames (0 of 307,200 pixels differ); the old pin `0x861b81d25eb5f7db` had been taken with AppleClang fusing multiply-adds (F3). The libm calls were still replaced, because their results are not specified bit for bit across platforms | enhanced RGB/fog path, sound volume/pan | every non-Apple platform | A3, A11 |
| F17 | `find_package(SDL3 3.2)` accepts distribution SDL 3.2.x, but tests assume 3.4 behaviour: `input` ("phone controls render outside the 4:3 scene viewport") fails with Debian's 3.2.10 | `CMakeLists.txt`, `tests/input_test.cpp` | Linux distro builds | A7 Step 5 |

### First Linux build (this review)

Debian trixie, `linux/arm64` Docker on the Apple-silicon host, GCC 14.2.0, distribution SDL 3.2.10, disc mounted read-only, Debug build of the default configuration (network transport on). The build had one error (missing `<algorithm>` in `tests/world_renderer_test.cpp`, fixed). **CTest: 117/121 passed.** The four failures were `oracle-actor-record` (F15), `raster-rgb-alloc` (F16), `input` (F17) and `agent-tooling` (this review's own new plan files, run before the index rows were added; fixed). Every simulation test passed, including `oracle-sim`, the three `oracle-sim-field*` replays, `race-soak`, `rollback`, `race_state` and the `net_*` suite. Details are in [33a](33a-linux-steam-deck.md) under "Baseline".

---

## Portability rules (apply to all code now)

- Platform calls go through SDL3 or the existing platform layer. New `#if` platform blocks are allowed only in `src/platform_*.cpp`, `import_ui.cpp` font discovery, packaging scripts and `windows/` / `linux/` / `macos/` resources.
- Simulation and rendering code must not depend on the compiler, STL, `libm`, `char` signedness, endianness, `long` width, floating-point contraction or unspecified evaluation order. Every `std::sort` whose output reaches the simulation, the screen or the audio mixer must use a total order (A2).
- Paths that cross the SDL boundary are UTF-8. Convert them with `rr::path_from_utf8` / `rr::path_to_utf8` (A4), never with `std::filesystem::path(const char*)` or `.string()`.
- Disc files are found case-insensitively at import and stored upper-case. Runtime reads use the stored upper-case names.
- Tools and tests must not assume `fish`, Homebrew paths, `sysctl` or BSD `sed`/`stat` flags. CTest drivers are C++ or Python, never shell.
- No game data, disc-derived dumps or upscaled art in any package. The app icon follows the existing Android/web policy (`tools/extract_app_icon.py`, derived locally at build time, never committed).

---

## Part A: shared portability groundwork

Each task lands separately, keeps every existing macOS test, golden and determinism hash unchanged, and is accepted on macOS plus the Linux Docker build.

### Part A results (October 29, 2026)

All eleven tasks are implemented on branch `port/plan33-part-a`. Evidence is in [VERIFICATION.md](../VERIFICATION.md) under "Plan 33 Part A". Decisions and deviations from the task text below:

- **A1** `src/wide_math.hpp`: `floor_q16_i64` (exactly `a >> 16` in C++20) and `mul_shr16` (`__int128`, MSVC `_mul128`/`__mulh`, or a portable 32-bit schoolbook reference). Both `texture.cpp` (the original 640×480 raster) and `raster_rgb.cpp` used `__int128`, not only the RGB path. `wide_math_test` compares every path on edge values and 4,000 LCG values instead of logged frame arguments; the pixel goldens cover real arguments.
- **A2** Every `std::sort` in `src/` carries a `total-order:` reason. Five sorts with possible ties became `std::stable_sort` (sound candidates, rider items, traffic draw order, traffic resources) or gained a name tie-break (load list). Stable order equals libc++'s insertion sort on these short lists, so macOS output is unchanged. Whether the original breaks depth ties the same way is **not** traced; that belongs to the native-scene-list comparison (plans 11/16), not to portability.
- **A3** `-ffp-contract=off` (GCC/Clang, including Emscripten and Android) and `/fp:precise` (MSVC). This was the real cause of the Linux `raster-rgb-alloc` failure (F16 corrected). The only golden that moved is that test's pin, now `0xbab29462f0960895`, measured identical on macOS ARM64 and Linux ARM64. Every other golden stayed unchanged.
- **A4** `src/utf8_path.hpp`: `path_from_utf8`, `path_to_utf8`, `open_file` at the SDL ↔ `std::filesystem` boundaries (pref paths, settings, saves, upscale cache, import, atomic writes). **Decision:** the many `std::string` paths elsewhere are UTF-8 by contract. On Windows, the narrow STL/CRT calls are made UTF-8 by the `activeCodePage` manifest (33b Task 2), rather than by rewriting every call site.
- **A5** `CaseResolver` in `importer.cpp`. `importer_case_test` covers a lower-case loop mount and a mixed-case copy. The "differ only in case" rejection runs where the temp file system is case-sensitive (Linux; it reports "not applicable" on default APFS). The ISO path already compared upper-case names with `;1` stripped (`iso9660.cpp`).
- **A6** `tests/temp_dir.hpp` (`process_tag()`) replaces `getpid()`. No test needed a POSIX permission call. The stb pragmas are guarded for GCC/Clang and MSVC.
- **A7** `RR_STATIC_SDL` (the pinned source SDL on any desktop; `RR_BUNDLE_SDL` keeps meaning "macOS app" and implies it). The pin is now SDL **3.4.18**. `find_package(SDL3 3.4)`; an older system SDL falls back to the pinned build automatically (F17). On Linux, SDL's own strict dependency check applies, and **`RR_SDL_HEADLESS`** builds SDL without X11/Wayland for CI, determinism and bot containers (`determinism.yml`, `determinism_matrix.py`, `deploy/road-rash-net/smoke.Dockerfile`). Android stays on the committed 3.4.8 AAR while its device work is in flight; the browser build moves to 3.4.18 with the shared pin.
- **A8** `src/system_fonts.cpp`: per-OS candidates (`%WINDIR%`; DejaVu, Noto and Liberation in Debian/Arch-SteamOS/Fedora/openSUSE locations), then `fonts/DejaVuSans*.ttf` next to the executable. **Decision:** no font binaries in the source tree. The 33a/33b package scripts place the pinned DejaVu files in `fonts/`.
- **A9** `llvm.cmake`, `tsan.sh` and `fuzz.sh` use Homebrew LLVM on macOS and the clang on `PATH` elsewhere (or `LLVM_BIN`). The CPU count comes from `nproc` first. `JAVA_HOME` defaults only where the Homebrew JDK exists. `tools/determinism_matrix.py` replaces the shell matrix (the `.sh` is a wrapper) and adds `linux-arm64`, `windows` and `windows-arm64`.
- **A10** The only failing site was the oracle billboard hook's `char clip`, now `signed char`, which is the same ABI on x86 MSVC. The `src/` audit found plain `char` only as text, and the text and glyph paths already use `unsigned char`/`uint8_t`. A lint rule for "numeric plain `char`" is not reliable, so the Linux ARM64 and Android ARM64 runs of the suite are the guard instead (33a Task 7 CI).
- **A11** `src/det_math.cpp`: fdlibm `exp`/`log`, plus `log2` (exact on powers of two) and `pow10`. It replaces `fog.cpp`, `raster_rgb.cpp` (mip LOD) and both `sound.cpp` `pow` calls. The volume, pan and fog tables hash identically to their pre-change macOS values, and the `det_math` bit hash `0x2012804f2dd7f3bf` is identical on macOS and Linux.
- **Lint:** `tools/portability_check.py` (CTest `portability-lint`) enforces A1, A2, A6 and A11.

Not covered by Part A, and left to the child plans: any Windows compile (33b Task 1), Linux x86-64 and Clang runs (33a Task 1), and device runs.

### Task A1: portable 128-bit fixed-point helpers (F1)

**Files:** Create `src/wide_math.hpp`, `tests/wide_math_test.cpp`. Modify `src/raster_rgb.cpp`, `CMakeLists.txt`.

- [x] **Step 1: Test first.** `wide_math_test` checks `rr::mul_shr64(a, b, s)` (signed 64×64 → 128-bit product, arithmetic right shift) and `rr::floor_div_pow2_64` against `__int128` where the compiler has it, and against a fixed table of edge vectors everywhere: `INT64_MIN`, `INT64_MAX`, ±1, the exact `floor64`/`prod` arguments seen in `raster_rgb` (log 10,000 real argument pairs from a Napa enhanced frame into `tests/data/wide_math_vectors.txt`).
- [x] **Step 2: Implement.** Use `__int128` under GCC/Clang. Under MSVC use `_mul128` on x64 and `__mulh` + `a*b` on ARM64, then combine with a two-word shift. Add a portable 32×32 schoolbook fallback that is tested on every platform.
- [x] **Step 3: Replace** the two lambdas in `raster_rgb.cpp`. Gate: `raster-rgb`, `raster-rgb-threads`, `hires-render` and `widescreen` stay bit-identical on macOS.
- [x] **Step 4:** Commit `portability: portable 128-bit fixed-point helpers`.

### Task A2: total-order sorts (F2)

**Files:** Modify `src/traffic.cpp`, `src/race_helpers.cpp`, `src/sound.cpp`, `src/course.cpp` (audit only), `src/frontend.cpp`. Create `tests/sort_order_test.cpp`.

- [x] **Step 1: Audit.** List every `std::sort`, `std::partial_sort`, `std::nth_element` and `std::set`/`std::map` key with a custom comparator (11 sorts today). For each one, record in REVERSE_ENGINEERING.md (draw/sound order) what the original does on ties. The original draws from linked lists in insertion order, so the native tie-break is the stable insertion index. Check this against `0x0043ae10`-family draw code before choosing it.
- [x] **Step 2: Test first.** `sort_order_test` builds tie cases (two cars at equal depth, two engines at equal distance) and requires the documented order.
- [x] **Step 3: Fix.** Use `std::stable_sort`, or add an explicit secondary key (index) so the comparator is a strict total order. `frontend.cpp:1311` (load list, equal mtimes) breaks ties by file name.
- [x] **Step 4: Lint.** Add a `tools/cpp_quality.py` check that rejects a new `std::sort(` in `src/` unless the line carries `// total-order:` with the reason.
- [x] **Step 5:** Gate: macOS goldens unchanged (or the changed golden is justified by Step 1 and recorded; never re-blessed silently). Commit `portability: total-order sorts for draw and sound order`.

### Task A3: identical floating point across compilers (F3)

**Files:** Modify `CMakeLists.txt`, `cmake/emscripten.cmake`. Create `tests/fp_contract_test.cpp`.

- [x] **Step 1: Test first.** `fp_contract_test` evaluates `a*b+c` patterns used in `race_helpers.cpp` / `framebuffer_rgb.cpp` with values whose fused and unfused results differ. It fails if the build contracts them.
- [x] **Step 2: Flags.** Add `-ffp-contract=off` on GCC/Clang (all targets, including Android and Emscripten) and `/fp:precise` on MSVC/clang-cl (with no `/fp:contract`). Never use `-ffast-math` or `-Ofast`.
- [x] **Step 3: Measure.** (October 29: contraction-off alone did not fix `raster-rgb-alloc` on Linux ARM64; see F16. Keep this task as a rule, because GCC's default `fast` contraction is a real hazard.) Re-run `hires-render`, `widescreen`, `raster-rgb` and `post-chain` on macOS. If a golden changes, the old golden was contraction-dependent. Record the change with before/after hashes in VERIFICATION.md and regenerate it once, with the reason.
- [x] **Step 4:** Commit `portability: disable FP contraction on every compiler`.

### Task A4: UTF-8 path boundary (F4)

**Files:** Create `src/utf8_path.hpp/.cpp`, `tests/utf8_path_test.cpp`. Modify `src/game_types.cpp`, `src/importer.cpp`, `src/game_main.cpp`, `src/race_caches.hpp`, `src/import_ui.cpp`, `src/atomic_file.cpp`, `src/frontend.cpp`, `src/settings.cpp`.

- [x] **Step 1:** `rr::path_from_utf8(std::string_view)` builds the path from `std::u8string`. `rr::path_to_utf8(const path&)` converts through `u8string()`. `rr::open_file(path, mode)` uses `_wfopen` on Windows and `std::fopen` elsewhere.
- [x] **Step 2: Test first.** `utf8_path_test` round-trips `Tëst ディレクトリ/Ünïcode.rrs`: create, write atomically, list, read back with `SDL_LoadFile`, rename. It runs on every platform. On Windows it is the regression test for F4.
- [x] **Step 3:** Replace every `fs::path(const char*)` that holds SDL-provided text and every `.string()` passed back to SDL or `fopen`. Use `rg 'path\(pref|\.string\(\)\.c_str|path\(override'` as the checklist.
- [x] **Step 4:** Commit `portability: UTF-8 path boundary`.

### Task A5: case-insensitive folder import (F5)

**Files:** Modify `src/importer.cpp`. Create `tests/importer_case_test.cpp` (synthetic fixture only).

- [x] **Step 1: Test first.** Build a synthetic disc folder with the reference layout in lower case (`roadrash/data/courses/napa.crs`) and in mixed case. Use a test-only reference table with synthetic hashes. The import must succeed, and the destination must hold the canonical upper-case names. Also build an ambiguous folder (`DATA/` and `data/` both present): the import fails with a clear message rather than picking one.
- [x] **Step 2: Implement** a resolver that walks each reference path component with a case-insensitive directory match, cached per directory. Also detect the `ROADRASH` sub-directory case-insensitively.
- [x] **Step 3:** Check the ISO path: `iso9660.cpp` compares names case-insensitively and strips `;1`. Add a Joliet/Rock Ridge test only if the parser reads those extensions.
- [x] **Step 4:** Commit `import: case-insensitive folder import`.

### Task A6: portable tests and pragmas (F6, F7)

**Files:** Create `tests/temp_dir.hpp`. Modify the eight tests listed in F6 and `src/import_ui.cpp`.

- [x] **Step 1:** `rr::test::TempDir` (RAII, `std::filesystem::temp_directory_path()` + random suffix, recursive removal) replaces `mkdtemp`/`getpid`/`unistd.h`. Permission tests that use `chmod` become `std::filesystem::permissions`; where Windows cannot express the case (read-only directory), the test exits 77 on `_WIN32` with the reason printed.
- [x] **Step 2:** Wrap the `#pragma GCC diagnostic` block in `#if defined(__GNUC__)`, and add the matching `#pragma warning(push/disable)` for MSVC.
- [x] **Step 3:** Commit `tests: portable temp dirs and warning pragmas`.

### Task A7: static SDL for every desktop package, SDL pin bump (F9)

**Files:** Modify `CMakeLists.txt`, `cmake/sdl3_static.cmake`, `tools/build_macos_app.sh`, `third_party/README.md`, `android/` (only if the AAR is bumped together; otherwise record the version split).

- [x] **Step 1:** Rename `RR_BUNDLE_SDL` to `RR_STATIC_SDL`, keeping `RR_BUNDLE_SDL` as a deprecated alias that sets it. Allow it on Apple, Linux and Windows. The macOS-only parts (`CMAKE_OSX_*`, `MACOSX_BUNDLE`, `codesign`) stay under `if(APPLE)`.
- [x] **Step 2:** Bump the pinned SDL to the newest 3.4.x release that is at least two weeks old and ≥ 3.4.12 (for `SDL_HINT_ENABLE_STEAM_SCREEN_KEYBOARD`). Record the URL and SHA-256 in `cmake/sdl3_static.cmake` and `third_party/README.md`. Linux static builds keep SDL's dynamic loading of X11, Wayland, libdecor, PipeWire, PulseAudio, ALSA and libudev (`SDL_*_SHARED=ON`), so one binary runs on X11, Wayland and gamescope.
- [x] **Step 3:** Gate: macOS standalone package (`agent_gate.py package`) and all CTests pass on the new SDL; determinism hashes unchanged.
- [x] **Step 4:** Commit `build: static SDL on all desktop platforms; SDL 3.4.x pin bump`.
- [x] **Step 5: Minimum SDL (F17).** Raise `find_package(SDL3 ...)` to the oldest 3.4.x on which the full suite passes. When the distribution SDL is older, configure fails with a message pointing at `-DRR_STATIC_SDL=ON`. Re-run `input` on Linux.

### Task A8: font discovery with a bundled fallback (F12)

**Files:** Modify `src/import_ui.cpp`. Add `third_party/fonts/DejaVuSans.ttf`, `DejaVuSans-Bold.ttf` and `third_party/fonts/LICENSE` (Bitstream Vera / DejaVu licence, free to redistribute). Modify `CMakeLists.txt` and `third_party/README.md`.

- [x] **Step 1:** Windows looks under `SDL_getenv("WINDIR")\Fonts`. Linux searches the Debian, Arch/SteamOS, Fedora and openSUSE DejaVu, Noto Sans and Liberation Sans locations.
- [x] **Step 2:** If no system face loads, load the two bundled DejaVu files from next to the executable (`SDL_GetBasePath()`), or embed them as a byte array for the Linux/Windows packages. Never fall back to the debug font on a desktop package.
- [x] **Step 3:** Test: `import_ui` font smoke with `RR_FONT_SEARCH=none` must load the bundled face.
- [x] **Step 4:** Commit `import-ui: font discovery with bundled DejaVu fallback`.

### Task A9: host-neutral tooling (F13)

**Files:** Modify `Makefile`, `tools/tsan.sh`, `tools/fuzz.sh`, `tools/agent_gate.py`, `tools/agent_doctor.py`, `tools/ci.sh`. Create `tools/determinism_matrix.py` (it replaces the `.sh` script, which becomes a thin wrapper).

- [x] **Step 1:** CPU count comes from `nproc`, `sysctl` or `os.cpu_count()` in that order. LLVM and Java come from `$LLVM_BIN` / `$JAVA_HOME` with per-OS defaults; tools never fail on a missing Homebrew path when the tool itself is not needed.
- [x] **Step 2:** `tools/determinism_matrix.py` is a Python port of the shell matrix, with targets `macos`, `linux`, `linux-arm64`, `wasm`, `android`, `windows` and `windows-arm64`. Each target is skipped with a reason when it is unavailable.
- [x] **Step 3:** `agent_gate.py native` runs on Linux and Windows hosts. It skips only the macOS app step.
- [x] **Step 4:** Commit `tools: host-neutral gates and determinism matrix`.

### Task A10: explicit byte signedness (F15)

**Files:** Modify `oracle/actor_record_hooks.h` and `oracle/actor_record_buffer_test.c` (coordinate with the oracle-proxy owner; `oracle/aweman_proxy.c` is shared), the `src/` sites found in Step 1. Create a `tools/cpp_quality.py` check.

- [x] **Step 1: Audit.** Find every plain `char` that holds a number rather than text: function parameters mirroring original x86 signatures, `char(...)` casts of EXE/disc bytes, and arithmetic on `char`. `rg '\bchar\b' src oracle tests` minus `const char*` / string use.
- [x] **Step 2: Fix.** Use `int8_t` / `signed char` where the original x86 code treats the byte as signed (MSVC `char` is signed), and `uint8_t` where it is unsigned. Do not use `-fsigned-char` as the fix, so the code states its own intent.
- [x] **Step 3: Gate.** `oracle-actor-record` passes on Linux ARM64. Run the determinism disc race on Linux ARM64 and the Android build (when a device is available). Add a lint rule against numeric plain `char` in `src/`.
- [x] **Step 4:** Commit `portability: explicit byte signedness`.

### Task A11: deterministic transcendental math (F16)

**Files:** Create `src/det_math.hpp/.cpp`, `tests/det_math_test.cpp`. Modify `src/fog.cpp`, `src/raster_rgb.cpp`, `tests/raster_rgb_alloc_test.cpp` (only if Step 1 shows the golden itself was platform-dependent).

- [x] **Step 1: Confirm the cause.** On Linux ARM64, dump the fog table and the per-quad mip LOD for the `raster-rgb-alloc` scene, then diff them against macOS. Record whether `exp`, `log2` or something else (a sort tie, F2, or a signedness case, F15) produces the first difference.
- [x] **Step 2: Replace platform libm** in every output-affecting path. Fog becomes a table computed once with a correctly rounded, self-contained `exp` (or an integer table generated offline and committed as data). Mip LOD uses an integer `log2` on fixed-point values (`std::bit_width` / `frexp`, which are exact). `std::sqrt`, `floor` and `lround` are exactly specified by IEEE and need no change.
- [x] **Step 3: Gate.** `raster-rgb-alloc`, `raster-rgb`, `hires-render`, `widescreen` and `post-chain` are identical on macOS, Linux (x86-64, ARM64), wasm32 and Windows. Add a lint rule rejecting new `std::exp/log/pow/sin/cos/tan/atan` calls in `src/` outside `det_math`.
- [x] **Step 4:** Commit `portability: deterministic transcendental math in render paths`.

---

## Per-OS acceptance (both child plans)

A platform is done only when all of these hold on real hardware, each recorded in VERIFICATION.md with the exact observation:

1. **Build:** a clean checkout builds with the documented preset. `ctest` passes with zero unexpected skips. Disc tests run with the disc present.
2. **Goldens:** every oracle and pixel golden matches the macOS result bit for bit. The determinism matrix includes the platform, both synthetic and disc runs.
3. **Package:** the release artefact contains no game data (the verifier passes) and launches on a machine with no development tools.
4. **Import:** first run imports from both the ISO and a folder (including a lower-case folder on Linux) and survives cancel and low-space.
5. **Play:** every course and mode, career save/reload, settings changes (display, scale, widescreen, enhanced presets, sound mix, controls), videos and music. Keyboard and at least one gamepad (Xbox-style; plus the Deck on Linux).
6. **Lifecycle:** alt-tab or focus loss pauses, sleep/resume does not fast-forward, and an audio device or controller change mid-race recovers.
7. **Online (plan 34):** cross-play with macOS and the browser through `rr-netd`.

Status: Part A implemented (October 29, 2026; see "Part A results"); 33a/33b not started. Tracked in [00-INDEX.md](00-INDEX.md).
