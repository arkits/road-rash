# Simulation Oracle and Tick Verification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Read [00-INDEX.md](00-INDEX.md) first.

**Goal:** Make the original game produce deterministic per-tick traces of every rider, bike and car from a fixed seed and a recorded control stream. Replay the same seed and controls through `rr::Race`, and find and fix every divergence in physics, AI racing, contact, combat and traffic.

**Architecture:** Three pieces.
- **Oracle side:** `oracle/aweman_proxy.c` gains three things:
  - a seed hook that sets the CRT `rand` state at race start;
  - an input hook (`0x0044f800`, already intercepted for `RR_ORACLE_DRIVE`) that records or replays one control word per tick;
  - a per-tick hook after the simulation step that writes object fields in the existing `# rr-oracle 1` golden format.
- **Native side:** `rr::Race` exposes the RNG state and a snapshot. A new `oracle-sim` test builds the identical race from the golden header, replays the controls and compares every field every tick. It reports the **first** divergent tick and field.
- **Fixes:** the remaining tasks fix the native port, one system at a time, until the scenarios pass.

**Tech Stack:** C++17, mingw-w64 (`oracle/build.sh`), Wine Staging, CTest, `tests/golden.hpp`.

**Spec:** [00-INDEX.md](00-INDEX.md) (review finding 2). The original plan 01 Task 7 and the plan 04/05 acceptance criteria are replaced by this plan.

## Global Constraints

- The reference EXE is never modified. Hooks patch only in-memory call sites, preflighted with `rr_patch_call` against their expected targets.
- Golden files use `# rr-oracle 1`, one `frame` per simulation tick (not per rendered frame), and integer values only.
- Native replay must not depend on rendering or frame pacing: the test calls `Race::step` exactly once per golden frame.
- Field names are identical on both sides and listed once in `tests/sim_fields.hpp`. The oracle C code prints the same names from a parallel table in `oracle/sim_fields.h`, and a test checks the two lists match.
- A scenario capture must finish in under 6 minutes of wall time, including the roughly 4-minute bootstrap.

## Review Focus

1. **Random-call drift:** one extra `rand()` call anywhere desynchronises everything after it. The `rng` field is compared first on every tick, and the failure report prints the tick, the first differing field, and the last 8 ticks of both RNG values.
2. **Tick versus frame:** the original runs up to 20 steps per frame (`0x004163d0`). Capturing per frame would hide steps. Task 1 must hook the per-step call, and a synthetic check asserts golden `tick` values are contiguous.
3. **Rider order:** the original's object order and the native `riders_` order may differ (player index, grid slots). Task 2 maps them explicitly by grid slot and never by vector index.
4. **Controls at a lifecycle pause:** recorded control words must be replayed per tick even when the original's input poll is skipped. Task 1 records at the step, not at the poll.
5. **Captures that end mid-race:** a truncated golden (Wine killed) has no `end` marker. `golden::load` already rejects it, and the test must report that clearly rather than as a divergence.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `oracle/sim_fields.h` | C table of `{name, object offset, width}` for rider/bike/car fields dumped per tick |
| `oracle/aweman_proxy.c` | Seed hook, record/replay control hook, per-step dump hook |
| `oracle/run.sh` | Already exports `RR_ORACLE_SEED/INPUT/RECORD`; add `RR_ORACLE_SIM=1` |
| `tests/sim_fields.hpp` | The same field list for native code |
| `src/race.hpp/.cpp` | `rng_state()`, `set_rng_state()`, `RaceSnapshot snapshot() const` |
| `src/traffic.hpp/.cpp` | `TrafficSystem::snapshot()` |
| `tests/oracle_sim_test.cpp` | Scenario replay and first-divergence report |
| `tests/sim_fields_test.cpp` | Field tables agree; synthetic snapshot round trip |
| `docs/SIM_SCENARIOS.md` | Each scenario: course, level, bike, character, seed, control script, purpose |

---

### Task 1: Trace the step boundary, the RNG global and the object lists

Time box: one session. This task produces findings and no native code.

**Files:**
- Modify: `docs/REVERSE_ENGINEERING.md` (new section "Simulation step and object lists")

- [ ] **Step 1: Find the per-step call.** Read `analysis/decompiled/004163d0.c` and its disassembly. Find the call that runs once per 1/60 s step (the update chain leading to `0x00442e10` and `0x0044b3b0`). Record:
  - the E8 call-site address;
  - its expected target;
  - whether the step count argument is a loop counter or a batch size.

  If it is a batch size, find the inner per-tick loop and hook inside it.
- [ ] **Step 2: Find the CRT rand state.** Disassemble the `rand` implementation the port reproduces (`seed * 0x343fd + 0x269ec3`, return `>>16 & 0x7fff`). Record the address of its state global and of `srand`. Also record every place where the original calls `srand` (race start, menus, `GetTickCount` seeding).
- [ ] **Step 3: Find the object lists.**
  - **Riders:** `0x004642d8` holds the player rider pointer (used by `hook_controls`). Find the array or linked list the step iterates, its count and its order.
  - **Cars:** find the free-traffic pool (`0x00438b70(0x210,0x10)` allocates 16 × `0x210`) and its active flag.
