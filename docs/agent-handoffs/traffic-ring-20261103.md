# traffic-ring-20261103

Task: Plan 10 steal-farm ring lateral divergence

Baseline: 0d4e140f36ef16156ba6174d56f32d0aa712d34f

Worktree: `/Users/archit/Dev/rr/native-port`
Build directory: `/Users/archit/Dev/rr/native-port/build-agent-traffic-ring-20261103`
Owner PID: 86170
Resource token: `1a4c8c83-fa4c-44cd-9e9e-96d9df65cf0c`

Owned files:

- `/Users/archit/Dev/rr/native-port/src/traffic.cpp`
- `/Users/archit/Dev/rr/native-port/tests/traffic_test.cpp`

## Progress / evidence

Investigation: baseline steal-farm first divergence tick 4059, crossing car
0x13e2038 lateral 161702 original / 157546 native. Native target becomes 0
because player at 109059 is 152 Q8 behind car at 109211, on the same sample.
0x00449da0 (via 0x00449ce0) limits both longitudinal scans strictly to 0x80;
0x0044a070 then filters same-sample. Native omitted the first limit.
Added strict +/-128 exclusion and synthetic boundary coverage. Focused traffic CTest passed before and after formatting. Strict human acceptance
passed: 1431/1431 identical ticks (tools/verify_sim.py --mode acceptance,
report /tmp/rr-traffic-acceptance.json). Full Debug build passed (also after formatting).
Single-anchor human steal-farm now matches through tick 6421; first mismatch
at 6422, rider.0.bike_c original 6423 / native 6424. Level-1 steal-farm
remains at its prior first mismatch tick 5697 (RNG), no regression.
No capture taken; these are diagnostic replay spans, not full acceptance.
REA 6.4.0 succeeded for 0x00438610, 0x0044dc10, and 0x00449da0; logs in
/tmp/rr-city-visibility-rea.log, /tmp/rr-traffic-gate-rea.log, and
/tmp/rr-crossing-scan-rea.log. Last includes assembly confirming the
0x80 range construction before both calls to 0x00449ce0. Reference SHA-256
73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2.
City-moving.tsv is renderer-only, not a simulation capture.
Owned Debug full baseline build passed. Client LSP unavailable; owned CDB healthy (426 entries);
Apple clangd check hits ExtractFunction tweak failures (no parse diagnostic).
First failed command: baseline oracle-sim-test steal-farm (expected tick 4059).
Temporary instrumentation removed.

## Checks / remaining work

Full integration CTest passed: 135/135, zero failures or skips (529.25s),
log /tmp/rr-traffic-ctest.log. Commit 38a2c14 pushed to origin/main.
Build logs /tmp/rr-traffic-final-build.log and /tmp/rr-traffic-formatted-build.log.
Focused traffic test passed. Format gate initially flagged wrapping, corrected
using tools/cpp_quality.py format; format-check and git diff --check now pass.
Replay logs /tmp/rr-steal-farm-fix.log and /tmp/rr-steal-farm-l1-fix.log.
Do not stage concurrent docs/PROGRESS.md or docs/REVERSE_ENGINEERING.md edits.
Next: implement the separately claimed player-cadence slice below.

## Live processes / resources

Traffic claims released. No traffic capture/build/test processes remain.

## Next slice research (read-only)

At new steal-farm mismatch tick 6422: original player bike last=6422,
interval=2 but next=6423; native next=6424. Export 0x00438170 tail
forces player bike AND half +0xc to tick+1 when DAT_004753c8 == 0.
Native schedule_pass currently ends after run_cars/run_peds, omitting that
write. REA 6.4.0 independently confirmed pseudocode AND assembly
(0x00438385 INC EBX, stores to half+0xc then bike+0xc) in
/tmp/rr-scheduler-rea.log. Next owned slice: src/race_schedule.cpp and
tests/race_schedule_test.cpp, after checking claims. Preserve the stored
interval; force next for human seats only, excluding attract/demo_hold.
Add a player far enough ahead of camera that interval >1, prove its
bike/half still execute next tick, and demo cadence is unchanged.
Re-run steal-farm, strict human acceptance and native integration.
