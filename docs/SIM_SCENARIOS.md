# Simulation scenarios (plan 10)

Each scenario captures the original game with `RR_ORACLE_SIM=1` (per-tick
`frame` entries, header frame 0 with course/level/seed). Courses use the
original order (`0x0046df58`): 1 CITY, 2 HIWAY, 3 MEDLY, 4 NAPA, 5 CANYN.
Control streams (`.controls`, one hex word per line) replay with
`RR_ORACLE_INPUT`. Native replay is `oracle-sim-test` (plan 10 task 4).

`RR_ORACLE_SOLO=1` freezes every scheduler node except the player bike and
player rider-half and nulls the traffic pool, isolating player physics.
Full-field replay now matches the complete 2400-tick Napa level-1 capture.
Race setup is seeded at menu entry, but the six extra personalities receive
speed-table jitter earlier in startup. New captures can therefore have
different field trajectories despite the same race seed. The older
attribution to animation frame pacing is superseded: the expanded capture
compares every AI animation against absolute simulation ticks. Human branch
acceptance and demo-driven live-field evidence remain distinct.

## napa-solo

Level 1, default bike and character, seed 1, drive=2 autopilot, solo.
First acceptance scenario: player physics on Napa.

```sh
RR_ORACLE_COURSE=4 RR_ORACLE_SEED=1 RR_ORACLE_DRIVE=2 RR_ORACLE_SIM=1 RR_ORACLE_SOLO=1 RR_ORACLE_RECORD='C:\sim-napa-solo.controls' RR_ORACLE_FRAMES=2400 oracle/run.sh sim-napa-solo
```

The drive capture fixes the control stream (2,221 words). The acceptance
golden is input-driven from that stream, so capture and replay have
identical, file-determined lengths (2,221 driven ticks + 181 input-exhaustion
coast ticks = 2,402 frames):

```sh
RR_ORACLE_COURSE=4 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1 RR_ORACLE_SOLO=1 RR_ORACLE_INPUT='C:\sim-napa-solo.controls' RR_ORACLE_FRAMES=2700 oracle/run.sh sim-napa-solo-b
RR_ORACLE_COURSE=4 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1 RR_ORACLE_SOLO=1 RR_ORACLE_INPUT='C:\sim-napa-solo.controls' RR_ORACLE_FRAMES=2700 oracle/run.sh sim-napa-solo-replay
python3 tools/golden_diff.py analysis/oracle/sim-napa-solo-b.tsv analysis/oracle/sim-napa-solo-replay.tsv
```

The committed `sim-napa-solo.tsv` is the input-driven capture (renamed);
`sim-napa-solo-replay.tsv` is its replay. All three pairwise comparisons
(drive vs input-driven, input-driven vs replay) report zero differences.

Self-check (tick contiguity) and determinism replay:

```sh
python3 tools/golden_diff.py analysis/oracle/sim-napa-solo.tsv analysis/oracle/sim-napa-solo.tsv
RR_ORACLE_COURSE=4 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1 RR_ORACLE_SOLO=1 RR_ORACLE_INPUT='C:\sim-napa-solo.controls' RR_ORACLE_FRAMES=2400 oracle/run.sh sim-napa-solo-replay
python3 tools/golden_diff.py analysis/oracle/sim-napa-solo.tsv analysis/oracle/sim-napa-solo-replay.tsv
```

## napa-solo-straight (field, reference only)

Same settings with the standard career field (`sim-napa-solo-straight.tsv`,
2,223 ticks). Deterministic through setup; attack landings diverge mid-race
(first observed at frame 392, rider.4.speed). Kept as task 6 input for
opponent slot mapping and fight-state work, not an acceptance golden.

## napa-solo-human (acceptance: player-control path)

Level 1 Napa, default bike and character, seed 1, solo (`RR_ORACLE_SOLO=1`,
which freezes every scheduler node except the player bike and the player
rider-half and nulls the traffic pool). Unlike `napa-solo` (drive=2
autopilot, whose recorded words are ignored), this scenario feeds a
scripted human-style control stream through `RR_ORACLE_INPUT`, so the
player think `0x00434660` takes the player-control branch (applies
throttle/steer/brake from `DAT_004b7ed0`) instead of the attract-demo AI
branch (`set_demo_hold`).