- [ ] **Step 4: Choose the fields.** For each rider/bike, take the offsets the native code already reads (`src/race.hpp`, `src/bike.cpp`). For cars, take the `TrafficCar` offsets in `src/traffic.hpp`. Minimum set:
  - **bike:** `+0x1c` position, `+0x28` lateral, `+0xec` speed, `+0x23c` gear, `+0x2f0` condition, `+0x2f4` crash, `+0x140` slice index, `+0xc0` flags, vertical height and velocity;
  - **rider:** stamina current/max, weapon `+0x1c8`, AI `+0x188..+0x1bc`;
  - **car:** `+0x1c`, `+0x28`, `+0xec`, `+0x73`, `+0x7a`, `+0x7f`, `+0x71`.

  Convert slice pointers to ring indexes in the hook, because pointers are process-specific.
- [ ] **Step 5: Document and commit.** Write the section with every address and how each was confirmed (disassembly lines).

```bash
git add docs/REVERSE_ENGINEERING.md
git commit -m "docs: simulation step boundary, rand state and object lists"
```

---

### Task 2: Shared field table and native snapshot

**Files:**
- Create: `tests/sim_fields.hpp`, `oracle/sim_fields.h`, `tests/sim_fields_test.cpp`
- Modify: `src/race.hpp`, `src/race.cpp`, `src/traffic.hpp`, `src/traffic.cpp`, `CMakeLists.txt` (add `sim_fields` to the synthetic test list)

**Interfaces:**
- Consumes: the Task 1 field list.
- Produces:
  ```cpp
  namespace rr {
  struct FieldValue { std::string name; int64_t value; };
  struct RaceSnapshot { int32_t tick; uint32_t rng; std::vector<FieldValue> fields; };
  // Race
  uint32_t rng_state() const { return seed_; }
  void set_rng_state(uint32_t s) { seed_ = s; }
  RaceSnapshot snapshot() const;              // rider fields keyed "rider.<grid slot>.<field>"
  // TrafficSystem
  void append_snapshot(std::vector<FieldValue>&) const;   // "car.<pool index>.<field>", active cars only, plus "car.<i>.active"
  }
  ```

- [ ] **Step 1: Write the failing test.**

```cpp
// tests/sim_fields_test.cpp
#include "sim_fields.hpp"
#include "../oracle/sim_fields.h"
#include <cstring>
#include <iostream>
#include <set>
int main() {
    int failures = 0;
    // Native and oracle tables list the same names in the same order.
    if (rr::sim::rider_fields.size() != RR_SIM_RIDER_FIELD_COUNT) { std::cerr << "rider field count\n"; ++failures; }
    for (size_t i = 0; i < rr::sim::rider_fields.size() && i < RR_SIM_RIDER_FIELD_COUNT; ++i)
        if (std::strcmp(rr::sim::rider_fields[i].name, rr_sim_rider_fields[i].name) != 0 ||
            rr::sim::rider_fields[i].offset != rr_sim_rider_fields[i].offset) { std::cerr << "rider field " << i << '\n'; ++failures; }
    if (rr::sim::car_fields.size() != RR_SIM_CAR_FIELD_COUNT) { std::cerr << "car field count\n"; ++failures; }
    for (size_t i = 0; i < rr::sim::car_fields.size() && i < RR_SIM_CAR_FIELD_COUNT; ++i)
        if (std::strcmp(rr::sim::car_fields[i].name, rr_sim_car_fields[i].name) != 0) { std::cerr << "car field " << i << '\n'; ++failures; }
    // Names are unique.
    std::set<std::string> seen;
    for (const auto& f : rr::sim::rider_fields) if (!seen.insert(f.name).second) { std::cerr << "duplicate " << f.name << '\n'; ++failures; }
    if (failures) return 1;
    std::cout << "sim field tables agree\n";
    return 0;
}
```

Also add a synthetic native check: build a `Race` on a 200-sample synthetic route (use the helper already in `tests/race_test.cpp`), step it 10 ticks with throttle, and assert that `snapshot().fields` contains `rider.0.position` and that the value equals `race.position()`.

- [ ] **Step 2: Run the test and confirm it fails.** Run `cmake --build build --target sim_fields-test && ctest --test-dir build -R sim_fields`. Expected: build failure, `sim_fields.hpp` not found.
- [ ] **Step 3: Implement.** Write the tables. Use `struct SimField { const char* name; uint32_t offset; uint8_t width; bool is_signed; };` and the same layout in C. For each entry, `Race::snapshot()` reads the bike word through the existing `Bike::i32/u8` accessors. Rider-only fields (stamina, weapon, AI state) map from `Rider` members. Give each its original offset as the table key so the two sides cannot drift.
- [ ] **Step 4: Run the test and confirm it passes.** Then run the full `ctest`.
- [ ] **Step 5: Commit.**

