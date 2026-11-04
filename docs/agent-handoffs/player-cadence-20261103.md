# player-cadence-20261103

Task: Plan 10 force human player due next tick, original scheduler tail

Baseline: 38a2c149dd65fbbb6d7792e5f549e64c1b8baeac

Worktree: `/Users/archit/Dev/rr/native-port`
Build directory: `/Users/archit/Dev/rr/native-port/build-agent-player-cadence-20261103`
Owner PID: 86170
Resource token: `b89aab59-1692-46e4-abbf-50e18ef08282`

Owned files:

- `/Users/archit/Dev/rr/native-port/src/race_schedule.cpp`
- `/Users/archit/Dev/rr/native-port/tests/race_schedule_test.cpp`

## Progress / evidence

Previous goal turn made authoritative progress: crossing-car fix 38a2c14,
135/135 CTests, strict human 1431/1431, evidence commit ead0c33; both pushed.

This slice implements 0x00438170's human next-tick override at
src/race_schedule.cpp after the merged car/ped/rider pass. Both player
node +0xc words become ticks+1; stored intervals are unchanged. Native
multiplayer applies the same rule to each human seat; demo_hold bypasses it.

Evidence: original disc SHA-256
73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2;
analysis/decompiled/00438170.c and independent npx rea-agents@6.4.0 export
/tmp/rr-scheduler-rea.log. Assembly 0x00438385..0x004383a2 increments
EBX (tick) then stores half+0xc and bike+0xc, gated by DAT_004753c8 == 0.

Regression tests/race_schedule_test.cpp uses absent slices to make both
stored intervals 30: human next=2 and next-step last=2; demo next=31 and
last stays1. It failed on baseline with the two human cadence assertions;
post-fix prints race schedule ok. This isolates the unconditional override
without relying on scene fixtures.

## Checks / remaining work

Full Debug build passed (/tmp/rr-cadence-build.log); format-check and diff
check passed. Reused owned build-agent-traffic-ring-20261103 to avoid a
redundant build tree. Client LSP unavailable; owned CDB healthy (426 entries).
clangd --check attempted through agent_lsp.py; exits3 from ExtractFunction
tweak checks, inspect /tmp/rr-cadence-clangd.log (no parse diagnostics).

Strict acceptance passed (session17768 exit0): 1431/1431 identical.
Steal-farm diagnostic now matches through tick6718 (previous first gap6422);
first mismatch tick6719 is RNG original2947348728/native1156752344.
Both entered from seed1512667433: original consumed49 draws, native17.
This is a ring-slice crossing (front179199 ->179203, camera158719 ->158723),
with player crash state2. Investigate TrackRing::advance / walker RNG next;
no correction or golden change made for this separate gap.

Full integration passed: 135/135 CTests, zero skips/failures, 424.06s,
log /tmp/rr-cadence-ctest.log (session78261 exit0). Includes police,
field/schema2/L5 replays, rendering and soak, and multiplayer matrix.
Steal-farm replay log /tmp/rr-cadence-steal-farm.log (session55817 exit1
at the separate split gap). Strict report /tmp/rr-cadence-acceptance.json.
All validation commands completed. No goldens changed.
Next: commit explicit owned files + this handoff, push main; advance the
split/merge ring traversal task described below.
Shared docs/PROGRESS.md and docs/REVERSE_ENGINEERING.md are concurrent
changes belonging to another session; preserve and do not stage them.

## Live processes / resources

Scheduler file claims above. No Wine captures. REA completed; no Ghidra
project edits. All listed sessions completed. No build/test/capture processes remain.

## Follow-up ring evidence

Read-only helper /tmp/rr-course-graph.cpp linked the existing rr-resource
library and parsed the original DATA/COURSES/NAPA.CRS with read_course.
Root node20 is a road of length700, next node60 is kind1 split.
Its halves are node156 (section1, length300) and node92 (section3,
length325), merging at node124 before road188 (section2, length1840).
Thus tick6719 is exactly front walker's first split entry, where native
TrackRing::step currently sets stopped/unsupported for any kind!=0.
The RNG discrepancy is consistent with missing dual-half terrain entry;
this is stronger evidence than a generic terrain-stream hypothesis.

Next port must cover original 0x00453030 full dual-half step and
0x00454c80 dual-half slice build, forward AND backward split/merge
transitions; not merely add RNG draws. Preserve field-specific branch,
terrain/actor-sink order and snapshot copy behavior. Existing probe_window
already enters two terrain halves, but is only setup-time scratch logic.
REA 6.4.0 completed successfully for 0x00453030 (session73896 exit0), local output
/tmp/rr-split-ring-rea.log. Current project exports also include
00452e10,00452fd0,00453d00,00453020 and00454c80.
