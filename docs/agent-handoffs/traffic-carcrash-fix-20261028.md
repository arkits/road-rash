# traffic-carcrash-fix-20261028

Task: Trace and fix tick 688 traffic capture divergence

Baseline: 66341cd34093ce51f7124c877663c2ea13fba13b

Worktree: `/Users/archit/Dev/rr/wt-traffic-field`
Build directory: `/Users/archit/Dev/rr/wt-traffic-field/build-agent-traffic-carcrash-fix-20261028`
Owner PID: 95735
Resource token: `7b47c23e-d413-4fe6-84d2-97234c1f028b`

Owned files:

- `/Users/archit/Dev/rr/wt-traffic-field/src/race.cpp`
- `/Users/archit/Dev/rr/wt-traffic-field/tests/field_replay.hpp`

## Progress / evidence

Pending.

## Checks / remaining work

Pending.

## Live processes / resources

File claims above. Record capture/Ghidra processes here.

## Recovered fixes and evidence

- `0x004368f0`: rider/car and crashed-half/car height gates use both
  objects' retained +0x20, and the bottom/top interval formula. A car lifted
  to height 6147 must not contact the grounded crashed bike at tick 688.
- `0x00449ce0`: lane obstacles with null +0x2c slices are excluded. Parking
  (`0x00406940`) clears both slice pointers; a car at position 40129 no longer
  stops for the parked bike at 40493 on tick 1059.
- `0x004368f0`: crashed halves participate in rider pairs, including their
  own bike. Separation returns for the +0x144 parent relationship
  (`0x00436a40`); callbacks still run in list order. The own stationary
  grounded bike/half guards in `0x004049c0` and `0x004046b0` are preserved.
  This matches the body/bike contact at tick 3387. Half route exclusion uses
  the half's own route, not its bike's route.
- `0x00452710` returns full-width 0x80000000 on an event endpoint;
  `0x00454240` tests the full value for hill flattening. `zone_end` preserves
  it when marking_flags' low byte is zero. Rendering and simulation now use
  the same ring_terrain_onto implementation. This fixes the one-unit hill
  ground difference at sample 428->429, tick 4279. A synthetic endpoint /
  ordinary hill / tunnel regression runs in course_scene-test.

Temporary contact/terrain diagnostics were removed. The attempted fork-link
hypothesis for tick 4279 was disproven and is not in the implementation.
No golden inputs, RNG anchors or visibility-load exemptions changed.

## Measured replays

All commands use `build/oracle-sim-test --driver=human CAPTURE ../cdrom/ROADRASH`
from this worktree; reanchor is not used.

- `sim-napa-traffic-field-carcrash-20261028a`: baseline first mismatch 688;
  now 4381/4381 identical, zero slice-load/rpm notes, 43 golden-only fields
  not compared (existing harness coverage limit).
- `sim-napa-traffic-field-{body,box,slice}`: 4381/4381 each. Box retains 2
  slice-load notes, slice retains 21; body has 50 nondeterministic sound
  channel notes and no slice notes. These remain notes, not newly accepted
  simulation divergences.
- October 26 `sim-napa-traffic-field`: first mismatch advances 1303 -> 2279.
  In particular tick 1303's crashed-half/car contact and RNG now match.
  Tick 2279 is a non-rider node position mismatch (draw 4482016:
  original 75067 vs native 75033), not a completed full-capture match.
- Strict human oracle gate passed, receipt
  `analysis/agent-gates/oracle-3d891a51fcd44eed9472fe0a86612881/receipt.json`.

Local replay/build logs use the `carcrash-` prefix in the worktree root;
commercial captures stay local in shared analysis/oracle. Capture receipt
and immutable trace details are in main's traffic-carcrash-20261028.md.

## Checks / remaining acceptance

Full Debug build passed. Full CTest still running; final result recorded below.
No native C++ LSP connector was available. Configured this owned build with
CMAKE_EXPORT_COMPILE_COMMANDS=ON. clangd parsed/indexed race.cpp without
compiler diagnostics, but its exhaustive ExtractFunction tweak checks returned
83 failures on break/continue (not compiler errors); the agent_lsp wrapper hit
its 60-second bound. Build verification is separate from that tool limitation.

The visibility-load rule, split-road crossing-car removal branch, October 26
post-2278 replay, and dedicated car/static callback coverage remain open.
No Android, sanitizer or packaging gate yet. Shared proxy/docs/frontend edits
were preserved. Private original install is retained until proxy integration.
