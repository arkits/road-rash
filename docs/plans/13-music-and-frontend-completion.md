# Race Music and Front-End Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Read [00-INDEX.md](00-INDEX.md) first. Independent of plans 10–12, except Task 4's busted-video rule, which needs plan 11 Task 6.

**Goal:** Finish the remaining audio and front-end behaviour:
- race MIDI music (`AUDIO/MUSIC/GRUNGE1..5.MID` with AWE32 `.SBK` banks);
- the sound mix, controls (key binding) and jukebox screens;
- the attract demo;
- complete cutscene triggers;
- original key bindings and the joystick analog path;
- verification of saves and menus against the original.

**Architecture:**
- **Music:** a new `rr::MusicPlayer` owns one music voice in the existing `rr::Mixer`. It plays either a streamed RRA track (moved out of `sound.cpp`) or a MIDI song rendered by a software synthesiser from the course's SBK bank.
- **Front end:** the three inert screens are ported from their EXE handlers into `frontend.cpp`, using the same table-driven approach as the other screens.
- **Verification:** new oracle modes dump the original menu surface and a written save file.

**Tech Stack:** C++17, SDL3, CTest. One new pinned single-header synthesiser library, selected in Task 1 under the licence rule.

**Spec:** [00-INDEX.md](00-INDEX.md) definition of done items 4 and 5. Recovered facts: [../REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md) "Sound effects, engines and soundtrack" and "Front end".

## Global Constraints

- **Music rules** from `0x00403040`, `0x00402f50`, `0x00433bb0` and `0x004160c0`:
  - races play MIDI `Grunge1..5` by course, using the `SbkGrng` bank;
  - the bank is `Klown` when the character alias is "krusty" or "bozo";
  - race start stops the RRA soundtrack;
  - showroom clips only signal the soundtrack thread, and other videos stop it.
- Music volume uses the existing 101-entry level curve (`0x004362e0`) with the music channel.
- No synthesiser code may allocate or lock in the SDL audio callback.
- Any third-party file goes in `third_party/` with its licence, unmodified, at a pinned version.

## Review Focus

1. **Missing or corrupt `.SBK`/`.MID`** (user imported a damaged copy and bypassed the hash check, or the bank parser hits an unknown chunk): the race must continue silently, with the error logged once. Task 2 has a truncated-bank test.
2. **App backgrounded during MIDI playback (Android):** music pauses and resumes in place without a burst. The existing lifecycle tests are extended in Task 2.
3. **Rebinding a key to one already in use:** the original's rule (swap or refuse) must be traced and pinned. Task 3 has a test.
4. **Attract demo interrupted by input or a controller connect:** it returns to the main menu cleanly. Task 5 has a scripted frontend test.
5. **Loading an original save made by the Windows game:** it must load without changes. Task 6 round-trips a real original save.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `src/sbk.hpp/.cpp` | EMU SoundFont 1 (`.SBK`) bank parser (bounds-checked) |
| `src/midi.hpp/.cpp` | Standard MIDI File parser → timed events |
| `src/synth.hpp/.cpp` | Wrapper over the chosen synthesiser; renders stereo int16 blocks |
| `src/music.hpp/.cpp` | `MusicPlayer`: RRA soundtrack thread logic and race MIDI |
| `src/frontend.cpp` | Sound mix, controls, jukebox screens; attract timer |
| `src/input.cpp`, `src/settings.cpp` | Original key bindings (`0x0040e5f0`) and joystick analog path |
| `tests/sbk_test.cpp`, `tests/midi_test.cpp`, `tests/music_test.cpp` | Parsers and player |

---

### Task 1: SBK research and synthesiser decision (time box: one session)

**Files:**
- Modify: `docs/REVERSE_ENGINEERING.md` ("Race MIDI and SBK banks"), `third_party/` (only after the decision)

- [X] **Step 1:** Dump the RIFF chunk tree of each `GRUNGE*.SBK` and of the Klown bank (`python3 -c` over `../cdrom/ROADRASH/AUDIO/MUSIC/*.SBK`). Confirm whether they are SoundFont 1 (`sfbk` with `ifil` major 1) or SoundFont 2. Record the sample format, the number of presets and instruments, and whether the MIDI files reference General MIDI programs or bank-specific ones.
- [ ] **Step 2:** Trace how `0x00433bb0` passes the bank and song to `AWEMAN32.DLL` (the oracle proxy already forwards it). Record which MIDI channels and programs are used, and whether the game sends extra controller messages (volume, reverb).
- [X] **Step 3: Decide**, and record the reasoning in REVERSE_ENGINEERING.md:
  - **A (preferred):** convert SF1 to SF2 in memory (`sbk.cpp`) and render with TinySoundFont + TinyMidiLoader. These are MIT-licensed single headers; pin the commit and add the licence file.
  - **B:** if conversion turns out lossy for these banks, write a minimal native sampler in `synth.cpp`: the generators the banks actually use, with linear interpolation and the volume envelope only.
  - **C (fallback if both overrun):** play race music from the RRA soundtrack instead. This is an explicit, documented deviation from the original and needs the owner's approval in chat before implementation.