```bash
git add tests/sim_fields.hpp oracle/sim_fields.h tests/sim_fields_test.cpp src/race.hpp src/race.cpp src/traffic.hpp src/traffic.cpp CMakeLists.txt
git commit -m "sim: shared per-tick field table and native race snapshot"
```

---

### Task 3: Oracle seed, control record/replay and per-step dump

**Files:**
- Modify: `oracle/aweman_proxy.c`, `oracle/run.sh`, `oracle/patch_test.c`
- Create: `docs/SIM_SCENARIOS.md`

**Interfaces:**
- Consumes: Task 1 addresses, Task 2 `oracle/sim_fields.h`.
- Produces: golden files `analysis/oracle/sim-<scenario>.tsv`. Header frame 0 carries `course` (original index), `level`, `bike`, `character`, `seed` and `riders`. Every frame carries `tick`, `rng`, `controls`, `rider.<slot>.<field>…` and `car.<i>.<field>…`. Control streams are written to `analysis/oracle/sim-<scenario>.controls`, one hex word per line.

- [ ] **Step 1: Write the failing check.** Extend `oracle/patch_test.c` so it preflights the new call sites against the reference EXE bytes (the same pattern as the existing four sites). It fails until the sites are added to the table.
- [ ] **Step 2: Implement the hooks.**
  - `RR_ORACLE_SEED=<n>`: at the race-start point found in Task 1, write `n` into the rand state after the original's own `srand`. This keeps the original's call sequence and only fixes the value.
  - `RR_ORACLE_RECORD=<path>`: at the per-step hook, append the control word used for that tick.
  - `RR_ORACLE_INPUT=<path>`: at `hook_controls`, return the next word from the file; at end of file, return 0 and set the quit flag after 0xb4 more ticks.
  - `RR_ORACLE_SIM=1`: at the per-step hook, after the original step, write one golden frame from the field tables. Use `readable()` before every dereference.
  - Keep the existing `RR_ORACLE_DRIVE` modes for convenience: drive mode plus record yields a control file.
- [ ] **Step 3: Capture the first scenario.** `napa-solo-straight` has level 1, the default bike and character, seed 1, the drive=2 autopilot and record enabled, and runs for 1,800 ticks. Run it in the background:
  ```sh
  RR_ORACLE_COURSE=4 RR_ORACLE_SEED=1 RR_ORACLE_DRIVE=2 RR_ORACLE_SIM=1 RR_ORACLE_RECORD='C:\sim-napa-solo-straight.controls' RR_ORACLE_FRAMES=2400 oracle/run.sh sim-napa-solo-straight
  ```
  Then confirm with `python3 tools/golden_diff.py analysis/oracle/sim-napa-solo-straight.tsv analysis/oracle/sim-napa-solo-straight.tsv` (it exits 0 and checks that `tick` values are contiguous).
- [ ] **Step 4: Check determinism.** Add `tools/golden_diff.py`:

```python
#!/usr/bin/env python3
"""Compare two rr-oracle golden files field by field; print the first difference."""
import sys

def load(path):
    frames, cur = [], None
    with open(path) as f:
        if f.readline().rstrip('\r\n') != '# rr-oracle 1':
            sys.exit(f'{path}: bad magic')
        ended = False
        for line in f:
            line = line.rstrip('\r\n')
            if not line or line.startswith('#'):
                continue
            if line == 'end':
                ended = True
                continue
            k, v = line.split('\t', 1)
            if k == 'frame':
                cur = {}
                frames.append(cur)
            elif cur is not None:
                cur[k] = int(v)
    if not ended:
        sys.exit(f'{path}: truncated capture (no end marker)')
    return frames

a, b = load(sys.argv[1]), load(sys.argv[2])
prefixes = tuple(sys.argv[3:]) or ('rider.', 'car.', 'rng', 'tick')
for i, fa in enumerate(a):
    if 'tick' in fa and fa['tick'] != a[0].get('tick', 0) + i:
        sys.exit(f'{sys.argv[1]}: tick not contiguous at frame {i}')
for i, (fa, fb) in enumerate(zip(a, b)):
    for k in sorted(set(fa) | set(fb)):
        if k.startswith(prefixes) and fa.get(k) != fb.get(k):
            sys.exit(f'frame {i} {k}: {fa.get(k)} != {fb.get(k)}')
if len(a) != len(b):
    sys.exit(f'frame counts differ: {len(a)} != {len(b)}')
print(f'{len(a)} frames identical')
```

  Replay the recorded controls with the same seed (`RR_ORACLE_INPUT`) into `sim-napa-solo-straight-replay`. `tools/golden_diff.py` on the two files must report zero differences. **If it does not, the original has a nondeterministic input** (`GetTickCount`, timer-driven behaviour). Find it and pin it in the hook before continuing, and document it.
- [ ] **Step 5: Document and commit.** Write `docs/SIM_SCENARIOS.md` with this scenario and the capture command, update `VERIFICATION.md`, and commit:

```bash
git add oracle/aweman_proxy.c oracle/sim_fields.h oracle/run.sh oracle/patch_test.c tools/golden_diff.py docs/SIM_SCENARIOS.md docs/VERIFICATION.md
git commit -m "oracle: seeded per-tick simulation dumps with control record and replay"
```

