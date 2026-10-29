# Traffic car-crash capture follow-up — October 28, 2026

Baseline inspected: main `541c3b2`; traffic worktree `66341cd` at
`/Users/archit/Dev/rr/wt-traffic-field`. No simulation or proxy source edits.
Existing frontend ownership and all uncommitted changes were preserved.
The earlier handoff's first-box comparison TODO is superseded by `66341cd`.

## New original capture

`analysis/oracle/sim-napa-traffic-field-carcrash-20261028a.tsv` in the main
repository (also visible via the traffic worktree's analysis/oracle symlink).
The receipt points to immutable run `46ccf5e304194b099f597eb4e15f7726`.
4381 contiguous ticks, human branch, complete, original EXE hash verified,
process exit 0. Proxy SHA256 `3bad09036d4d1d92da7ed8e17bb9a72017475b4997766e6642d366b671437338`;
trace SHA256 `2c9b7470c55b3f24b6d22125d9d6a7408714bab9d7a7c9f8b368d7af29fd7960`.

Recipe from the traffic worktree:

```sh
env RR_ORACLE_SIM=1 RR_ORACLE_COURSE=4 RR_ORACLE_LEVEL=5 \
 RR_ORACLE_SEED=1 RR_ORACLE_HUMAN=1 \
 RR_ORACLE_INPUT='C:\tf-body-20261028.controls' RR_ORACLE_FRAMES=4381 \
 python3 tools/oracle_capture.py capture sim-napa-traffic-field-carcrash-20261028a \
 --workspace /Users/archit/Dev/rr \
 --install /Users/archit/Dev/rr/prefix/drive_c/ElectronicArts/RoadRashOracle-tf20261028 \
 --timeout 240 --session traffic-carcrash-20261028
```

The player enters crash state 1 with bike animation 29 at ticks 1549 and
3331 (body height 19200 at each entry). Animation 29 is the reason-2 car
crash selection in the recovered crash-entry path; the capture does not dump
reason +0x2f8 directly. This is evidence of suitable later car-crash events,
not proof that the car-vs-crashed-body contact callback fires.

## Replay failure / next action

```sh
build/oracle-sim-test --driver=human \
 analysis/oracle/sim-napa-traffic-field-carcrash-20261028a.tsv ../cdrom/ROADRASH
```

Exit 1; first divergence frame 687 / tick 688:
`rider.7.lateral original 10772 native -2271 (native rider 4)`.
Repeating with `RR_FIELD_ALL=1` additionally reports
`rider.7.velocity original 1295 native 1536` (vertical velocity).
Logs in the traffic worktree: `carcrash-replay-20261028a.log` and
`carcrash-replay-20261028a-all.log` (local, untracked).

At tick 687 the original rider-7 body is at position 38406, height 26966,
lateral 21302; tick 688 position 38637, height 29298, lateral 23624.
The reported lateral mismatch is the bike field, not body_28. Investigate
bike contact/separation at tick 688 before reaching the later player car
crashes. Do not reanchor or claim those later events matched.

The previous tick-1303 mismatch, visibility-load notes, car-static coverage,
and split-road removal branch remain unresolved. No new parity acceptance.
No build/tests beyond the two replays (no source changes). No live processes
or held capture claims remain. The private oracle install is retained.
