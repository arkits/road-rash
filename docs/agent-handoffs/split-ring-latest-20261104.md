# Split-ring current handoff — November 4, 2026

This is the concise current handoff. Historical recovery/stages are in
[split-ring-20261103.md](split-ring-20261103.md). Goal remains active.
Previous turn made source, regression, replay and build progress.

## Baseline and ownership

Repository /Users/archit/Dev/rr/native-port; HEAD40dc4f9 (another session
committed its projected-x docs; preserved). Our last pushed code dc96eab.
All current source edits are ours, uncommitted. Explicit files:
src/{course.cpp,course.hpp,race.cpp,track_ring.cpp,track_ring.hpp,traffic.cpp,traffic.hpp}
tests/{course_test.cpp,race_test.cpp,track_ring_test.cpp,traffic_test.cpp}.
Claims held: split-ring-20261103 (ring files), split-ring-race-20261103
(race.cpp), split-ring-race-test-20261103 (race_test),
split-ring-course-20261103 (course files/test), split-ring-traffic-20261103
(traffic files/test). Run agent_workspace.py list before edits.
Owned build: build-agent-traffic-ring-20261103 (export compile commands).

## Implemented and tested

- Dual section walkers, exact section step return0/1/2/3; terminal reverse.
- Forward/back split/merge boundary entry, selected half-first stepping/build.
- Cached81-sample windows, separation threshold/counter, cached reentry,
  global branch commitment/collapse/reset events, NALR lane-edge offsets.
- Human Race::cross_sample commits both walkers even for chosen half0.
- Synthetic width, reverse, cached-window, commitment and snapshot tests.
- Fixed course.cpp opposite-half annotation: it had selected its OWN branch
  as other. Regression failed both branch directions; fix swaps ternary.
- New route_half helper selects physical section/path/offset plus opposite
  metadata; intersection constructor/signal geometry now uses event half,
  not the route's selected road. Controller retains half for crossing spawn.
- Synthetic opposite intersection (quiet hill type3, left0/right1 lanes,
  centre1000) creates ONLY right signal, at1300*256. Previously extra left
  signal. Original NAPA sample729 section3 intersection was hidden by bad
  opposite mapping. Fix progresses replay past7435 node gap.
- Empty restored DZHR stream guard: backward cursor idx-1 must not index
  empty records on forward step. Debugger stack confirms original crash.
- Ground height0x00439b10 follows current sub-slice+0xc, right-forward link.
  At split this points half1; Race::vertical now selects that next half.
  This fixes body ground11/native7 at tick7868 AND the earlier finish failure.

## Validation and live handles (do not restart a live gate)

Full current Debug build PASSED /tmp/rr-split-current-full-build.log,
handle38905 exit0. Current full CTest RUNNING handle13156, log
/tmp/rr-split-current-full-ctest.log. This is the authoritative NEW gate.
Previous integration16957 finished134/135, finish run failed because player
was busted, bike condition0, crash2 at sample1451 (finish1715). LLDB logs
/tmp/rr-ring-finish-lldb-fields.log. NO test assertion/fixture was weakened:
the ground-link fix makes the UNCHANGED finish test pass now.
Latest focused course/traffic/race_state3/3 pass (12.78s),
/tmp/rr-split-ground-tests.log. Strict human1431/1431 PASSED,
/tmp/rr-split-ground-acceptance.json (handle90803 exit0).
Original steal-farm matches through7872; next mismatch tick7873 is
rider.0.body_f4 original1302/native1032 (vertical velocity),
/tmp/rr-split-ground-steal.log (handle58890 diagnostic exit1).
No golden changes; diagnostic replay improvement is not full human parity.

REA6.4.0 independent0x00439b10 succeeded handle63164 exit0,
/tmp/rr-split-ground-rea.log. Confirms +0xc next link in assembly/pseudocode.
Other REA ground-window and traversal logs are in historical handoff.
Client LSP unavailable; use agent_lsp.py with owned CDB. Latest traffic
check /tmp/rr-dual-road-clangd.log handle61704 completed exit3 (Apple tweak
checks). Full compiler passed; don't report a green client LSP session.
Format-check and git diff --check passed before latest full build/gate.
No live builds/replays/REA/debuggers remain; only full CTest13156.

## Next actions / open scope

1. Poll SAME full CTest13156; inspect complete result. Preserve current source
   until gate ends so its evidence remains valid. Do not repeatedly run tests
   once green without another change/failure.
2. Inspect staged diff and record exact coverage/docs, then commit explicit
   owned paths and handoffs and push main when this phase is validated. Keep
   plan10/17 broader acceptance open: no claim all splits/human paths exact.
3. Continue original replay at body vertical velocity7873. Ground current
   and next at split now match through7868; next gap5ticks later may be
   boundary wall/vertical/contact logic. Trace before changing semantics.
4. Remaining architectural limits: arbitrary per-split route choices (Race
   stores all-false/all-true routes), full dynamic sub-slice neighbor topology,
   car road-half sampling after crossing spawn, complete other-course/human
   acceptance. Do not disguise these as completed by the current component
   and replay spans. Continue plan work after the next stable checkpoint.

All commercial evidence is local/ignored. Never stage raw captures or images.

## Read-only note for next vertical-velocity investigation

Golden frame7871(tick7872): body_20=517, body_24=64, body_f4=0;
frame7872(tick7873): body_20=0,body_24=67,body_ec=7498,body_f4=1302.
Native first gap is body_f4=1032 at7873; delta270 is not divisible by
speed>>7 (58), so don't guess a path-rise difference. Landing law uses
rebound from vy=body_11c*dt+body_f4 and body_170; inspect hidden body_11c,
1b4 gravity and170 rebound/template values BEFORE changing terrain again.
Original0043a190 sets11c=-1b4 on airborne branch and -2*1b4 on landing.
Possible actor-template gravity mismatch becomes observable here, but this
is only a hypothesis, NOT recovered fact. Verify raw imported templates or
an original object dump. Projected state fields not dumped can't be
inferred solely from a green replay prefix.
