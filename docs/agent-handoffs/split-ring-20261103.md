# split-ring-20261103

Task: Plan 10/17 complete split and merge dual-half ring traversal and slice construction

Baseline: dc96eab6f9c8173473d4302e18f19938abf3878e

Worktree: `/Users/archit/Dev/rr/native-port`
Build directory: `/Users/archit/Dev/rr/native-port/build-agent-split-ring-20261103`
Owner PID: 86170
Resource token: `01ded84e-b48b-46d2-b1af-55bc7adf5dda`

Owned files:

- `/Users/archit/Dev/rr/native-port/src/track_ring.cpp`
- `/Users/archit/Dev/rr/native-port/src/track_ring.hpp`
- `/Users/archit/Dev/rr/native-port/tests/track_ring_test.cpp`

## Progress / evidence

Previous goal turn completed scheduler override dc96eab: full Debug build,
135/135 CTests (424.06s), strict human1431/1431; regression failed baseline
and passed fix. Push15000 completed successfully; origin/main is dc96eab.
Scheduler source claims released; this session now owns the ring files.

Read docs/agent-handoffs/player-cadence-20261103.md follow-up ring evidence.
New measured first gap: steal-farm tick6719 at first front split sample700.
NAPA graph parsed by /tmp/rr-course-graph.cpp: road20 length700 -> split60,
halves156(section1 length300),92(section3 length325) -> merge124 ->road188.
Current TrackRing::step stops unsupported at kind!=0, so it misses both
halves. Original consumes49 draws vs native17 on that tick (seed1512667433).

Existing project exports 00453030,00452e10,00452fd0,00453020,00453d00,
00454c80 describe dual-half traversal, commit/collapse, slice construction.
REA6.4.0 independent 00453030 export succeeded, /tmp/rr-split-ring-rea.log.
Read entire function and check suspicious offsets/widths against assembly.
Port full forward/back split and merge behavior, source-specific terrain and
actor order, and branch commitment. Do not patch RNG counts alone.

Current ring private Walker holds one node and section iterators. Consider
separating section-half state from traversal state (two halves, primary,
window node, flags, separation). Preserve existing single-road snapshots and
caller API where possible. TrackWalkerState consumers: ring_probe.cpp,
track_ring_test.cpp, oracle_sim_test.cpp initial terrain anchor.
Course.cpp make_route_choices already has split/merge geometric annotations
around lines464-620; inspect before duplicating geometry, but these are static
route approximations rather than runtime shared-RNG walker evidence.
Full slice builder00454c80 also controls half static/actor sink ordering.
Current sinks keyed section/local may support both halves, check Race hooks.

Ring source edits now in progress; see current implementation below. Build reuse: build-agent-traffic-ring-20261103 is
owned by this session and has export compile commands. Client LSP absent,
clangd fallback available (Apple reports refactor-tweak errors, no parser
errors on scheduler check). Use owned CDB. All prior tests/REA completed.
Shared docs/PROGRESS.md and REVERSE_ENGINEERING.md edits are concurrent and
must be preserved; use this unique handoff. Next: inspect claims/state, finish
pending scheduler push, implement measured dual traversal with synthetic
forward/back split/merge and snapshot tests, replay steal-farm, strict human,
full native build+CTest before commit and push main.

## Checks / remaining work

Pending.

## Live processes / resources

File claims above. Record capture/Ghidra processes here.

## Current implementation (unfinished, do not commit as a finished split port)

Authoritative tree edits: src/track_ring.cpp/.hpp and tests/track_ring_test.cpp.
- Replaced separate terrain-only probe_enter/probe_step with the full shared
  enter(SectionWalker)/step_section(SectionWalker) operation (0x00452e10).
  Exact return codes0/1/2/3, terminal/null links leave iterators intact.
- Walker now holds two SectionWalkers, traversal-global position, active half,
  dual flag and stopped flag. Existing runtime still uses half0 and stops on
  split/merge result2: full traversal remains OPEN, do not claim parity.
- Extracted build_half (0x00454240) from build_slice (0x00454c80); wrapper
  selects active half first, opposite second when dual. No dual flag set yet.
  Original 00454c80 tail confirms this order, NOT half0 unconditionally first.
- probe_window retains the full section iterator arrays in windows_[node]
  (Window) after its81 scratch steps. This is needed for cached late window
  entry; original uses window node+0x1c snapshot. Cached geometry/countdown
  metadata still needs recovery and implementation.
