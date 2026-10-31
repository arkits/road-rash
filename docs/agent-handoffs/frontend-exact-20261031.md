# frontend-exact-20261031

Task: plan 13 Task 6 Step 2 remainder — exact menu pictures, GDI-hinted
glyph bitmaps, AWE/MIDI music timbre. Pictures and text done; timbre open.

Baseline: `c9eb240`, rebased onto `6b3b6be`. Build directories: `build-agent-frontend-exact-20261031`
(Debug), `build-agent-frontend-exact-web` (emscripten Release).

Adopted the abandoned October 26 frontend-acceptance worktree changes
(owner PID 95735 gone; its handoff `frontend-acceptance-all-20261026.md`
never left "Pending"): `cmake/jpeg_scalar.cmake`, the libjpeg `src/rri.cpp`
decoder, `oracle/frontend_probe.h` + the `aweman_proxy.c` hook,
`tools/frontend_acceptance.py`, `third_party/JPEG-*`. Those are the files
that produced the git-ignored acceptance goldens other sessions already pin.

## Done (verified)

- Menu pictures bit-exact: 226 original decoded-image cache dumps (20
  pictures, including Postcard/City and Street/Klub composites) checked in
  `frontend-test`. Composition is `compose_menu_picture` in `src/rri.cpp`.
- `darken555`: 6-bit green kept even, like the picture surface.
- Menu text colours are the RGB565 read-backs (golden pixel census).
- Full-frame exact vs goldens: restroom 96%, jukebox 98%, graphics 97%,
  characters 95%. Remaining red pixels are glyph shapes and the box edges
  that follow from them.
- libjpeg-turbo/IJG notices in the macOS bundle, APK, Linux tarball/Flatpak
  and Windows package; Flatpak gets the pinned archive via
  `RR_JPEG_SOURCE_DIR`. Not built here: Linux/Windows packages and the MSVC
  branch of `cmake/jpeg_scalar.cmake` (`jpeg-static.lib`); check them in CI.

## Done later the same day (owner approved fetching FreeType)

- `cmake/freetype.cmake`: FreeType 2.13.3 (Wine's bundled release) built
  static from the pinned tarball; offline `RR_FREETYPE_SOURCE_DIR` for the
  Flatpak; `third_party/FreeType-FTL.txt` in every package and verifier.
- `src/text.cpp` renders through FreeType. `TextSurface::rgb565` (menus)
  draws 17-level anti-aliased glyphs and blends them like Wine's 16-bit DIB
  driver; `indexed8` (race HUD, overlays) draws 1-bit glyphs. Wine word wrap
  (`TEXT_WordBreak`), DrawText rect clipping, and the Restroom title's
  unclamped text width in `src/frontend.cpp`.
- Ground truth: `tools/glyphprobe.c` (mono, and `gray` 32bpp), new
  `tools/blendprobe.c` (RGB565 blend), `tools/textprobe.c` (CALCRECT). Dumps
  in `analysis/frontend/glyphs-20261031/` (git-ignored); `text-test` replays
  them: mono 8446/8448 (BADLOC 14 Í/Ú), smoothed 8448/8448, blend
  491520/491520.
- `frontend-test` full-frame ceilings: characters 119, restroom 83, jukebox
  83 (all the Wingdings focus arrow), graphics 787 (arrow + port-only
  Display row), loading/schmooze/wrecked/busted/lost 0.

## Open

1. **Focus arrow.** The original draws it with Wingdings, a Windows system
   font that is not on the disc. Exactness needs that font from the player's
   system, or a recorded bitmap of the arrow at its sizes.
2. **AWE/EMU8000 timbre.** `src/race_music.cpp` applies only attenuation,
   pitch and a 5 ms release. The banks use vol/mod envelopes, initialFc/Q,
   mod/vib LFOs, pan, reverb/chorus sends and exclusive classes (igen census
   over GRUNGE1-5.SBK and RRDRUMS.SBK). Wine has no EMU8000, so it is no
   timbre oracle, and the factory-ROM presets need an owner-supplied ROM
   dump. Implementing the SF1 generator semantics is the next independent
   slice.
3. Outside this task's screens: y=380 player/price footers (thrash,
   bulletin, showroom, panzer) and New Courier standings rows.

## Checks

See VERIFICATION.md "Exact menu pictures, dark fills and text colours".

## Live processes / resources

None held. The scratch Wine prefix lives under this session's scratchpad.
The shared oracle prefix and DLL install were not touched.
