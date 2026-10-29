# traffic-field-20261028 — plan 10 Task 5/7: Napa L5 traffic field replay

Task: replay the Napa level-5 field-with-traffic scenario natively
(`sim-napa-traffic-field`, never replayed before) and fix each divergence
from its original function.

Baseline: `541c3b2`. Worktree `/Users/archit/Dev/rr/wt-traffic-field`
(branch `wt/traffic-field-20261028`), build dir `build/` inside it; the
worktree's `analysis/oracle` is a symlink to the shared capture directory
(git-ignored, read-only use). Baseline comparison build:
`/Users/archit/Dev/rr/wt-baseline-20261028` (detached at `541c3b2`).

## Claims respected

`frontend-acceptance-all-20261026` (Codex, PID 95735, still alive) claims
`oracle/aweman_proxy.c`, `CMakeLists.txt`, `docs/PROGRESS.md`,
`docs/VERIFICATION.md`, `docs/REVERSE_ENGINEERING.md` and has uncommitted
proxy edits in the shared tree. This session therefore did **not** edit
those files on `main`:

- The proxy additions used for the new captures are in
  [traffic-field-20261028-proxy.patch](traffic-field-20261028-proxy.patch)
  (applies to `541c3b2`; touches only the per-rider dump block, not the
  frontend-probe hunks). The integrator applies it once the claim is
  released.
- This handoff carries the PROGRESS / VERIFICATION / REVERSE_ENGINEERING
  entries for the integrator to merge.

## New captures (original, receipted, valid)

Same recipe as `sim-napa-traffic-field` (course 4, `RR_ORACLE_LEVEL=5`,
seed 1, `RR_ORACLE_HUMAN=1`, field, input `sim-napa-traffic-field.controls`,
controls sha `b7235bf04a56`), each through `tools/oracle_capture.py` with a
**private** oracle install
`prefix/drive_c/ElectronicArts/RoadRashOracle-tf20261028` (copy of the shared
install; the shared `AWEMAN32.DLL` was never touched). 4381 frames, human
branch, complete:

| Capture | Proxy sha | Adds |
| --- | --- | --- |
| `sim-napa-traffic-field-body` | `67330184b4c8` | `rider.N.body_*` crash-body words for every crashed slot |
| `sim-napa-traffic-field-slice` | `a3b5ffebcbf3` | + `rider.N.slice_{3d,60,61,64,68,6c}` own-slice static-collision state |
| `sim-napa-traffic-field-box` | `3bad09036d4d` | + `slice_ac`, first loaded box `slice_box0..3` |

The pre-`srand` brain jitter (`0x004078f0`, rows 9..14) is capture input, so
each capture is a different race; all replay with one RNG anchor.

## Results (`oracle-sim-test --driver=human <capture> <disc>`)

| Capture | Baseline `541c3b2` | Now |
| --- | --- | --- |
| traffic-field-box | (new) | **4381 identical** (2 slice-load notes; `+0xac` compared) |
| traffic-field-slice | 1923 | **4381 identical** (21 slice-load notes) |
| traffic-field-body | 2576 | **4381 identical** (second commit; first commit 3068) |
| traffic-field (Oct 26) | 1303 | 1303 (see open items) |
| solo-split-left/right, solo-script, fight | identical | identical (unchanged) |
| contact / fight-steal / steal-vary | 3841 / 896 / 883 | same ticks (no regression) |

Police: every original police capture still matches in `police-test`
(napa-cops-parked arrest 4552, crashed, foot 4616, foot2 4544, city).

## Fixes (all cited to original addresses)

1. **Remount re-anchors the finish distance** (`0x00404560` tail →
   `0x00407410` on the half after the `0x004067d0` copy-back):
   `crash_recovery` sets `finish_distance = finish - body` on remount.
   body 2575: `+0x1f8` 1496540 → 1496547.