- New synthetic terminal test: front reaches the end of a300-sample road,
  retains section sample299 and can reverse to298; original result3 must
  not poison traversal as unsupported. Existing probe RNG tests still pass.

Checks: focused build rr-resource,track_ring-test,oracle-sim-test passed
(/tmp/rr-split-section-check-build.log); after Window cache addition rebuilt
rr-resource+track_ring-test successfully (/tmp/rr-split-cache-build.log).
Focused track_ring CTest passed (0.21s). Format and diff checks passed.
Before cache-only change, strict human1431/1431 passed
/tmp/rr-split-refactor-acceptance.{log,json}; steal-farm remains at identical
RNG gap tick6719 (/tmp/rr-split-refactor-steal.log, expected exit1).
No full integration gate since this unfinished refactor. No commit yet.
Current LSP fallback check session18875 writing /tmp/rr-split-state-clangd.log;
check its result (client LSP absent; initial check used old header, redo done).

Recovery notes / next edits:
Original traversal layout: +0 globalQ8, +4 activehalf, bytes+8 branch-committed,
+9 collapse pending,+10 direction,+11 event; +0xc countdown,+0x10 window node,
+0x14 delta-separation,+0x18 separation,+0x1c kind1/2,+0x20/+0x24 half indices;
SectionWalker0 at+0x28, half1 at+0x21c (500-byte stride).
Original split/merge node+0x18 low byte and+0x19 high byte are separate half
indices. CourseNode currently retains only high byte as primary; likely needs
secondary byte added to course.hpp/cpp (claim before edits), not a guessed XOR.
0x00453b60 cached window snapshot stores countdown limit at+0x410, node+0x14
becomes81*256 after setup. Current Window only retains halves/active: port
separation and collapse threshold semantics too, following full original.
Branch-commit global DAT_00476cd4 and chosen DAT_00476ce4 are shared between
front/back walkers; 00452fd0 collapses only when committed. Need Race branch
selection wiring; inspect existing static course.cpp annotation at464-620,
then claim any required interfaces before mutation.
0x00451a40 reads path per-sample signed-byte deltas (not terrain randomness);
map these to PathSample fields before separation math. Section iterators
stored in cached snapshots include BORR/DZHR/terrain; no draw-count shortcuts.
All original exports plus independent REA00453030 log retained locally.

Next: complete full dual transitions and cached reentry; test forward/back,
primary order, branch commitment, merge unequal lengths, snapshots; replay
steal-farm through split, strict human, full build+CTest before commit/push.
No tests or build processes remain except LSP check above. Claims stay held.

LSP session18875 completed exit3 (Apple clangd check); full JSON log retained.
No live processes remain.

## Boundary implementation progress (latest; supersedes no-dual-entry notes above)

TrackRing::step now handles result2 forward split/backward merge by
entering BOTH SectionWalkers before stepping half0 thenhalf1 with the
original fractional residual; selects window.primary and enables dual.
At opposite window edges, copies selected half to0 and enters common
road, using exact old section position +/- residual from00453030.
For ordinary dual steps, active half first then opposite half.
Boundary-only path does not yet implement cached81-sample window entry,
branch commitment/collapse, separation/offset geometry, events or Race
branch wiring. Do not commit as a complete task or claim all split parity.

Public TrackRingState diagnostics now expose front/back_other,
front/back_dual and front/back_active while preserving prior fields.
Synthetic tests cover primary1 split entry, dual slice order {section2,1},
both section positions advancing, reversing through split, forward merge
into common road120, backward merge entering both ends at sample399.
The first reverse test expected sample300; project00453030 backward
collapse uses old_position + delta (-256) after enter-at-end, yielding299.
Corrected expectation from that evidence, test passes.

Build track_ring-test+oracle-sim-test passed
/tmp/rr-split-entry-test-build.log; updated synthetic tests rebuilt in
/tmp/rr-split-merge-test-build.log, track_ring CTest passed0.33s.
Strict human acceptance remains1431/1431 identical:
/tmp/rr-split-entry-acceptance.json (session84894 exit0).
Steal-farm now matches through7434 (previous6718); first gap tick7435:
original includes two extra nodes (draw0, positions186448/186624), native
omits them. RNG and measured field values match through first split.
Replay log /tmp/rr-split-entry-steal.log, session88780 exit1 diagnostic.
Original node28/29 have kind0, draw0; identity not yet established (may be
quiet traffic signal/controller, not proven; do not guess pedestrian).

