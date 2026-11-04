# remount-mirror-20261104

Task: traffic-field tick-2279 shadow divergence (plan 10 Task 6/7)

Baseline: d063704a59f647f0b83dff0dc661aba47c017084

Worktree: `/Users/archit/Dev/rr/native-port`
Build directory: `/Users/archit/Dev/rr/native-port/build-agent-remount-20261104`
Owner PID: (session)
Resource token: none (no oracle capture; disc read-only)

Owned files (claimed): `src/race.cpp` (`crash_recovery` remount branch),
`tests/crash_test.cpp` (live-remount mirror block). No live session claims
these files. Shared docs below are merged by the integrator on conflict.

Changed: `src/race.cpp`, `tests/crash_test.cpp`,
`docs/{PROGRESS,VERIFICATION,REVERSE_ENGINEERING,SIM_SCENARIOS,CURRENT_STATUS,agent-status.json}`,
`docs/plans/10-simulation-oracle.md`, `docs/agent-handoffs/INDEX.md`.

## How the 2279 gap was found

1. `oracle-sim-test analysis/oracle/sim-napa-traffic-field.tsv --driver=human`
   on the baseline binary: first divergence at tick 2279, node list only.
   Original shadow node `(4482016, 75067)`, native `(4482016, 75033)`.
   4482016 = 0x4463e0 (shadow at terrain ground, `src/actor_nodes.cpp:38`).
2. Golden archaeology (read-only parse of the TSV): original node.28 is the
   only moving shadow; it jumps 75033 -> 75067 exactly when rider slot 3's
   crash flag clears 3 -> 0 (frame 2278). Slot 3's dumped position is 75067
   on both sides, so the bike matches and only the half image is stale.
3. Native `field_replay.hpp::nodes` reads the shadow from
   `r.half_mirror.position`. In the live scheduler path nothing re-synced
   the mirror at remount: `Race::crash_recovery` called
   `remount_crash_actor` (bike 0x2f4 = 0, body = bike) but not the
   0x004067d0 copy-back, and the bike node was not due that tick, so no
   `live_standings` ran either. The non-live path was masked by
   `update_halves` at end of tick.
4. Disassembly (project export `analysis/decompiled/00404560.c:37`):
   the 0x00404560 remount tail calls `FUN_004067d0(param_1)` right after
   zeroing 0x2f4, then thunks 0x00407410. `analysis/decompiled/004067d0.c`
   copies the bike motion words to `*(bike+0x2fc)` gated on 0x2f4 == 0.
   REA 6.4.0 was requested for a second opinion but its Ghidra headless
   launch fails persistently in this sandbox (`GhidraLaunchError`, doctor
   shows ghidra/headless/java healthy; retried twice). The export is the
   evidence; no Ghidra writes were made.

## Fix

`src/race.cpp`, `crash_recovery` remount branch: `live_standings(r)` after
`r.crash_active = false`, mirroring the 0x004067d0 call order (copy, then
the 0x00407410 finish recompute). `live_standings` is the native 0x004067d0
image (same words; slice presence follows the +0x2c copy) and is already
what `park()` calls after its inline remount. Non-live behavior is
unchanged: `update_halves` rewrites the same words at end of tick, and the
copy draws no RNG.

## Evidence

- New regression in `tests/crash_test.cpp` (live-schedule Race, forced
  crash state 3 with the pickup animation past 41, bike moved on from the
  stale body, only the half node due): pre-fix it fails with
  `Live remount did not rejoin the half: mirror 17920/32768`; post-fix it
  passes (`Live remount rejoins the half on the remount tick`).
- Scenario replays (`oracle-sim-test --driver=human`, post-fix binary):
  traffic-field 2279 -> **4381/4381 identical** (full pass);
  steal-farm 3627 -> 4059; steal-farm-l1 2837 -> 5697; contact 4096,
  fight-steal 896, fight-steal-l5 816, steal-bunch 1399, steal-vary 941
  unchanged (no regressions).
- No regressions: `napa-field-l1` / schema2 / l5 2400/2400; city
  `cops-parked` 4552 end + arrest, 2x randlog 2400/2400 (`police-test`);
  strict `verify_sim.py --mode acceptance` 1431/1431.
- Full gate: Debug build + `ctest` in `build-agent-remount-20261104`
  (see VERIFICATION entry for counts and receipt).

## Remaining (next actions)

- The projected-x node draw test for pedestrian loads (city-rng-20261102
  handoff item 1) is still open: 0x00438610 gates the +0xdc draw on
  `(node+0x50 rounded)>>8` in `[param_2,param_3)`, the half span from
  +0xe8/+0xec (or +-10000). Needs render-half spans plumbed sim-side.
- City with a moving player (`analysis/oracle/city-moving.tsv` exists but
  is not wired to any test) and other City levels: capture with
  `RR_ORACLE_INPUT` and replay.
- The improved-but-open scenarios above (steal-farm 4059 ring car
  lateral, steal-farm-l1 5697 RNG, contact/fight/steal first divergences).

## Live processes / resources

None running. No oracle capture was taken (disc read-only). Claims on
`src/race.cpp` and `tests/crash_test.cpp` are released after the push.
