# Settings and Options Screens Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Read [00-INDEX.md](00-INDEX.md) first. Independent of plans 10–12 and 16–17. It takes over [13](13-music-and-frontend-completion.md) Task 3 (sound mix and controls) and [14](14-android-port.md) Task 2 Step 3 (the original low-resolution view); those steps now point here.

**Goal:** Give the game a complete set of settings screens in the original front end's look:
- the original's own options (Graphics Options `0x13`, Configure Input `0x12`, the sound mix, Jukebox `0x11`) working the way the 1996 game does;
- the native options a modern port needs (**render resolution up to 2560 × 1920**, window or fullscreen, scaling, filtering, vsync, frame counter, touch controls, and the upscaling options of plan 20) added as extra screens drawn with the same fonts, colours, pictures and popups;
- an in-race pause menu in the same style that reaches the sound and display settings without leaving the race.

**Architecture:**
- **One settings model.** `rr::Settings` (`src/settings.hpp`) gains the native fields. They are saved in the same key=value file as the original's registry values.
- **One display module.** `src/display.*` turns `Settings` into SDL window/renderer state through a pure function (`display_config`, unit tested) and one apply function (`apply_display`), called at start-up and whenever a setting changes.
- **Native screens use the original machinery.** `Frontend` already draws screens from `MenuItem` tables read out of the EXE. Native screens are extra `MenuScreen` entries built at runtime, with ids above the original ones. Their geometry is copied from original items (the Graphics Options buttons and the resolution popup), so they inherit the original layout, fonts (Futura Condensed buttons, Badloc 72 titles), colours (`#DEFFF0` normal, `#E1E180` focused with the Wingdings arrow, `#A0A0A0` disabled, `#FF6432` titles) and background picture. Native text lives in a small native string table; it is our own text, not original data.
- **The pause menu is a frontend screen too.** A race pause enters a native pause screen drawn over the darkened race frame, so it uses the same input, focus and drawing code as every other screen.

**Tech Stack:** C++17, SDL3 3.4.8, CTest. No new dependencies.