---

### Task 4: Native replay test with first-divergence report

**Files:**
- Create: `tests/oracle_sim_test.cpp`
- Modify: `CMakeLists.txt` (register `oracle-sim` with `SKIP_RETURN_CODE 77`, one CTest per scenario file present)

**Interfaces:**
- Consumes: `Race::snapshot`, `set_rng_state`, `TrafficSystem::append_snapshot`, and the existing helpers `rr::original_opponents`, `rr::make_route`, `rr::read_course`, `rr::CourseScene`.
- Produces: `oracle-sim-test <golden> [assets]`. It exits 0 on a full match, 1 on divergence and 77 when the golden is missing.

- [ ] **Step 1: Write the test.** Outline:

```cpp
int main(int argc, char** argv) {
    const std::string file = argc > 1 ? argv[1] : rr::golden::oracle_dir() + "/sim-napa-solo-straight.tsv";
    if (!std::filesystem::exists(file)) { std::cerr << "SKIP missing " << file << '\n'; return rr::golden::skip; }
    const auto g = rr::golden::load(file);
    if (g.exe != "73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2") throw std::runtime_error("different executable");
    const auto& h = g.frames.at(0);
    static const char* original_courses[] = {"CITY", "HIWAY", "MEDLY", "NAPA", "CANYN"};   // 0x0046df58
    // Build exactly what rr-game's career start builds for course/level/bike/character,
    // using the same factory function (extract it from game_main.cpp into src/race_setup.cpp
    // as `RaceSetup make_race_setup(const Assets&, const std::vector<uint8_t>& exe, const RaceConfig&)`).
    // Set race.set_rng_state(uint32_t(h.at("rng"))) BEFORE tick 0 so the stream starts aligned.
    // For each frame f: race.step(uint32_t(frame.at("controls"))); compare snapshot to frame.
    // On the first mismatch print: tick, field, original, native; then the rng of ticks f-8..f
    // for both sides; return 1.
}
```

Extracting `make_race_setup` from `game_main.cpp` is part of this task. `rr-game` must call the same function, so the test and the game cannot build different races:

```cpp
// src/race_setup.hpp
namespace rr {
struct RaceConfig { std::string course; int level = 0; int bike = 0; int character = 0; bool career = true; };
struct RaceSetup {
    Course course; std::unique_ptr<CourseScene> scene; std::unique_ptr<VehicleLibrary> vehicles;
    std::unique_ptr<TrafficSystem> traffic; std::unique_ptr<Race> race;
};
RaceSetup make_race_setup(const Assets&, const std::vector<uint8_t>& exe, const RaceConfig&);
}
```

- [ ] **Step 2: Run against the Task 3 golden.** Expected: FAIL. The current port has never been compared, so record the first divergence it reports in `VERIFICATION.md`. **Do not fix anything in this task.**
- [ ] **Step 3: Gate.** Run the full `ctest`. The test must SKIP cleanly when the golden is absent (`RR_ORACLE_DIR=/nonexistent ctest -R oracle-sim`).
- [ ] **Step 4: Commit.** The failing scenario is marked in CTest with `WILL_FAIL` **only** while it is listed as a known divergence in `VERIFICATION.md`, and that marker is removed in Task 5.

```bash
git add src/race_setup.hpp src/race_setup.cpp src/game_main.cpp tests/oracle_sim_test.cpp CMakeLists.txt docs/VERIFICATION.md
git commit -m "test: tick-by-tick native replay against original simulation dumps"
```

---

### Task 5: Player physics and movement to zero divergence

**Files:**
- Modify: `src/bike.cpp`, `src/race.cpp`, `docs/REVERSE_ENGINEERING.md`, `docs/VERIFICATION.md`, `docs/PROGRESS.md`

**Scenarios to capture first (Task 3 procedure, documented in SIM_SCENARIOS.md):**
- `napa-solo-straight` (Task 3).
- `napa-solo-script`: a scripted control file with full throttle 300 ticks, hard left 60, brake 120, right 60, and a gear-change pattern, repeated to the first split.
- `napa-solo-split-left` and `napa-solo-split-right`: commit to each branch at the first split. These verify the rider-commit assumption (`0x00439390`).
- `napa-solo-jump`: crest sections with airborne ticks.

To isolate the player, opponents and traffic must be off in these captures. Trace the original's opponent and traffic count globals and zero them from the hook, then document which globals were written. If that is not possible, compare only `rider.0.*` and `rng` and record that the test is limited to those.

- [ ] **Step 1:** Run `oracle-sim-test` on each scenario and record the first divergence of each.
- [ ] **Step 2:** For each divergence, re-read the named original function, fix the native translation, and rerun. **Never change a golden.** Typical suspects:
  - the `sar` rounding on negative values;
  - `uint32_t` position wrap;
  - the order of `move_along` versus `vertical`;
  - the camera update order (`0x00437c30`) relative to the bike step.