- [X] **Step 4:** Commit: `docs: SBK bank structure and race music synthesis decision`.

---

### Task 2: MIDI and SBK playback through `MusicPlayer`

**Files:**
- Create: `src/sbk.*`, `src/midi.*`, `src/synth.*`, `src/music.*`, `tests/sbk_test.cpp`, `tests/midi_test.cpp`, `tests/music_test.cpp`
- Modify: `src/sound.cpp` (move the RRA soundtrack logic into `MusicPlayer`), `src/game_main.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  namespace rr {
  struct MidiEvent { uint32_t tick; uint8_t status, a, b; };
  struct MidiSong { uint16_t division; std::vector<MidiEvent> events; std::vector<std::pair<uint32_t,uint32_t>> tempo; };
  MidiSong read_midi(const std::vector<uint8_t>&);            // throws std::runtime_error on malformed input
  struct SoundBank;                                            // opaque, from read_sbk
  std::shared_ptr<const SoundBank> read_sbk(const std::vector<uint8_t>&);
  class MusicPlayer {
  public:
      MusicPlayer(const Assets&, Mixer&, const Settings&);
      void play_soundtrack(int track);                         // 0x00403040 order 0..13
      void play_race(int original_course_index, const std::string& character_alias);  // 0x00433bb0
      void stop();                                             // race end / non-showroom video
      void pause(bool);                                        // lifecycle
      void update();                                           // game thread: refill render queue
  };
  }
  ```

- [X] **Step 1: Write the failing tests.**
  - **Synthetic MIDI:** a hand-built two-track format-1 file (bytes written in the test) with tempo change, running status and a SysEx event parses to the expected event ticks. Every truncation of it throws.
  - **Synthetic SBK:** a minimal `sfbk` RIFF with one preset, one instrument and one 16-sample sine parses. Truncated and garbage inputs throw.
  - **Corpus (skip 77):** all five `GRUNGE*.MID` and every `.SBK` parse.
  - **Player:** with a fake mixer sink, `play_race(4, "")` selects the song for original course 4 (NAPA), following the traced Grunge-by-course mapping. `stop()` silences within one block, and `pause(true)` holds the song position.
- [X] **Step 2:** Run the tests and confirm they fail.
- [X] **Step 3:** Implement per the Task 1 decision. Rendering happens on the game thread in `update()` into the mixer's SPSC queue, which keeps the callback lock-free. Volume follows the music level curve.
- [X] **Step 4:** Run the tests and confirm they pass. Then run the full gate and the sanitizer build.
- [ ] **Step 5: Listen.** Start a Napa race in `rr-game` and confirm music plays. Record in VERIFICATION.md that this was a manual listening check. Commit: `audio: race MIDI with SBK banks; soundtrack moved to MusicPlayer`.

---

### Task 3: Sound mix and controls screens; original bindings

> **Moved (October 11, 2026):** this task is now [plan 18](18-settings-and-options.md) Tasks 6 (sound mix) and 7 (Configure Input), which also add the native options screens. Do not execute it here.

**Files:**
- Modify: `src/frontend.cpp`, `src/frontend.hpp`, `src/input.cpp`, `src/input.hpp`, `src/settings.cpp`, `tests/frontend_test.cpp`, `tests/input_test.cpp`, `tests/settings_test.cpp`

**Starting point:**
- handlers `0x00401df0` and `0x0040fd90` (sound mix, controls; currently `return 0` in `frontend.cpp`);
- key binding table `0x0040e5f0`;
- Mixer block `0x00463030`;
- control word `0x004b7ed0` built by `0x0044f800`.

- [ ] **Step 1: Trace** both handlers: items, value ranges, the conflict rule for rebinding, and the stored registry values. Also trace the joystick analog steering path in `0x0044f800`.
- [ ] **Step 2: Write the failing tests.**
  - The scripted frontend flow opens each screen, changes a value and returns.
  - The setting persists through `settings.cpp`.
  - Rebinding to an already-used key follows the traced rule.
  - The analog stick maps to the same control bits as the original thresholds.
- [ ] **Step 3: Port.** Mixer changes apply live to the existing channel levels. Bindings replace the hard-coded defaults that PROGRESS.md lists under "Not ported yet". Touch and gamepad mappings stay native additions, documented as such.
- [ ] **Step 4:** Commit: `frontend: sound mix and controls screens with original bindings`.

---

### Task 4: Jukebox and complete cutscene triggers

**Files:**
- Modify: `src/frontend.cpp`, `src/game_main.cpp`, `tests/frontend_test.cpp`