**Spec:** this plan (the owner's request of October 11, 2026: "graphic settings, resolution, sound etc., typical game settings screens, integrated into the game style"), [00-INDEX.md](00-INDEX.md), [../REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md) "Front end".

## Global Constraints

- The original options keep their original behaviour and storage keys: `HiRes` `0x00475380`, `Inset` `0x00475390`, `HiResBikers` `0x00475384`, `VideoMode` `0x0047538c`, `MiniDash` `0x00467a98`, `OpponentList` `0x004753a8`, the Mixer block `0x00463030`. `0x004457f0` maps resolution choices 1, 2, 3 to low fullscreen, low inset and high fullscreen.
- Native settings never change simulation results. Nothing in `src/race.cpp`, `src/traffic.cpp`, `src/police.cpp` or `src/crash.cpp` may read a native display or input setting. The race view mode (low/inset/high) changes only drawing, as in the original.
- Native screens never embed original text. Native strings are English, written by us, and listed in `src/native_strings.cpp`. Original screens keep using the user's EXE strings in the user's language. The README and USER_GUIDE note that native option labels are English only.
- Every setting persists across restarts and applies without restarting the game. The one exception is a change that the original also applies only at race start (the race view mode); the screen says so in its description box.
- Android hides window-only options (windowed mode, window size). macOS hides touch options unless a touch device is present.
- Settings files from older builds load unchanged: unknown keys are ignored, missing keys take defaults (existing `parse_settings` rule).

## Review Focus

1. **Fullscreen toggled while a video or the race is running:** the renderer must keep every texture and the letterbox must recompute; nothing may stretch for a frame. Task 3 toggles fullscreen during a scripted video and a headless race and compares the next frame's logical rectangle.
2. **A settings file with out-of-range or hand-edited values** (`window_scale=99`, `scale_mode=7`, a truncated last line): the game must start with defaults for those keys only. Task 2 has a garbage-file test.
3. **Volume changes during a race:** engine and effects levels change on the next mixer block, without clicks or a restart of the soundtrack. Task 6 has a mixer-level test.
4. **Opening the pause menu during the start countdown, a crash, or the last tick before the finish:** resuming must continue the same tick with no skipped or doubled simulation ticks. Task 8 runs a headless race that pauses at those points and compares the per-tick snapshot with an unpaused run.
5. **Controller only, no keyboard or mouse** (the MANGMI AIR X): every options screen, popup, slider and the pause menu must be reachable and leavable with the d-pad, A and B. Task 5 and Task 8 drive their screens through `MenuInput` built from gamepad events only.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `src/settings.hpp/.cpp` | Add the native fields, parse/format/clamp them |
| `src/display.hpp/.cpp` | `display_config` (pure) and `apply_display` (SDL) |
| `src/native_strings.hpp/.cpp` | Native English labels, descriptions and choices; slot numbers above the EXE table |
| `src/native_screens.hpp/.cpp` | Builds the native `MenuScreen`s and popups from original item geometry |
| `src/frontend.hpp/.cpp` | Screen ids above `0x23`; slider items; native setters; pause screen entry/exit |
| `src/game_main.cpp` | Applies display settings; race view mode; pause menu over the race; FPS counter |
| `src/view.hpp`, `src/hud.cpp`, `src/rider_sprites.cpp` | Low/inset race view and LOBOB when `HiResBikers` is off |
| `tests/settings_test.cpp`, `tests/display_test.cpp`, `tests/native_screens_test.cpp`, `tests/frontend_test.cpp` | Tests |
| `docs/USER_GUIDE.md`, `docs/REVERSE_ENGINEERING.md`, `docs/PROGRESS.md`, `docs/VERIFICATION.md` | Docs |

## Screen map

```
Restroom (0x10) ──► Jukebox (0x11)               original
               ├──► Configure Input (0x12)        original, Task 7
               └──► Graphics Options (0x13)       original: Resolution, Bikers, Videos
                        ├──► Display (0x24)       native, Task 5
                        └──► Sound (0x25)         native look-alike of Win32 dialog 300, Task 6 (Task 1: sound mix is a modal dialog, not a screen)
                        (Display also hosts RENDER RESOLUTION from plan 19 Task 8,
                         and TEXTURE DETAIL, ARTWORK, SCREEN FILTER from plan 20)
Race pause ──────────► Pause (0x26)               native, Task 8: Resume, Sound, Display, Controls, Quit race
```

Task 1 confirms where the original reaches the sound mix and controls handlers and whether the 1996 game has an in-race pause screen. If the original's sound mix is a separate screen or dialog, Task 6 draws that one instead of a native screen, and the map above is corrected in the same commit.

---

### Task 1: Trace the original options and pause behaviour (time box: one session)

**Files:**
- Modify: `docs/REVERSE_ENGINEERING.md` (new subsection "Options screens and pause"), `docs/plans/18-settings-and-options.md` (correct the screen map if needed)

- [X] **Step 1:** Read the item tables of screens `0x10`–`0x13` with `read_frontend_data` and print them (a throwaway loop in `build/rr-inspect`-style code or a debugger; do not commit the output). Record for each item: caption, target, handler, flags and geometry.
- [X] **Step 2:** Read `analysis/decompiled/` for handlers `0x00401df0` (sound mix), `0x0040fd90` (controls) and `0x00445600`/`0x00445730` (jukebox). If a file is missing, generate it with `tools/decompile_at.sh <address>`. Record: is the sound mix a menu screen, a popup or a Win32 dialog resource (`DIALOG`/`TRACKBAR` in the EXE resources; check with `build/rr-inspect`-style PE resource listing in `src/pe_strings.cpp`)? What ranges and steps do the Mixer channels have, and does a change play a preview sound?
- [X] **Step 3:** Trace in-race pause in `0x0044f800` (control word) and the race loop `0x0044b3b0`: which key pauses, what is drawn, which keys work while paused (the original had in-race toggles for the mini dashboard and opponent list; record their keys), and how Escape ends a race (outcome code and screen).
- [X] **Step 4:** Write the findings, with addresses, into REVERSE_ENGINEERING.md. Correct the screen map in this plan. Commit: `docs: original options screens, sound mix and pause behaviour`.

---

### Task 2: Native fields in the settings model

**Files:**
- Modify: `src/settings.hpp`, `src/settings.cpp`, `tests/settings_test.cpp`

**Interfaces:**
- Produces (added to `struct Settings`, after the original fields):
  ```cpp
  enum class DisplayMode : uint8_t { windowed = 0, fullscreen = 1 };       // fullscreen = borderless desktop
  enum class ScaleMode : uint8_t { fit = 0, integer = 1, stretch = 2 };    // letterbox, whole multiples, fill
  enum class FilterMode : uint8_t { sharp = 0, smooth = 1 };               // nearest, linear
  // in struct Settings:
  DisplayMode display_mode = DisplayMode::windowed;
  int window_scale = 2;            // 1..4: window is 640x480 (or 640x510 in a race) times this
  ScaleMode scale_mode = ScaleMode::fit;
  FilterMode filter = FilterMode::sharp;
  bool vsync = true;
  bool show_fps = false;
  int touch_opacity = 50;          // 0..100, Android touch overlay (plan 14 Task 3)
  bool haptics = true;             // Android rumble on crash/hit where the device supports it
  ```

- [X] **Step 1: Write the failing test.** Add to `tests/settings_test.cpp`, before the final `std::cout`:

```cpp
    {
        rr::Settings n;
        n.display_mode = rr::DisplayMode::fullscreen; n.window_scale = 3; n.scale_mode = rr::ScaleMode::integer;
        n.filter = rr::FilterMode::smooth; n.vsync = false; n.show_fps = true; n.touch_opacity = 20; n.haptics = false;
        const rr::Settings m = rr::parse_settings(rr::format_settings(n));
        check(m.display_mode == rr::DisplayMode::fullscreen && m.window_scale == 3 && m.scale_mode == rr::ScaleMode::integer &&
              m.filter == rr::FilterMode::smooth && !m.vsync && m.show_fps && m.touch_opacity == 20 && !m.haptics,
              "native fields round trip");
        const rr::Settings bad = rr::parse_settings("window_scale=99\nscale_mode=7\nfilter=-1\ndisplay_mode=2\ntouch_opacity=101\nvsync=");
        check(bad.window_scale == d.window_scale && bad.scale_mode == d.scale_mode && bad.filter == d.filter &&
              bad.display_mode == d.display_mode && bad.touch_opacity == d.touch_opacity && bad.vsync == d.vsync,
              "native fields reject out-of-range values");
        const rr::Settings old = rr::parse_settings("hires=1\nmaster_volume=40\n");   // a file written before this task
        check(old.high_resolution && old.master_volume == 40 && old.window_scale == d.window_scale, "old files still load");
    }
```

- [X] **Step 2:** Run `cmake --build build --target settings-test && ctest --test-dir build -R settings --output-on-failure`. Expected: compile error (the new members do not exist).
- [X] **Step 3: Implement.** Add the enums and members above to `settings.hpp`. In `parse_settings` add:

```cpp
        else if (k == "display_mode") { if (parse_int(v, 0, 1, x)) s.display_mode = DisplayMode(x); }
        else if (k == "window_scale") { if (parse_int(v, 1, 4, x)) s.window_scale = x; }
        else if (k == "scale_mode") { if (parse_int(v, 0, 2, x)) s.scale_mode = ScaleMode(x); }
        else if (k == "filter") { if (parse_int(v, 0, 1, x)) s.filter = FilterMode(x); }
        else if (k == "vsync") flag(s.vsync);
        else if (k == "show_fps") flag(s.show_fps);
        else if (k == "touch_opacity") { if (parse_int(v, 0, 100, x)) s.touch_opacity = x; }
        else if (k == "haptics") flag(s.haptics);
```

  In `format_settings`, append before `"\nalias="`:

```cpp
      << "\ndisplay_mode=" << int(s.display_mode) << "\nwindow_scale=" << s.window_scale << "\nscale_mode=" << int(s.scale_mode)
      << "\nfilter=" << int(s.filter) << "\nvsync=" << s.vsync << "\nshow_fps=" << s.show_fps
      << "\ntouch_opacity=" << s.touch_opacity << "\nhaptics=" << s.haptics
```

  In `default_settings()`, on Android (`#ifdef __ANDROID__`) set `display_mode = DisplayMode::fullscreen`.
- [X] **Step 4:** Run the test again. Expected: `0 failures`, PASS.
- [X] **Step 5:** Commit: `settings: native display, scaling, vsync and touch fields`.

---

### Task 3: Display module and live application

**Files:**
- Create: `src/display.hpp`, `src/display.cpp`, `tests/display_test.cpp`
- Modify: `src/game_main.cpp` (every `SDL_SetRenderLogicalPresentation`, `SDL_SetRenderVSync` and `SDL_CreateTexture` call site), `CMakeLists.txt` (add `display.cpp` to the game library and register `display-test`)

**Interfaces:**
- Consumes: `rr::Settings` (Task 2).
- Produces:
  ```cpp
  namespace rr {
  struct DisplayConfig {
      bool fullscreen = false;
      int window_w = 1280, window_h = 960;          // ignored when fullscreen
      int logical_w = 640, logical_h = 480;
      SDL_RendererLogicalPresentation presentation = SDL_LOGICAL_PRESENTATION_LETTERBOX;
      SDL_ScaleMode texture_scale = SDL_SCALEMODE_NEAREST;
      int vsync = 1;
  };
  // logical_h is 480 for menus/videos and 510 for the race (dashboard strip below the view).
  DisplayConfig display_config(const Settings&, int logical_h, bool mobile);
  // Idempotent: only calls SDL for fields that differ from the window's current state.
  void apply_display(SDL_Window*, SDL_Renderer*, const DisplayConfig&);
  // For every streaming texture the game creates.
  void apply_texture_scale(SDL_Texture*, const DisplayConfig&);
  }
  ```

- [X] **Step 1: Write the failing test** `tests/display_test.cpp` (synthetic, no SDL window needed):

```cpp
#include "display.hpp"
#include <iostream>

int main() {
    int failures = 0;
    auto check = [&](bool v, const char* what) { if (!v) { ++failures; std::cerr << "FAIL " << what << "\n"; } };
    rr::Settings s = rr::default_settings();
    s.display_mode = rr::DisplayMode::windowed; s.window_scale = 2;
    auto c = rr::display_config(s, 480, false);
    check(!c.fullscreen && c.window_w == 1280 && c.window_h == 960 && c.logical_h == 480, "2x window for menus");
    c = rr::display_config(s, 510, false);
    check(c.window_h == 1020 && c.logical_h == 510, "race window keeps the dashboard strip");
    s.scale_mode = rr::ScaleMode::integer;
    check(rr::display_config(s, 480, false).presentation == SDL_LOGICAL_PRESENTATION_INTEGER_SCALE, "integer scaling");
    s.scale_mode = rr::ScaleMode::stretch;
    check(rr::display_config(s, 480, false).presentation == SDL_LOGICAL_PRESENTATION_STRETCH, "stretch");
    s.filter = rr::FilterMode::smooth;
    check(rr::display_config(s, 480, false).texture_scale == SDL_SCALEMODE_LINEAR, "smooth filter");
    s.vsync = false;
    check(rr::display_config(s, 480, false).vsync == 0, "vsync off");
    s.display_mode = rr::DisplayMode::windowed;
    check(rr::display_config(s, 480, true).fullscreen, "mobile is always fullscreen");
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
```

- [X] **Step 2:** Register `display-test` in `CMakeLists.txt` next to `settings-test`, build, run `ctest -R display`. Expected: build failure (`display.hpp` missing).
- [X] **Step 3: Implement** `src/display.cpp`:

```cpp
#include "display.hpp"

namespace rr {
DisplayConfig display_config(const Settings& s, int logical_h, bool mobile) {
    DisplayConfig c;
    c.fullscreen = mobile || s.display_mode == DisplayMode::fullscreen;
    c.logical_w = 640;
    c.logical_h = logical_h;
    c.window_w = 640 * s.window_scale;
    c.window_h = logical_h * s.window_scale;
    c.presentation = s.scale_mode == ScaleMode::integer ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
                   : s.scale_mode == ScaleMode::stretch ? SDL_LOGICAL_PRESENTATION_STRETCH
                                                        : SDL_LOGICAL_PRESENTATION_LETTERBOX;
    c.texture_scale = s.filter == FilterMode::smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
    c.vsync = s.vsync ? 1 : 0;
    return c;
}

void apply_display(SDL_Window* window, SDL_Renderer* renderer, const DisplayConfig& c) {
    const bool is_full = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (is_full != c.fullscreen) SDL_SetWindowFullscreen(window, c.fullscreen);
    if (!c.fullscreen) {
        int w = 0, h = 0;
        SDL_GetWindowSize(window, &w, &h);
        if (w != c.window_w || h != c.window_h) SDL_SetWindowSize(window, c.window_w, c.window_h);
    }
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode{};
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    if (lw != c.logical_w || lh != c.logical_h || mode != c.presentation)
        SDL_SetRenderLogicalPresentation(renderer, c.logical_w, c.logical_h, c.presentation);
    SDL_SetRenderVSync(renderer, c.vsync);
}

void apply_texture_scale(SDL_Texture* texture, const DisplayConfig& c) {
    if (texture) SDL_SetTextureScaleMode(texture, c.texture_scale);
}
}
```

  `display.hpp` declares the interface above and includes `<SDL3/SDL.h>` and `settings.hpp`.
- [X] **Step 4:** Run `ctest -R display`. Expected: PASS.
- [X] **Step 5: Wire into the game.** In `src/game_main.cpp`:
  - after `SDL_CreateWindowAndRenderer`, load settings and call `apply_display(window, renderer, display_config(settings, 480, mobile))` where `mobile` is `#ifdef __ANDROID__ true`;
  - replace each `SDL_SetRenderLogicalPresentation(renderer, 640, 510, ...)` with `apply_display(window, renderer, display_config(settings, 510, mobile))`, and each `640, 480` one with the 480 form;
  - after each `SDL_CreateTexture`, call `apply_texture_scale(texture, current_config)`;
  - add the hotkeys: Alt+Enter and Cmd+F (macOS) toggle `display_mode`, save settings and re-apply;
  - add `--fullscreen` and `--window-scale N` command-line options that override the saved values for that run only.
- [X] **Step 6: Review Focus 1 test.** Add a `--toggle-fullscreen-at N` debug option. Run `build/rr-game --assets ../cdrom/ROADRASH --race --ticks 300 --toggle-fullscreen-at 150 --screenshot analysis/fs-toggle.bmp` and `build/rr-game --assets ../cdrom/ROADRASH --video VIDEO/INTRO.AVI --ticks 300 --toggle-fullscreen-at 150 --screenshot analysis/fs-video.bmp`. Both must exit 0 with a non-black 640×480 (or 640×510) screenshot. Record the result in VERIFICATION.md.
- [X] **Step 7:** Run the regression gate (00-INDEX). Commit: `display: window, fullscreen, scaling, filtering and vsync from settings`.

---

### Task 4: The original race view modes (low, inset, high) and low-detail bikers

Moved here from plan 14 Task 2 Step 3. The game currently always renders the high-resolution race view and loads `HIBOB.DAT` whatever the settings say.

**Files:**
- Modify: `src/game_main.cpp` (race framebuffer size and `view_mode` call), `src/hud.cpp`/`src/hud.hpp` (low-resolution dashboard), `src/rider_sprites.cpp`/`src/actor_nodes.cpp` (bob file from `HiResBikers`), `tests/hud_test.cpp`, `tests/view_test.cpp`, `tests/oracle_scene_test.cpp`

**Starting point:**
- `0x00448870` (`src/view.hpp`: `view_mode`) sets 640×480 high or 320×240 low; inset puts the low view at (160,120) inside a 640×480 display (`0x004457f0` choices 1/2/3).
- `0x00416310` selects the dashboard artwork per mode; `hud.cpp` only implements the full high-resolution dashboard.
- `HiResBikers` (`0x00475384`) selects `DATA/BIKERS/HIBOB.DAT` or `LOBOB.DAT`. Trace where the original reads it (search the disassembly for `0x00475384`).

- [ ] **Step 1: Trace.** Record in REVERSE_ENGINEERING.md: the low-mode dashboard resources and layout from `0x00416310`; the bob file choice; whether low mode halves sprite scale or uses separate low-resolution actor graphics.
- [ ] **Step 2: Capture.** Two original pixel goldens with the plan 10 `napa-solo` control file: `napa-drive-low` (`HiRes` off, inset off) and `napa-drive-inset` (inset on). Set the registry values in the oracle prefix before the run (`oracle/install.sh` documents the key). Every 150 frames, 1,200 frames.
- [ ] **Step 3: Write the failing tests.** In `hud_test`: the low dashboard composes at the traced size for all 24 style combinations. In `oracle_scene_test`: the two new scenarios compare every pixel of the natively rendered frames. Run them; expected FAIL.
- [ ] **Step 4: Port.** The race framebuffer takes its size from `view_mode(settings.high_resolution, settings.inset)`; in inset mode, the race view is drawn into a 640×480 frame at (160,120) with the rest of the frame as the original fills it. The HUD composes the traced low dashboard. `ActorNodes` loads the bob file selected by `high_res_bikers`.
- [ ] **Step 5: Accept.** Both goldens match every pixel; high mode's existing goldens still pass. Commit: `render: original low and inset race views and low-detail bikers`.

---

### Task 5: Native Display screen in the original style

**Files:**
- Create: `src/native_strings.hpp`, `src/native_strings.cpp`, `src/native_screens.hpp`, `src/native_screens.cpp`, `tests/native_screens_test.cpp`
- Modify: `src/frontend.hpp`, `src/frontend.cpp`, `src/career.hpp` (screen ids), `tests/frontend_test.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `FrontendData`, `MenuItem`, `MenuScreen`, `Frontend::open_popup`, the popup at `0x00474240` (resolution) as the geometry template, `Settings` (Task 2), `display_config`/`apply_display` (Task 3).
- Produces:
  ```cpp
  namespace rr {
  namespace screen_id { constexpr int native_display = 0x24, native_sound = 0x25, native_pause = 0x26, screen_count = 0x27; }
  constexpr int native_slot_base = 0x10000;                        // caption/desc slots >= this are native strings
  const std::string& native_string(int slot);                      // empty for unknown slots
  // Native string slots (values are native_slot_base + n).
  namespace ns { enum : int { display_title = native_slot_base, display_mode, window_size, scaling, filtering, vsync, fps,
                              touch_opacity, haptics, back, windowed, fullscreen, scale_1x, scale_2x, scale_3x, scale_4x,
                              fit, integer, stretch, sharp, smooth, on, off, /* descriptions follow */ d_display_mode,
                              d_window_size, d_scaling, d_filtering, d_vsync, d_fps, d_touch, d_haptics,
                              sound_title, master, music, effects, engines, others, sirens, interface, mute, swap,
                              pause_title, resume, sound, display, controls, quit_race, d_quit_race, count }; }
  // Builds the native screens and popups from original item geometry already in data.
  void add_native_screens(FrontendData& data, bool mobile);
  // Native popups, keyed like FrontendData::popups (fake addresses 0xF000_0000 + n, never valid EXE addresses).
  namespace native_popup { constexpr uint32_t display_mode = 0xF0000001, window_size = 0xF0000002, scaling = 0xF0000003,
                                              filtering = 0xF0000004, on_off = 0xF0000005; }
  }
  ```
  `FrontendData::screens` becomes `std::array<MenuScreen, screen_id::screen_count>`; `read_frontend_data` fills the first `0x24` exactly as now. `Frontend::str` returns `native_string(slot)` for `slot >= native_slot_base`. `Frontend::enter` quits only for `screen >= screen_id::screen_count` (and still for 6 and negatives).

- [X] **Step 1: Write the failing test** `tests/native_screens_test.cpp` (the string check is synthetic; the rest skips with 77 without the disc, because the geometry comes from the user's EXE; register it with `SKIP_RETURN_CODE 77` like `frontend-test`):

```cpp
#include "assets.hpp"
#include "frontend.hpp"
#include "native_screens.hpp"
#include "native_strings.hpp"
#include <iostream>

int main(int argc, char** argv) {
    int failures = 0;
    auto check = [&](bool v, const char* what) { if (!v) { ++failures; std::cerr << "FAIL " << what << "\n"; } };
    for (int s = rr::native_slot_base; s < rr::ns::count; ++s) check(!rr::native_string(s).empty(), "every native slot has text");
    const std::string root = argc > 1 ? argv[1] : RR_SOURCE_DIR "/../cdrom/ROADRASH";
    const rr::Assets assets(root);
    if (!assets.exists("ROADRASH.EXE")) { std::cout << "skip: no disc\n"; return failures ? 1 : 77; }
    rr::FrontendData data = rr::read_frontend_data(assets.read("ROADRASH.EXE"));
    rr::add_native_screens(data, false);
    const auto& display = data.screens[rr::screen_id::native_display];
    check(display.title == rr::ns::display_title, "display screen has the native title");
    check(display.items.size() == 7, "desktop display screen: mode, size, scaling, filtering, vsync, fps, back");
    const auto& gfx = data.screens[0x13].items.front();
    for (const auto& m : display.items)
        check(m.w == gfx.w && m.h == gfx.h && m.x == gfx.x, "native buttons copy the Graphics Options button geometry");
    for (size_t i = 0; i < display.items.size(); ++i) {   // vertical chain, wrapping, Escape on Back
        const auto& m = display.items[i];
        check(m.nav[3] == (i + 1) % display.items.size() && m.nav[2] == (i + display.items.size() - 1) % display.items.size(), "up/down chain");
    }
    check((display.items.back().flags & rr::item_flag::escape) && display.items.back().target == 0x13, "Back returns to Graphics Options");
    rr::FrontendData mobile = rr::read_frontend_data(assets.read("ROADRASH.EXE"));
    rr::add_native_screens(mobile, true);
    check(mobile.screens[rr::screen_id::native_display].items.size() == 6, "mobile hides window size and adds touch, haptics");
    bool linked = false;
    for (const auto& m : data.screens[0x13].items) linked |= m.target == rr::screen_id::native_display;
    check(linked, "Graphics Options has an item that enters the Display screen");
    std::cout << failures << " failures\n";
    return failures ? 1 : 0;
}
```

  Desktop items: display mode, window size, scaling, filtering, vsync, FPS, Back (7). Mobile items: scaling, filtering, FPS, touch opacity, haptics, Back (6). Plans 19 and 20 later insert RENDER RESOLUTION, TEXTURE DETAIL, ARTWORK and SCREEN FILTER above Back and update these counts in their own tests; mode and size do not apply, and Android's compositor forces vsync. If device testing (plan 14) shows vsync matters on Android, change the expected count and this note together.

  Extend `tests/frontend_test.cpp`'s scripted flow: Restroom → Graphics Options → Display → open Scaling → choose Integer → Back. Assert `settings.scale_mode == rr::ScaleMode::integer`, the saved settings file contains `scale_mode=1`, and the focus returned to the Display item it left from.
- [X] **Step 2:** Run both; expected FAIL (missing headers).
- [X] **Step 3: Implement.**
  - `native_strings.cpp`: a `static const std::array<std::string, ns::count - native_slot_base>` with labels in the original's capitalisation style (captions upper case like the original buttons, descriptions in sentence case), e.g. `"DISPLAY"`, `"Show the game in a window or fill the whole screen."`.
  - `native_screens.cpp`: copy the first Graphics Options item as the button template; place the N native items in a column at the template's x, starting at the template's y, spaced by `template.h + 4`; set captions, description slots, flags `title_desc | description`, `enabled = true`, the up/down/tab chain, and the Back item with `item_flag::escape` and `target = 0x13`. Each value item has `handler = 0` and a native `target` that `Frontend::activate` turns into `open_popup(data_.popups.at(native_popup::...), current_choice, native_setter)`. Build each native popup from the resolution popup `0x00474240`: its header item becomes the header with a native caption, its first choice item is copied once per choice and offset by its own height. Append one item to screen `0x13` copied from its last button, with caption `ns::display` and `target = screen_id::native_display`, and fix the nav chain of its neighbours.
  - `frontend.cpp`: the value text of a focused native item is shown in the description box as `"<description>\nCurrent: <value>"`; setters write `Settings`, call `save_settings`, and push a new `FrontendEvent::Kind::apply_display` (add it to the enum) that `game_main.cpp` handles by calling `apply_display`.
  - Background: `background_for` returns the Graphics Options picture (`Postcard/Napa` or `Postcard/QNapa`, see `frontend.cpp` case `0x13`) for the native screens.
- [X] **Step 4:** Run both tests. Expected: PASS. Then take a menu screenshot: `build/rr-game --assets ../cdrom/ROADRASH --menu-script restroom,graphics,display --menu-frames 60 --screenshot analysis/display-screen.bmp` (add `--menu-script` if it does not exist: a comma list of screen names fed through `MenuInput`). Look at it: fonts, colours, arrow and background must match the neighbouring original screens. Do not commit the screenshot (it contains original art).
- [X] **Step 5:** Commit: `frontend: native Display options screen in the original menu style`.

---

### Task 6: Sound mix

**Files:**
- Modify: `src/frontend.cpp`, `src/frontend.hpp`, `src/native_screens.cpp`, `src/sound.cpp`/`src/mixer.cpp` (live channel levels), `tests/frontend_test.cpp`, `tests/mixer_test.cpp`

**Interfaces:**
- Consumes: `Settings` volume fields (`master_volume`, `music_volume`, `effects_volume`, `engine_volume`, `others_volume`, `siren_volume`, `interface_volume`, `mute`, `swap_speakers`), the level curve `0x004362e0` already used by `mixer.cpp`.
- Produces: a slider item kind, `item_flag::native_slider = 0x01000000` (a bit the original never sets; assert that in `native_screens_test`), drawn as the caption plus ten segments; Left/Right change the value by 10; Home/End go to 0/100. `FrontendEvent::Kind::apply_mixer` pushed on every change.

- [X] **Step 1:** Use Task 1's finding. If the original sound mix is a menu screen, port its handler `0x00401df0` and items exactly, adding only the slider drawing it needs. If it is a Win32 dialog, build `screen_id::native_sound` with the dialog's controls in the dialog's order and ranges, drawn as menu sliders; record that as a documented deviation (the original look is a Windows dialog).
- [X] **Step 2: Write the failing tests.**
  - `frontend_test`: open Sound, move to Music, press Left 3 times → `settings.music_volume == 50`; press End → 100; toggle Mute; Back. The file round-trips.
  - `frontend_test`: the slider bit `0x01000000` is never set on any original item read from the EXE.
  - `mixer_test`: changing the engine level between two `Mixer::mix` blocks scales the next block by the new curve value with no sample from the old level after the change point (Review Focus 3).
- [X] **Step 3: Implement** and run the tests. Expected: PASS.
- [X] **Step 4:** Preview: when a slider moves, play the original interface click (the event the original plays, if Task 1 found one; otherwise none). Commit: `frontend: sound mix with live channel levels`.

---

### Task 7: Configure Input (original bindings, plus native gamepad and touch pages)

Moved here from plan 13 Task 3.

**Files:**
- Modify: `src/frontend.cpp`, `src/input.cpp`, `src/input.hpp`, `src/settings.cpp`, `tests/input_test.cpp`, `tests/frontend_test.cpp`, `tests/settings_test.cpp`

**Starting point:** handler `0x0040fd90` (currently `return 0` in `frontend.cpp`), key table `0x0040e5f0`, control word `0x004b7ed0` built by `0x0044f800`, joystick analog path in `0x0044f800`.

- [x] **Step 1: Trace** the items of screen `0x12`, the rebinding flow ("press a key"), the conflict rule (swap or refuse) and the registry storage. Record them. (Traced in the prior session: EXE control tables asserted against the defaults in `frontend_test`, conflict rule in `apply_rebind`.)
- [x] **Step 2: Write the failing tests.**
  - `input_test`: the default bindings equal the traced table; the analog stick maps to the original control bits at the traced thresholds; a rebound key produces the bound control bit.
  - `frontend_test`: rebind Accelerate to a key used by Brake → the traced rule happens (Review Focus of plan 13 kept here). Escape during "press a key" cancels without changing anything.
  - `settings_test`: bindings round-trip; an unknown key code in the file falls back to the default for that action only.
- [x] **Step 3: Port** screen `0x12` exactly. Add two native pages reached from it, built like Task 5's screen: **Gamepad** (shows the fixed SDL gamepad mapping, with a "Swap punch and kick" toggle and stick dead zone 10–40 %) and, on touch devices, **Touch** (overlay opacity from `touch_opacity`, left-handed layout toggle). Native pages are documented as native additions.
- [x] **Step 4:** Commit: `frontend: Configure Input with original bindings; native gamepad and touch pages`.

Correction (November 1, 2026): the October port mapped rows 3–6 and 9 to the
wrong actions and read the table's codes as SDL scancodes, so the original
defaults, the first rebind and saved bindings left keys unbound, and the
Keyboard/Joystick defaults rows did nothing. Rows now set bit `1 << row`, codes
are the original keyboard codes (US VK table `0x00465fe8`), Turn/Lean capture
two keys (`0x0040fc70`), conflicts clear pair partners, the defaults rows run
`0x0040fdf0`, and labels follow `0x0040e7b0` including joystick text. See
[controls-key-labels-20261101](../agent-handoffs/controls-key-labels-20261101.md).
November 2: table joystick codes drive the race (`0x0044f880`, with the analog
throttle/steer values), capture takes pad codes including the paired Turn/Lean
rules, and saved files mark the code space (`controls_codes=original`).

---

### Task 8: In-race pause menu

**Files:**
- Modify: `src/game_main.cpp` (replace `draw_pause` and the pause key handling in `run_race` and `run_game`), `src/frontend.cpp`/`src/frontend.hpp` (pause screen entry and its events), `src/native_screens.cpp`, `tests/frontend_test.cpp`, `tests/race_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  // Frontend
  void open_pause(const RgbImage& race_frame);   // darkens the frame as the original standings screen does, enters native_pause
  bool pause_open() const;
  // New FrontendEvent kinds: resume_race, quit_race (the race ends with the outcome traced in Task 1).
  ```
  The pause screen items: RESUME, SOUND (→ `native_sound`), DISPLAY (→ `native_display`), CONTROLS (→ `0x12`, read-only while racing if the original forbids rebinding mid-race), QUIT RACE (confirmation dialog via the existing `show_dialog`). Back/B/Escape on the pause screen resumes. Sub-screens' Back returns to the pause screen, not to Graphics Options, when entered from it.

- [x] **Step 1: Write the failing tests.**
  - `frontend_test`: `open_pause` → focus on RESUME; Down×4 → QUIT RACE; select → dialog; choose No → still paused; Escape → `resume_race` event.
  - `race_test` (Review Focus 4): run a headless race twice for 900 ticks with the same controls; in the second run, call the pause path (no ticks while paused, 120 frames) at ticks 10 (countdown), at the first crash tick, and at the tick before the finish of a short scripted race. Compare `Race` snapshots tick by tick: identical.
  - Controller-only (Review Focus 5): the same flow driven by `MenuInput` produced from `SDL_GAMEPAD_BUTTON_*` events only.
- [x] **Step 2:** Run them; expected FAIL.
- [x] **Step 3: Implement.** The race loop stops stepping the simulation while `pause_open()`; the mixer pauses race sounds but keeps the interface channel live so the sound sliders can be heard. The lifecycle pause (app backgrounded) opens the pause menu when the app returns, instead of the old "PAUSED" text. Delete `draw_pause`.
- [x] **Step 4:** Run the tests and the regression gate. Commit: `game: in-race pause menu with sound, display and controls`.

---

### Task 9: FPS counter, docs and gate

**Files:**
- Modify: `src/game_main.cpp`, `docs/USER_GUIDE.md`, `docs/PROGRESS.md`, `docs/VERIFICATION.md`, `README.md`

- [x] **Step 1:** `show_fps` draws frames per second and simulation ticks per second in the top-right corner with `TextRenderer` (Futura Condensed Bold 24, `#DEFFF0`, 2-pixel black shadow like all original text). It is updated twice per second.
- [x] **Step 2:** USER_GUIDE gains a "Settings" section: where each option lives (screen map above), what each does, which are original and which native, the settings file location per platform, and the hotkeys (Alt+Enter, Cmd+F).
- [x] **Step 3:** Run the regression gate and `tools/ci.sh`. Refresh the macOS `.app` (`tools/build_macos_app.sh`, `tools/verify_macos_app.py`).
- [x] **Step 4:** Commit: `docs: settings guide; game: FPS counter`.

## Exit criteria

- Every original options handler (`0x00401df0`, `0x0040fd90`, `0x00445600`/`0x00445730` via plan 13 Task 4, `0x004457b0`/`0x004458a0`/`0x00445960`) works; none returns the "not ported" stub.
- The native Display, Sound (if native) and Pause screens are drawn with original geometry, fonts, colours and pictures, and every one is usable with a controller only.
- Low, inset and high race views match their original pixel goldens.
- Settings persist, old settings files still load, and no native setting affects a plan 10 tick golden.

## Higher resolutions and upscaling

Rendering above 640 × 480 was a non-goal in the first version of this plan. The owner asked for it on October 11, 2026, so it now has its own plans:
- [19-high-resolution-rendering.md](19-high-resolution-rendering.md): render scale 1–4× and match-display for the race, dashboard and menus, with the original 640 × 480 path kept bit-identical, threaded rasterisation, and the RENDER RESOLUTION item on this plan's Display screen.
- [20-asset-upscaling.md](20-asset-upscaling.md): MMPX textures, Real-ESRGAN artwork and GPU screen filters, with TEXTURE DETAIL, ARTWORK and SCREEN FILTER items on the Display screen.

Widescreen stays out of scope here; plan 19 Task 9 is a decision-gated trace for it.
