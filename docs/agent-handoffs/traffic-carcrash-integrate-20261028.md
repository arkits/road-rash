# traffic-carcrash-integrate-20261028

Task: Integrate measured traffic fixes with remote main

Baseline: 13fb314c9a7598c099d30fae222878b234459d62

Worktree: `/Users/archit/Dev/rr/native-port`
Build directory: `/Users/archit/Dev/rr/native-port/build-agent-traffic-carcrash-integrate-20261028`
Owner PID: 95735
Resource token: `4e174914-4db2-4be1-8710-37db1ef7db99`

Owned files:

- `/Users/archit/Dev/rr/native-port/src/race.cpp`
- `/Users/archit/Dev/rr/native-port/src/course_scene.cpp`
- `/Users/archit/Dev/rr/native-port/tests/course_scene_test.cpp`
- `/Users/archit/Dev/rr/native-port/src/race.hpp`
- `/Users/archit/Dev/rr/native-port/src/traffic.cpp`
- `/Users/archit/Dev/rr/native-port/src/traffic.hpp`
- `/Users/archit/Dev/rr/native-port/src/race_schedule.cpp`
- `/Users/archit/Dev/rr/native-port/src/race_setup.hpp`
- `/Users/archit/Dev/rr/native-port/tests/field_replay.hpp`
- `/Users/archit/Dev/rr/native-port/tests/police_test.cpp`
- `/Users/archit/Dev/rr/native-port/tests/race_screen_test.cpp`
- `/Users/archit/Dev/rr/native-port/docs/agent-handoffs/INDEX.md`

## Progress / evidence

Pending.

## Checks / remaining work

Pending.

## Live processes / resources

File claims above. Record capture/Ghidra processes here.

## Integration

Remote main already contained the three traffic-field commits through 66341cd.
Merged them with local 13fb314 without rewriting history, then cherry-picked
worker af60a09 as **9b40606**, pushed to main over authenticated HTTPS.
The earlier SSH push stall was bypassed with the existing GitHub credential
helper; no credential values were emitted by Git commands.

Changed implementation: src/race.cpp, src/course_scene.cpp,
tests/course_scene_test.cpp. Evidence and recovered addresses are in
traffic-carcrash-fix-20261028.md. Shared CMakeLists.txt, src/rri.cpp and
oracle/aweman_proxy.c edits remain untouched and uncommitted by this session.
The proxy patch remains pending its existing owner's integration.

## Verification

- Worker full Debug build and CTest **118/118**, zero skips (475.48 s).
- New receipted car-crash capture and existing body / box / slice captures:
  **4381 identical ticks each**, single RNG anchor. Visibility notes and sound
  channel notes remain counted, as described in the fix handoff.
- Final worker strict human oracle gate passed:
  `../wt-traffic-field/analysis/agent-gates/oracle-e42fa17aed134268ae46aab41afecb39/receipt.json`.
- Main full native integration gate: **120/121 passed**, zero skips,
  486.40 seconds. The existing render-threads-alloc test reported four
  steady-state allocations under parallel load. Immediate isolated
  `ctest --rerun-failed --output-on-failure` passed **1/1** in 0.10 s.
  Includes the pre-existing frontend edits and 121 tests. The full receipt
  remains **failed**, not promoted by the isolated rerun:
  `analysis/agent-gates/native-9186e06048344bad9cae0fc89633305f/receipt.json`.
  Rerun log: `analysis/traffic-carcrash-integration-rerun.log`.
  No renderer/thread-pool implementation or allocation-test code was changed.
- The first main gate was interrupted during serial CTest and restarted with
  CTEST_PARALLEL_LEVEL=8. The interrupted receipt is not a pass:
  `analysis/agent-gates/native-ce6d09dfebd444498fab1157637f21bf/receipt.json`.
- No Android, sanitizer, package or hardware gates were run in this slice.

## Remaining work

The October 26 first mismatch advances from 1303 to **2279**, where a
pedestrian draw node (0x4463e0) differs in position: original 75067, native
75033. A native pedestrian/car contact breakpoint did not fire at that tick;
do not assume a car callback is responsible without tracing the original.

Tick 1303 is now measured: car +0x20 = 6147, body +0x20 = 22060,
dz = 14633, within summed extents 16640; the native crashed-half/car contact
fires. This supersedes the old inference that the body itself must be lowered.

A car/static breakpoint did not fire in the box capture. Dedicated car/static
coverage, the 23 visibility-load notes and the unported split-road crossing-car
removal branch remain open. Private RoadRashOracle-tf20261028 install retained
until proxy patch integration. No Wine or debugger processes left running.

## Completion and resources

Fixes and capture results are pushed on main at 9b40606. Documentation-only
completion commit follows that source revision. Worker claims are released.
The integration claim is released after the completion docs commit/push.
No captures, compilers, CTests, debuggers or Wine processes owned by this slice
remain running. The traffic worktree and private install are retained.
