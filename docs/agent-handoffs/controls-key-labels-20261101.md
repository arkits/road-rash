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
- Resolved in the follow-up below.

## Follow-up: rows, key codes and Turn/Lean pairs

Changed `src/input.cpp`/`.hpp`, `src/settings.cpp`/`.hpp`,
`src/frontend.cpp`/`.hpp`, `tests/input_test.cpp`,
`tests/settings_test.cpp`, `tests/frontend_test.cpp`; docs as in PROGRESS
"Configure Input rows, key codes and Turn/Lean pairs".

- Row i sets bit `1 << i`; the October action-table note in
  REVERSE_ENGINEERING was wrong (corrected there, with the reason).
- Table codes are original keyboard codes (high byte); the port had read the
  low byte as an SDL scancode, so original defaults and saved bindings were
  dead in the race.
- Remaining: custom-table joystick codes drive nothing natively; the
  joystick half of `0x0040fc70` is not ported; legacy captures of SDL
  scancodes `0x25`–`0x28` read as joystick codes.

## Live processes / resources

None held. Build directory `build-agent-keylabels` is this session's.