Control bits (bike.hpp, tables `0x00465fb9`/`0x00476c00` order):
accelerate `0x1`, brake `0x2`, left `0x4`, right `0x200`. There are no
manual gear bits; gears shift automatically on speed thresholds
(`shift_gears`, `0x004391d0`: gear 0→1 at speed ~3000, observed at tick
189 in this capture). The script phases (1,250 words; indices are 0-based
into `sim-napa-solo-human.controls`, control ticks start 2 words after the
59-frame start-hold, i.e. word N drives golden tick 60+N):

| Words | Ticks | Controls | Maneuver |
| --- | --- | --- | --- |
| 0–299 | 60–359 | `1` full throttle | launch, gear 0→2 climb |
| 300–359 | 360–419 | `5` throttle+left (60) | hard left |
| 360–479 | 420–539 | `2` brake (120) | braking, downshift |
| 480–539 | 540–599 | `513` (`0x201`) throttle+right (60) | hard right |
| 540–839 | 600–899 | `1` full throttle (300) | straight |
| 840–899 | 900–959 | `5` throttle+left (60) | hard left |
| 900–1019 | 960–1079 | `2` brake (120) | braking |
| 1020–1079 | 1080–1139 | `513` throttle+right (60) | hard right |
| 1080–1249 | 1140–1309 | `1` full throttle (170) | straight |
| — | 1310–1431 | `0` coast (181 ticks) | input-exhaustion coast per the 0xb4 rule |

Capture (seed the control file on the Wine C: drive first; `RR_ORACLE_INPUT`
opens it inside the emulator):

```sh
cp analysis/oracle/sim-napa-solo-human.controls "$HOME/prefix/drive_c/napa-human.controls"  # path varies; run.sh maps C:\X to drive_c/X
RR_ORACLE_COURSE=4 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1 RR_ORACLE_SOLO=1 RR_ORACLE_INPUT='C:\napa-human.controls' RR_ORACLE_FRAMES=1800 oracle/run.sh sim-napa-solo-human
```

Result: `analysis/oracle/sim-napa-solo-human.tsv` (1,431 frames, ticks
1–1431, green at tick 60 after a 59-frame start-hold, `end` marker present)
plus `analysis/oracle/sim-napa-solo-human.controls` (a copy of the input
script). Self-check and determinism replay (Task 3 Step 4 procedure):

```sh
python3 tools/golden_diff.py analysis/oracle/sim-napa-solo-human.tsv analysis/oracle/sim-napa-solo-human.tsv
RR_ORACLE_COURSE=4 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1 RR_ORACLE_SOLO=1 RR_ORACLE_INPUT='C:\napa-human.controls' RR_ORACLE_FRAMES=1800 oracle/run.sh sim-napa-solo-human-replay
python3 tools/golden_diff.py analysis/oracle/sim-napa-solo-human.tsv analysis/oracle/sim-napa-solo-human-replay.tsv
```

Both report zero differences (replay verified 1,431 frames identical;
`sim-napa-solo-human-replay.tsv` is git-ignored scratch, not committed).

## napa-solo-script (braking/steering, player-control; green, no CTest)

Level 1 Napa, default bike and character, seed 1, solo (`RR_ORACLE_SOLO=1`).
Plan 10 Task 5 braking/steering scenario: full throttle 300, hard left 60,
brake 120, right 60, repeated to the first split (sample 700). Control bits
are hex words (`1` throttle, `5` throttle+left, `2` brake, `201`
throttle+right, `0` coast; `fscanf %x`). Gears auto-shift; no manual gear bits.

