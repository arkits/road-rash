# split-ring-resume-20261105

Task: Integrate validated split-ring checkpoint and investigate vertical replay gap

Baseline: 7e37568f547bc78b7b2e4e66bb56938e32b25dbd

Worktree: `/Users/archit/Dev/rr/native-port`
Build directory: `/Users/archit/Dev/rr/native-port/build-agent-split-ring-resume-20261105`
Owner PID: 40378
Resource token: `cc7c5dce-45b7-4f91-ba9d-7705d1e8b6cf`

Owned files:

- `/Users/archit/Dev/rr/native-port/src/course.cpp`
- `/Users/archit/Dev/rr/native-port/src/course.hpp`
- `/Users/archit/Dev/rr/native-port/src/race.cpp`
- `/Users/archit/Dev/rr/native-port/src/track_ring.cpp`
- `/Users/archit/Dev/rr/native-port/src/track_ring.hpp`
- `/Users/archit/Dev/rr/native-port/src/traffic.cpp`
- `/Users/archit/Dev/rr/native-port/src/traffic.hpp`
- `/Users/archit/Dev/rr/native-port/tests/course_test.cpp`
- `/Users/archit/Dev/rr/native-port/tests/race_test.cpp`
- `/Users/archit/Dev/rr/native-port/tests/track_ring_test.cpp`
- `/Users/archit/Dev/rr/native-port/tests/traffic_test.cpp`
- `/Users/archit/Dev/rr/native-port/docs/PROGRESS.md`
- `/Users/archit/Dev/rr/native-port/docs/VERIFICATION.md`
- `/Users/archit/Dev/rr/native-port/docs/REVERSE_ENGINEERING.md`
- `/Users/archit/Dev/rr/native-port/docs/SIM_SCENARIOS.md`
- `/Users/archit/Dev/rr/native-port/docs/plans/10-simulation-oracle.md`
- `/Users/archit/Dev/rr/native-port/docs/agent-status.json`
- `/Users/archit/Dev/rr/native-port/docs/CURRENT_STATUS.md`

## Progress / evidence

Resumed from `split-ring-latest-20261104.md`, baseline 7e37568. Verified prior
owner PID 86170 absent; reaped its five source claims and claimed this slice.
Prior full gate finished 135/135 (428.01s), `/tmp/rr-split-current-full-ctest.log`.
Resume full build and formatting passed. Fresh parallel full gate passed135/135, no skips/failures (434.36s):
`/tmp/rr-resume-20261105-ctest-parallel.log`.
Original EXE SHA256 matches inventory: 73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2.
REA6.4.0 0x0043a190 completed exit0, `/tmp/rr-vertical-rea-20261105.log`;
corroborates landing formula, not runtime gravity values.
CDB preflight healthy (426 entries), clangd check timed out after60s;
`/tmp/rr-resume-lsp.log`. No client LSP tool exposed.

## Checks / remaining work

Full gate completed successfully before commit. Body vertical mismatch7873 remains open.
LLDB `/tmp/rr-vertical-all-lldb-20261105.log` measures detached body before
vertical7873: dt1, sample700, previous699, height517, ground64, previous-ground64,
speed7498, f4=0, acceleration11c=-74, rebound170=24, gravity1b4=74.
These are NATIVE measurements only. Initial human==0 probe inspected bike,
not body; superseded by this all-actor probe. Next inspect post-landing values
and subsequent contacts before inferring missing original hidden fields.

## Live processes / resources

File claim token cc7c5dce-45b7-4f91-ba9d-7705d1e8b6cf (owner40378).
Inherited build reused only after verifying old owner absent.
No Wine capture or persistent Ghidra database mutation.
All build, CTest, REA and debugger processes completed. No live captures.

## Investigation correction

The landing-line probe `/tmp/rr-landing-lldb-20261105.log` never hits at7873;
the body takes the grounded branch, not landing. With matched ground67 and
previous ground64, `f4 = 3*256 + body12c`; native1032 implies body12c264,
original1302 implies534. This is a derived constraint, not a measurement of
original hidden fields. Next probe `Race::accumulate` (0x004394c0) at split700
and inspect route/physical half and rise weighting. Do not change gravity.

Body native movement7873: from179189 (fraction245) to179218 (fraction18),
11 units in old sample699 and18 in new sample700; native rise term264.
A rise difference15 on the new18 units would account for270, but that is
a hypothesis pending physical-half evidence. Last landing-line probe finished
exit0 (LLDB itself; inferior diagnostic exit1). No source change made.

`/tmp/rr-rise-lldb-20261105.log` now directly measures `Race::accumulate`
for body7873: route1, from179189 to179218, samples699->700, rise_old24,
rise_now0, in_old11, in_new18. Thus264 =24*11+0*18. Original inferred534
would be24*11+15*18. Next compare constructed split sub-slice rise+0xbc
(0x00454240/0x00454c80) against static route1 sample700, including physical
branch identity and first-sample interpolation. No native semantics changed.
