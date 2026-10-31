# frontend-exact-20261031

Task: plan 13 Task 6 Step 2 remainder — exact menu pictures, GDI-hinted
glyph bitmaps, AWE/MIDI music timbre.

Baseline: `c9eb240`. Build directories: `build-agent-frontend-exact-20261031`
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
- libjpeg-turbo/IJG notices in the macOS bundle and the APK.

## Open

1. **GDI glyph bitmaps.** Ground truth: build `tools/glyphprobe.c` with
   `i686-w64-mingw32-gcc -O2 -o glyphprobe.exe tools/glyphprobe.c -lgdi32`
   and run it under Wine Staging 11.18 with a scratch `WINEPREFIX`, passing
   the folder holding the disc `TEXT/*.DLL` (trailing backslash). Model (see
   REVERSE_ENGINEERING.md "GDI glyph bitmaps"): integer ppem,
   `FT_LOAD_TARGET_MONO`, metric bbox, `FT_Outline_Get_Bitmap`. With
   Homebrew FreeType 2.14.3 it is 6986/8448 exact (ASCII 99.1%). Wine ships
   FreeType 2.13.3. Next action: add `cmake/freetype.cmake` (ExternalProject,
   `freetype-2.13.3.tar.xz`, SHA-256
   `0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289`,
   zlib/bzip2/png/harfbuzz/brotli off), re-run the comparison against
   2.13.3, then replace the stb bitmap path in `src/text.cpp` (keep the GDI
   ppem/advance/pitch rules) and pin a glyph golden test against a
   probe dump (git-ignored, skip 77). The fetch/build of the
   FreeType tarball was refused by this session's permission classifier;
   it needs owner approval.
2. **AWE/EMU8000 timbre.** `src/race_music.cpp` applies only attenuation,
   pitch and a 5 ms release. The banks use vol/mod envelopes, initialFc/Q,
   mod/vib LFOs, pan, reverb/chorus sends and exclusive classes (igen census over
   GRUNGE1-5.SBK and RRDRUMS.SBK; RRDRUMS also uses pan and initialQ). Wine has no EMU8000, so it cannot serve as a
   timbre oracle (not measured which mix the oracle install selects), and
   the factory-ROM presets need an owner-supplied ROM dump. Implementing
   the SF1 generator semantics is the next independent slice.

## Checks

See VERIFICATION.md "Exact menu pictures, dark fills and text colours".

## Live processes / resources

None held. The scratch Wine prefix lives under this session's scratchpad.
The shared oracle prefix and DLL install were not touched.