`RR_ORACLE_HUMAN=1` is required for the player-control branch
(`DAT_004753c8` cleared post-setup; `player.demo_hold == 0` every tick).
`RR_ORACLE_INPUT` alone leaves `demo_hold == 1` (attract-demo AI branch,
INPUT dumped but ignored -- first attempt in this slice captured 1801
demo-branch ticks, correctly rejected for player physics; re-ran with
`HUMAN=1`). Script: 1,620 words, 3 cycles of
`[1*300, 5*60, 2*120, 201*60]` (`analysis/oracle/sim-napa-solo-script.controls`,
git-ignored; seeded as `prefix/drive_c/napa-script-20261026.controls`).

```sh
RR_ORACLE_COURSE=4 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1 RR_ORACLE_SOLO=1 RR_ORACLE_HUMAN=1 RR_ORACLE_INPUT='C:\napa-script-20261026.controls' RR_ORACLE_FRAMES=1801 oracle/run.sh sim-napa-solo-script
```

Result: `analysis/oracle/sim-napa-solo-script.tsv` (1,801 frames, ticks
1–1801, 1 frozen frame, green at tick 2, `end` marker, `demo_hold == 0`,
trace `97ec1aac…`, receipt git-ignored). Native replay
(`oracle-sim-test`, auto human driver from measured `demo_hold == 0`):
`1801 ticks identical` (+5 golden-only, 0 uncovered). No divergence, no
porting needed -- player throttle/brake/steer/gears hold through braking,
hard turns and the split approach. Like L5d/L5e, the golden-only suffix can
neither pass nor pin as a CTest, so verified manually, not registered.

## Replay driver axis (replay prerequisites, Oct 12)