2. **Lateral clamp lands on the crash body** (`0x00406b40` clamps the half's
   own `+0x28`, which is the body while crashed): `run_half_node`.
   slice 1923 (walk-back starts from 819200).
3. **Static collision boxes follow the previous frame's draw**
   (`0x00438610` → `0x0044b5f0`/`0x0044b630` fill `+0x61/+0x68` on first
   draw): `prepare_static_boxes` now records the draw at the end of a step
   and `apply_static_boxes` loads it at the start of the next; reach is
   slices starting ≤ ring camera (`DAT_004b6e5c`) + `0x1e00`, nothing before
   the first frame. Measured on 22,800 rider-slice samples: exact except 23
   original "not loaded" samples (2 at the far edge, 21 one AI rider
   ~7100 Q8 ahead): projection visibility not ported, reported as
   `slice load note` (any contact it changed would still fail as a field/RNG
   divergence). Previously native marked 33 slices from `camera_position()`.
4. **Car slice reads use the recycled ring slot** (`+0x2c` points into the
   128-slot ring): a car behind the ring back reads the slice built 128
   samples ahead (surface class via `0x0043a0c0`, lane centre `+0xcc`).
   `0x0044dc10` only drops a crossing car for a missing intersection flag
   **inside** `[back, front)`. box/slice 3834: crossing car drag class 3
   (accel −14), speed 1392, +14.
5. **Intersection teardown is deferred** (`0x0041f1f0` → `0x00437c10`
   marks; `0x00437df0` prepass unlinks next tick) and **setup controllers
   think on 1 + 30k** (`TrafficSystem::setup_tick` / `ring_setup_tick` 2 → 1).
   The old "2" came from reading the unlink tick as the creation phase; it
   agrees with the police capture (`0x0041f040` at 751) and keeps
   napa-field-l1 (teardown 392), schema2 and L5 identical. box 3861/3862.
6. **Cop spawn replan runs before `DAT_004642dc`** (`0x00406b80`: line 45
   `0x00405990` → `0x00405c80` → `0x004082f0`, then line 48 stores the cop):
   the spawn-time replan treats the cop as an opponent (cap `0xa00`, edge
   test). box 4096: cop state 1 with clock 4, not state 4.
7. **Cop deletion marks, prepass unlinks** (`0x004073a0`: siren off,
   `DAT_004642dc = 0`, `0x00437c10` on half and bike: `+0xc0 = (& ~4) | 8`,
   `+0xc = +0x10 = tick + 100`); the rider is erased at the next step's
   start. box 4104: `bike_c = bike_10 = 4204`, flags 11, gone on 4105.

Second commit (same session):

8. **Far single-box widening honours `+0xac`**: `0x0044b630` writes
   sub-slice `+0xac` (= `slice+0x60+0x4c`) with word `[3]` (the flags) of
   the frame-2 texture record when it carries the `0xbc614e` magic, in the
   same block that loads the boxes; `0x004373f0` widens a far single box
   ×3 only when bit `0x4000` is clear. The slot rebuild (`0x00451cb0`) never
   clears it, so native keeps `+0xac` per ring slot (`slot_ac_`). The old
   note "never written by the ring" was wrong. body 3068 (lateral-1396
   BORR, flags `0x4002`) passes. The first draw starts one slice behind the
   camera slice (box capture: slot 49 loaded at start, seen stale on 177
   and 305).
9. **Ring slot lookup counts only built slices**: one helper
   `ring_slot_sample` (newest same-residue sample below `front >> 8`) for
   `plan_speed`'s lane words and `move()`'s slice reads. The previous
   lane-word rule took the sample *at* the front, which is not built yet.
   body 4278: oncoming car at 352 kept driving (target 2180), native had
   stopped it on 480's lanes.

Ported from disassembly, not yet exercised by any capture (kept, labelled):