No full native integration since unfinished ring port. All build/replay
sessions completed. Current LSP fallback session IDs and log:
/tmp/rr-split-boundary-clangd.log; inspect tool result for current handle.
Next: finish cached window/collapse semantics and branch wiring before
full integration, commit and push. Source changes remain uncommitted;
shared PROGRESS and REVERSE_ENGINEERING docs remain other-session edits.

Latest clangd fallback handle93898; log /tmp/rr-split-boundary-clangd.log.

clangd93898 completed exit3 (Apple check); log retained. No live processes.

## Cached window and commitment progress (latest; supersedes earlier missing-cache notes)

Added Window dx_sep/x_sep/threshold metadata. Probe mirrors original
0x00453b60: after initial dual entry, measure clearance, then80 steps.
Forward separation adds old dx then new bend difference; backward computes
new dx from current bend difference before stepping, then subtracts it.
Threshold is first sample where separation>=256+inner shoulder widths,
else80. REA6.4.0 0x00451910 /tmp/rr-window-width-rea.log completed exit0;
assembly explicitly XOR EAX,EAX before every return, including wall/mountain.
Thus the two original helper return values contribute0 to the threshold;
no guessed terrain extents added. PathSample.bend is signed HTPR byte used
by0x00451a40; shoulder_right/left map0x00452b00 local_18/local24[0].

Walker now tracks window,countdown,dx_sep,x_sep,committed,collapse_pending,
event and direction(front). Global branch_committed_/chosen_half_ mirror
DAT00476cd4/ce4. Public commit_branch(half) is0x004540e0: updates both dual
walkers to selected half. collapse is0x00452fd0: only collapses while globally
committed, copies selected half into0, clears local window/flags.
Ordinary dual stepping updates separation and countdown; beyond81 samples
or on section handover latches pending collapse. Single-road stepping
restores cached opposite iterator81 samples before merge (forward) or
at81 samples past split (backward), copies cached separation, steps only
the fraction, retains active section, sets global chosen/commit.
Per-slice event ring tracks the traversal event; camera crossing forward
merged event1/backward split-anticipation event3 clears globalcommit per
00454120. No freshly initialized terrain is used on cached reentry.

Tests: commit half1 after split, step82 -> single node80 at82; reverse1 ->
cached split reentry, chosen node80 at81 opposite node60 at80; forward to
branch319 -> cached merge reentry, both at319, chosen half1. Existing
boundary/reversal tests also pass. Latest track_ring CTest passed0.24s;
focused build log /tmp/rr-window-pending-collapse-build.log passed.
Earlier focused build of oracle-sim-test with reentry succeeded,
/tmp/rr-window-reentry-build.log. Strict human1431/1431 passed
/tmp/rr-window-reentry-acceptance.json (session57668 exit0).
Steal-farm remains tick7435 node-list gap, RNG matches; log
/tmp/rr-window-reentry-steal.log (session19877 exit1).
No full integration yet. No commit; implementation remains unfinished.

Additional claim split-ring-race-20261103, token
fea0c9c5-0d4d-4f00-80ad-0744cb4b3bcd owns src/race.cpp; NO edits there yet.
Need wire original00439390 ->004540e0 when human player's sub-slice pointer
chooses a branch. Read original00439390 whole head: forward compares
new half0 right edge; backward compares previous half0 right edge when
prev links differ. Existing Race::cross_sample handles forward split only
and route0 only (src/race.cpp436). Extend carefully to each human seat's
ring, including reverse merge entry; do not merely callcommit everytick.
Original004540e0 updates active/committed on BOTH dual walkers (confirmed).

Still open: SectionWalker lateral offset alignment (NALR lane-width shifts),
full slice neighbor/half-index connectivity and CourseNode lowbyte+0x18,
branch-relative ring actor/controller integration (7435 missing draw0nodes),
unequal branch lengths and snapshot replay tests, signed-wrap semantics
for new separation arithmetic, whole gate. Original00453030 geometrical
handling must be preserved even where native static routes approximate it.
Current new separation additions use int32 arithmetic; wrap explicitly
before sanitizers for long pathological synthetic courses.

