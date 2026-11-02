# sbk-timbre-20261031

Task: the race-music renderer plays the SBK banks with their SF1 generator
semantics (open item 1 of
[frontend-exact-20261031](frontend-exact-20261031.md); facts in
REVERSE_ENGINEERING.md, "SBK generator semantics"). Plan 13, music timbre.

Baseline: `003ec9b`. Build directory: `build-sbk-timbre` (Debug).

## Owned and changed files

- `src/race_music.cpp`, `src/race_music.hpp`: generator-table parser
  (global zones, SF1 merge, SBK→SF2 conversion), the new `Sf1Synth` voice
  engine, the offline render and the live player on top of it.
- `tests/music_test.cpp`: general synthetic SF1 builder plus 50 rule checks.
- Docs: PROGRESS.md, VERIFICATION.md, REVERSE_ENGINEERING.md (renderer-slice
  sub-bullet), CURRENT_STATUS.md, plans/00-INDEX.md, plans/13, and open item
  1 of frontend-exact-20261031.md.

## Decisions (all spec-derived; no EMU8000 oracle)

- Unit rules follow TiMidity's `sbkconv.c`/`sffile.c`/`sndfont.c` (GPL; read
  only, nothing vendored). That includes its integer truncation (atten 101 →
  198 cB) and its SBK defaults: an unset sustain is silent or zero level,
  unset LFO rates are −725/−15600 absolute cents, and unset times are
  −12000 timecents.
- Where TiMidity is an approximation of its own, the SF2 spec wins:
  −12000 timecents is 1 ms, not instant; the real mod envelope drives pitch,
  not TiMidity's sustain tune shift; drums use plain key − root (no keynote
  retune); there is no root −60 correction.
- Velocity, CC7 and CC11 use the SF2 concave 960 cB curve (amplitude ∝ x²).
  The old renderer used linear velocity. The SF2.01 velocity→Fc modulator is
  left out, as SF2.04 does.
- RBJ low-pass with Q = 10^(cB/200) floored at 0.7071; the filter is active
  only for Fc 1500..13500 abscents (TiMidity's window), so 127 stays open.
- Master gain 0.25 (−12 dB) for headroom. Centre-panned voices land at the
  old live level per channel.
- Polyphony 32 (EMU8000 oscillators); stealing takes the quietest released
  voice, else the oldest.
- Song loop in the live player: all notes released and controllers reset
  at `total_ticks`. Release tails continue instead of being cut.

## Evidence

- Pitch check without an oracle: autocorrelation of the GRUNGE2/5 zones that
  set only samplePitch (4281, 4782) gives 42.8/47.9 semitones at 44100 Hz.
  Scratch scripts are in the session scratchpad and are not committed.
- Corpus mono renders (scratch dump, not committed): SBKGRNG1..5 peak
  19777/20835/17021/15237/19610, zero clipped samples.
- Checks: see VERIFICATION.md, "SBK generator semantics in the race-music
  renderer".

## Remaining

- Reverb/chorus: done November 1 as stand-in buses (see below).
- Factory-ROM presets (SBKGRNG1 program 18, KLOWN): still the synthesized
  stand-ins; needs an owner-supplied ROM dump.
- No measured comparison is possible without real AWE32 hardware or an
  EMU8000 emulator capture.

## Live processes / resources

None held after the gates. The Android gate used the shared Gradle daemon.

## Follow-up: reverb and chorus (November 1, 2026)

Baseline `aa9cb6d`. Changed `src/race_music.cpp`/`.hpp` and
`tests/music_test.cpp`; docs as in PROGRESS.md "November 1, 2026".

- The game sends no AWEMAN32 effect-mode message, so the driver defaults
  (Hall 2, Chorus 3) apply. The algorithms are stand-ins: a Freeverb-tuned
  hall (~2.3 s RT60) and a 23 ms single-tap chorus. Freeverb's tunings are
  public domain; nothing was vendored.
- Send = zone gen 15/16 + 200·CC/127 per mille. Power-on CC91 40, CC93 0.
- Gates: ctest 134/134, format-check, portability 0, Android debug PASS
  (`analysis/agent-gates/android-debug-ded4d33de53143f3861486ddb32a1c75`,
  hardware not run).
- Still open: the factory ROM (owner dump) and any measured comparison.
- No processes or resources held.