- [X] **Step 1: Jukebox.** Port handlers `0x00445600`/`0x00445730` on top of `MusicPlayer::play_soundtrack`, then add a scripted test.
- [X] **Step 2: Cutscene audit.** Build a table in REVERSE_ENGINEERING.md of all 59 videos: trigger, original address and native status. Videos: `INTRO`, `START1–6`, `WIN1–6`, `LOSE1–10`, `WRECK1–6`, `BUSTED1–6`, `LEVEL1–6`, `FINALWIN`, `DUEL`, `JESSIE`, and the bike showroom folders `RAT`/`SPORT`/`SUPER`. Port every missing trigger. `BUSTED*` uses plan 11 Task 6's outcome.
- [X] **Step 3: Test.** The scripted frontend test drives each outcome (won, lost, wrecked, busted, level up, finale) and asserts the queued video path follows the traced rule for a fixed random value.
- [X] **Step 4:** Commit: `frontend: jukebox and complete cutscene triggers` (covered by step commits `bd31058` Step 1, `e00880c` Step 2, `7e7fa6e` Step 3 — October 12, 2026; no squash needed).

---

### Task 5: Attract demo

**Files:**
- Modify: `src/frontend.cpp`, `src/game_main.cpp`, `tests/frontend_test.cpp`

- [X] **Step 1: Trace** the 60-second main-menu timer and attract mode 0 (`0x004753b0`): which course, which riders, and how input ends it. Mode 0 also routes the race outcome back to the main menu (`0x00416120`).
- [X] **Step 2: Write the failing test.** A scripted frontend run idles 60 s of frames, enters the demo race, receives a key and returns to the main menu. A second run connects a controller during the demo.
- [X] **Step 3:** Port, reusing `make_race_setup` with an AI-controlled player. Commit: `frontend: attract demo`.

---

### Task 6: Save round trip and menu pixel comparison

**Files:**
- Modify: `oracle/aweman_proxy.c` (`RR_ORACLE_MENU=1`: dump the menu surface and palette when a screen settles; `RR_ORACLE_SAVE=1`: copy the written `.RRS`), `tests/career_test.cpp`, `tests/frontend_test.cpp`

- [ ] **Step 1: Saves.** Drive the original through a career start, one race and a save. Copy the `.RRS` to `analysis/oracle/`. The test loads it with `career.cpp`, re-serialises it and compares byte-for-byte. It also loads it into `rr-game` and checks cash, level, bike and relationships against values dumped from `0x004b8a10`.
- [ ] **Step 2: Menus.** Capture every menu screen the scripted flow visits. Compare native frames pixel-for-pixel, with a documented tolerance mask **only** for text glyphs: GDI hinting is a known deviation, see PROGRESS.md. Every non-text pixel must match.
  - October 26, 2026 (slice done, step open): the frontend-acceptance run
    screens are pinned in `tests/frontend_test.cpp` with the item-rect mask
    (title banner, bio box, y>=370 button band, every item rect dilated
    3px): fe-main 60%/98%/0%, fe-street 67%/96%/0%, fe-characters 67%/86%/6%
    (pin 0.80/0.08), fe-restroom 69%/85%/2%, fe-jukebox 69%/88%/1%,
    fe-controls 92%/80%/2% (cover 0.95), fe-graphics 68%/77%/4% (standard).
    Second slice (20261027 runs): thrash/bulletin/panzer/olleys/loading/
    wrecked/busted/won/lost at standard pins (93-100% exact, 0-4% big),
    penalty 100%/0%, standings 100%/0% (cover 0.95), schmooze 100%/0%
    (Pearl-N override), showroom 93%/4% (standard). Fixes: test-only
    Restroom/Rest3 + Wreck4/Bust4/Win4/Lose1 pins and schmooze rider/
    attitude override; jukebox/graphics background inheritance re-verified;
    `0x00420470` desc-fill gate (raw `flags & 8`); standings rows/title/
    darken in the 0x00421290 handler with bare direct entry and a
    dynamic-slot footer; showroom blurb-only desc + Demi-24 Length footer;
    player/price footers centered at y=380 with mapped content (formats,
    cash/trade/bike args, 0x420aa0 names); GDI ppem advances + tmHeight
    pitch + measured-width CALCRECT in `src/text.cpp` (380/380 ASCII,
    292/292 metrics; bitmaps stay unhinted). Open: exact hinted bitmaps,
    `>=0x80` bytes, schmooze line choice (random, masked), native
    dialogs/pause/pages (no original).
- [ ] **Step 3:** Record the results in VERIFICATION.md and commit: `test: original save round trip and menu frame comparison`.

## Exit criteria

- Race MIDI plays per the original rule, or an owner-approved fallback C is documented.
- No front-end handler returns the "not ported" stub. Mano-a-mano stays disabled unless plan 09 is approved.
- Every video has a native trigger, and an original save round-trips.

#### October 12 playtest follow-up

Live race MIDI was present but unconnected to the game pump. It is now pumped
per unpaused render frame, with PCM backpressure and synchronous mixer detach
at stop. The thirty-second Napa streaming regression passes; exact AWE/MIDI
audio fidelity remains subject to this plan's acceptance. See
[handoff](../agent-handoffs/playtest-audio-ai-loading.md).

#### October 31, 2026 status (Task 6 Step 2)

Menu pictures are bit-exact against the original's decoded-image cache (226
dumps), and the darkened fills and text colours are RGB565-exact. Still open:
hinted glyph bitmaps (FreeType 2.13.3 mono model measured, renderer pending)
and AWE/EMU8000 timbre. See
[frontend-exact-20261031](../agent-handoffs/frontend-exact-20261031.md).