Current clangd fallback log /tmp/rr-window-state-clangd.log is running;
inspect returned handle. No build/replay/REA processes remain.

clangd fallback handle44585, log /tmp/rr-window-state-clangd.log.

clangd44585 completed exit3 (Apple check); no live processes remain.

## Latest gameplay/offset/snapshot integration and opposite-route finding

Current owned source edits now ALSO src/race.cpp and tests/race_test.cpp.
Race::cross_sample calls seat ring commit_branch for human forward split or
reverse merge branch entry using selected half0 right edge. Existing rider
route0/route1 architecture is unchanged; arbitrary per-split route choice
and differing prefix lengths remain broader plan17 limits, not claimed fixed.
New synthetic gameplay course root60,split primary1: original ring stayed on
half1 when human chose0. Regression failed baseline; passes with wiring.
Initial synthetic fixture lacked root backward terminal, make_route threw
Missing route node; added proper kind3 terminal before using test evidence.

SectionWalker.offset and diagnostic offset now retain NALR lane edge shifts
at split/merge. Cached opposite offset is copied relative to the live
selected section, preserving original selected edge. Probe caches include
those offsets. Added explicit32-bit wrap for new separation updates and
lane-shift products (memcpy bit conversion). Asymmetric fixture: half0
left1lane vs old2 ->offset-250; half1right1lane ->offset+250. Merging chosen0
restores commonoffset0. Snapshot restore test retains both iterators and
RNG stream through split.

Debugger caught a real empty-stream issue in cached merge restoration:
backward-entered empty DZHR has idx-1; step_section indexed it on positive
step. Original00452230 skips null streams. Added records.empty guard.
Stack log /tmp/rr-ring-snapshot-lldb-stack.log points track_ring.cpp441
while asymmetric merge test loops (not the snapshot copy). New snapshot
and width tests plus gameplay tests now pass.
Full Debug build passed /tmp/rr-ring-integrated-final-build.log. Focused
race+track_ring CTests2/2 /tmp/rr-ring-gameplay-offset-tests.log (4.32s).
Full integration is RUNNING tool16957 /tmp/rr-ring-integrated-ctest.log.
Do not restart just because output stalls; inspect same handle/process.
Strict gate running tool59425 /tmp/rr-ring-integrated-acceptance.{log,json};
steal replay tool78126 /tmp/rr-ring-integrated-steal.log; known expected7435
node gap. LSP fallback62912 /tmp/rr-ring-gameplay-clangd.log. REA540e0
session58186 /tmp/rr-ring-commit-rea.log. Inspect exits and record them.

Authoritative NEW next fix (no course edits yet): src/course.cpp split
annotation stores own branch as other:
 splits.push_back({route.size(), branch ? node.previous : node.next, branch});
 offset = branch ? node.previous : node.next;
The first expression MUST choose the opposite branch (original00453030
enters half0=node.next and half1=node.previous). Read-only helper
/tmp/rr-route-events.cpp parsed original NAPA.CRS; /tmp/rr-route-events.log:
A at729 section1 otherSection1 otherEnd0 type0;
B at729 section3 otherSection3 otherEnd1 type3 (quiet intersection, params2,100).
Correct A.other should section3 and zone_end1 at729. This explains missing
original controller/signal nodes186624/186448 while RNG still matches.
TrafficSystem::build_slice ALREADY runs own path then at.other ifdual, but
bad annotation hid the opposite event. place_intersection still reads
route own lane/offset when handed at.other path: audit that next, don't
assume one-line route fix completes traffic parity.

Claim split-ring-course-20261103 token1f6b7aab-291e-4dc7-9afb-f58fb23ebef8:
src/course.cpp/.hpp,tests/course_test.cpp. tests/course_test already checks
other_node's path matches data but doesn't assert it differs from own node.
Add regression proving distinct/opposite branches (and event identity),
then fix mapping. Current full gate is previous source; keep that evidence
separate and run new checks after integrating course fix. No commits yet;
shared PROGRESS/REVERSE_ENGINEERING edits remain other-session owned.

Gate handle updates: strict59425 exited0 (1431/1431 exact); replay78126
exited1 at same7435 node gap; REA58186 exited0; clangd62912 timed out124
(agent_lsp60s bound), no claim of a successful client LSP check. Full
CTest16957 remains running; most recent observed16/135 passed, no failures.
Format-check and diff-check passed on all owned modified C++ files.