- `collide_traffic_half`: crashed halves (flags 7 after `0x00403d60`) pair
  with cars in `0x004368f0` (`+0x1a0` both, `0x00436a40`, `0x0044efa0` car
  callback, `0x004046b0` half callback). Evidence it happens: Oct 26 capture
  tick 1303 (half raw `+0x190` = 1800, `+0x1a0` = the car, extra `rand()`).
  It does not fire there natively: the native body is 22,060 high, the
  height test needs ≤ 17,920 (see open items).
- `collide_car_statics`: due cars are outer nodes of `0x004368f0` and sweep
  their slice through `0x004373f0`; `0x00437750` lets cars (`+0xc4 & 0x10`)
  meet only type-0 billboards; `0x0044efa0` with the static partner sets
  `+0x1f8 = 0x3c`, no horn draw.

## Harness (`tests/field_replay.hpp`)

- `rider.N.body_<hex>` compared against `crash_actor.motion` (fatal).
- `rider.N.slice_*`: BORR count, billboard count and first slot lateral
  compared (fatal); load state reported as counted `slice load note`s; the
  player's half slot reads the crash body's slice while down.
- Earlier in this session the body/slice checks were mistakenly added to
  the solo loop in `oracle_sim_test.cpp`, which field captures never reach;
  that edit was reverted and any intermediate "validated" claim from it is
  void. Only `field_replay.hpp` results above are measured.

## Test change

`police-test` "Live wiring" (synthetic, default game config, no capture)
asserted that the periodic cop arrests a player parked on the grid. With the
corrected controller phase the traffic ahead of the cop differs, the cop
reaches 10,225 and stops 0x63c past the player, outside the `0x00407860`
0x500 forward window. The block now asserts spawn at 4096 behind the
player, no place, sirens, and pursuit (state 4 targeting the player); the
arrest outcome is asserted on the original capture of the same scenario
(`napa-cops-parked-20261026`, arrest 4552 as captured, unchanged).

`race-screen` had a second copy of that fixture (table bike, character 1)
feeding the busted screen flow; it is replaced by a pointer to the riding
and on-foot arrest fixtures in the same test, which cover the same flow and
pass.

## Open items (next actions)

1. (done in the second commit: body 3068 was the `+0xac` widening gate.)
   The `box` capture's `slice_box0..3` keys are not compared yet.
2. Oct 26 capture 1303: crashed-half vs car pair (above) blocked on the
   body height after a reason-2 car crash (native 22,060 at tick 1300).
   No later capture has had a car-crash body; capture one with the patch.
3. Projection visibility for box loading (23 notes).
4. `0x0044dc10` second branch (dual-road lateral removal through slot
   `+0x28` type 2) is not ported.

## Checks

- Full Debug build clean (`cmake --build build --parallel`), clang-format
  applied to changed lines.
- `ctest --test-dir build -j8` (worktree, captures linked): 117/118 on the
  first run, the one failure `race-screen` (fixture above); after the
  fixture change `race-screen` passes. All `sim`-labelled tests pass
  (oracle-sim, -field, -field-schema2, -field-l5, police, race_setup,
  race-soak, rollback, race_state, net_client, ...).
- `tools/agent_gate.py oracle --build-dir build` (= `make verify-sim`):
  passed, receipt
  `analysis/agent-gates/oracle-dcf968c97a864f799e934362b7e1918a/receipt.json`
  (in the worktree).
- Replays in the results table, current build vs `541c3b2` baseline build.
- Second commit: full Debug build, CTest **118/118**, oracle gate passed
  (receipt `analysis/agent-gates/oracle-ca7aaeae21d74902b6eac521279b6937/receipt.json`
  in the worktree).
- Not run: Android, sanitizers, package gates (no platform code changed).

## Resources

No live processes. Private oracle install
`prefix/drive_c/ElectronicArts/RoadRashOracle-tf20261028` (473 MB) can be
deleted once the patch is integrated; the shared install is untouched.
Captures under `analysis/oracle/sim-napa-traffic-field-{body,slice,box}*`.