- [ ] **Step 2a: RNG acceptance rule (added October 11).** Scenarios pass only when replayed with a single RNG anchor at the green light. The current per-tick re-anchoring hides missing and extra `rand()` calls, so keep it only as `oracle-sim-test --reanchor` for localising a divergence. At least one scenario must use recorded human-style controls rather than the attract-demo AI branch (`set_demo_hold`), so that `0x00434660`'s player-control path is exercised.
- [ ] **Step 3:** When all five scenarios pass, remove the `WILL_FAIL` markers and run the full regression gate, including `build-ubsan`.
- [ ] **Step 4:** Commit: `sim: player physics matches original tick traces`.

---

### Task 6: AI racing, contact and combat to zero divergence

**Files:**
- Modify: `src/race.cpp`, `src/race.hpp`, docs

**Scenarios:**
- `napa-field-l1`: seven opponents at level 1, with the player on drive=2 autopilot.
- `napa-field-l5`: the same at level 5. Rubber band and throttle scale differ.
- `napa-fight`: the player holds attack controls alongside an opponent. Script it with drive=2 plus the punch bit every 30 ticks.
- `napa-contact`: steer into an opponent without attacking.

- [ ] **Step 1:** Capture and run; record the divergences.
- [ ] **Step 2:** Fix in this order, because later systems consume earlier ones' RNG calls:
  1. grid setup at tick 0 (rider slots, values, personalities);
  2. AI re-plan (`0x004082f0`);
  3. target choice (`0x004090a0`/`0x004091f0`);
  4. speed control (`0x00409f70`);
  5. contact (`0x004368f0`, `0x00436a40`, `0x004049c0`);
  6. combat (`0x00409c10`, `0x00409ad0`, `0x004052c0`, `0x004050c0`).

  **Expected:** the stand-ins listed in PROGRESS.md (attack 18/7 ticks, AI attack every 45 ticks, grab 1 in 4) will diverge. Replace each with the traced original rule. Attack timing comes from the RRAN hit frames (`0x0043ae90`); coordinate with plan 11 Task 3.
- [ ] **Step 3:** Remove each replaced stand-in from PROGRESS.md "Assumptions", and add the evidence to REVERSE_ENGINEERING.md.
- [ ] **Step 4:** Commit per subsystem, e.g. `sim: AI re-plan matches original`, `sim: combat timing from original animation hit frames`.

---

### Task 7: Traffic to zero divergence

**Files:**
- Modify: `src/traffic.cpp`, `src/race.cpp`, docs

**Scenarios:**
- `napa-traffic-solo`: traffic on, opponents off.
- `napa-traffic-field`: both on, level 1.

