# integ-race-state-20261104

Task: integration check of the shared worktree (plan 10; read-only, no source edits)

Baseline (committed HEAD at check time): `40dc4f9` (docs) on `dc96eab`
(player cadence) on `ead0c33` on `38a2c14` (crossing cars) on `0d4e140`
(remount copy-back). Worktree additionally holds the owning agent's
uncommitted split-ring work (`src/race.cpp` cross_sample commit_branch,
`src/track_ring.*`, `tests/race_test.cpp`, `tests/track_ring_test.cpp`).

Worktree: `/Users/archit/Dev/rr/native-port`
Build directory: `/Users/archit/Dev/rr/native-port/build-agent-remount-20261104`
(owner: this session; other agents' build dirs untouched)
Resource token: none (no oracle capture; disc read-only)

Owned files: none (verification only). Changed: this handoff +
`docs/agent-handoffs/INDEX.md` (one row).

## Result

Full Debug build exit 0; `ctest` 126/135 passed
(log `build-agent-remount-20261104.integ-ctest.log`).

- 8 failures are the known sandbox-environmental set, identical on the
  untouched baseline build (`build-agent-city-rng-20261102`, commit
  `d063704`): `first-run-smoke`, `first-run-smoke-unicode`
  (no video displays / no XPC services), `game-race-wiring`,
  `net_ws`, `net_hostile_ws`, `server_health`, `net_presentation`,
  `net_rtc` (TCP bind / ICE, sandbox network restrictions).
- 1 failure is new and real: `race_state` #128, reproducible standalone
  (`./build-agent-remount-20261104/race_state-test`):

```
napa-solo: compared 2640 ticks, crashed 1483, walking 1248, cop 2621, pedestrians 2947, finished 0, branch 0
FAIL the finish run reaches the line
```

## Attribution (not a fix)

- `dc96eab` was reported 135/135 in
  [player-cadence-20261103](player-cadence-20261103.md) (that session's
  result; not re-run here). That commit already contains the remount
  copy-back (`0d4e140`), and the remount regression (`crash-test`),
  traffic-field 4381/4381, field l1/schema2/l5 2400/2400, city captures
  and strict acceptance 1431/1431 all still pass in this worktree build.
- The only delta since that green gate is the uncommitted split-ring
  work. The failing assertion (a finish run that never reaches the line,
  `finished 0`) sits in the blast radius of the uncommitted
  `cross_sample` change, which newly calls `commit_branch` on split/merge
  crossings for human seats: a misrouted finish-run rider would produce
  exactly this symptom on Napa (which has splits).
- No source file was read-modified, stashed, or restored for this check;
  the owning agent's (PID 86170) uncommitted edits and claims
  (`src/race.cpp`, `src/track_ring.*`, `tests/race_test.cpp`,
  `tests/track_ring_test.cpp`, `src/traffic.cpp`,
  `tests/traffic_test.cpp`, `src/race_schedule.cpp`) are untouched.

## Suggested next action (owning agent)

Re-run `race_state-test` with the split-ring work in progress; if the
finish run is misrouted at the first split, check the new
`commit_branch` call in `cross_sample` (half selection and walker commit
when the route stays 0). This handoff makes no claim about the correct
split behavior.

## Live processes / resources

None started. No claims held.
