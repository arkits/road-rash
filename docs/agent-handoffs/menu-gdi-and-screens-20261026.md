# Handoff: GDI-hinting research + remaining-screen captures (plan 13 Task 6 Step 2)

Task: (1) GDI-hinting research to tighten the characters/graphics pins;
(2) Wine captures for the remaining scripted screens + pins.

Baseline: `901a021` (plus the frontend-acceptance worker's still-untouched
uncommitted files — see coordination). Owned build dir:
`build-agent-menu-gaps` (Debug, full gate 106/106).

INTEGRATION NOTE (pushed 2026-10-27, after the session's main push failed):
upstream `origin/main` moved meanwhile (plan-34 race/netcode work through
`58e8fd6`) and now touches shared docs (`PROGRESS.md`,
`REVERSE_ENGINEERING.md`, `VERIFICATION.md`) plus `CMakeLists.txt`, which
has the frontend-acceptance worker's UNCOMMITTED edits. A rebase/merge here
would tangle their in-flight work, so this slice is pushed as branch
`agent/menu-gdi-screens-20261026` (commit `494999e`, self-contained on
merge-base `80f3e72`) for the integrator — no history rewritten, no other
worker's files touched. Merge order: shared docs (append-only dated
entries), `frontend_test.cpp` (append-only blocks), `frontend.cpp/hpp`,
`text.*`, `race_cache_test.cpp` (baseline constants).

## Changed files (mine — safe to commit with explicit paths)

- `src/text.cpp`, `src/text.hpp`: GDI ppem rule + tmHeight pitch +
  measured-width CALCRECT; advance = `round(design*ppem/upm)`;
  `line_height()` returns tmHeight; `draw_box` steps by pitch. Bitmaps stay
  unhinted stb (documented).
- `src/frontend.cpp`, `src/frontend.hpp`: desc fill gate corrected to raw
  `flags & 8`; standings rows/title/darken moved to the 0x00421290 handler
  (`standings_ready_` gate; bare direct entry); standings footer from the
  dynamic slot in the last row's rect, disabled 0x22 rows skipped in paint;
  showroom blurb-only desc + Demi-24 Length footer (`length_footer_`);
  player/price footers centered at y=380 (`player_footer_` on `flags & 0x800`
  focus with 0x46b09c content, `price_footer_` on showroom with 0x46b058
  content, trade = price/2, bike names via 0x420aa0); schmooze
  rider/attitude test override.
- `tests/frontend_test.cpp`: 13 new screens pinned (thrash/loading/olleys/
  showroom/panzer/schmooze/bulletin/wrecked/busted/won/lost/penalty/
  standings); characters 0.80/0.08, everything else standard except
  standings/controls cover 0.95.
- `tests/race_cache_test.cpp`: scale-2/scale-4 RGB baselines re-recorded
  (HUD glyphs move with the advance rule; scene hashes intact).
- `tools/textprobe.c`: Wine-GDI ground-truth probe (i686 mingw, headless).
- Docs: `docs/PROGRESS.md`, `docs/VERIFICATION.md`,
  `docs/REVERSE_ENGINEERING.md` ("GDI text metrics", fill-gate correction,
  standings/footers), `docs/plans/13-music-and-frontend-completion.md`.

## GDI findings (all measured, evidence in REVERSE_ENGINEERING.md)

- LOGFONTs: positive cell heights, weight 0 (boldness in FUTB file),
  OUT_TT_ONLY_PRECIS, DEFAULT_QUALITY (sole CreateFontA at 0x4487e3,
  table at 0x004239e5).
- ppem rule: `round(H*upm/cell)`, step down once when the rounded scaled
  cell exceeds H. 292/292 TEXTMETRIC points, all four faces, heights 8..80.
- Advances: `round(design*ppem/upm)` — 380/380 ASCII vs GetCharABCWidthsA
  (hdmx NOT used). Pitch = tmHeight (33 at 32px FUTR — the old drift).
- Residuals: unhinted bitmap shapes; ~3px right-aligned pen offset on
  Demi-24 descs (CALCRECT-vs-draw nuance, open); >=0x80 cp1252 bytes differ
  (GDI default-char widths vs .notdef).

## Capture runs (new output dir, existing harness unmodified)

- `analysis/frontend/acceptance-20261027/40c6e15.../`: street → thrash,
  loading, olleys, showroom, panzer, bulletin. All screens correct.
  Thrash/bulletin share Postcard+City; loading = Chars/Cydney-F;
  showroom/penalty inherit previous (Olleys/Lose1).
- `analysis/frontend/acceptance-20261027/6fc8190.../`: street → wrecked,
  busted, won, lost, penalty, standings. Trace caches give variants:
  Wreck4/Bust4/Win4/Lose1 (pinned via `set_rest_picture_for_test`).
- Scripts are embedded in each run's `receipt.json`; re-runnable via
  `tools/frontend_acceptance.py` (other worker's file, used read-only).

## Corrections to previous claims

- Desc fill is raw `flags & 8`, NOT `flags & 0x20f08`: showroom (0x184)
  and course (0x4815) bios have no box (pixel-verified vs photo); only
  0x8 items fill. The 0x4208d0 `or ebx,0xc` serves another purpose.
- Characters was never a portrait-variant mismatch (Axle-N was right);
  the gap was the desc fill, then line pitch.
- The 0x22 screen populates in the 0x00421290 handler, not on entry
  (disassembled + golden-verified).

## Open (all masked or documented; none block the gate)

1. ~~Price/player footers~~ DONE this session: formats byte-exact
   (0x46b058/0x46b09c), args mapped (slots 29/30/31/504/505, level slot
   91+level, cash, trade = price/2, 0x420aa0 bike names), centered y=380,
   visually verified vs goldens. `[0x4b8a12]` level index is an assumption.
2. ~~Schmooze golden~~ DONE: run-3 capture (Pearl-N) pinned 100%/0% via
   rider/attitude override; the random line stays masked.
3. Exact hinted bitmaps (FreeType vendoring decision — project-level call).
4. Native dialogs/pause/pages have no original — never pinnable.
5. `tools/textprobe.c` committed; `/tmp` holds ephemeral analysis tools.

## File claims / coordination

- Other worker's files (`CMakeLists.txt`, `aweman_proxy.c`, `rri.cpp`,
  untracked harness) untouched. My `src`/`tests` edits overlap their
  *claimed* (not yet edited) files — integrator: merge order on
  `frontend_test.cpp` (append-only blocks) and `frontend.cpp/hpp`.
- Oracle DLL at `oracle/build/AWEMAN32.DLL` was already current (newer
  than both sources); no rebuild was needed. No live processes held;
  captures used a fresh output dir, not the worker's.

## Checks

- `cmake --build build-agent-menu-gaps --target frontend-test` clean.
- `./build-agent-menu-gaps/frontend-test ../cdrom/ROADRASH` → 0 failures
  (20 masked lines; see VERIFICATION.md for the table).
- `ctest --test-dir build-agent-menu-gaps` → 106/106 pass (includes the
  re-recorded `race-cache` RGB baselines; scene hashes intact).
- ASan/UBSan trees not run (shared dirs left alone); changes are
  arithmetic/branch-only plus handler relocation covered by the gate.