- [ ] **Step 1:** Capture and run. The `rng` field will reveal whether `TrafficSystem::step` consumes `rand()` at the original point in the tick. The current call sits at the end of `Race::step` and its position is unverified.
- [ ] **Step 2:** Move the traffic step to the original position within the tick (Task 1's step trace). Fix spawn timers, lane choice, placement (`0x0044df60`), vehicle choice and car movement/horn (`0x0044efa0`) until both scenarios pass.
- [ ] **Step 3:** Update the `race.hpp` comment ("random-call interleaving … is not verified") to cite the passing scenario.
- [ ] **Step 4:** Commit: `sim: traffic matches original tick traces`.

## Exit criteria

- Every scenario in `docs/SIM_SCENARIOS.md` passes `oracle-sim` with zero differing fields on every tick.
- PROGRESS.md marks bike physics, AI racing, contact, combat and traffic as "oracle-verified (scenarios …)".
- The full regression gate passes.

### October 14 car-contact continuation

Task 5/6 translation correction: `contact_car` now continues through the
original separation/cleanup tail after a knock-off, guards only its steering
kick with `local_21`, and floors reverse contact speed at -1000. Synthetic
`car_contact` covers both contact classes; REA corroborates `0x004049c0`.
This does not complete either task: the current solo traces still have the
recorded grid/contact divergences and fresh SIM captures remain prerequisite
for frozen-pool initialization evidence.

### October14 capture prerequisite recovered

Fresh SIM capture is no longer blocked by the old two-tick/frame-regression
fault: a missing original billboard forward and unbounded actor-record
accumulation were fixed, with actual-hook regression and successful bounded
900/300-tick captures. SIM frame limits count dumped ticks. Declared frozen
setup fields now replay once with strict identity/completeness checks.

Task5/6 acceptance remains open. The new capture still diverges at210, but
now proves frozen scheduler dt4 versus native1. Next: record +0c/+10/+14/+18
and translate `0x00437e60` metadata cadence even with NULL callbacks, then
rerun the same trace before progressing beyond the first divergence.

### October 15 scheduler-metadata dump (Task 6 fix order #1c)

The oracle dumps per-tick scheduler state without changing any call site:
`sched.<addr>.next/last/interval/dt` for type-4 nodes (address-keyed like
the `ring.%x` car slots; the first-frame `nodeaddr` block maps address to
grid position) plus `+0x0c/+0x10/+0x14` in the slotted-rider `extra[]`
window (existing `+0x18` kept). REA 6.4.0 corroborates `0x00437e60` and
`0x00438170`; see the capture spec in `REVERSE_ENGINEERING.md` and the
`sched900` recipe in `SIM_SCENARIOS.md`. Native translation is intentionally
deferred until that capture lands: no interval is hardcoded in `src/race.*`.

### October 15 translation (Task 6 fix order #1c done for cadence)

Native models the frozen `0x00437e60` cadence (`Rider::sched_*`,
`sched_live`-gated stepping, capture hydration, dt into bike `+0x18` so
both `contact` directions push correctly) and solo replay detaches native
traffic to match the nulled pool. sched900 replay: first divergence tick
376 (was 210). Next cause isolated to the `0x0044dd90` spawn-timer lane
draw against the nulled pool -- Task 7 scope (timer state/rates/taps).

### October 16 human-branch proof (Task 5 Step 2a)

`player.demo_hold` is now dumped every tick and `RR_ORACLE_HUMAN=1`
selects the genuine player-control branch at race start (documented
seed-hook-pattern instrumentation). `sched-human-20261016` proves
`demo_hold=0` across 1431 ticks with real human inputs; the harness
infers `driver=human` with no override. First divergence is tick 2
(`ai_target_x` 18176 vs 0): the human path must maintain AI target
state. Five further human gates are demo-tuned natively (steer, skid,
move-push, sched-copy, wreck rule). Demo solo stands at tick 376
(spawn-timer lane draw, Task 7).

### Countdown timer integration (October 15 continuation, Task 7 Step 2)

The zero-distance hold history is now integrated: initial traffic ring
zones precede hold timers, hydrated frame 0 banks one timer tick, and solo
replay retains lane-pick draws against disabled placement. Demo sched900
now matches through tick 574 (was 375); first gap at 575 is brain target
`+0x168` identity resolution (reported original rider -1, native 13; the
original pointer is nonnull and the resolver uses current positions).
Human remains at tick 189 terrain
RNG. Legacy diagnostic signatures/allowlist and goldens are unchanged.
No Step 2/exit acceptance checkbox is closed: traffic-field replay and
full human parity remain required.

Next: resolve the tick-575 target identity/selection difference and expand
seeded terrain fixtures to all eight points, windows/slopes and CRT state.
See `agent-handoffs/countdown-timers-20261015.md` and
`agent-handoffs/terrain-next-20261015.md`.

### Stable replay pointer identity (October 15 continuation)

Tick575 was a resolver defect, not an original NULL target: map original
node addresses to initial hydrated native identities once, retaining that
map when positions change. Implemented with movement/alias regressions.
Demo sched900 now first differs at576 in target lateral brain+194
(-16384 vs29952), RNG matched. Human stays189. Next AI work belongs to
Task6 target selection; traffic-field acceptance remains open.

### Frozen type8 scan state (October 15 continuation, Task6)

Hydrated frozen halves now retain their own position/speed/slice under bike
contact. Same sched900 single-anchor replay first differs at584 in speed
(6188 vs6187), after the original576 half-target matches. Human remains189.
No golden or allowlist changes. Next: trace the one-unit speed arithmetic
and finish independent seeded all-eight-point terrain fixtures.

### Seeded terrain component gate (October 15 continuation, Task5)

Expanded original fixtures now compare all8 hidden point windows/slopes,
iterator position/record and CRT state.960 states plus18 terminal states
match exactly across6 seeds and forward/reverse/roundtrip paths. The
original race-walker hidden-state/init interleaving remains unproved;
next capture pairs front/back walkers from setup through189. No speculative
hill-step correction was made. Fresh600-tick demo retains the584 speed
gap and captures original throttle7352/force26 for the ramp investigation.

### Player-row identity and NULL-slice scans (October 15 continuation)

The584 speed gap was initialized rider mass, not integration rounding:
original row0 produces cap9188/rise918/fall612; character mapping row8 gave
9160/916/610. Factory explicit row selection landed15ebc38, with sentinel
validation and row0/row8 regressions60bae61. Demo600 matches all compared
fields; sched900 next differed735 in overtaking.

Original00449ce0 rejects NULL self+2c and skips NULL candidate+2c. Captured
frozen bikes/halves can independently have NULL slices; native scan had
manufactured valid route samples. Hydration now retains both memberships
and scans apply those guards without excluding all frozen halves. Same
single-anchor sched900 first differs893 in RNG, with all compared state
through892 matching. Next diagnostic is the original contact candidate
byte+c0 flags gate, not a guessed change to terrain arithmetic.

Fresh declared human terrain trace identifies a still earlier setup gap:
frame0/tick1 front point0.end original132/native133. The older189 RNG gap
was merely the earliest difference visible in the coarser trace. Seeded
component parity does not prove race setup ordering; strict human acceptance
still fails. No golden/allowlist/reanchoring changes. See
[research](../agent-handoffs/codex-sim-research-20261015.md) and
[integration](../agent-handoffs/codex-sim735-integrate-20261015.md).

Ring setup window walks (October 16, [handoff](../agent-handoffs/ring-terrain-20261016.md)).
The tick-1 terrain gap was missing setup draws, not hill arithmetic:
`0x00453e60` runs `0x00453b60` at every split and merge, walking a scratch
walker 0x51 slices through both halves, and its terrain iterators draw.
Napa has 3 splits and 3 merges (9,089 draws from the old start). Ported in `TrackRing`;
`ring-probe` then finds the ring setup at srand(1) with **zero** pre draws
and frame-0 walker terrain exact, ending 16 draws before the green anchor
(the old 9,098/11 split absorbed the missing draws). `terrain-human200`
now replays 200 ticks identical including the walker trace;
`sched-human-20261016` advances **189 -> 371** (`rider.0.velocity` 40 vs
296, RNG in sync). Demo sched600 (600 identical) and sched900 (893) are
unchanged; legacy pinned diagnostics differ only in the closure line.

Ring-built terrain slices (October 16). Physics read off-road ground from
the scene's isolated-stream terrain; the ring's slice builds now supply it
(`0x00451910`). Human replay advances **371 -> 702** (RNG at a speed drop
3956 -> 992 at lateral −271123). Demo replays unchanged.

Solid roadside objects (October 16). Tick 702 was a BORR tree hit; with
the `0x004373f0` sweep, frame collision boxes and the type-1 static contact
ported, **`sched-human-20261016` replays all 1431 ticks identical** in every
compared field with one RNG anchor. Strict acceptance remains unmet on
field coverage (264 golden-only fields) only.

Strict acceptance met (October 17,
[handoff](../agent-handoffs/coverage-20261017.md)). The extended capture
`sched-human-cov2-20261017` adds the riding-animation block, stamina and the
nitro globals; the `node.N.*` scheduler list now compares as a per-frame
multiset of (draw `+0xdc`, position) with rider parking (`0x00406940`) and
the frozen-pedestrian semantics ported. Replay is `1431 ticks identical`
with **0 golden-only and 0 uncovered**; `tools/verify_sim.py --mode
acceptance` (new defaults: this fixture, driver human) reports
`acceptance_met: true`. Remaining outside this capture: live-AI
parking/waking, the 70 unmodeled-but-constant original words, and the
traffic-field scenarios.

### Live rider scheduler (October 17, Task 6)

`src/race_schedule.cpp` now ports `0x00438170(0)`/`0x00437e60` for bikes and
rider halves (own cadences, the interval LOD), parking `0x00406940`, the
parked virtual walk and tracker wake of `0x004060b0`, and the `0x00407610`
pace band. `RaceConfig::live_schedule` (default true) runs it for every
non-solo race, so the native game path carries the original cadences; the
solo freeze and strict acceptance are unchanged (`verify_sim.py --mode
acceptance` exit 0, `1431 ticks identical`).

The non-solo harness is `tests/field_replay.hpp`: slot identity follows the
scheduler list walk (the proxy's `rider.<k>` contract), parked bikes become
`node.*`/`sched.<addr>` keys, and row 9..14 brain levels load from the
capture. On the new 2400-tick `napa-field-l1-20261017` capture the replay
matches through tick 226 (countdown included) with rng in sync — tick-1
parkings, their tick-9 advances and the spine of names/positions all equal;
the first divergence is the tick-226 free car's scheduled move, which is
Task 7 scope (car cadence and `0x0044e710`/`0x0043adc0` dt physics).
Diagnostics that exclude cars/pedestrians and load the captured car states
compare to frame 623 (tick 624) and first diverge at tick 625
(`rider.1.lateral`), past the tick-337 parking. Remaining Task 6 items: the
interleaved walk of pedestrian/car nodes, tracker split/merge hand-over,
the crashed-bike move slot, and the `napa-field-l5`/`napa-fight`/
`napa-contact` scenarios.

### Car scheduler cadence and dt motion — October 17, 2026

Plan 10 Task 7 continuation from `70a8c04`: cars now retain the original
scheduler last/next/interval/dt words (template interval 10) and choose the
`0x00437e60` type-0x10 distance/relative-velocity LOD after moving. The race
passes the player's forward velocity, and scheduling uses the ring window
before the driver's camera advance. Timer-spawned cars first run on the
following tick. Think `0x0044e710` / `0x00449880` / `0x004498c0` uses dt for
acceleration and stop-line lookahead; move reuses `integrate_speed`
(`0x004390c0`) and `advance_actor_motion` (`0x00439920`) with the car template,
surface drag and sine table. The proximity-based per-move horn RNG draw was
removed: `0x0044efa0` is a contact callback. The drift table is four signed
bytes, corrected from the former word indexing.

On the unchanged `napa-field-l1-20261017` trace, countdown (5) and ticks
6..307 match every compared rider/brain/half/scheduler/node-list word and RNG.
The harness additionally compares captured free-car position/lateral/speed
triples as a multiset, without hydrating cars or excluding fields. Tick 227
now matches: dt 11, think speed 341, post-drag speed 231, position 34828
(+12). Tick 308 first differs on a pedestrian node, original 13604/native
13605; car position 35240, lateral 32000, speed 1431 still match.

This completes the requested tick-227 unblock, not Task 7 or live-field
acceptance. Interleaved car/pedestrian scheduler nodes, the pedestrian
boundary, car lane changes, split-road teardown, curved-route heading
transport, wall/contact response, and later scenarios remain open. No new
capture or golden edit. See [handoff](../agent-handoffs/car-cadence-20261017.md).


### Live scheduler/contact continuation — October 17, 2026

Continued from `f987c86`. Pedestrian cadence now uses the pre-driver ring
camera, type-0x20 vertical motion stays grounded, and live teardown marks
then unlinks on the following tick. Frozen solo node lifetimes remain at
their measured behavior. Cars run in position order between rider nodes,
before contacts and driver timers. Car contact eligibility follows the
ascending outer node's due tick; live separation uses the shared recovered
separator and persistent car motion words.

`napa-field-l1-20261017` now matches through tick **787**, including the
337 parking and 765 wake, RNG and car motion, with no hydration or comparison
exclusions. The 308, 625, 647, 685 and 761 blockers are resolved. The next
failure at 788 is an AI state-3 combat mismatch (two missing RNG draws and
rider-12 steering), consistent with the still-omitted `0x00409460` /
`0x004095d0` branch. Tasks 6/7 and full field acceptance remain open;
pedestrian/controller node interleaving, full mixed contact-list ordering,
car lane/split/wall behavior and combat scenarios are not established by
this prefix. See [handoff](../agent-handoffs/ped308-20261017.md).


### Full live-field case passes (October 18, Tasks 6/7)

`napa-field-l1-20261017` now free-runs all2400 ticks with every reliable
captured state and RNG equal. Corrected-schema companion
`napa-field-l1-schema2-20261018` additionally compares valid car metadata,
every AI animation and the complete relationship table; it also passes2400.
Both CTest regressions now require a complete match when their local captures
are present; the former field known-divergence rule is removed.

Recovered fixes:409460/409510/4095d0 attack branch;4096f0 descriptor OR frame
hit event and408d50 relationships; original queues and stamina regeneration;
pre-sort car scan keys and signed108 velocity;44efa0 car response/RNG;
37c10 deferred car teardown;437750 marking callbacks and33-slice lazy box
preparation;408ad0 parked-target clearing;407f30 pre-driver animation camera.
See SIM_SCENARIOS for hashes, recipe and the legacy metadata schema defect,
and VERIFICATION for final gates. No accepted golden was changed.

This completes the requested full live-field capture. The task checkboxes
above remain open for the other specified scenarios (level5, dedicated fights,
steals and isolated traffic) and unmeasured police/split/mixed-actor branches.
Do not infer all-game or all-course parity from this single captured route.


### City pedestrian RNG (November 3)

`city-cops-parked-20261026` now matches RNG, ring-window riders and the cop
to its 4552 end, with the original arrest on 4552 asserted. Two new
2400-tick City captures match end to end. The fixes are pedestrian rules:
resources load only for pedestrians inside the drawn slices, and the
road-clear scan follows `0x00449ce0` (slice-holding nodes, car `+0x108`).
This covers City at level byte 2 (`RR_ORACLE_LEVEL=3`) with a parked player only. Moving-camera City races,
other levels and the remaining courses still need captures. See
[handoff](../agent-handoffs/city-rng-20261102.md).

### Remount rejoins the half on the remount tick (November 4)

`sim-napa-traffic-field.tsv --driver=human` diverged at tick 2279 on one
0x004463e0 shadow node (original 75067, native 75033) while the rider
position matched: the original's node.28 jumps exactly when rider slot 3's
crash flag clears 3 -> 0. `Race::crash_recovery` ran `remount_crash_actor`
but not the `0x004067d0` copy-back that the `0x00404560` remount tail calls
(export line 37, gated on the just-zeroed 0x2f4), and the bike node was not
due that tick. One-line fix: `live_standings(r)` in the remount branch
(matching the original call order; the non-live path is unchanged because
`update_halves` rewrites the same words with no RNG). New `crash-test`
block fails pre-fix and passes post-fix. Results: traffic-field 2279 ->
**4381/4381 identical** (Task 7 traffic-field scenario now fully replays);
steal-farm 3627 -> 4059, steal-farm-l1 2837 -> 5697; contact/fight/steal
first divergences unchanged; field/city/acceptance goldens unchanged. See
[handoff](../agent-handoffs/remount-mirror-20261104.md).


### November 4, 2026: split-ring continuation checkpoint

Dual-half runtime walkers, cached window reentry, commitment and lane offsets
are implemented with synthetic forward/back/snapshot coverage. Static route
opposite identity, intersection roadway selection and the ground right-forward
link are corrected. Single-anchor `sim-napa-steal-farm` now matches through7872;
first gap7873 is body_f4 original1302/native1032. Strict human1431/1431 remains
exact. No all-split or full-race acceptance claim: arbitrary route choices,
complete neighbor topology and other-course/human cases remain open.
[Evidence and next action](../agent-handoffs/split-ring-latest-20261104.md).