`solo` (field isolation: scheduler freeze, traffic nulled) and
`driver=human|demo` (the `0x00434660` branch) are independent axes.
`oracle-sim-test` selects the branch from the golden's measured
`player.demo_hold` (the oracle's `DAT_004753c8` sample), overridable with
`--driver=human|demo`. It never infers the branch from `solo`, the
`scenario` name, or nonzero recorded input. All captures to date predate
the probe and require `--driver`.

Player-think entry/counter (`0x00434660`): the harness reports
`player.think`/`player.think_count` when the probe records them. Measured
in current captures: `rider.0.think == 4225680` (`0x00407a90`, the AI-think
callee) on the demo/AI side, while the player-think entry `0x00434660`
(= 4408928) shows as `rider.1.think` (the frozen opponent keeps the
player-think dispatch word). New captures MUST record
`player.think`/`player.think_count` directly.

One stable race-setup factory (game + replay): player bike/rider resolve
through the career tables (character 1 → rider 8 via `0x00415fc0`,
`level_bike` at `0x004641c8`, rider row at `0x00463c78`), opponents via
`original_opponents` (offline original: 14, slots 1..14, `0x00405600`;
`DAT_004642d4 = 0x10`), personalities via `read_personalities`, the
vehicle library WITH EXE descriptors, traffic + crash config attached to
the Race, and the Napa road-base anchor through the shared
`rr::race_road_base_q8` helper into the same `Race` ctor the game uses
(the game passes 0 = relative route frame; replay passes the measured
anchor: `road_base = golden_frame0_lateral - route[grid].offset*256 -
grid_word`, 1792 on the solo golden). No hidden replay-only setup.

New captures MUST record, every frame: `player.demo_hold`
(`DAT_004753c8`), `player.think` (entry) + `player.think_count`
(`0x00434660` invocations), `demo.base_speed`/`demo.lane`/`demo.weapon`/
`demo.stats_*`, `grid.bias`, and `DAT_00467a6c` (the brain personality
row — the course-entry bootstrap leaves it at startup 0, not the career
rider). Pre-step RNG capture (or proven post-step equivalence: every
hold frame constant rng with `rng_calls==0`, else exit 1) is required at
the green boundary; ticks stay absolute (animation deadlines accumulate
from absolute ticks). See `analysis/replay-prereq-notes.md`.

Comparator contract: the intended field set (every native snapshot field
per live rider + every traffic pool slot + rng) is REQUIRED — a missing
golden field is a FAIL, never skip-green. Era carve-outs (car.* rows
absent in solo captures; steer/ground/stamina/weapon/slice/ai/anim/nitro
offsets no dumped era covers) are counted as uncovered and reported, not
silently skipped. Car/police/animation fields are in the set: the proxy
must dump them before acceptance.

Single-anchor replay status (`oracle-sim-test --driver=demo
analysis/oracle/sim-napa-solo.tsv`): road_base 1792 (measured per
golden, not a course constant), 59 frozen hold frames, green at tick 60,
green through tick 74 with rng equal. First divergence frame 74 (tick
75): `rider.0.lateral` original 18173 vs native 18053. Mechanism
(measured, not a physics bug): the golden carries a frozen rider.1 that
the harness cannot yet build (opponent slot mapping is task-6 work; the
port's `frozen` flag never gates `step_rider`), so the original's
pre-green replan scaled current 87→81 with rider.1 ahead and its green
tick ran state 2 with the replan target (tx snapped to 18176), while
native runs state 1 straight target (tx=1799) with no target. The
earlier tick-120 speed gap was the same class of harness artifact (no
personalities wired in); the tick-139 traffic-timer RNG note below is
superseded — the run now reaches the tick-75 field divergence first.

Known limitation: menu course-entry captures (`RR_ORACLE_COURSE`) run the
player on the demo AI branch, so `DAT_004b7ed0` words are written to the
dump but never read by the think function. The native replay mirrors this
with `--driver=demo` for `napa-solo`; a TRUE human golden needs the
`RR_ORACLE_HUMAN` hook (clear `DAT_004753c8` post-setup, open). The
current `napa-solo-human` file documents the control stream + determinism
proof, not the control-driven branch.
## napa-field-l1 (live field, task 6 diagnostic)

Level 1 Napa, seed 1, drive=2 autopilot, no `RR_ORACLE_SOLO`: live field
with 14 opponents (the offline `0x00405600` build, `DAT_004642d4 = 0x10`),
traffic and pedestrians, 2400 ticks.

```sh
RR_ORACLE_COURSE=4 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1 RR_ORACLE_DRIVE=2 RR_ORACLE_FRAMES=2400 tools/oracle_capture.py capture napa-field-l1-20261017
```

`analysis/oracle/napa-field-l1-20261017.tsv` is git-ignored (`99e20344…`,
receipt in its `.captures/` dir). It is not an acceptance golden: it is the
`oracle-sim-field` ctest known-divergence diagnostic, skipped when the
capture is absent. It pins the live rider scheduler replay (`0x00438170`
cadences, parking, virtual advance), extended by live car/pedestrian
scheduler and contact fixes: countdown and ticks 6..787 compare identical
on rider, brain, half, sched, node-list and car motion triples with RNG in
sync. It now replays the tick-765 wake. First divergence is tick 788:
original RNG takes three draws, native one, with a simultaneous state-3
steering mismatch on native rider 12. The `0x00409460` combat arm remains
unported. No car hydration, reanchoring or comparison exclusions; this is
diagnostic progress, not live-field acceptance. See
[continuation](agent-handoffs/ped308-20261017.md).
`RR_FIELD_IGNORE_CARS=1 RR_FIELD_IGNORE_PEDS=1 RR_FIELD_CARS_FROM_GOLDEN=1`
historically compared to frame 623 (tick 624) and first diverged at tick 625
(`rider.1.lateral`) for scheduler debugging; that hydrated run does not
establish native traffic parity.

## Planned (tasks 5–7)

- `napa-solo-split-left`, `napa-solo-split-right`: branch commits at the first split (solo).
  Captured October 26 (6,681 ticks each, HUMAN=1, git-ignored; scripts identical
  through tick 4,500). Replay identical through the full 6,681 ticks with 0
  sim-field divergences (19 golden-only; 8/94 rpm-timing notes on the proven
  nondeterministic sound-thread channel). Covers the first crash (static
  class-2 gap at tick 2055), the solo freeze-hack half nulling at 2102, and
  the full slide-out; the split branch commit itself was never reached (the
  crash ends forward progress). Remaining: jump, fight/contact,
  traffic-solo/field (separate captures).
- `napa-solo-jump`: airborne ticks on crest sections (solo).
- `napa-field-l5`: 14 opponents at level 5.
- `napa-fight`, `napa-contact`: combat and contact scenarios.
  `napa-fight` captured October 26: course 4/seed 1/field/HUMAN=1,
  2,400-word throttle script with punch (`0x20`) every 30th word,
  FRAMES=2700, ended 2,581 via the quit rule (validated partial).
  Replay `2581 ticks identical`, 0 uncovered, 57 golden-only
  (ring/car diagnostics), 0 rpm notes. Covers punch acquire
  (`0x00409400` target record + `0x004095d0` side incl. the ped
  fallback), crash stamina (+3/tick at crash 2 via the wrapper regen),
  and walk-back auto-heading (raw `0x7800` gate). No CTest registration
  (golden-only suffix).
- `napa-contact` captured October 26: course 4/seed 1/field/HUMAN=1,
  4,200-word throttle-only script (isolate contact, no combat),
  FRAMES=4500, ended 4,381 via the quit rule (validated partial).
  Replay identical through tick 3840 with 0 sim-field divergences
  (1469 ring-front via half-image camera, 2700 half `+0x1a0` link
  excluded as a host address, 2724 remount clears the half crash bits).
  First divergence tick 3841: single static glance the golden skips --
  the 3x BORR rule (`0x004373f0:47`) needs sub-slice `+0xac` bit
  `0x4000`, whose writer is not in the export set (triple required at
  the 2055 crash, spurious here; pinned, no golden touched).
- `napa-traffic-solo`, `napa-traffic-field`: traffic scenarios.

## October14 forensic SIM recovery (not acceptance)

New isolated `codex-sim-frozen900` (course4, seed1, drive2, solo, SIM,
RR_ORACLE_FRAMES=900) has900 complete contiguous ticks. The repaired proxy
counts SIM dumps for the limit. Header probe.frozen_words=1 declares the
previous-copy/velocity/flag/crash field set, matched through type4 bike and
its type8 half pointer. Old v1 goldens continue to load. This trace lacks
full required player-think/count evidence; explicit --driver=demo is a
diagnostic override only. First mismatch remains tick210.

`codex-sim-mask300` adds temporary per-tick frozen bike metadata/road-mask
fields and measured player.demo_hold=1; it is likewise forensic-only. It
shows the candidate takes3840-unit nudges at208/209 with dt4, while native
dt1 leaves an extra contact at210. Capture and translate scheduler metadata
next. Neither file replaces the old accepted fixtures or relaxes parity.

## Scheduler-metadata capture (plan 10 Task 6 fix order #1c, not acceptance)

`sched900` recipe (same as frozen900 plus scheduler words): course 4,
seed 1, drive 2, solo, SIM, `RR_ORACLE_FRAMES=900`. The proxy dumps
per-tick `sched.<addr>.next/last/interval/dt` for type-4 nodes and
`rider.<slot>.bike_c/bike_10/bike_14` for slotted riders. Purpose: prove
the frozen interval and last-tick cadence (expected interval 4, lasts
1,5,...,209) before translating the `0x00437e60` cadence natively.

## Genuine human-branch capture (plan 10 Task 5 Step 2a, October 16)

`sched-human-20261016` (course 4, seed 1, solo, SIM, human): 1431 complete
contiguous ticks, `player.demo_hold == 0` on every tick
(`control_branch: human` by instrumented evidence, never by name). Input is
the recorded 1250-word human stream `C:\napa-human.controls` (varied
accel/brake/steer/attack bits, also saved as
`analysis/oracle/sim-napa-solo-human.controls`); the tail coasts after
exhaustion plus the `0xb4` quit window. The course-menu bootstrap leaves the
attract hold set, so the proxy selects the branch explicitly
(`RR_ORACLE_HUMAN=1` clears `DAT_004753c8` at race start, seed-hook pattern;
nothing re-arms it mid-race). Previous "human" goldens (`sim-napa-solo-human*`,
no demo_hold probe) are demo-branch replays and prove no player control.
Single-anchor replay with harness-inferred `driver=human` (no CLI override):
green at tick 2 (1 hold frame), first divergence frame 1 (tick 2)
`rider.0.ai_target_x` original 18176 vs native 0 -- the native human path
skips AI target selection. RNG in sync. Next: maintain target state on the
human path (0x00434660 trace), then the five human physics gates (steer
authority 0x0040ad30, skid 0x00438c70, move-push 0x004395e0, scheduler +0xc
copy 0x00438170, wreck attack rule 0x0040a600), all demo-tuned natively.

Ring setup calibration (October 16, ring-terrain-20261016): with the
`0x00453b60` window walks ported, the ring setup starts directly at
srand(seed) (`ring.pre_draws` default **0**, was 9,098) and ends
`ring.post_draws` **16** draws before the green anchor (was 11). Since spawn-20261017 those 16 draws are ported (0x00405600: one
lane `rand()%3` per rider, player first, then `rand()%100` for the name
set), so `ring.post_draws` defaults to **0**. Measured
with `build/ring-probe analysis/oracle/terrain-human200-20261015.tsv
../cdrom/ROADRASH 20000`: K = 11,461 back from the anchor is state 1 and
leaves both walkers' frame-0 terrain exact. Headers may still override
both; `oracle-sim-test --ring-pre-draws=N` is a diagnostic override only.
Results: `terrain-human200-20261015` 200 ticks identical (walker trace
every frame); `sched-human-20261016` first divergence tick 371
(`rider.0.velocity` 40 vs 296, RNG in sync), tick 702 (RNG) once the
race reads ring-built terrain slices, and **all 1431 ticks identical** once
roadside objects are solid (same day).

## Extended human capture and strict acceptance (plan 10 Task 5/6 exit, October 17)

`sched-human-cov2-20261017` (same recipe as `sched-human-20261016`: course 4,
seed 1, solo, SIM, `RR_ORACLE_HUMAN=1`, 1431 contiguous ticks, branch human)
adds the proxy's riding-animation block, stamina (`*(half+0x1e0)+0x38/0x3c`)
and nitro globals (`DAT_004c5f10`, `DAT_00465a34`, `DAT_00465a28..3c`). Every
word shared with `sched-human-20261016` is equal on every tick; only the
wall-clock frame schedule (`+0x44`, and the rpm it gates) differs at ticks
1197 and 1220.

Replay with `driver=human` is `1431 ticks identical` with **0 uncovered
native comparisons and 0 golden-only keys**: `node.N.*` now compares as a
per-frame multiset of (draw callback `+0xdc`, position) over the 0x00437e60
list minus the player's two slots. The 70 words first accepted as verified
invariants now compare against native state (rider build port,
spawn-20261017: player brain window, rider-half object image, half raw
window); the invariant category is gone.

`tools/verify_sim.py --mode acceptance` now defaults to this fixture with
`driver=human` (diagnostic keeps `sim-napa-solo.tsv`/`driver=demo`) and
reports `acceptance_met: true`. The node-list comparison is gated on a
declared `probe.frozen_words` capture: parking is rebuilt from the frozen
slice flags, which the legacy demo captures do not carry, so their pinned
diagnostics are unchanged.


## Complete Napa live field, October 18, 2026

- `napa-field-l1-20261017.tsv`: 2400 contiguous ticks, course 4, level 0
  (display level 1), seed 1, demo driver, solo 0, 14 opponents, traffic and
  pedestrians. SHA-256 `99e2034496086990d67d130defd3fbea5af6708498d791feb524f5cafab68fe5` unchanged.
  All reliable captured rider/brain/half/scheduler/node/car-motion fields and
  RNG match. Its four legacy car metadata aliases have invalid byte offsets;
  the replay reports that capture defect separately.
- `napa-field-l1-schema2-20261018.tsv`: same full 2400-tick scenario,
  `ring.car_schema=2`, valid car kind/vehicle/lane/phase offsets and optional
  complete AI animation/relationship probes. SHA-256
  `294343d533ae73fa1fea78678bb57fd0ea3901e23f2a09f248ff9bec66d275de`.
  Every compared field and RNG match, with zero golden-only state keys.
  Original EXE SHA-256 remains `73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`.

Capture recipe for schema2 (the extra values select recorded pre-race speed
configuration only; no live step is modified):

```sh
RR_ORACLE_COURSE=4 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1 RR_ORACLE_DRIVE=2 \
RR_ORACLE_FRAMES=2400 RR_ORACLE_RATINGS=1 \
RR_ORACLE_EXTRA_SPEEDS=89,101,120,81,105,117,93,112,117,82,109,125,90,107,116,90,112,122 \
python3 tools/oracle_capture.py capture napa-field-l1-schema2-20261018 --workspace ..
```

Replay either with `oracle-sim-test --driver=demo <trace>`. Both run the
countdown natively, prove ring/build closure and anchor RNG once at green.
`oracle-sim-field` and `oracle-sim-field-schema2` register these regressions;
the disc-derived traces and receipts stay local. The schema2 capture also
supersedes the legacy metadata probe, not the original golden file.

## Police pursuit captures, October 26, 2026

Napa level 3 (UI; `RR_ORACLE_LEVEL=3`), seed 1, human player parked on the
grid (zero controls, `RR_ORACLE_HUMAN=1` clears demo hold, no `DRIVE` or
`INPUT`), full field, one RNG anchor at tick 1. The periodic cop
(`0x00407f30`, every 4096 ticks) spawns behind the player, chases, and
arrests into the `0xb4` race-end timer. Two capture rules learned the hard
way: set `RR_ORACLE_ORDER=1` (otherwise `race.end_timer`/`race.outcome` are
never dumped) and quit with `RR_ORACLE_FRAMES` while the race is still
running (past the arrest the tick counter resets for post-race and breaks
contiguity). `police_test` replays harnesses: cop position/speed every tick
from the 4096 spawn, player crash state, RNG and per-row field to the
asserted end, native arrest tick vs the measured `end_timer` tick.

- `napa-cops-parked-20261026`: FRAMES=4800 run ended at the race-end tick
  reset (4731 contiguous ticks); truncated past the reset for the harness.
  Cop matches on all 635 pursuit ticks, RNG and field identical to tick
  4730, native arrest on tick 4552 as captured. ORDER=1 mid-race re-run
  pending for direct `end_timer` evidence.
- `napa-cops-crashed-20261026`: same recipe plus `RR_ORACLE_KNOCK=4400`
  (0x00403d60 reason 1 on the player), FRAMES=5031.
- `napa-cops-order2-20261026`: second KNOCK=4400 run, FRAMES=5031.
- `napa-cops-foot-20261026`, `napa-cops-foot2-20261026`: KNOCK=4460,
  FRAMES=4701 (on-foot arrest window).
- `city-cops-parked-20261026`: `RR_ORACLE_COURSE=1`, FRAMES=4552. Since
  November 3 the RNG, ring-window riders and cop match to 4552, and the
  arrest on 4552 is asserted.
- `city-parked-randlog-20261102` (trace `a0dae00d7e0b`) and
  `city-parked-randlog2-20261102` (trace `0fef564dd1bf`): same City recipe
  with FRAMES=2400, `RR_ORACLE_ORDER_RANGE=0x3000` (pedestrian laterals ~10000
  Q8 ahead of the parked player), the second also with
  `RR_ORACLE_RANDCOUNT=1 RR_ORACLE_RANDLOG=1` (per-draw callers). Proxy
  `b02fffda7725`, private install `RoadRashOracle-cityrng20261102`. Both
  replay 2400/2400 in `police_test`.

Example recipe:

```sh
RR_ORACLE_COURSE=4 RR_ORACLE_LEVEL=3 RR_ORACLE_SEED=1 RR_ORACLE_SIM=1 \
RR_ORACLE_HUMAN=1 RR_ORACLE_ORDER=1 RR_ORACLE_FRAMES=4731 \
oracle/run.sh napa-cops-parked-20261026
```
