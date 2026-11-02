# Controls key labels and Wingdings arrow keys (November 1, 2026)

Task: open item 2 of [frontend-exact-20261031](frontend-exact-20261031.md)
(plan 13/18 front end). Baseline commit `c4e1490`.

## Changed files

- `src/frontend.cpp`, `src/frontend.hpp`: `key_name` builds the
  `0x0040e7b0` slot text; new `draw_arrow_label` ports `0x00420280`; both
  button paths strip the `0x1a` prefix (`0x0041ff80`). Test hook
  `set_string_for_test`.
- `tests/frontend_test.cpp`: label text, unbound row, rebind label and
  arrow-draw checks; `--dump` writes the 1x/2x controls frames.
- Docs: PROGRESS, VERIFICATION, REVERSE_ENGINEERING, CURRENT_STATUS,
  agent-status.json, plan 13, README.

## Evidence

Decompiled `analysis/decompiled/0040e7b0.c`, `00420280.c`, `0041ff80.c`;
EXE bytes at `0x00466658..0x00466670` and `0x0046b048`; ENU.DLL strings
280 `<Keyboard>`, 384 `<Unassigned>`. Checks in VERIFICATION.md.

## Remaining

- No original capture with filled labels: the label rendering is unmeasured.
- The Configure Input rows read Accelerate, Brake, Turn, Lean, Nitro boost,
  Swing, Kick, Backhand, Dismount, while the port's row actions are
  accelerate, brake, left, right, punch, kick, backhand, -, - (nitro on row
  9). Row 4 shows Z ("Nitro boost") but Z punches. Check the original's
  row meaning (race input reader of `0x465fb8`) before changing anything.

## Live processes / resources

None held. Build directory `build-agent-keylabels` is this session's.
