# Binary evidence and next work

## Reference binary

`cdrom/ROADRASH/ROADRASH.EXE` on the supplied 1997 rerelease disc:

- SHA-256: `73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`
- 541,184 bytes, PE32 / i386, image base `0x00400000`.
- Entry RVA `0x583c0`; timestamp August 23, 1996.
- No debug directory; symbols and line numbers stripped.
- 1,585 Ghidra-discovered functions exported; no export timeouts/failures. Function boundaries and inferred types are not all manually validated.

Work uses this original disc executable, not the modified files in the pre-existing `android/` or Wine prefix outside `native-port/`.

## Resource container

Observed in 13 files, including bike specifications, five courses, five car resources, families and global data. All integer fields below are little-endian. Tags appear reversed as ASCII on disk because they were stored as integer FourCC values.

| Offset | Field | Evidence |
| --- | --- | --- |
| `0x00` | `CRSR` magic (integer `RSRC`) | All 13 containers |
| `0x04` | Header value `0x30` | Observed; not yet assigned full semantics |
| `0x0c` | `LBTR` tag (integer `RTBL`) | All 13 containers |
| `0x10` | Absolute directory offset | Header reader at `0x00414ca0`, confirmed with assets |
| `0x14` | Directory byte extent | Header reader at `0x00414ca0` |

At the directory offset: `LBTR`, a length word, entry count at `+8`, and a runtime link at `+12`. Entries start at `+16`, with a 32-byte stride. Original runtime fixup loop `0x00415360` confirms the count and stride.

| Entry offset | Field |
| --- | --- |
| `+0` | FourCC integer |
| `+4` | Resource ID |
| `+8` | Absolute payload offset |
| `+12` | Payload byte length |
| `+16` | Flags; original loader tests bits `0x100` and `1` |
| `+20` | Runtime payload pointer/cache slot |
| `+24`, `+28` | Not yet identified |

Native code currently returns validated directory metadata. It does not implement nested payload decoding, compression, caching, or original runtime fixups. It deliberately treats file offsets and host pointers as different types.

`BIKESPEC.RSC` contains 15 `SPEC` resources, IDs 1–15, each 356 bytes. Original function `0x00405e30` explicitly loops over those 15 IDs; `0x00405ea0` consumes the records and applies mass-dependent calculations. Field meanings beyond the observed offsets need further reconstruction.

`NAPA.CRS` contains two `CEL ` resources plus `NOD ` and `SGS ` resources. The segment payload starts with bytes `SGSR` (integer tag `RSGS`), and the node payload starts with bytes `DONR` (integer tag `RNOD`).

## Course geometry recovered

Native reader: `src/course.cpp`. Validated against all five courses (`rr-course`): every node reachable, node count matches header, all lane intervals ordered.

- `SGS ` payload: `SGSR` chunk (16-byte header, count at `+8`), one 52-byte row per section. Each row points at per-section chunks, each walked by one iterator that `0x00452cb0` sets up in the traversal half. Tags below are as stored on disk; the in-memory integer tag is the reverse (e.g. `RPTH`).

| Row offset | Chunk tag (disk) | Sections having it | Iterator (half offset, init / step) | Status |
| --- | --- | --- | --- | --- |
| `+0x04` | `HTPR` path | 109 | `+0x2c`, `0x00451970` / `0x004519f0` | Decoded |
| `+0x08` | `BORR` | 108 | `+0x84`, `0x00451b60` / `0x00451e20`; per-record flag word unpacked by `0x00451a90` | Not decoded |
| `+0x0c` | `MSRR` | 109 | `+0x70`, `0x004522a0` / `0x00452580` | Not decoded |
| `+0x10` | `DLBR` / `LIHR` / `NTMR` (building / hill / mountain) | 34 / 64 / 11 | `+0xb4`, `0x004510c0` / `0x004516e0`; `0x00451910` dispatches on the tag and sets sub-slice byte `+0x3c`, the projection case in `0x00444630` | Type mapping traced (below); payload not decoded |
| `+0x14` | `DLSR` sides | 109 | `+0x38`, `0x00452a00` / `0x00452b70` | Decoded; widths rendered, heights decoded but unused |
| `+0x18` | `NALR` lanes | 109 | `+0x10`, `0x00452820` / `0x00452920` | Decoded |
| `+0x1c` | `DZHR` | 109 | `+0x1b8`, `0x00452060` / `0x00452230` | Not decoded |
| `+0x24` | `CESR` | 97 | `+0x5c`, `0x00452610` / `0x00452690`; `0x00452710` derives sub-slice flags | Not decoded |
- `HTPR`: 16-byte header, then two signed bytes per sample: **bend** then **rise**. Header `int16` at `+0xc` is the running elevation at the section start, `+0xe` at the end (`0x00451970`). In 97 of 109 sections, start + Σrise = end; the other 12 differ, so the header end value is not trusted.
- **Rise is a height delta** per sample: `0x004519f0` accumulates it once into elevation.
- **Bend is curvature**: the projector (`0x00443d70`) adds bend into a lateral slope, which then adds into lateral offset. Split tracking (`0x00453030`) likewise double-integrates the bend difference between branches.
- `NALR`: 20-byte records `{start, end, left0, right0, left1, right1 (bytes), Q16 left slope, Q16 right slope}`. Walked by `0x00452820`/`0x00452920`; absent table means width `0x200` (two lanes each side, `0x004528f0`). `0x00454240` converts lane units to world units as `trunc(q8 × 250 / 256)`: a lane is 250 world units.
- `DLSR` (sides): 16-byte header (count at `+8`), then 36-byte records:

  | Offset | Field |
  | --- | --- |
  | `+0`, `+4` | Start and end sample |
  | `+8`, `+9` | Pair A (left, right) at start |
  | `+10`, `+11` | Pair A at end |
  | `+12`, `+13` | Pair B (left, right) at start |
  | `+14`, `+15` | Pair B at end |
  | `+16`, `+17` | Two bytes copied to sub-slice `+0x14a`/`+0x14b` (observed 0, 64, 128, 192, plus a few odd values) |
  | `+20`…`+32` | Four Q16 per-sample slopes: A left, A right, B left, B right |

  Interpolation and stepping match `NALR`. Values are `byte << 16` and are read back `>> 16` (`0x00452b00`), so they are plain world units.
  - Pair A widens the road: `0x00454240` sets sub-slice `[0x3a] = left edge − A.left` and `[0x3b] = right edge + A.right`. `0x00444630` projects these to screen `+0x60`/`+0x68`. Pair A is therefore the shoulder width.
  - Pair B is a height: for sub-slice type 0, `0x00444630` sets rows `+0x54`/`+0x5c = surface row − B·S`.
  - Checked on all 109 sections (389 records): extent = 16 + 36·n everywhere; all 261 interpolating records' slopes equal `(end − start byte)·65536/(end − start sample)`; 274 of 280 records start where the previous ended.
  - Common widths: 224 (791 bytes), 255, 200, 250, 150, 112. Heights are mostly 0, then 20, 18, 25, 15, 10.
  - The meaning of the flag bytes at `+16`/`+17`, and how type ≠ 0 sub-slices use pair B, is not yet established.
  - Native reader: `course.cpp` walks `DLSR` into `PathSample::shoulder_*`, `height_*` and `side_flags`. `road.cpp` projects the outer shoulder edges (`RoadSurface::shoulder_left/right`), and the viewer draws them. Heights are carried but not drawn, since the original applies them only for type 0 sub-slices.
- **Interval tables (`NALR`, `DLSR`) are walked exactly as the iterators step them** (`walk_intervals` in `course.cpp`).
  - Values are seeded from record 0's start bytes at sample 0.
  - Stepping to sample `j`: `j < end` adds the slopes, `j == end` loads the end bytes, and past the end the next record is entered only when `j` equals its start, keeping the current values.
  - This replaced an earlier closed-form lookup that reset to a record's start bytes. The two differ on 6 samples (HIWAY section 6, whose first lane record starts at sample 1); the walker matches `0x00452920`.
- Terrain type chunk (row `+0x10`) → sub-slice `+0x3c` (`0x00451910`):
  - `RBLD` (building) → type 0 (`0x004517b0`). This is the only type that uses `DLSR` pair B heights, as rows `+0x54`/`+0x5c` in `0x00444630`.
  - `RMTN` and `RDWD` → type 1 (`0x00451710`), which builds wall/cliff profile fields `+0x14c`…`+0x160` from tables in the chunk.
  - `RHIL` → type 4 (`0x004517e0`), with three control points per side (`+0xf0`…) and 0–31 shading gradients.
  - Types 2/3 are assigned elsewhere (the split-road handling in `0x00454240` uses 3).
  - Each type copies two bytes per sample into `+0x148`/`+0x149`, probably colour or texture selectors; not yet confirmed.
- `NOD `: `DONR` header (size at `+4`, node count at `+8`, root at `+16`). Kinds 0 (road, section index + length), 1 (split), 2 (merge), 3/4 terminal (`0x00447b80`).

## Road projection recovered

Native port: `src/road.cpp`, tested by `tests/road_test.cpp`.

- Road samples live in a 128-slot ring (`0x0049e820`), 80 samples ahead and 48 behind the player, filled by `0x00454c80` → `0x00454240`.
- `0x00443a00` → `0x00443d70` projects `0x21` (33) slices ahead of the camera position `0x4bc7e4` (Q8 samples). Initial state for fraction `f`: `dx = trunc(bend·(256−f)/256)`, `y = −((f·rise) >> 8)`, `x = 0`, `z = 256 − f`.
- Per slice (`0x00444630`): `S = table[z/2]`, `screenY = (camY − y)·S + 90<<16`, `screenX = (x + edge − camX)·S + (320/2)<<16`; then `z += 256`, `x += dx`, `y += rise`; the caller adds the next sample's bend to `dx`.
- View space is fixed at 320×240 (`0x46667c`/`0x466678` are never written) with the horizon on row 90.
- `S` is a static, non-linear Q16 table at `0x0046f098`: 4,352 non-increasing entries, flat 163,840 up to depth 276. The port reads it from the user's `ROADRASH.EXE` and rejects other binaries.
- Camera: `0x00437d80` copies `0x4b7ee0` into `0x4bc7d0`. Height is bike `+0x24` plus a cinematic offset, minimum 150 (`0x96`), and race init (`0x0044b250`) uses 150. Position is one sample (`0x100`) behind the bike.

## Splits and merges recovered

Native port: `make_route` in `src/course.cpp` (traversal) and `project_road` in `src/road.cpp` (projection).

- **Node fields:** a split (kind 1) has outgoing roads at `+8` (half 0) and `+12` (half 1) and the incoming road at `+16`. A merge (kind 2) is the reverse. `+0x14` is `0x3c00` on disc, but `0x00453b60` rewrites it to `0x5100` (81 samples) at load, after walking 81 samples from the node.
- **Lookahead traversal** (`0x00453030`, instances `0x49ee30` front and `0x49ea20` back) carries two cursors (`+0x28`, `+0x21c`), a primary half `[1]`, and the separation of half 1 from half 0: slope `[5]` and lateral offset `[6]`.
  - Forward step in a dual stretch: move both cursors, `[6] += [5]`, then `[5] += bend₁ − bend₀` at the new samples. The backward step is its exact inverse.
- **At a split:** `[5] = bend₁ − bend₀`, `[6] = 0`. Half 0's centre shifts so its left edge matches the incoming road; half 1's so its right edge matches (`trunc(Δq8·250/256)`).
- **Collapse:** once the rider has committed (`0x00476cd4`, set by `0x00439390` when the bike leaves the overlap), the other road is dropped (`0x00452fd0`) when the primary passes local sample 81 or enters a new road node.
- **Merge snapshot:** `0x00453b60` walks 81 samples backward from each merge and stores the traversal at `node+0x1c`. When a single-road traversal is 81 samples short of a merge (`0x00453030`, event 6), it restores that snapshot, so the other road converges and the separation reaches zero at the merge. The kept edge is realigned when the roads join (event 1).
- **Projection** (`0x00443d70`):
  - Both halves integrate their own bend and rise.
  - When the camera starts inside a dual stretch, the other half starts from the slice's `[9]`/`[10]` (the traversal's `[6]`/`[5]`).
  - Ahead, a split's second road starts as a copy of the current road; a merge window's is offset by the stored separation.
  - Vertical separation is never tracked. The branches have independent height profiles (NAPA branches differ by up to 3,369 units within 40 samples of a merge), so the other road is drawn relative to the camera's road height.

Validated on all five courses (`rr-course`): 11 split/merge pairs per route pair, every merge window restored, separation zero one sample before every merge.

**Preview simplifications:**
- The rider is assumed already committed to the selected branch, so the second road is dropped 82 samples after a split. In the game this depends on the rider's lateral position.
- When a collapse keeps half 1, its next bend comes from the kept road. The original reads a stale ring sub-slice there.
- The merge snapshot uses the last sample's lane width where the original uses the terminal lane record.

**Not yet ported:**
- Shoulder heights (pair B) and shoulder blending at splits (`0x00454890`)
- Terrain profiles for buildings, mountains and hills (`0x00451710`, `0x004517b0`, `0x004517e0`)
- Road/verge colours and textures
- Horizon placement, `CEL ` scenery and sprites, occlusion clipping (`0x4c10c8`)
- Road types from the `+0x10` chunk (`0x00451910`), and the `BORR`, `MSRR`, `DZHR`, `CESR` chunks
- Bike-driven camera x and height

## Riders and bike physics recovered

Native port: `src/bike.cpp`, `src/race.cpp`.

- **Pair construction:** `0x00405600` creates the racers through `0x00405990`. Each is a **bike** object (table `0x00463ba8`, AI `0x00463ad8`) plus a **rider** object (`0x00463c10`, AI `0x00463b40`), linked through `bike+0x2fc` and `rider+0x1c4`.
- **Grid placement chain (REA-confirmed October 12, 2026; plan 10 Task 6 trace input):** `0x00405990` places each pair at `*(param_3 + 0xcc) + param_4`, where param_3 is the grid slot (`*(slot + 0x14)`, chosen in `0x00405600` from the `0x0049e820` table by the `0x00464298/0x00464218` start-grid words) and param_4 is the grid lateral offset. `0x004387a0` → `0x00438930` then set bike lateral `+0x1c = *(*(slot + 0x28) + 8) + grid` and copy `*(slot + 0x28) + 8` words into `+0x1c..+0x30`; `0x00439b10(slot, type, lateral)` resolves the sub-slice (type switch on slot `+0x3c`: 0 flag-gated `+0x140/+0x144`, 1 interpolated ramps, 3 edge select, 4 lateral-blended 999/lerp, 5 gate). Pinned REA 6.4.0 (Ghidra 12.1.4) pseudocode for all three functions is whitespace-identical to the project's `tools/decompile.sh` exports (`analysis/oracle/rea-005990-placement.md`, `rea-4387a0-subslice.md`, `rea-439b10-slice.md`; disc-derived, git-ignored). This closes the sibling session's open "`*(slot+0x14)+0xcc` placement field" item at the trace level; replay seeding of frozen laterals from the frame-0 dump is still the Task 6 stopgap until the native ctor consumes absolute sub-slice centres.
  - Rider-build RNG audit (October 12, 2026; plan 10 Task 5 single-anchor input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00405600` rider build (5363 chars), `0x004078f0` personality copy (1033) and `0x00441750` rand wrapper (111). Setup draws in order: `0x004078f0` makes 10×5 = 50 draws (`rand % 0xd − 6` personality jitter); `0x00405990` draws lane `rand % 3` per AI pair (**corrected Oct 17**: every pair including the player draws when offline, `DAT_004c5b00 == DAT_004c5af9 == 0`; first post-ring draw %3 = 1 = the human capture's player brain lane); `0x00405600` tail draws `rand % 100` once (`0x00402e10`); `0x00441750` returns live `rand()` except mode 3 (`DAT_004b7ecc`-gated replay constant). The 11,461-call pre-green stream is therefore countable per pair — a future harness can reproduce setup exactly instead of anchoring at green. Files: `analysis/oracle/rea-405600-riderbuild.md`, `rea-4078f0-personality.md`, `rea-441750-rand.md` (disc-derived, git-ignored).
  - **Offline field count correction (October 18, 2026):** the unmodified disc EXE
    SHA-256 `73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`
    matches `analysis/inventory.json`. In `analysis/original-disassembly.txt`,
    `0x00405614..0x00405621` tests both network flags; offline jumps to
    `0x0040564b`, setting `DAT_004642d4 = 16`. The player is built separately;
    `0x00405867..0x00405876` advances AI row 1 through 14 (`row < count-1`).
    **Thrash/attract: 14 opponents, 15 racers. Big Game: 13 opponents, 14 racers**:
    `0x00405783..0x004057a4` checks mode 2 and signed-byte `DAT_00467a6c`,
    clears both racer-table entries for that row and skips its constructor.
    The selected character maps to rows 1..8 via `0x00415fc0`. Thus the earlier
    blanket statement that every offline race builds fourteen opponents needs
    this Big Game exception. The native CLI quick race uses the Thrash field;
    explicit `--opponents 0..14` is a native diagnostic override. This is static
    constructor evidence, corroborated by the existing fourteen-opponent field
    replays; no new live mode-selection capture was taken.
  - **Full-field names:** `0x00407990` sets spare-row personality byte `+0x165`
    to `row+1` (10..15). `0x00402d60` fills name-table indices 10..15 from
    six consecutive string slots, beginning at the course's `0x004635b8` entry
    (CITY 320, HIWAY 326, MEDLY 332, NAPA 338, CANYN 344). `0x00402e10`
    instead selects the six pointers at `0x004635d0` for roll 42 or
    `0x004635e8` for roll 69. `0x00446bf0` (HUD) and `0x00437860` (finish)
    both resolve that personality byte, not the compact native rider index.
  - Rider build after the ring (October 17, 2026; spawn-20261017): offline `DAT_004642d4 = 0x10`, so 0x00405600 builds the player and opponents 1..14 through 0x00405990 (lane `rand()%3` each, 15 draws), then `rand()%100` for 0x00402e10 (42 → "Honest Phil" set, 69 → "Veldaar" set, else the default rider names via 0x00402d60): the 16 draws between the 0x004552e0 ring setup and green. 0x00405c80 (per rider): stamina max/current from the row, weapon words, brain +0x160 = 0, byte +0x164 = 0, +0x168..+0x180 = 0, bike +0x2f0 = +0x2ec, 0x004091f0 straight target, **bike +0x28 = brain +0x194**, bike +0x30c = 600, +0x310 = ((level·5 + 0x2d)·200 >> 8)·600, +0x314 = 0, half +0x1f8 = 0x00407410, +0x1fc = 0, +0x1f4 = half +0xc, then 0x004082f0. 0x004082f0 reads the player half's +0x1fc word (DAT_004642d8 NULL → 0xf); during the player's own build its scan finds no riders. 0x00438930/0x004387a0 build the half from table 0x00463c10 (bike 0x00463ba8; opponents 0x00463ad8/0x00463b40, which differ only in interval and think): flags 0 → 4, +0x160 = 37, +0x180 = 192, +0x194 = 25, +0x1a4 = 0x004046b0, +0x1a8/+0x1ac = 1, +0x1b4 = 74. 0x00438a80 does not clear a pool slot, so unwritten words keep the previous contents (zero in a fresh process). Brain +0x184: no instruction in the EXE writes it. 0x00446060 (both blocks, from 0x00405990): 0x0043ae10(2), 0x0043ae60(2), +0xc = 0, 0x0043ae90.
- **`SPEC`** (`BIKESPEC.RSC`, 15 × 356 bytes), applied by `0x00405ea0`:
  - byte 0 → `+0x1bc`; dwords 1–5 → `+0x168`, `+0x16c`, `+0x170`, `+0x1b4`, `+0x1b8`; dword 6 = mass; dwords 7–82 → `+0x1c4…+0x2f3`
  - the gear table is at `+0x1d0 + 16·g` (drive factor, upshift speed, downshift speed), 7 gears
  - derived values: maximum throttle `+0x260` and brake `+0x27c` scale with mass and the rider value; throttle/brake ramp rates `+0x264`/`+0x268`/`+0x280`/`+0x284`; steering limit `+0x29c = 0x2e00` and rates `+0x2a8`/`+0x2ac`
- **Controls:** `0x0044f800` combines `0x0044fa80` (keyboard via the `0x004c5d90` key → action table) and the joystick into `0x004b7ed0`. Bits in `0x00476c00` order: `0x1` accelerate, `0x2` brake, `0x4`/`0x200` steer, `0x8`/`0x400` lean, `0x10` nitro, `0x20`/`0x40`/`0x80` attacks, `0x100` dismount. `0x00434660` applies them through:
  - Drive leaves REA re-confirmation (October 12, 2026; plan 10 port input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0040adb0` throttle (1336 chars: wrecked/cop-hold → brake fallback, clamp to `+0x260`, `+0x264/+0x268` up/down ramps on `+0x240`, gear-factor `+0x1d0[gear]·drive>>16` into `+0x114` with nitro `0x00465a2c` scale), `0x0040aea0` brake (1056: stopped → 0, cop `0x180/0x100` scale, `+0x284/+0x280` ramps, reverse-sign `+0x114`) and `0x0040ad30` steer-target (494: digital ±0x20 or joystick `DAT_004b7e7c−0x20`, analog `param<<5`). Files: `analysis/oracle/rea-40adb0-throttle.md`, `rea-40aea0-brake.md`, `rea-40ad30-steertgt.md` (disc-derived, git-ignored).
  - Controls REA re-confirmation (October 12, 2026; plan 10/18 port input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0044f800` poll (615 chars: keyboard `0x0044fa80` unless `DAT_004c5b10`, joystick OR via `0x00476c00` table × `DAT_0049e65c`), `0x0040acd0` drive dispatch (308: 1 → throttle `+0x260`, 2 → brake `+0x27c`, else coast by `+0x240` sign) and `0x0040af50` steering (2375: `sin(angle)·speed/+0x2b0` target, ±(`+0x29c` + quarter when leaning) clamp, `+0x2a8/+0x2ac` ramp choice by shrinking-vs-growing, lean-scaled `+0x2d0/+0x2d4` sub-steps, `+0xf0 = steer·+0x2b0>>8`). Files: `analysis/oracle/rea-44f800-controls.md`, `rea-40acd0-drive.md`, `rea-40af50-steer.md` (disc-derived, git-ignored).
- **Integration, once per tick** (`0x0043a580`):
  - `0x00438c70` traction: grip from the surface table `0x0046dce8`, skid when demand exceeds grip, steering damping `0x0046dd08`/gain `0x0046dcf8`, rough-surface curve factors `0x0046dd28`
  - `0x004390c0` speed: `v += dt·(drive − drag − |lateral|/16)`, drag = aero·(v>>8)>>8 + surface drag `0x0046dce4` + rolling
  - `0x004391d0` gear shift
  - Integrator REA re-confirmation (October 12, 2026; plan 10/16 port input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00438c70` traction (5976 chars: surface-class grip, skid-vs-demand with `+0x15c` clamp, yaw from `sin(steer)·speed` table, low/high-speed steer taper at 4000/8000), `0x004390c0` speed (1488: drive−drag−|lat|/16 with aero/surface/rolling terms) and `0x004395e0` movement (4255: trapezoidal distance, curve-push via bend swept, lateral `+= dt·+0x10c`). No RNG in the tick physics — divergences are deterministic given the same inputs. Files: `analysis/oracle/rea-438c70-traction.md`, `rea-4390c0-speed.md`, `rea-4395e0-move.md` (disc-derived, git-ignored).
  - `0x0043a610` wall/terrain collision (not ported)
  - Physics leaves REA re-confirmation (October 12, 2026; plan 10/16 port input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x004391d0` gear shift (669 chars: clamp 0–6, upshift past `+0x1d4[gear]`, downshift below `+0x1d8[gear]`), `0x004394c0` bend sweep (1100: same-sample vs split-sample lateral/height accumulation into `+0x128/+0x130/+0x12c`), `0x0043a0c0` surface class (844: the 0/1/2/3 rule already documented below) and `0x0043a190` vertical (4870: crest launch, rebound, wobble). The tick-physics trace is now complete except the `0x0043a580` dispatcher itself (a straight-line caller). Files: `analysis/oracle/rea-4391d0-gear.md`, `rea-4394c0-bendsweep.md`, `rea-43a0c0-surface.md`, `rea-43a190-vertical.md` (disc-derived, git-ignored).
- **Surface class** (`0x0043a0c0`): 0 road, 1 shoulder (type 0 terrain), 2 rough, 3 beyond the shoulder.
- **Units:**
  - speed `+0xec`: upshift points 3,000–21,504, top ≈ 11,800–16,600 in our runs
  - position `+0x1c`: Q8 samples
  - lateral `+0x28`: world units × 256
- **Timing:** 60 ticks/s from the performance counter (`0x004163d0`), capped at 20 steps per frame.
  - Camera + AI think REA re-confirmation (October 12, 2026; plan 10/16 port input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00437c30` chase camera (1842 chars: split-sample centre blend, ±200/−200 clamp, crash lift/pull-back ramps `+0x300/+0x3e00`, height floor 0x96) and `0x00407bd0` AI rider think (2221: crash-state dispatch, attack-input gates, `0x00407f30` tail). `0x00434660` (player think) and `0x00406890` (rider move) are absent from REA's procedure database — valid code at `434660`/`406890` per `original-disassembly.txt`; port from the project exports. Files: `analysis/oracle/rea-437c30-camera.md`, `rea-407bd0-aithink.md` (disc-derived, git-ignored).
- **Grid:** `0x00464218`/`0x00464258` hold 8 (lateral, sub-slice) slots at samples 50/51 with lateral offsets 16384, −3616, −23616, −43616 (added to the road offset); used when the field is small. Level < 4 full-field construction (`0x00464088`, read from the EXE) places 15 pairs: the race field at consecutive samples 50–57 (slots 0, 9, 4, 10, 2, 11, 3, 12 by position) plus 7 extra bikes parked ahead at 21248–31488. Solo oracle captures freeze all 14 non-player pairs on the first step (`sim-kink-probe7`). Native two-row placement does not match; see the October 12 VERIFICATION.md entries.
- **Math helpers:** `0x0044f5a0` is a piecewise-linear atan2 (256 per turn); `0x0044f4a0` is a quarter-wave sine table at `0x00475768` (1,024 entries, Q15, 4,096 per turn).
  - Math-helper REA re-confirmation (October 12, 2026): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0044f5a0` atan2 (831 chars), `0x0044f4a0` sine table (398) and `0x0044a870` mip-size select (355). (Scene builders `0x0043c170`/`0x0044bd40`/`0x0041d260` have no decompile exports — documented from proxy fixtures instead; not REA-traceable.) Files: `analysis/oracle/rea-44f5a0-atan2.md`, `rea-44f4a0-sin.md`, `rea-44a870-mipsize.md` (disc-derived, git-ignored).

## Rider sprites, AI and combat recovered

- **Sprites:**
  - `0x00410160` loads `Data/Bikers/Template` (MIP, not decoded).
  - `0x00443610` loads, through `0x004436a0`, bob (326), cop (45), shadow (326) and shadow-C (45) frame sets from `Data/Bikers/Hi*` (falling back to `Lo*`). Frame layout: width byte, height byte, indices.
  - `0x00443820` copies a frame into a 256-wide sprite page, remapping each pixel through `0x0046e158 + rider*256`. Ids `0xde`/`0xdf`/`0xe0` select shadow frames.
  - Index 0 is transparent.
- **Animation (not ported):**
  - Each object has an anim struct at `+0x70`, with `+8` → a table of animations by code (`0x00474d78[type]`, filled at runtime by `0x00445a60` from `GLOBAL.RSC` `CANS` 1–2 and `ANIM` 1–4 through `0x0040b6a0`).
  - Frame records are 20 bytes: duration at `+0xc`, loop flags at `+0xd`. `0x0040ac70` sets the animation; `0x0043ae90` advances it.
  - Anim-advance REA verification (October 12, 2026; plan 10 Task 5/6 merge `36c03dc` + plan 11 Task 5): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0043ae90` advance (1355 chars: deadline/loop/frame stepping the merge's `advance_ride_animation` ports) and `0x0043ae10` set (535: code → slot lookup the merge's selection path uses). `0x00407a90` bike-anim think is absent from REA's procedure database (gap #8, scheduler-dispatched pattern; valid 1631-char export) — port from the project export. Files: `analysis/oracle/rea-43ae90-animadv.md`, `rea-43ae10-animset.md` (disc-derived, git-ignored).
  - Anim queue/loop REA verification (October 12, 2026; plan 11 Task 5 crash-anim input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0040aca0` queue-with-fallback (252 chars: `0x0043ae60` try, else `0x00464430[code]` fallback) and `0x0043ae60` loop-set (285: empty-slot or zero-frame guard, `+0x12` queue write). (`0x0040ac70` re-fetched identical a second time; duplicate file removed.) The crash bike/rider advance ports (`advance_crash_bike/rider_animation`) rest on these plus the confirmed advance/set. Files: `analysis/oracle/rea-40aca0-animqueue.md`, `rea-43ae60-animloop.md` (disc-derived, git-ignored).
- **AI personalities:** 10 records of `0x204` bytes at `0x00464558`, copied to `0x004c6b40`. Each holds per-level target speeds at `+0x1bc` (5 levels; e.g. rider 1: 99/115/130/150/185) and a lane preference at `+0x18c`. `0x004078f0` also makes 6 copies with ±6 random variation.
- **AI driving:**
  - Target point: `+0x194` lateral, `+0x198` = position + `0x3c0`, `+0x19c` speed.
  - Speed compare: `(v>>12)+(v>>7)+(v>>9)` against the target.
  - Speed/obstacle helpers (REA-confirmed October 12, 2026; all four whitespace-identical to project exports): `0x00409020` target speed = brain `+0x1a4`, quarter-reduced when bend-average `|0x00449b40(slice@0x800, 4)| > 0x30000` (`−((q/2·|avg|>>8)>>8)` curve taper, then `+q`). `0x00449a80(self, gear_v, lo, hi, target, tgt_v, max_dv, mode, bias, unk)` clamps target speed by gap/mode with the ±(max_dv) slew and lo/hi rails — the shared follow-speed choke used by cars and AI alike. `0x0044a070` lane-obstacle scan walks `0x00449e60` for the first node in the same lane (lane-of via centre formula) or same 0x100 position page. `0x00449b40(slice, n)` = sign-fixed mean of `+0xb4` over n slices (`>>2/>>3/>>4` fast paths for 4/8/16). No RNG in any helper. Files: `analysis/oracle/rea-409020-tspeed.md`, `rea-449a80-fspeed.md`, `rea-44a070-obst.md`, `rea-449b40-bend.md` (disc-derived, git-ignored).
  - AI callees (REA-confirmed October 12, 2026; all five whitespace-identical to project exports): `0x004090a0` (empty-scan curve follow): unless already `0x3c0` ahead of target, bend-average `0x00449b40(slice, 8)` — `|avg| < 0x30000` or gear-speed `< +0x19c/2` → `0x004091f0` lane line; else lateral = curve-scaled `(clamp(avg,0x60000)<<8)/0x60000 · halfwidth·0x100` signed by bend + `(edge + centre)·0x100 + halfwidth`, clamped to own lateral on the bend side, speed `0x00409020`. `0x004091f0` (lane line): lateral from personality lane pref `+0x18c` (0 centre, 1 right-half, 2 full-width average) + bend/8 + road centre `+0xcc` + `+0x190`, speed `0x00409020`. `0x004095d0` (attack start, side/weapon/variant): animation code from `0x00464518` (left) / `0x004644f8` (right) by side of target; victim `0x00408d50` relationship pings (1/0/3) gated on aggression-table delta; `0x0040ac70` sets the code. `0x004096f0` (contact hit resolver): gated on attack-frame flag `0x600000` + frame 2, victim via `0x00409400`, reach `0x00409c10`, victim-invuln/anim/cooldown gates, steal roll `(rand&3)==3` (`0x00441750`), then `0x00409ad0` + `0x00408d50` pings (2/4) + cooldown `(5−level)·6` + steer kicks `±(1000/2000/3000)` clamped to `+0x29c` (replay RNG: steal + `rand % (6−level)` shove rolls in order). `0x004092b0` (behind test): `self_pos + self_len < target_pos − target_len` (returns byte + `>>8`). Pinned REA 6.4.0 pseudocode (`analysis/oracle/rea-4090a0-nobody.md`, `rea-4091f0-lane.md`, `rea-4095d0-attackstart.md`, `rea-4096f0-hit.md`, `rea-4092b0-behind.md`; disc-derived, git-ignored). AI subsystem trace is now complete except `0x00409400` victim pick, `0x004095a0` weapon reach, `0x00409ad0`/`0x00409bd0` damage, `0x00408d50` relationship write.
- **Rider table** `0x00463c78` (`0x40` bytes per rider):
  - word 3 = rider value × 2
  - word 5 = bike/colour data
  - words 6–10 = weapon per level
  - words 12–15 = strength, base, maximum and current stamina
- **Contact:** `0x004368f0` runs from the scheduler. Pairs overlap when within the half-lengths `+0x178`, the half-widths `+0x174` and the heights `+0x17c`, and on the same road-half bits. `0x00436a40` separates them along the axis that was clear on the previous tick, scaled by mass `+0x194`, and records impulses `+0x184`/`+0x188`/`+0x18c` and contact flags `+0x19c`; then each object's `+0x1a4` handler runs (bike: `0x004049c0`).
  - Keymap + contact REA re-confirmation (October 12, 2026; plan 10/16 port input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0044fa80` keyboard map (868 chars: 256 scancodes via `GetKeyboardState`, `0x004c5d90` binding → `0x00465fb9` code → `0x00476c00` bit, `0x0044fa50` repeat hook), `0x004368f0` contact dispatch (1769: same-tick `+0x10` stamp + `+0xc0&6` live bits, `0x004373f0` near-camera cull, pairwise half-length/width/height + road-half test, `0x00436a40` separation + `+0x1a4` handlers) and `0x004049c0` bike contact (9952: rider/car/ped branches, impulse/sound/crash gates — the 11a car-contact port's source). Files: `analysis/oracle/rea-44fa80-keymap.md`, `rea-4368f0-contact.md`, `rea-4049c0-bikecontact.md` (disc-derived, git-ignored).
  - Separation + scheduler REA re-confirmation (October 12, 2026; plan 10/16 port input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00436a40` separation (12102 chars — largest confirmation to date: mass-ratio split along the previously-clear axis, impulse/flag records, `+0x1a4` handler dispatch), `0x004373f0` near-camera cull (3196) and `0x00437e60` scheduler dispatch (4565: think/move/slice calls, NULL-think parking that the solo-freeze proxy exploits). Files: `analysis/oracle/rea-436a40-sep.md`, `rea-4373f0-cull.md`, `rea-437e60-sched.md` (disc-derived, git-ignored).
- **Combat:**
  - Control bits `0x20` strike, `0x40` kick, `0x80` backhand.
  - Attack animation codes come from `0x004644f8`/`0x00464508`/`0x00464518`/`0x00464528` (weapon + variant × 4; left or right).
  - The hit lands on frames flagged `0x600000`.
  - Reach (`0x00409c10`): |dz| < `0x40`, |dx| < reach × `0x1000`, |dy| < `0x3000`.
  - Damage: max(maximum/base × strength, strength/2) × weapon multiplier / 256.
  - Stamina and bike condition: the maximum drops by damage×4 and the current by damage×64; kicks also wear the bike (`+0x2f0`) by damage/4 + 1. Either reaching zero knocks the rider off (`0x00403d60`).
  - Damage splits + side search (REA-confirmed October 12, 2026; all three whitespace-identical to project exports): `0x004050c0(bike, dmg)` — remote bikes (`+0x324 == −1` or non-matching `+0x328` char, or cop-bike without the player char) skip damage and return current stamina; else max −= dmg·4 (floor max/2), current −= dmg·64 (floor 0), `0x0043c9f0` HUD hook on player-char hits; returns current stamina. `0x004051e0(bike, dmg)` is the bike-condition twin (`+0x2f0` −= dmg, floor 0, same skip/HUD gates). `0x00449da0(half, mask, side)` alternates `0x00449ce0` backward/forward with a ±0x80 window, skipping same-lane nodes (`self_lane < node_lane == side || ==`), returning nearer by lane order. Pinned REA 6.4.0 pseudocode (`analysis/oracle/rea-4050c0-stam.md`, `rea-4051e0-bike.md`, `rea-449da0-side.md`; disc-derived, git-ignored). Task-6 trace backlog is now empty: every AI/combat/scheduler function named in the sibling findings has exact original text.

## Original image loading recovered

- `.RRI` files begin with JPEG/JFIF headers. Six menu files decode to 640×480.
- `0x0044c5d0` calls the raw indexed-image loader with `0xbe` × `0x40`: 190×64. `BIGMETER.BOB` is exactly 12,160 bytes.
- `0x0044c5f0` reads one byte per pixel and copies scanlines into locked DirectDraw surfaces. Low-resolution mode halves both dimensions.
- `0x0044c440` sets the high-resolution horizon row width to `0x2d0` (720) and splits each row into two equal halves. High-resolution horizon files are 230,400 bytes, giving 320 rows. The viewer displays the combined source image, not the original game's scrolling/cropping behavior.
- `0x0044c730` reads exactly 1,024 palette bytes. `0x00448f90` forces the first entry's RGB components to zero and constructs a half-brightness palette.
- `0x0041e710` converts the palette to BMP ordering by exchanging components 0 and 2, corroborating RGB-plus-flags ordering in the source palette.

## Function map

Names here describe observed behavior; they are not recovered original symbols.

| Address | Observed role | Native status |
| --- | --- | --- |
| `0x004583c0` | CRT startup calling `0x00448200` | Replaced by native entry |
| `0x00448200` | Windows startup, menus, race transition | Not ported |
| `0x004163d0` | Race loop, counter-based pacing, update/render calls | Not ported |
| `0x00442e10` | Update function called by race loop with tick count | Needs detailed analysis |
| `0x004439f0` | Frame routine called after update | Road projection path ported |
| `0x00443d70` | Build 33 projected road slices | Single and dual-road paths ported |
| `0x00453030` | Lookahead traversal, split/merge state | Replayed along route (`make_route`) |
| `0x00453b60` | Precompute 81-sample merge snapshots | Ported as backward walk |
| `0x00444630` | Project one road slice | Road surface/edges ported |
| `0x00454240` | Fill ring slice from path/lane iterators | Widths and bend/rise ported |
| `0x00452a00` / `0x00452b70` / `0x00452b00` | `DLSR` sides iterator | Ported (`walk_intervals`) |
| `0x00451910` | Terrain-type dispatch → sub-slice type | Mapping traced |
| `0x00452cb0` / `0x00452e10` | Initialise / step all section iterators of a traversal half | Mapped (row-chunk table) |
| `0x00416310` | Select and load horizon, meter, dashboard | Asset selection traced |
| `0x00441940` | Course resource load/fixup | Sections, lanes and node graph decoded |
| `0x00405e30` | Load 15 bike specs | Directory reader only |
| `0x00405ea0` | Apply bike spec to runtime bike fields | Ported (`apply_spec`) |
| `0x0043a580` | Per-tick bike physics | Ported except wall collision `0x0043a610` |
| `0x00437c30` | Chase camera | Ported (single road) |
| `0x00434660` | Player rider think (controls) | Riding part ported; demo/mode AI branch ported (`Race::set_demo_hold`) |

Demo-player finding (October 11, 2026, commit `ca57525`): oracle captures launched
through menu course entry run the `0x00434660` head's AI branch (hold or
race-not-started mode), so the recorded control words are never read. Evidence:
the spare rider-half node keeps think `0x00434660` while bike `+0x288`/`+0xf0`
sit at 0 for 120 ticks under controls=5. The demo rider is character 1's rider 8
(bike SPEC id 2 by condition 142, value 91, weapon 2, stats 7/3000, base speed 95
= personality 8 level-0). Lane shortlist {0,1} (lane 2 diverges at frame 72);
bias = slot-0 grid word 16384. Remaining: lane final fit, and the tick-184 AI
state transition (now traced to the state 1→5 switch below).

AI-brain finding (October 12, 2026): the rider half (`*(bike+0x2fc)`) is not the
AI state. Word `rider_half+0x1d8` points at a separate brain struct that
`0x00407bd0` steps. Offsets confirmed by live dumps (`rider.<slot>.brain_<off>`
in the sim golden, `sim-kink-probe3/4`): `+0x160` state, `+0x164` skid,
`+0x168` target object, `+0x16c/+0x170` neighbours, `+0x174` overtake step,
`+0x178` per-state timer, `+0x188` replan accumulator, `+0x18c` lane, `+0x190`
bias, `+0x194/+0x198/+0x19c` target x/position/speed, `+0x1a0/+0x1a4` current
speed, `+0x1bc` base speed, `+0x1dc/+0x1e0/+0x1e4/+0x1e8` lookahead
(`0x0040a100`). `rider_half+0x1e0` is the stats-table pointer (`0x00463c78` rows).
The state dispatch table `0x00465980` maps 0→`0x00408e30`, 1→`0x00409000`
(racing), 2→`0x004092d0`, 3→`0x00409460`, 4→`0x00409ca0`, 5→`0x0040a4f0`,
6→`0x0040a560` (read from the reference EXE `.data` at file offset `0x63f80`).
  - Dispatch-table byte verification (October 12, 2026; plan 10 Task 5/6 merge `36c03dc`): the 8 dwords at file offset `0x63f80` read `408e30 409000 4092d0 409460 409ca0 40a4f0 40a560 40a5b0` — all 7 state targets exact, verified with a direct EXE read (no REA needed for data). The merge's `0x00465980` dispatch port rests on these bytes. (Slot tables `0x00464088`/`0x00464108` and the `dt 2 at tick 1` countdown claim are behavioral; they stand on the merge's replay test, not re-verified here.)
  - Replan + brain-init REA verification (October 12, 2026; plan 10 Task 5/6 merge `36c03dc`): pinned REA 6.4.0 pseudocode whitespace-identical for `0x004082f0` replan (10023 chars) and `0x00407990` brain init (1171: lane-bias `+0x190` + target `+0x194` stores the merge ports). The merge's tick-207 parity claim is behavioral and stands on its own replay test; this note covers export fidelity only. Files: `analysis/oracle/rea-4082f0-replan.md`, `rea-407990-braininit.md` (disc-derived, git-ignored).
  - Dispatch + neighbour REA verification (October 12, 2026; plan 10 Task 5/6 merge `36c03dc`): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00408cd0` neighbour pick (466 chars: mask-8 ring walk skipping off-road nodes, self-exclusion only for the cop-owned half) and `0x00409f70` shared tail (1720 — the steer/throttle/brake closer every state arm ends in). `0x00408e30` (state-0 no-op, empty body) is absent from REA's procedure database — correctly so; nothing to confirm. Files: `analysis/oracle/rea-408cd0-neighbour.md`, `rea-409f70-tail.md` (disc-derived, git-ignored).
  - Racing-arm REA verification (October 12, 2026; plan 10 Task 5/6 merge `36c03dc`): pinned REA 6.4.0 pseudocode whitespace-identical for `0x004090a0` empty-scan fallback (1417 chars), `0x004091f0` lane line (1001) and `0x00409000` racing baseline (165). (`0x00409460`/`0x0040a560` remain REA procedure-database gaps from an earlier turn; `0x00409ca0` already confirmed.) The merge's follow/shadow-state ports rest on these plus the previously confirmed scan/tail/predicates. Files: `analysis/oracle/rea-4090a0-nobody.md`, `rea-4091f0-lane.md`, `rea-409000-racing.md` (disc-derived, git-ignored).
  - Countdown-dt trace closure (October 12, 2026; plan 10 Task 5/6 merge `36c03dc` open question): `0x00438930` line 46 stores `+0x18 = 0` at build; the scheduler's per-tick dt update then yields 2 on the first scheduled tick (creation tick − 1 per the template interval) and 1 thereafter — exactly the merge's `countdown_step`/`step` 2-then-1 pattern (`src/race.cpp:2134,2198`, cited comments). The open question is closed at trace level; behavioral parity stays with the merge's replay test. (Separately: native `collide_hazards` tests sample-granularity spans while `0x004373f0` sweeps at 0x80-fraction precision — a plan-16 Task 2 fidelity question for its owner, not this merge.)
  - Thunk/gap disassembly proof (October 12, 2026; closes the REA-gap thread): `0x004092d0` is a single `jmp 0x00409000` (disassembly line 10640, matches the 91-char thunk export); `0x00408e30` is a lone `retl` (line 10182, empty body as exported); `0x0040a560` head matches its export prologue instruction-for-instruction (`push %esi/%edi`, `0x2fc→0x1d8`, `call 0x00408e40`, lines 12231–12237). All REA procedure-database gaps are now disassembly-resolved: no-ops/thunks confirmed trivial, everything else confirmed by matching exports. No open trace items remain in the AI dispatch.
Solo-capture evidence: the demo rider runs state 1 with `target_x` 18688 until
tick 182, then `0x00408ad0` moves it to state 5 and `0x0040a4f0`→`0x00408e40`
writes `target_x` 43264; bike `+0x288` ramps +165/tick from tick 183 and
`+0x10c` flips −34→+86 at tick 184. Native `ai_think` has no state machine and
holds `+0x288` 0, diverging on `lateral` at tick 184 (17073 vs 16953).
The transition needs the frozen opponent nodes: solo captures still construct
7 opponents (think/move nulled), and replan `0x00408cd0` picks the target
(`+0x168`) among them. Native solo builds zero opponents, so it can never take
this transition; the replay harness must build frozen opponents (open).
Steer rates verified: bike `+0x2a8/+0x2ac` read 165/141 live, matching native
`apply_spec` output for SPEC id 2 exactly — no divergence there.
Target identification (same session, `sim-kink-probe6`): the `+0x168` value
`0x1402f60` is scheduler-ring node 28, think 0, position 13312 — a frozen
opponent node the demo rider is pulling away from. The player bike is ring
node 32. So `0x0040a4a0`'s ahead-window (`0xf0`) trips on a receding frozen
node, and `0x00408e40` then steers off it. Solo replay must mirror the frozen
opponent construction exactly.
| `0x00414ca0` | Open resource container and load table | Native directory reader |
| `0x00415360` | Iterate table and optionally preload | Layout recovered |
| `0x004153a0` | Find/load/cache resource payload | Not ported |
| `0x00415470` | Search through resource collections | Not ported |
| `0x004154d0` | Binary search table by tag and ID | Not ported |
| `0x00415610` | Read payload from file/backing memory | Layout corroboration |

Some decompiled return types are visibly wrong (e.g. functions returning registers inferred as `void`), and imported runtime labels such as `__write` are not trustworthy. Check assembly and call sites before translating these functions.

## Next milestones

1. Correct signatures and export caller/callee references for the course loader, tick update and frame routines. Establish explicit fixed-width structures and signed-overflow behavior.
2. ~~Decode `SGS ` and `NOD `; render original road geometry~~ (done, single-road path). Split/merge roads also done. Shoulder widths also done. Remaining work: terrain profiles, shoulder heights and road colours; decode `CEL ` scenery; compare against Wine captures of the original at matching positions.
3. Reconstruct one bike's spec application, acceleration, steering and collision behavior. Add deterministic recorded-input checks before expanding to AI and combat.
4. Recover sprite compression/animation and compose bike, scenery and HUD layers with the native renderer.
5. Implement audio, input bindings, original menus, persistence and platform lifecycle behavior; package standalone macOS and Android builds.

No playable-game completion date is established. Asset viewing is the first validated milestone, not a substitute for recovering the simulation.

## Original frame oracle and course selector (October 2, 2026)

- `0x004439f0` calls road setup, display-list render and dashboard presentation with no arguments. Its four E8 call sites at `0x004163fa`, `0x00416406`, `0x0041656d`, `0x00443249` are verified against the reference binary before interception.
- After it returns, 33 projected entries at `0x004bc800` have 548-byte stride. Entry dwords 1/2 are kind/sample; dwords 5/6 point at projected road halves. Logged fields follow `0x00444630`, including depth/scale at `+0x18/+0x20`, centre/surface row at `+0x48/+0x4c`, road edges `+0x50/+0x58`, and shoulders `+0x60/+0x68`. The sub-slice pointer at `+0x0c` yields type byte `+0x3c`.
- The original course index (`0x00467a6d`) uses the string pointer table at `0x0046df58` in `0x00441940`: CITY, HIWAY, MEDLY, NAPA, CANYN. Horizon selection separately uses `0x00467aa0` in `0x00416310`: City, Sierra, Penin, Napa, PCH. These indexes must not be confused with the native CLI ordering.
- `analysis/oracle/napa-attract.tsv` contains 600 CITY frames; the scenario label predates the recovered ordering. Native comparison matches all measured numeric fields without tolerance. Coverage is limited to samples 49–74, so split/merge assumptions remain unverified.

## ISO data import (October 2, 2026)

The actual rerelease ISO uses 2048-byte blocks. Its primary volume descriptor at sector 16 reports 252,822 blocks; root directory is sector 24 and ROADRASH is sector 26. Both-endian extents/lengths, sector-contained directory records, `;1` version suffixes and directory flags decode natively. Files are checked against the existing 341-entry SHA-256 inventory before a staging directory is renamed into use. Synthetic tests cover invalid extents, truncated volumes, inconsistent endian fields and directory cycles. All 341 real files import and the viewer decodes successfully from the imported copy. SDL's Android picker supplies content URIs; random access through SDL_IOFromFile is built but not physically validated.

## Audio/video and pixel capture follow-up (October 2, 2026)

Original high-resolution capture: draw call `0x004439f5` invokes `0x00450420`. Locked state `0x00475620`, base `0x004b7ec0`, pitch `0x004b7ec4`, logical dimensions `0x0047539c`/`0x004753a0`, palette `0x0049e138`. Captured active shift `0x00481fa8` is 15, converting Q16 320×240 projection coordinates to a 640×480 surface. Low mode remains unmeasured. Capture is before dashboard drawing.

`0x0040ff40` builds transformed linked nodes (next at +0x68); `0x0040ffc0` converts coordinates; `0x0044d300` locks and traverses the display list; polygon fill/span routines are `0x0044d620`/`0x0044db30`. Earlier plan filler addresses were incorrect and have been corrected.

`0x004349c0` computes engine frequency from speed times the active gear ratio (+0x1cc+gear*0x10), divided by 256, plus 0x2000. Its smoothing limits changes to ±0x2000 on the ground, increases by 0x1000 airborne (cap 0xffff), and drops 0x1000 when crashed with prior frequency >0x2000. `0x00434ce0` multiplies this by Doppler (+0xc8), divided by 512, and chooses normal/nitro/tunnel buffers. This rule is traced, not yet connected or oracle-validated.

`0x004141c0` converts effect volume to DirectSound attenuation `clamp(volume*10000/16384-10000,-10000,0)`, clips pan to ±10000 (optionally reversed), frequency to 100..100000, and stops looping playback below -8999 attenuation. Audible gain must use the DirectSound logarithmic scale; the generic linear mixer alone does not reproduce this wrapper.

AVI corpus: 59 Cinepak files, 44 stereo WAV IMA audio streams and 15 silent bike showcases. Sequential RIFF LIST/movi scanning supplies complete packets without relying on idx1. IMA predictors/indexes restart per block; stereo payload alternates four bytes per channel, low nibbles first. Standard shift/add IMA rounding matches every original audio sample. Cinepak state retains separate 256-entry V1/V4 codebooks per strip and the previous RGB frame, with shared/independent strip books, partial update masks and skip/vector flags.

Codec comparison algorithm reference: [FFmpeg Cinepak decoder](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/cinepak.c) and [FFmpeg ADPCM decoder](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/adpcm.c). Native implementation is independently structured C++; FFmpeg CLI is used only for byte comparisons. No FFmpeg runtime dependency or original assets are embedded.

Original MCI destination setup `0x00402820`: native-size x=160 and y=240-height/2 when flag 1 is absent and screen mode is not 2; doubled width/height at x=0, y=240-height otherwise; explicit flag 1 changes y to 20. Native CLI playback currently uses the normal rectangle. Frontend call-site flags still require mapping.

## Packaging evidence (October 2, 2026)

No additional original-game behavior is inferred by packaging. The standalone macOS build uses the [official SDL 3.4.8 release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.8), verifies the published source archive digest, links it statically, and is ARM64/macOS 13 with Apple system dependencies only. Android still uses the existing verified SDL AAR. Both packages contain licence notices and exclude original disc assets. Hardware and full game fidelity remain separate acceptance items.

## Flat polygon raster recovered (October 2, 2026)

`0x0044d620` receives four integer surface vertices and one palette index, with bbox globals prepared by `0x0044d300`. It calls `0x0044ce60` to normalize vertex topology/orientation, then builds up to four directed edges. Horizontal edges are inactive. Each lower-to-upper edge starts at `x<<16`; slope is `(dx%dy<<16)/dy + (dx/dy)*65536`, using signed divisions and 32-bit wrap. An upper endpoint below the next vertex in that edge's direction has its final row decremented. Edge slots are acquired in original edge order at each start row, stepped with wrapped addition and released after their inclusive final row.

Each row floors signed Q16 edge positions, clips the lesser x to the left bound and greater x to the right bound, and `0x0044db30` writes exactly right-left bytes (exclusive right). The bbox bottom is inclusive and capped by the clip bottom. Fully horizontal polygons draw one span. The original's degenerate adjacency tables `0x0047564c..0x00475687` correspond to skipping duplicate neighboring vertices; the native filler computes that relation without embedding table bytes.

The native `fill_quad` accepts normalized polygons, bounds coordinates to the supported original Q16 edge range and rejects unnormalized shapes requiring more than two active edges. This is a partial primitive port: arbitrary topology normalization/texture handling are not implemented. All 64 captured normalized fixture images match exactly; this does not establish a full-frame world renderer match.

Oracle loader mode 2 calls the original pure filler on synthetic points into a separately allocated 640×480 buffer, restoring pixel pointer, pitch, clip and bbox globals afterward. Original EXE bytes on disk remain unchanged. Original function normalization is captured in the recorded points. Capture outputs contain synthetic geometry rather than disc graphics. Live mode 1 additionally permits component capture after the first locked draw.

## View coordinate conversion (October 2, 2026)

`0x00410160` sets shift/mask globals `0x00481fa8` / `0x00481ff4`: low uses 16 / `0xffffffff`, high uses 15 / `0xfffffffe`. `0x0040ffc0` applies arithmetic shift then mask to eight Q16 node coordinates. High mode therefore computes twice the low-mode floor, snapping all vertices to even pixels. Negative values require signed floor, not division truncation. The earlier description of shift 15 alone was incomplete.

`0x00448870` sets logical view dimensions to 640×480 or 320×240. Low inset mode sets display size 640×480 and view origin (160,120); high mode uses origin zero. `0x004457f0` maps settings 1, 2, 3 to low fullscreen, low inset, high fullscreen. `view_mode` implements these fields; the existing game scene does not yet consume this abstraction. Twenty-four original conversion-helper fixtures match including both signed extremes.

## Terrain iterator recovery (October 2, 2026)

Section row +0x10 points to terrain. `DLBR`: 16-byte header, +8 sample count, then two selector bytes per sample. `NTMR` (and code-supported `DWDR`): 24-byte header, +8 sample count, +12/+16 left/right record counts, then 16-byte wall records for each side and selector pairs. Records contain unsigned sample start +0, eight parameter bytes +4..+11 and signed base height +12.

`0x004510c0` initializes wall width at byte +6, height jitter at byte +11×2, lateral jitter at byte +8×2. `0x00451220` changes each once when integer sample changes, using original random wrapper values modulo (2×variation+1) minus variation. Variations are +5/+10/+7, bounds are [0,2×byte6], [0,4×byte11], [0,4×byte8]. `0x00451710` exports width, lateral jitter+(byte6+2×byte9)×2, base height+height jitter. Final road-relative wall transforms are applied later by `0x00454240`, not by the iterator.

`LIHR`: 20-byte header, +8 record count, 76-byte records {signed start,end, two selector bytes, two reserved bytes, eight 8-byte point definitions}. Each definition is {unsigned min/max x, x variation, signed min/max height, height variation, min/max interval length}. `0x00450fc0` draws initial x in 32-unit bounds and height in 16-unit bounds. `0x00451380` chooses interval lengths and x/height slopes bounded by the profile; `0x004515a0` preserves forward/backward transition and last-sample behavior. The fourth point per side is stateful but only three per side export. `0x004517e0` exports absolute edge-relative x/height and six 0..31 gradient indices using signed division.

The real disc contains unsorted mountain starts and negative/inverted CITY hill intervals; these are accepted in physical order, with the original single record transition per call. Structural bounds remain strict. The native API takes an external random source so it can later share the game's original stream. Fixed-random component comparisons cover 864 original slices with exact equality. This does not yet validate live course-cache initialization or the shared random stream in natural races.
  - Terrain-iterator REA re-confirmation (October 12, 2026; plan 10 Task 5 `TrackRing` input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00451220` wall jitter (1972 chars: 3 draws per side per new sample — the merge's cited RNG cost), `0x004515a0` hill transitions (2043) and `0x00451380` interval/slope choice (2603: draws when an interval expires). Confirms the exact draw sites the merged `TerrainIterator` must reproduce in order. Files: `analysis/oracle/rea-451220-walljit.md`, `rea-4515a0-hilljit.md`, `rea-451380-terrstep.md` (disc-derived, git-ignored).
  - Walker enter/init REA re-confirmation (October 12, 2026; plan 10 Task 5 `TrackRing` input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00450fc0` LIHR init (1390 chars: 2 sides × 4 points × 2 draws = 16 draws per walker — the merge's cited init cost), `0x00452cb0` section enter (966: dispatches all seven feature inits terrain/BORR/DZHR/wall/hill) and `0x00452f50` walker root (619: 0x410-byte alloc + dual `0x00452d70` + enter). With this, every function in the merge's draw-order list is double-confirmed except `0x00453ae0` walk-to-camera. Files: `analysis/oracle/rea-450fc0-lihrinit.md`, `rea-452cb0-secenter.md`, `rea-452f50-walkroot.md` (disc-derived, git-ignored).
  - Walk-to-camera REA re-confirmation (October 12, 2026; plan 10 Task 5 `TrackRing` input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00453ae0` walk-to-camera (615 chars: 0x100-chunked `0x00453030` steps toward the target) and `0x00452e10` section change (1526: boundary cross → child enter via `0x00452cb0`, then all seven feature steps terrain/BORR/DZHR/wall/hill + `0x00452230`). The merge's draw-order list is now FULLY double-confirmed — every cited function has matching exports + independent REA. Files: `analysis/oracle/rea-453ae0-walktocam.md`, `rea-452e10-secchange.md` (disc-derived, git-ignored).

## Hill projection recovery (October 2, 2026)

`0x0041d6d0` receives a projected road record with source sub-slice pointer +0x0c, world centre x +0x10, road elevation +0x14, scale +0x20 and surface row +0x4c. It projects six source points +0xf0..+0x11c into Q16 output +0x70..+0x9c. X uses `(world_x-camera_x+point_x)*scale + (view_width & ~1)*32768`; y uses `(camera_height-road_y-point_height)*scale + 90*65536`. Every arithmetic operation wraps as signed 32-bit. EAX returns the maximum y including the initial surface row (confirmed by disassembly, despite the decompiler's void signature).

At scale ≥65,536, a negative x with a nonnegative wrapped product yields −0x40000000. A nonnegative x whose product-plus-centre is negative yields +0x40000000. Other x values use the wrapped projection. At lower scales the sentinel checks are omitted. Native `project_hills` reproduces this behavior; 96 isolated original calls compare exactly. This verifies the helper, not live world visibility/rendering.

## Polygon topology/orientation recovery (October 2, 2026)

`0x0044cd70` computes four signed 32-bit wrapped corner cross products, marks positive and zero corners, then propagates orientation through collinear corners three times. Entirely collinear returns 255. `0x0044ce60` repairs one concave corner with a wrapped/truncated neighbor midpoint, swaps crossed edges, or rejects orientation masks 5/10. It then orders by the lowest two y values (default), x values (flag 8), or texture orientation (flag 4). Ties follow the original strict/non-strict comparisons. The default lookup tables encode a shared cyclic edge of the lowest two vertices; opposing vertices have no preferred start. Flag 4 toggles a provided texture-flip state when reversing.

Native `normalize_quad` matches 2,048 original calls across modes 0/8/4, including collinear, duplicate and wrapped-cross fixtures. `draw_flat_quad` uses the default normalization and preserves the original horizontal bypass, then calls `fill_quad`. All 64 indexed reference images also match from their captured pre-normalization points. The existing filler still rejects pathological shapes with more than two active edges; behavior of original rejected polygons and texture drawing is not established by this milestone.

## Indexed texture raster recovery (October 2, 2026)

`0x00418910..0x00419e10` are opaque spans for strides 1,2,4,..256; `0x0041a0b0..0x0041bc20` preserve destination where the sampled palette index is zero. A generic native loop replaces alignment/unrolled variants with equal bytes. Sampling uses unsigned wrapped `(((row_q16 & 0xffff0000) << stride_shift) + column_q16) >> 16`; row/column increments wrap per pixel. Bounds checks prevent malformed UVs reading outside imported texture storage.

`0x004184a0` writes inclusive edge rows into one of two 480-entry {Q16 x,U,V} tables. Its reciprocal is `2^32 / ((y1-y0)<<16)` with signed division; signed 64-bit slope products retain bits 16..47. Negative rows advance values without writing; rows cap at 479. Horizontal segments do nothing. `0x004180b0` walks the two chains from the first minimum-y corner, skips horizontal top edges, and later segments replace shared rows.

`0x00418760` floors x, tests the initial right edge >0, swaps edge triples if needed, uses reciprocal floor(65536/span_width), advances clipped negative-x UVs and clips span right. The static reciprocal table at `0x00467ebc` agrees with this division. Disassembly shows ECX=count, EDX=V, stack=U,dU,dV: texture spans use V as row and U as column, despite the ambiguous decompiled call expression. `draw_textured_quad` ports normalized convex scan conversion with origin-zero left clip, caller-supplied UVs, mip pixels and transparency.

Mip selection compares inclusive projected bbox dimensions with inclusive source rectangle dimensions, reduces each axis while destination < source/2 and another level exists, then selects the smaller axis level. Original UV shorts shift right by that level and resource mip entries are twelve bytes apart from +0x38. `texture_mip_level` and UV shift match the original in three-level synthetic quad captures. Resource header/atlas/display-list preparation is still pending.

All 1,008 span fixtures and 64 full textured quad images compare exactly. These are synthetic original-function fixtures; they do not establish a full race or sprite render match.

## CEL wrapper and PC indexed PDAT (October 2, 2026)

Original `0x0040b800` walks size-delimited chunks until pixel data: optional `OFST`, `CCB `, `PLUT`, `XTRA`, optional `HSPT`, then `PDAT` (code also supports `RPDT`/`RRPD`). On-disk tags are reversed little-endian bytes. CCB is 80 bytes with its body at chunk+12: flags there, width/height encodings at chunk+72/+76. `0x0044fb60` interprets sign-bit dimensions as powers of two and other dimensions by low nine bits, then replaces them with its lookup encoding. The native reader preserves numeric dimensions/flags rather than 32-bit runtime pointers. PLUT has count +8 and little-endian 16-bit entries from +12; its use for nonindexed legacy drawing is still pending.

PC pixel bodies start at PDAT+8 with magic `0x00bc614e`. Header: flags +12, name +16 (16 bytes), width +32, height +36, mip count +40, flat palette index +44. Per-level pointer word at +56+12×level is relative to pixel-body+28. `0x0040b7b0` relocates these words in place. Native bounds-checks them and copies only indexed mip storage, retaining caller ownership. Width is a power-of-two stride ≤256; reduced height bottoms at one storage row. Mip data contains direct PC palette indices, requiring no PLUT-to-RGB conversion.

Seventeen GLOBAL textures contain 117 indexed mip blocks. Twenty-five other GLOBAL/course CEL resources contain only four legacy pixel bytes; the reader retains those words and does not pretend they are decoded images. Course selectors additionally reference external/local CEL tables (`0x0044ac00` / `0x0044aca0`), whose preparation is not yet ported.

The oracle calls original `0x0044fb10` to initialize a saved dimension table, runs `0x0040b800` on copied disc resource payloads, captures numeric fields and writes each original-relocated mip byte range, then restores the table. All 42 dimensions/flags and all 117 byte ranges match native decoding. This establishes resource interpretation, not original scene selection or full sprite drawing.

### Ordered node renderer recovered (October 2, 2026)

`0x0040ff40` transforms the source linked list in place into 64-byte nodes at source +0x68, stopping on the +0x0c sentinel 0x40000000. `0x0040ffc0` maps eight Q16 coordinates at +0x44..+0x60. `0x00410080` consumes the source +8 pixel-resource pointer directly, accepts magic 0xbc614e via `0x0040feb0`, and copies signed bounds at resource +4/+6/+8/+10 through `0x0040fe50`. The source is not an extra CCB pointer layer. Rejected resources with source +0x66 set become flat-template nodes colored from +0x67.

`0x0044d300` walks in list order; quality-mask intersection with resource flags selects flat fill (resource +44 color, or node +60 for the flat template). Texture paths normalize points, update horizontal flip, inset each UV endpoint by one, and call `0x004180b0`. Mip choice uses the original pre-normalization integer bounding box and inclusive signed UV extents. Quality masks observed in settings are 16, 48 and 112. Rider flag 0x2000 specializes the resource with `0x00443820`; that path remains unported.

The native generic renderer matches 32 synthetic original lists (384 nodes, both 320×240 and 640×480 modes), comparing all 6,144,000 pixels exactly. CEL signed UV bounds also match the original relocated loader on all 17 real indexed textures. This verifies generic dispatch/composition, not natural scene construction or whole race frames.

### Lane-panel construction and road material table (October 2, 2026)

`0x0043c170` builds one quad per lane using projected lane spacing (+0x24) of the neighboring/current slice, starts (+0x30/+0x40), and rows (+0x28/+0x38) masked with 0xffffc000. It expands the first and last panels by 0x40000 in logical Q16. Marking flag 0x10 reverses the two ends. Counts come from source bytes +0x34/+0x35; the source Q8 lane values at +0x30/+0x2c determine boundary/interior material roles via `0x0043c0b0`. Surface flag 0x20 selects the plain template. Marking flags 8/16 select marked variants, 0x20 selects the alternate material, and 0x40 suppresses special selection.

`0x00450720` emits quads in first-left, first-right, second-right, second-left order, with U/V reversals when end ordering is inverted. `0x0043c170` uses PRE0 0x18 for consecutive crossover terrain (type 3), otherwise 0x1f001f00. PRE0 is captured for structural verification; indexed texture drawing uses resource mip data.

`0x00441770` reads 20-byte load descriptors at `0x0046dfb0`: destination, resource tag, zero-based ID, loader state fields. CEL IDs are incremented before lookup. Destination slots 0x4c3cc0/0x4c3cb0 and the 0x4c3cd0/0x4c3ce0 arrays identify plain, alternate, marked and normal panels. The native loader reads these IDs from the original executable, resolving imported CELs without embedding the table. `0x00444e30` aliases marked slot 1 to marked slot 0.

All 343 panels emitted by 128 original calls match coordinates/material/PRE0/flips. Substituting actual original-loaded road CEL pixel resources into those original lists and drawing through `0x0040ff40` yields 24,576,000 pixels identical to native construction + imported material binding + generic renderer. Natural frame visibility/cache/order is not covered.

## Language string tables (October 2, 2026, session 20)

- `0x0042f340` picks `Text\<LOCALE_SABBREVLANGNAME>.DLL` (via `0x0042f410`/`0x00443380`). If that fails it tries the primary language's default sublanguage (`0x400 | primary`), then `0x409` (ENU). Only ENU/DEU/FRA/ESP/ITA exist on the disc. The native rule maps the first SDL preferred locale's ISO 639 code (`de`, `fr`, `es`, `it`) and otherwise uses English.
- `0x0042f5d0` builds the global string table at `0x00475378`. String 0 is `"%d %d"` (buffer bytes, count). Strings `count..1` are then loaded in descending order with `LoadStringA` into one shared buffer. When a loaded string's last ANSI byte is `0xff`, the byte before it is a signed index: string `3000 + index` overwrites those two bytes, and this repeats. Slot 0 is null. `LoadStringA` truncates to the remaining buffer.
- All five DLLs yield 985 non-empty slots (1,360–1,379 raw `RT_STRING` entries, including the 3000+ continuation pool). Strings are Windows-1252, as the game saw them.
- `TEXT/BADLOC.DLL`, `FUTB.DLL`, `FUTD.DLL` and `FUTR.DLL` are TrueType fonts, not PEs: Badloc and Futura Bold/Demi/Regular. `0x0042f720` installs them with `AddFontResourceA`. `0x00442990` creates Badloc at heights 0x12/0x48 (low resolution) or 0x20/0x80 (high resolution), plus Wingdings, and Arial/Small Fonts/New Courier for debug overlays. `0x00448790` draws captions in bold Arial Narrow with a 2-pixel black drop shadow. Arial Narrow, Arial and Wingdings are Windows system fonts, not game data.

## Career rules and save files (October 2, 2026, session 20)

**Game state block `0x004b8a10`** (32 bytes, initialised from `0x004754b0`, saved verbatim):

| Offset | Meaning | Evidence |
| --- | --- | --- |
| +0 | network player slot; cleared at race start outside mano-a-mano | `0x004160c0` |
| +1 | character 1..8 (Axle, Bose, Cydney, Jon, Pearl, Rhonda, Jim, Mike), 0 = none | `0x0041f5a0`, `0x0041c060` |
| +2 | level 0..4 | `0x0041f6d0`, `0x004453f0` |
| +3 | qualified-course bits (bit n−1 for course n; `0x1f` = level complete) | `0x00449150` |
| +4 | bike id 0..14 | `0x0041f630`, `0x004215e0` |
| +5 | course 1..5 of the race started (1 City, 2 Sierra Nevada, 3 Peninsula, 4 Napa, 5 Pacific Highway) | `0x004160c0`, menu items 16..20 |
| +6 | 0-based finishing place (`0x004c5d78`) | `0x00416120`, `0x00437860` |
| +7 | unknown; initial value 1 | `0x004754b0` |
| +8 | cash (u32) | `0x00421290`, `0x004214e0` |
| +0xc | alias (NUL-terminated) | `0x0041c060` |

**Game mode** `0x004753b0`: 0 attract, 1 Thrash, 2 Big Game, 3 mano-a-mano. `0x00445090` resets level, qualifications and character when the mode changes.
  - Attract demo trace (October 12, 2026; plan 13 Task 5 Step 1 — disassembly + EXE-table dumps, no capture): entering main menu (screen 9) re-arms Win32 timer 9 for 60000 ms (`0x00421df0` case 1 at `0x421e93`: KillTimer + SetTimer; dispatch map `0x4223ac` dumped). First fire (`0x423cb7`): KillTimer, toggle `0x46afec` set; course counter `0x46aff0` cycles 1–5 (hits 6 → reset to 1, so 6 never runs); `0x00421430(course)` rebuilds standings (thrash/big-game path by current mode) and stores the course by writing screen-8 item 0's target (`0x469ec4` = screen array `0x469ec0` + 4); `DAT_004753c8 = 1` (demo hold → player think `0x00434660` takes the AI branch, confirmed at `0x4346a1`); course art via `0x0041dbc0(0x1b, …)`; `0x00421df0(8)` enters race loading → AI demo race. Second fire (toggle clear): random JESSIE/DUEL video (`rand & 1` → `0x46b1f8`/`0x46b1f0` via `0x004023e0`), back to menu 9. Mode 0 outcome routing confirmed earlier (`0x00416120` → main menu). Input-exit mechanism (which message ends the demo race) NOT pinned — Step 2/3 port detail, behaviorally tested (key → main menu). Controller-connect exit likewise open.

**Characters:** `0x00415fc0` maps character 1..8 to personality 8, 7, 2, 5, 4, 6, 3, 1. Starting cash comes from u16 table `0x00469000` by character. The default bike is `0x004641c8[level + personality*5]` (`0x0041f630`). Choosing a character in Big Game restarts the career (level 0, no qualifications, starting cash).

**Race outcome** `0x004c5d7c` (`0x00416010`): code `0x7f` (finished) becomes 2 (won) when the 0-based place is < 3 in single player, else 3 (lost). 4 is busted, 5 wrecked. `0x00416120` maps 2/3/4/other to results screens `0x1e`/`0x1f`/`0x1d`/`0x1c`, or to the main menu in attract mode.
  - Results-chain REA re-confirmation (October 12, 2026; plan 13 Task 4 / plan 6 input — via 3 parallel subagents, MATCH claims independently re-verified): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00416120` outcome→screen map (1447 chars: quit/abort→0xb, attract→9, 2→0x1e won, 3→0x1f lost, 4→0x1d busted, other→0x1c, plus registry save + cutscene replay) and `0x00421290` results screen (1413: row table, highlight clamp, prize payout, `InvalidateRect`, returns 0x22). `0x004214e0` standings-advance is a REA procedure-database gap (valid bytes at file offset 133344, Ghidra auto-analysis gap between `0x4214d8` and next function — worker forensics; port from the project export). Files: `analysis/oracle/rea-416120-outmap.md`, `rea-421290-results.md` (disc-derived, git-ignored; gap error kept in `rea-4214e0-standings.md`).

**Results** (`0x00449150`, `0x00421290`, `0x004214e0`, `0x0041f6d0`):
- A win ORs the course bit into the qualifications (except in mano-a-mano).
- Big Game pays every finisher (won or lost) `prize[place] × (level+1)`. `prize` is the static first dword of the 0x4c-byte standings rows at `0x0046ab48`: 1000, 750, 500, 400, 300, 250, 200, 160, 130, 100, 70, 50, 30, 20, 10.
- Big Game busted: the fine is `(level*5+5)*0x50` = 400 × (level+1). Wrecked: repairs cost `bike_price/10` (u32 table `0x00468fc0`). If cash ≥ cost it is paid (messages 197/194). Otherwise message 198 ("outta money") is shown and `0x0041f630` restarts the career.
- In Thrash, wrecked/busted clears the qualifications (messages 193/196). There is no money.
- After the results, all five qualified sets the save-pending flag and clears the qualifications. Then either the level increments (screen `0x20`) or, at level 4, the finale (`0x23`). Otherwise the next screen is Thrash course select (`0x0a`), The Street (`0x0f`) or character select (`0x0e`).
- Videos: `Wreck`/`Busted`/`Win`/`Level` + random 1..6, `Lose` + 1..10, `FinalWin`.
  - Cutscene audit (October 12, 2026; plan 13 Task 4 Step 2 — disc census vs native triggers, no capture needed): disc holds 44 `VIDEO/*.AVI` + 15 showroom clips (`RAT`/`SPORT`/`SUPER`, 5 each) = 59. Native status: `INTRO` (frontend `start()`), `START1–6` (`0x004213c0` course-chosen), `WIN1–6`/`LOSE1–10`/`WRECK1–6`/`BUSTED1–6`/`LEVEL1–6`/`FINALWIN` (`results_video` in `career.cpp`, wired in `race_finished`, pinned by `frontend_test`), 15 showroom clips (showroom handler `0x004216e0`, pinned), `JESSIE`/`DUEL` (jukebox handlers `0x00445600`/`0x004455b0`/`0x00445730`/`0x004456d0` PORTED October 12, 2026 — Step 1 commit `bd31058`, pinned by `frontend_test`). `DUEL` additionally needs mano-a-mano (plan 09, disabled). All 59 videos have native triggers; remaining Task 4 work was Step 3 (done: outcome-video test `7e7fa6e`).
  - Jukebox VideoMode verification (October 12, 2026; self-review of the Step 1 port against export `004023e0.c`): `0x004023e0(path, variants, flags)` plays nothing when VideoMode is 0, draws `rand % variants + 1` ONLY when VideoMode ≠ 0 and variants > 0, and stops (flag bit 0 clear) then restarts (`0x00402c40`) the soundtrack. The port matches on all three: force-2/restore (as `0x004456d0` does: save, 0→2, play, restore), variants = 0 (no RNG — matches), stop-before/restart-after via the existing non-showroom `play_video` path in `game_main.cpp`. Matte blit (VideoMode ≠ 2 + flag0 clear) has no native equivalent — documented architecture difference, same as all menu video playback.
  - Cutscene/prize-tail REA re-confirmation (October 12, 2026; plan 13 Task 4 input — via 3 parallel subagents, MATCH claims independently re-verified): pinned REA 6.4.0 pseudocode whitespace-identical for `0x004210a0` cutscene replay dispatcher (2833 chars: timing-table scan + queue via `0x00420e30` + playback via `0x00421cf0` + `0x00430cf0` fallback) and `0x00402f50` soundtrack stop (327: quit msgs + wait + close, re-init via `0x00413c50` when flagged). `0x0041f6d0` prize hook is a REA procedure-database gap (8 bytes into Ghidra's `0x0041f6c8` function after padding — worker proved via control at `0x0041f6c8` matching `0041f6b0.c` exactly; port from the project export). Files: `analysis/oracle/rea-4210a0-cutreplay.md`, `rea-402f50-sndstop.md` (disc-derived, git-ignored; gap error kept in `rea-41f6d0-prize.md`).

**Bike shop** `0x004215e0`: showroom slots `0x80..0x8e` map to bike ids via `0x0041ff00`. Owning the bike already gives "WAKE UP!" (187/188). If `price[current]/2 + cash < price[new]` (unsigned), the result is "IN YOUR DREAMS!" (185/186). Otherwise cash += `price[current]/2 − price[new]` and the bike changes.

**Save file** (`0x0042f000` writer, `0x0042f110` reader), 0xa0 bytes:
- +0: checksum `0x0042efc0` over the following 0x9c bytes. Over bytes b with countdown n, `sum += b`, `weighted += n*b`, `alt += (n odd ? b : −b)`; the checksum is `(alt<<16 | sum&0xffff) ^ weighted`.
- +4: magic `0xcafedead`.
- +8: the 32-byte game state.
- +0x28: 10 × 3 dwords. Each is the triple at `+personality*12` in each runtime personality copy (`0x004c6b40`, 0x204 bytes each; defaults are the first 0x78 bytes of `0x00464558` records). These are the relationship values Schmooze reads (`0x00420bb0`).
- The reader checks magic (error string slot 369), then checksum (slot 370). It then restores the personalities, copies the state and relations, and forces Big Game mode.
- The native reader additionally rejects out-of-range state bytes; the original performs no range checks.
  - Save-file trio REA re-confirmation (October 12, 2026; plan 13 Task 5 input — via 3 parallel subagents, MATCH claims independently re-verified, CLEAN SWEEP): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0042efc0` checksum (408 chars: sum + weighted + alternating-sign triple accumulator), `0x0042f000` writer (1464: 0xa0-byte record + `0xcafedead` magic + checksum + WriteFile) and `0x0042f110` reader (1775: magic/checksum validation + state/relationship restore + Big Game force). Save format fully double-confirmed for the port. Files: `analysis/oracle/rea-42efc0-savecrc.md`, `rea-42f000-savewrite.md`, `rea-42f110-saveread.md` (disc-derived, git-ignored).
  - Writer output pinned against the running original (October 26, 2026; plan 13 Task 6 Step 1): `RR_ORACLE_SAVE=1` calls `0x0042f000` in-process with a seeded state and seeded personality triples; `analysis/oracle/save-seed-20261026.rrs` (+ `.receipt.json`, git-ignored). Native `read_save` accepts it and `write_save` reproduces it byte-for-byte (`career-save-corpus`). Layout consequence confirmed at runtime: triples come from `+rider*12` in each `0x004c6b40` copy with the rider index from `0x00415fc0` held in `0x00467a6c`; error slots 369/370 and `0x00444fb0` were not exercised (success path only).

**String slots used:** 192/195/199/200 are the results captions; 201+place is "%s finished in …"; 197/194 are the fine/repair formats; 193/196 the Thrash penalty texts; 198 game over.

## Front end (October 2, 2026, session 20)

The menus are a Win32 window with child "RashButton" windows over two 640×480 15/16-bit DirectDraw surfaces. One surface holds the picture; the other is the same picture with every 5-bit channel halved (`0x0041e6e0`) and sits behind text boxes and buttons. Menus therefore do not use the 8-bit race surface; the native front end draws into a 32-bit image quantised to RGB555.

**Screen table** `0x0046a390`, 0x24 entries × 0x14 bytes: item count, item array, title string slot, title DrawText format, last focus index. At start-up `0x00423881` sets the counts of screens `0x10`/`0x11`/`0x12`/`0x13`/`0x0b`/`0x0d` from `0x00474668`/`0x0047466c`/`0x00465fb0`/`0x00474670`/`0x0046ca88`/`0x0046ce80` (13, 9, 22, 8, 6, 18). A screen with no items is a race-start or quit code: `0x00421df0(n)` posts `WM_QUIT(n)`, and the main loop (`0x00448200`) starts course n (1..5) or quits (6).

**Items** (0x38 bytes):

| Offset | Field |
| --- | --- |
| +4 | target screen |
| +8 | caption slot |
| +0xc..+0x18 | x, y, w, h |
| +0x1c | navigation bytes: left, right, up, down, tab, shift-tab (`0x004227d0` follows a chain until an enabled item without flag `0x40000`) |
| +0x24 | flags (see below) |
| +0x28 | enabled |
| +0x2c | handler; activation `0x00423200` calls it, else enters +4 |
| +0x30 | description slot |
| +0x34 | box format |

Flags: 1 title box shows the caption; 2 title box shows the description; 4 description box; 0x10 popup item; 0x20 the button keeps the picture behind it (otherwise darkened); 0x40 light colour when disabled; 0x2000 price box; 0x4000 course length; 0x8000 Escape target; 0x40000 skipped; 0x80000 dimmed under a popup.

**Screens:**
- 7 Credits (1 s timer)
- 8 "GAME LOADING / Get ready to race!"; its item's target is the course
- 9 main menu
- 0x0a Thrash courses
- 0x0b/0x0c/0x0d mano-a-mano
- 0x0e characters
- 0x0f The Street
- 0x10 Restroom
- 0x11 Jukebox
- 0x12 Configure Input
- 0x13 Graphics Options
- 0x14 Olley's
- 0x15/0x16/0x17 Rat/Sport/Super bikes
- 0x18 showroom
- 0x19 Der Panzer Klub
- 0x1a Schmooze
- 0x1b Big Game bulletin board
- 0x1c..0x1f wrecked/busted/won/lost
- 0x20 level progression
- 0x21 fine/repair/penalty
- 0x22 race results
- 0x23 finale (2 s)

The course screens share one item array; the sixth item is copied from `0x00469130` (Thrash: Restroom), `0x00469168` (mano: Disconnect) or `0x004690f8` (Big Game: Der Panzer Klub).

**Pictures:**
- `0x0041d970` picks the screen picture: Credits, Main, Postcard, Street, Olleys, Panzer, `Bikes/<class>/<name>`, `Restroom/Rest1..6`, `Results/Wreck|Bust|Win|Lose 1..6`, `Results/Level<level>`, `Results/Finale`, and `Chars/<name>-F` behind the loading screen.
- `0x0041dbc0` swaps the picture while an item has focus: `Postcard/[Q]City|Sierra[R]|Penin|Napa|PCH` (Q = qualified, R = alternate artwork toggled with A), `Chars/<name>-N`, `Showroom/<class>/<name>`, Street/Klub/Olley/Home.

**Fonts** (`0x00423881`): title boxes Badloc 72; buttons Futura Condensed Bold 24; full-width buttons and descriptions Futura Condensed Regular 32 (Demi 24 when flag 0x80; Regular 36 in Schmooze); "QUALIFIED" Badloc 42 at (452,272); standings New Courier Bold 18.

**GDI text metrics (October 26, 2026; Wine-GDI ground truth via `tools/textprobe.c`)**
- LOGFONTs (sole `CreateFontA` site `0x004488e3`, menu table `0x004239e5`): positive cell heights (72/42/36/32/24/20/18/16/15), lfWidth 0, weight 0 (FW_DONTCARE — boldness lives in the FUTB file) except New Courier Bold 700, OUT_TT_ONLY_PRECIS, DEFAULT_QUALITY.
- ppem rule (292/292 TEXTMETRIC heights 8..80, all four faces): `ppem = round(H*upm/cell)`, then step down once when `round(cell*ppem/upm) > H`. tmAscent/tmDescent = rounded scaled win metrics; tmHeight = their sum (e.g. 33 at 32px FUTR — the old multiline drift). tmExternalLeading is 0 everywhere.
- Advances = `round(design*ppem/upm)` (380/380 ASCII vs GetCharABCWidthsA; hdmx is NOT used). Multiline DrawText pitch = tmHeight. Bitmaps stay unhinted in the port (remaining fringe).
- DrawText CALCRECT returns the measured wrapped width even under maxw (probe: 296/293/269/156 vs input 300); the port's old wordbreak carve-out pinned boxes to maxw and shifted right-aligned text. Fixed.
- Remaining GDI deltas (open): hinted bitmap shapes (1-bit scan conversion); `>=0x80` cp1252 bytes differ (GDI has real default-char widths; the port uses .notdef advance).

**Colours** (`0x0041fc90`): normal `#DEFFF0`, focused `#E1E180` with a Wingdings arrow, disabled/dimmed `#A0A0A0`, skipped `#FF6432`, titles `#FF6432`. All text has a 2-pixel black shadow.

**Box geometry:**
- Title box (`0x0041f770`): left (text + 0x31 by text + 0x2a), right-aligned (min 0x13c), centred at 3/4 height, or 640×300.
- Description box (`0x00420470`): ≤300 wide at x = 8 or 316, y = 122; or full width at y = 380.
- Restroom/jukebox items without a width take the title box width, clamped to 0xe6/0xa0..0x118 (`0x004459f0`).
- Description dark-box repaint (October 26, 2026; static disassembly of `0x00420470` + pixel evidence): the fill/`0x41fb90` invalidate is gated on the item-flags bit 3. Pixel-verified across goldens: restroom/jukebox/graphics (`0xc`) fill; characters (`0x5`), course (`0x4815`) and showroom (`0x184`) bios draw text on the photo. Net rule: fill iff `flags & 8` (the 0x4208d0 dynamic path's `or ebx,0xc` does not force it — showroom proves the negative).
- Standings (`0x22`): rows, title and the darkened fill are populated by the `0x00421290` results handler (per-row flags `0x8040` shown vs `0x8000`), not on screen entry: direct `0x00421df0` entry shows the bare photo (traced golden) with only the dynamic-slot result line as a footer in the last row's rect. The port mirrors this (rows/title/darken gated on handler state).
- Footers (traced positions, masked): course Length at (8, y367) is the desc's line 8; the showroom draws the blurb only and a separate Demi-24 Length footer at (right, y301); results footers are the wide item caption (dynamic slot, masked y>=370). Price (`0x46b058`) and player (`0x46b09c`) footers at y380 are real-play draws whose content mapping (0x4208d0 bh&1/bh&8 arg slots) is recovered but not yet ported — masked in acceptance, open for real-play fidelity.
- Footer content mapping (October 26, 2026; byte-exact formats + golden-verified):
  player `0x46b09c` = `"     %s     %s     %s%s  $%u     "` with
  (player_name, str(91+level), str(29) "Cash", str(504) ":", cash);
  bike `0x46b058` = `"  %s     %s%s%s%s $%u   %s%s %s  "` with
  (player_name, str(29), str(505) "+", str(30) "Trade", str(504) ":",
  cash+bike_price/2, str(31) "Bike", str(504) ":", str(bike_slot)).
  Runtime table `0x475378` is slot-indexed (`+0x74`=slot 29, `+0x7e0`=slot
  504, `+0x7e4`=slot 505). Bike-name slots via `0x420aa0` (jumptable at
  `0x420b0c`: bike 0→0x70, 1..14→0x71,0x72,0x74,0x73,0x7a,0x7d,0x7b,0x7e,
  0x7c,0x75,0x78,0x76,0x79,0x77). Trade-in = `bike_price[bike]>>1`
  (`0x468fc0`, confirmed `$2748 = 1000 + 3497>>1` for Cydney). Both footers
  centered in 640, Regular 32 (caller-0x4208d0 desc face/height); Length
  footer is Demi 24. Player footer gated on focused-item `flags & 0x800`
  (raw high byte bh&8 — verified on street/thrash/bulletin/panzer goldens,
  including a masked street footer); price footer on the showroom screen.
  The `[0x4b8a12]` level-string index is assumed to track career level
  (slot 91+level; golden has 0 there).
- Screen pictures with no `0x0041d970` case keep the previous background: jukebox (`0x11`) and Graphics Options (`0x13`) inherit the restroom photo (acceptance trace cache 1 holds `Restroom/Rest3` through restroom/jukebox/controls/graphics). Characters entry shows the focused rider's `Chars/<name>-N` full-screen via the focus-image swap (`0x0041dbc0`); the `-F` variant is the race-loading picture.
- Configure Input (`0x12`) key labels live in string slots 160-168, shared with the character bios: the acceptance `enter 18` golden shows stale `Height: ...` bios because the script bypasses the restroom-row handler (`0x0040fd90`) that refreshes them in real play. The port refreshes labels on every `0x12` entry, which matches all reachable play paths; the stale-label pixels sit inside masked item rects.

**Popups** `0x004224b0`: header plus choices for mode `0x004735c0`, level `0x004736a0`, name `0x004737f0`, resolution `0x00474240`, biker `0x00474320` and videos `0x004743c8`. Choosing calls the stored setter (`0x0049e0a8`) with the item target: `0x00445090` mode, `0x004453f0` level, `0x004457f0` resolution, `0x00445900` biker detail, `0x00445990` videos.

**Restroom enabling** `0x00444fb0`: Y/N strings `0x00473598..0x004735b4` by mode, character and save file.

**Course length** (flag 0x4000): u32 tenths of miles at `0x00468f58[course*5 + level]`; metric ×1.609 truncated (`0x00462008`), units string 27/28.

**Character text:** `"Height/Weight/Bike/Cash"` uses language strings 4000..4003 (feet/inches or metres; lbs or kg ×0.454) and rider heights/weights at `0x00463c78` +8/+0xc. The bike is the level default.

**Standings rows:** `"%2d)  $%4d  %*s  %s"` (Big Game) or `"%2d)  %*s  %s"`, time `"%d:%02d<decimal>%d"` in tenths, `"------------"` for none. Three rows are shown, plus the player's row when placed below third (`0x00421290`).

**Schmooze** `0x00420bb0`:
- Pick a random rider 0..7 not yet seen this visit; the player's own bit is set on entry.
- Compute `value = rel[r][character][1] − rel[r][character][0] + rel[r][character][2]`.
- Attitude is `0x0046ab44[value < 16000 − 1500L ? 1 : value ≥ 32000 − 3000L ? 3 : 2]`.
- Two to four candidate lines come from `0x0046a6e0[level*8 + r]` (shorts; extra lines when +24/+26 name the player's character). The picture is `Chars/<name>-<"FNH"[attitude]>`.
- A visit shows at most five riders (`0x00420b50`).

**Menu colour pipeline (October 26, 2026; plan 13 Task 6 Step 2 input):**
- `0x0041e090` builds the 64K-entry `0x00485df8` table from the display mode masks: for 555-grid inputs it emits RGB565 (all 32768 grid points map to `(r5<<11)|(g5<<6)|b5`, dumped live from the running original via the `RR_ORACLE_MENU` proxy thread; no odd 6-bit greens). The menu picture surface is therefore RGB565, not RGB555. The native picture decode is `quantise565` (`src/rri.cpp`); green-channel agreement on the captured screen 9 confirms it.
- Dark fills (button bar, description/title box backgrounds) measure as halved-555 of the decoded photo (button bar matches `halve(photo555)` within JPEG-decoder noise: R ±8, G/B exact at five sample points), NOT as the `0x7bde`-masked 565 mapping the same table would give. The halving code path is unlocated (0x0041e6e0 is the table path; no second `>>1` packing was found in the exports). The native dark fill stays `darken555`. Open question recorded; it blocks exact-pixel acceptance, not the current characterization pin.
- Screen 9 capture: `RR_ORACLE_MENU=9` screenshots the composed 640×480 client window once `0x0046a660` holds 9 (`analysis/oracle/menu-09-20261026.bmp` + receipt, git-ignored; cnc-ddraw gdi renderer, boot takes ~100 s under Wine). First-slice `frontend-test` comparison: title/description/button areas masked (GDI text, wrap and layout gaps), picture + fills compared directly (masked 60%, unmasked exact 15%, large-diff (>32) 1%). Thresholds are characterization pins; full-pixel acceptance stays open.

**Not ported or replaced natively:**
- Mano-a-mano is disabled. The original protocol is traced in [MULTIPLAYER.md](MULTIPLAYER.md); the port is [plan 34](plans/34-multiplayer.md).
- Jukebox track list and video popup ported (plan 13 Task 4 Step 1); Sound Mix and Configure Input items do nothing yet (plan 18 Tasks 6-7).
- The 60-second attract-demo timer on the main menu is not implemented (traced October 12, 2026: plan 13 Task 5 Step 1; see Game-mode bullet).
- Windows common file dialogs are replaced by numbered `<Character> N.RRS` saves in the preference directory and a list of the newest eight.
- MessageBox prompts are replaced by an in-picture dialog.
- Wingdings and New Courier are substituted.
- Title text placement within its box is approximate.

**Credits** (`0x00424140`, `0x00424230`, `0x00424340`):
- Screens 7 and `0x23` scroll string tables 1000 (opening, from y 0x30, left aligned with DRAWTEXTPARAMS tab length 35 and left margin 40) and 2000 (finale, from y 0x1c8, centred). Both are loaded with the `0x0042f5d0` rules over the normal picture.
- Twenty 24-pixel lines are drawn in Futura Condensed Bold 24 with a shadow. The first character of each line picks its colour: G grey, O orange, R red, Y yellow, others pale.
- The first timer tick re-arms for 1.5 s, then the roll scrolls `0x00468f50` (1) pixels every 15 ms. '+'/'-' double or halve the speed. When the table ends, the screen's item (handler `0x004241e0`) leads to `0x0041f6d0`. Escape skips.

**Race end** (`0x00416010`): finishing (`0x00434660`, code `0x7f`), wrecking (`0x0040a600`/`0x0043a190`, code 5) and being busted (`0x00407f30`, code 4) set the outcome once. The race then keeps running for 0xb4 ticks (`0x00442e10`). Confirming the in-race Escape prompt (string 316) while still racing sets outcome 5, so quitting counts as wrecked (`0x00423408`).
  - Outcome + step-driver REA re-confirmation (October 12, 2026; plan 10/15 port input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00416010` outcome (1272 chars: first-write-wins on `DAT_004c5d7c`, 0x7f→place-gated 2/3, network name copy), `0x0044b3b0` step (1844: 2/3-timers, rider pass, scheduler, stamp-gated camera, 0x100-chunked ring advance, traffic timers on moved distance, same-sample distance accumulator) and `0x0044b250` race start (511). Files: `analysis/oracle/rea-416010-outcome.md`, `rea-44b3b0-step.md`, `rea-44b250-start.md` (disc-derived, git-ignored).

## Sound effects, engines and soundtrack (October 2, 2026, session 20)

**Effect slots** (`0x00435e60`): 0x45 slots, one DirectSound buffer each, loaded from `Audio/Effects/<name>.WAV`.
- Slot 0 is the player's engine, chosen by bike id: ids 0–4 EngRat, 5–9 EngSport, 10–14 EngSuper.
- Slots 1–6 are EngNitro, EngTunne, EngOther, Engcar11, EngOther, Engcar11.
- Slots 7/8 are M_YEAH/F_YEAH; 9/10 CHAINIMP/CLUBIMPA; 11 BOUNCE; 12–14 CRASHHAR/MED/LIT.
- Slots 15–20 are GRUNTM1–3 and GRUNTF1–3; 21 KICK_MW4; 22 PUNCHHEA; 23/24 SWISHCHA/SWISHGEN; 25/26 SKIDONNR/SKIDOFFR; 27 FOOTSTEP; 28 RIDERSLI; 29 BMWLONG; 30/31 SIREN/SIRENFST.
- Slots 32–68 are rider and pedestrian voices.
- A retrigger restarts only that slot's buffer, so there is no voice stealing.

**Event queue** (`0x00436830` → `0x004367f0`): 10 records of {code, lateral, height, position, speed}. Codes and their call sites:
  - Queue + consumer REA re-confirmation (October 12, 2026; plan 13 Task 1 input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x004351f0` event consumer (5040 chars: siren-19 fast path, per-code volume/frequency/dispatch switch, the finish-yell and female-grunt branches) and `0x004367f0` queue pop (312). (A redundant `0x00436830` re-fetch was compared with a truncated window and deleted without claim; the prior turn's 965-char confirmation stands.) The full queue→consumer path now has exact source text. Files: `analysis/oracle/rea-4351f0-consumer.md`, `rea-4367f0-queue.md` (disc-derived, git-ignored).
- 0/1/2 crash light/medium/hard, and 3/4 grunts, from contact `0x004049c0` (player only). Below 0x1771 impulse it is a grunt (rand&1)+3 plus crash rand&1; above, grunt +4 and crash +1, and nothing when below 0x7d1 with a small +0x188.
- 5 wall grunt (`0x0043a610`).
- 6/7 chain/club swish (`0x004096f0`).
- 8/9/10/11 impacts chain/club/kick/punch; kick alternates 10/11 on the tick parity (`0x00409ad0`).
- 12 footstep, 13/17 skid on/off road (`0x00434bd0`, +0x164 > 0x32 and surface class ≤ byte `0x0046dce5`), 16 slide (`0x00434a80`).
  - Skid/slide REA re-confirmation (October 12, 2026; plan 11/13 sound input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00434bd0` skid gate (430 chars: `+0x160` live + `+0x108` speed + `+0x164 > '2'` + uncrashed → event 0xd on-road / 0x11 off-road by `+0x158` vs `DAT_0046dce5`) and `0x00434a80` slide dispatch (1439: skid call + tunnel flag + crash-state event select 0x10/0xc + `DAT_0046d9a8` state mirror). No RNG in either path. Files: `analysis/oracle/rea-434bd0-skid.md`, `rea-434a80-slide.md` (disc-derived, git-ignored).
- 14/21 nitro on/off (`0x0040b110`/`0x0040b170`), 18 car horn (`0x0044efa0`), 19 siren (`0x00435ae0`), 20 landing bounce (`0x0043a190`), 22 speech.
  - Nitro REA re-confirmation (October 12, 2026; plan 13/17 sound input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0040b110` nitro-on (366 chars: `+0xc0&0x20` clear + `DAT_00465a34` charges left + level-gated `DAT_00465a38` cooldown → decrement charges, set bit, event 0xe) and `0x0040b170` nitro-off (219: bit set → event 0x15, clear bit). No RNG in either path. Files: `analysis/oracle/rea-40b110-nitron.md`, `rea-40b170-nitroff.md` (disc-derived, git-ignored).
  - Siren REA re-confirmation + capture-probe outcome (October 12, 2026; plan 24 Task 2 / plan 13 Task 1 input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00435ae0` siren (2077 chars: dual-buffer alternation at 0x2b11 Hz, `0x259/0x79`-tick phase counters, distance/volume via `0x00435840`). No RNG in the siren path (pure phase machine). Smoke capture `smoke-napa-default` (SIM=1, seed 1, drive 2, 300 frames, 420 s) still fails exit 124 with the known signature (2 frames, course 0, tick frozen at 2 + Wine page fault) — pipeline still down; proxy `g_race_started` fix still blocked on shared ownership. File: `analysis/oracle/rea-435ae0-siren.md` (disc-derived, git-ignored).

**Event consumer** (`0x004351f0`): most effects play once at volume `(rand%0x1001+0x3fff)/100 × effects` and frequency `rand%0xbb9 + 0x214d` Hz. Footsteps, skids, slides and speech use their own constants. The finish yell (M_YEAH or F_YEAH) plays once when the player's bike gets flag 0x10. Grunts use the female set (+3) for characters 3, 5 and 6 (`0x00435da0`).

**DirectSound mapping** (`0x004141c0`/`0x004142c0`):
- Volume in hundredths of dB is `(v×10000 >> 14) − 10000`, clamped to −10000..0; a looping buffer stops below −9000.
- Pan is ±10000 hundredths of dB, negated when speakers are swapped.
- Frequency is clamped to 100..100000 Hz.
  - DirectSound-mapping REA re-confirmation (October 12, 2026; plan 13 Task 1 SDL-input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x004141c0` play (1226 chars: `(v·10000>>14)−10000` dB clamp, swap negate, 100–100000 Hz clamp, −9000 loop-stop, vtable volume/pan/freq + play-if-idle), `0x004142c0` loop-start (1134: same clamps, unconditional play) and `0x00414160` stop (132). The SDL mixer port maps these onto volume/pan/frequency voice params; the vtable slots are the only Win32-specific surface. Files: `analysis/oracle/rea-4141c0-dsplay.md`, `rea-4142c0-dsloop.md`, `rea-414160-dsstop.md` (disc-derived, git-ignored).

**Engines** (`0x00434ce0`, `0x00434db0`, `0x004349c0`):
- The listener is the camera (`0x00437c30`: position Q8, lateral, position change × 256).
- Distance is `|dp| − min(|dp|,|dl|)/2 + |dl|` within 0x1e00; volume is `((0x1e00−d)×8/256)²/256`.
- Doppler is `0x13b0000/(0x13b00 − relative speed)`; pan is the `0x0044f5a0` octant angle × 0x62.
- RPM is `gear_ratio[+0x23c]×speed/256 + 0x2000`, moving at most ±0x2000 per update, +0x1000 while airborne, −0x1000 after a crash.
- Engine frequency is `doppler × rpm / 512` Hz; volume is `(+0xcc × 0x9ffe / 0xef10) × level`.
- The two vehicles nearest the camera (`0x004350e0`) use slots 3/5 (bikes) or 4/6 (cars).
- `0x00434fd0` routes the two ranks: bikes (`+0xc4 == 4`) refresh rpm
  (`0x004349c0`), play slot base (`0x00435030`) and stop base+1; cars
  (`+0xc4 == 0x10`) stop slot base and play slot base+1 through `0x00435090`:
  frequency `doppler(+0xc8) × 0x3b`, volume `(+0xcc × 0x4fff / 0x6400) × level`,
  pan `+0xd4`. Car Doppler/attenuation come from the same `0x00434db0`
  core, fed with the car position/lateral/velocity.
- Contact pairing (`0x004368f0`): every candidate pair must satisfy
  `(cat_a & cat_b & 3) != 0` before separation/contact run. Pedestrians are
  type `0x20` (`& 3 == 0`), so pedestrian pairs never contact in the
  original; the native scan passes them through with no handler.
- Tunnel engine (`0x00434a80`): `DAT_0046d97c` is set when the byte at +0x3c
  of the player's `+0x140` object is 3; `0x00434ce0` then plays the tunnel
  variant (slot 2, `EngTunne`) instead of the bike engine. Natively that is
  `Race::player_in_tunnel()`: the terrain type of the player's route sample
  (course_scene assigns 3 to mountain tunnel samples, `0x0041c200` rail
  strips). Pinned in `sound_test` (type 3 true, types 0/2 false).
  - Sound-helper REA re-confirmation (October 12, 2026; plan 11a/13 port input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x004349c0` rpm (955 chars — the `update_engine_rpm` source), `0x00434ce0` engine mix (720: `0x00434db0` + rpm + Doppler/volume + normal/nitro/tunnel buffer routing) and `0x00436830` sound event (965). `0x0044efa0` horn is absent from REA's procedure database (valid 651-char export; single `rand()%10==1` draw + `0x1f8` timer gate) — port from the project export. Files: `analysis/oracle/rea-4349c0-rpm.md`, `rea-434ce0-engine.md`, `rea-436830-soundevt.md` (disc-derived, git-ignored).
  - Spatial-helper REA re-confirmation (October 12, 2026; plan 13 Task 1 siren/engine input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00435840` siren spatial (1805 chars: 0x1e00 range gate, `|dp| − min/2 + |dl|` distance, `((0x1e00−d)×8/256)²/256` volume, Doppler `0x13b0000/(0x13b00−Δv)`, octant pan ×0x62) and `0x00434db0` engine select (2043: same distance/volume core, `+0xd8` approach/depart flag, `+0xcc/+0xc8/+0xd4` Doppler/volume/pan stores). The distance/volume formulas in the bullets above are now double-confirmed at source level; no RNG in either helper. Files: `analysis/oracle/rea-435840-spatial.md`, `rea-434db0-engsel.md` (disc-derived, git-ignored).
  - Engine-slot REA re-confirmation (October 12, 2026; plan 13 Task 1 input — REA + parallel build check): pinned REA 6.4.0 pseudocode whitespace-identical for `0x004350e0` nearest-two select (1625 chars: ring walk keeping the two closest bikes/cars by `|pos − camera|`, slots 3/5 bikes or 4/6 cars), `0x004362e0` level curve (3724: 101-entry `curve[level·master/100]` table build + channel stores) and `0x00414180` playing check (167: vtable playing-status query, skip-retrigger gate). No RNG in any of the three. Build passes in parallel (`rr-game` links). Files: `analysis/oracle/rea-4350e0-engslots.md`, `rea-4362e0-levels.md`, `rea-414180-dsplaying.md` (disc-derived, git-ignored).

**Levels** (`0x004362e0`): each channel uses `curve[level × master / 100]` from a 101-entry table built in code. The native reader decodes that code's stores from the user's executable. The Mixer block (`0x00463030`) holds master 100 and interface/player engine/effects/music/other engines/siren at 80, plus mute and swap.

**Music:**
- The soundtrack thread `0x00403040` streams `Audio/Music/<track>.RRA` from the saved track index and advances 0..13 when a track ends (HB_Trip, HB_Pass, MM_Dino, P_Pansy, P_Jessie, P_Bridge, SG_Rusty, SG_Outsh, SG_Kicks, SG_Super, SD_Train, SD_Duel, T_Teeth, T_Auto).
- Race start (`0x004160c0` → `0x00402f50`) stops it. Races play MIDI `Grunge1..5` by course (`SbkGrng` with AWE32 SBKs; Klown when the alias is "krusty" or "bozo"), via `0x00433bb0`.
  - Race-MIDI chain REA re-confirmation (October 12, 2026; plan 13 Task 1 input — via 3 parallel subagents, MATCH claims independently re-verified): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00433bb0` MIDI starter (1114 chars: krusty/bozo→Klown alias, Grunge vs SbkGrng select via probe, load + volume), `0x00434500` SBK probe (1665: AWE32 Ordinal_2 checks, RRDrums vs per-course Grunge bank load) and `0x00434230` MIDS parser (2611: RIFF/MIDS + fmt/data validation, MIDIHDR chain, compressed-track decode). No RNG in starter/probe (parser draws none; randomness lives in stream timing). Files: `analysis/oracle/rea-433bb0-midistart.md`, `rea-434500-sbkprobe.md`, `rea-434230-midsparse.md` (disc-derived, git-ignored).
  - MIDI pump-tail REA re-confirmation (October 12, 2026; plan 13 Task 1 input — via 3 parallel subagents, MATCH claims independently re-verified): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00433e30` stream pump (1643 chars: MIDS-magic gate, open vs restart paths, header feed, error teardown), `0x004340a0` session alloc (526: teardown-old + alloc + feed + open chain, 0/1/2 codes) and `0x00434150` track feed (1038: MDSI block + file-map + parser call, 0/1/2/4 codes). The race-MIDI chain starter→pump is now fully double-confirmed; remaining: volume/teardown `0x00433cb0`/`0x00433cf0`/`0x00433f70`/`0x00433dd0`. Files: `analysis/oracle/rea-433e30-streampump.md`, `rea-4340a0-session.md`, `rea-434150-trackfeed.md` (disc-derived, git-ignored).
  - MIDI volume/teardown REA re-confirmation (October 12, 2026; plan 13 Task 1 input — via 3 parallel subagents, MATCH claims independently re-verified): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00433cb0` volume pack (186 chars: two percent volumes → 0–255 → both-channel DWORD), `0x00433cf0` session teardown (384: header cleanup + stream teardown + free/null + AWE Ordinal_2 commands), `0x00433f70` header cleanup (670: MDSI-magic gate, midiOutReset + unprepare chain + stream close) and `0x00433dd0` stream teardown (385: close-if-open + buffer free + magic stamp). The race-MIDI chain is now COMPLETE end-to-end (starter→probe→parser→feed→session→pump→volume/teardown). Files: `analysis/oracle/rea-433cb0-midivol.md`, `rea-433cf0-teardown.md`, `rea-433f70-hdrclean.md`, `rea-433dd0-streamdown.md` (disc-derived, git-ignored).
  - SBK-loader callees REA re-confirmation (October 12, 2026; plan 13 Task 1 input — via 3 parallel subagents, MATCH claims independently re-verified): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00434620` SBK bank-load slot wrapper (243 chars: slot + opaque args → AWEMAN32 Ordinal_2 func 0xcc), `0x00434420` compressed-track decode (1352: alignment/bounds-checked header + payload copy) and `0x00433380` (2076: CORRECTION — not an Audio path resolver as the scout labeled it, but a serial-port session `__thiscall` ctor: dual 16000-byte buffers, HANDLE + SetupComm/DCB/timeouts, `0x004337c0` post-init. Out of MIDI scope; recorded to prevent misattribution). SBK-load + track-decode paths confirmed; the serial ctor needs no port. Files: `analysis/oracle/rea-434620-sbkload.md`, `rea-434420-trackdecode.md`, `rea-433380-audiopath.md` (disc-derived, git-ignored; the last filename is a misnomer kept for traceability).
- Showroom clips (`0x004023e0` flag 1) only signal the soundtrack thread; other videos stop it.
  - SBK bank structure + synthesis decision (October 12, 2026; plan 13 Task 1 Steps 1–4 close-out, disc analysis via python3): all six banks are SoundFont 1 (`sfbk`, `ifil` v1.0, 16-bit `sdta/smpl` PCM): GRUNGE1 (127060 B, 3 presets + EOP: RUSHTUTT/Dist guitar/Pulsy VCO Bass 2), GRUNGE2 (110056, Slap Bass/FUNKCHRD), GRUNGE3 (136916, GTRSTRNA/RUSHTUTT/FINGBAS1), GRUNGE4 (115614, DVBASSHI/GTRCHNKA/Dist guitar/PGTRHARM/DVBASSPL), GRUNGE5 (113886, Slap Bass 1/FUNKMUTE/FUNKCHRD/BASSCPOP), RRDRUMS (373246, Heavy Drums + 20 instruments). Programs are bank-specific, not General MIDI (songs reference channels 0–15 with bank-local programs; GRUNGE/SBKGRNG/KLOWN `.MID` are RIFF `MIDS` with `fmt `+`data`, not SMF — consistent with the confirmed `0x00434230` parser). Generators in use span loop offsets, mod/vib LFO→pitch, filter Fc/Q + mod/env→filter, mod LFO→volume, chorus/reverb sends, pan, full mod/vib envelopes, keyRange, attenuation, coarse/fine tune, sampleID/modes, exclusiveClass, overridingRootKey. DECISION: option B (minimal native sampler in `src/race_music.cpp`: zone-mapped 16-bit voices, linear interpolation, volume envelope) — option A rejected because SF1→SF2 conversion + two pinned single-headers outweigh the tiny bank surface (≤6 presets, 1–20 instruments), and no third-party file was added. Race continues silently on missing/corrupt bank (logged once) per the Task 2 truncated-bank test.

## Resource packing, cel groups and dynamic rider textures (October 2, 2026, session 20)

**Packing** (`0x004157b0`): a resource payload read through the resource stream starts with 16 bytes {unpacked size, stored size, CRC, packed flag}.
- Packed data uses the PKWARE Data Compression Library "implode" format, exploded by `0x004555e0` (`0x0040e4d0` / `0x0040e530`, "Error in compressed data").
- `0x0040e440` keeps a reflected CRC-32 (0xedb88320) of the output, which must equal header word 2.
- All 163 non-empty `FAM ` records in FAMILIES.RSC are packed this way. The native `unpack_resource` explodes them and checks size and CRC; all pass.

**Family containers** (`0x0044aa80`): a dword count followed by that many offsets, relative to the container, to nested chunks.
- Leaf chunks are `CCB `, `PLUT`, `RRAN` (animation), `CANS`, `OFSS`/`OFST` (offset tables), `FORM`, and `CLGP` (cel groups).
- `CLGP` (`0x0044a840`):
  - +8 (relative to +0xc) is the shared CCB body; +0xc (relative to +0xc) is the PLUT.
  - Byte +0x14 is the mode (bits 0–1) plus a power-of-two flag (0x40). Byte +0x15 is the dword index of the block table; bytes +0x18/+0x19 are counts; +0x1a/+0x1b are shifts.
  - Mode 0 (`0x0044a660`): table entry j points (relative to the chunk) at a list of +0x18+1 pixel-block offsets, each relative to +8.
  - Mode 1 (`0x0044a760`): the table holds the pixel-block offsets directly, with sizes in byte pairs at index +0x16.
- Every pixel block is an indexed texture with magic `0xbc614e` and mip offsets relative to +28 (`0x0040b7b0`), the same body as a CEL `PDAT`.

**Dynamic rider textures** (`0x00410080`, `0x00443820`):
- A display node whose resource is the "Bob" marker (`0x00466684`, stored at `0x00466688`) carries a frame at source +0x34 and a colour at +0x38.
- Colours 0..14 select bike frames remapped through `0x0046e158[colour]`. 0xde selects cop frames, 0xdf cop shadows, 0xe0 bike shadows (sheets loaded by `0x00443610` from `Data/Bikers/<Hi|Lo>{Bob,Cop,Shadow,ShadoC}.DAT`).
- Out-of-range pairs are dropped. At draw time the frame is copied into a 256-stride page with bounds (0,0,w,h).

**Display list** (`0x00450370`/`0x00450420`): nodes are 0xa8 bytes, bump-allocated from `0x0049e66c` up to a capacity of 0xfa (250) and flushed through `0x0040ff40` → `0x0044d300` at the end of the frame (or when full). A natural CITY frame holds 90–99 nodes.

## Simulation step and object lists (October 11, 2026, plan 10 task 1)

**Step boundary.** `0x004163d0` is the frame loop: each pass calls `0x00442e10(steps)` with the step count in `ecx` (capped at 0x14 = 20 per frame, `analysis/decompiled/004163d0.c` lines 101–107). `0x00442e10` polls controls once per frame (`DAT_004b7ed0 = 0x0044f800()`, already hooked at site `0x00442ea8`) and then calls the batch stepper `0x0044b2e0(steps)`. `0x0044b2e0` holds the inner per-tick loop (`decl esi; jne 0x0044b30c`, disassembly `0x0044b396`): each iteration increments the tick counters `DAT_004b7ecc`/`DAT_004c3990` first, then runs one step. The per-step hook site is the `E8` at **`0x0044b324`** (`e8 87 00 00 00`), expected target **`0x0044b3b0`**. The hook runs the original call and then dumps: at that point `DAT_004b7ecc` is the new tick and `DAT_004b7ed0` is the control word for the tick (polled once per frame, so multi-step frames reuse it; record at the step, not at the poll). Post-loop tail calls `0x00438450` once.

**Per-step driver** (`0x0044b3b0`): alternating timers `0x00475610/0x00475614`, rider/object pass `0x0044b0e0`, object scheduler `0x00438170(0)`, chase camera `0x00437c30` (gated on the player slice stamp equalling the tick), ring advance `0x00454120`, and traffic timers `0x0044dd90(distance_moved)` — so traffic `rand()` calls sit inside the tick at a fixed position after the rider pass.

**RNG.** `0x00441740` is `srand` (`movl %ecx, 0x49bce8`, fastcall). `0x00441750` is the game `rand` wrapper: if `DAT_004753b0 == 3` it returns `DAT_0049bce8` unchanged (the fixed-random mode the terrain/scenery oracles already use); otherwise it jumps to CRT `0x00455eb0`. `0x00455eb0` reads its seed from `getptd()+0x14` (`0x004592b0` is MSVC `_getptd` via TLS imports) and the arithmetic was verified by hand against the disassembly (`0x00455eb5..0x00455edb`): `seed*214013 + 2531011` (`0x343fd`/`0x269ec3`), returning `(seed >> 16) & 0x7fff` — the exact MSVC stream `Race::random()` reproduces (`src/race.cpp:531`). Consequence for the oracle: the live stream state is **not** `0x49bce8` (that global is only live in mode 3). The seed hook must write `*(getptd()+0x14)` by calling `0x004592b0` in the hook, and the per-tick `rng` field must read the same word after the step. `srand` call sites: `0x0043097c` (reseeds from its own `rand()` output) and `0x00431385` (seeds from `*param_1` via `0x00431380`; its caller is not yet identified, likely race setup through a behaviour table — open).

**Object lists.** `0x00438170(0)` walks the scheduler ring: `S = [0x0046dcc8]`, end marker `[S]`, first node `[[S+4]+4]`, links at `+0x4`, dispatch through `0x00437e60(node, tick, …)` (verified in disassembly `0x0043833a..0x00438365`; Ghidra's `DAT_0046dcc8[1]` prints as an address-relative index but is sentinel-relative `*(S+4)` — reading address `0x46dcc8+4` instead finds an unrelated flag, cleared at `0x00438379`). `0x00437e60` calls think at `+0xe0` and move at `+0xe4`. Node words: `+0xc`/`+0x10` schedule ticks, `+0x1c` position (the `0x00438aa0` insert sort key), `+0x28` lateral, `+0x2c` flag, `+0xc4` flags, `+0xec` speed. Riders and cars share this list (traffic despawn parks the object back to the free pool, matching `TrafficSystem` pool size 16 from `0x00438b70(0x210, 0x10)`). Bikes and rider-halves are separate nodes: every bike (player included) thinks with `0x00407a90`, rider-halves with `0x00434660` (the player's half mirrors the player bike's position/lateral). Non-rider nodes are dumped raw. Rider identity in the hook: node pointer equality with the player pointer `DAT_004642d8` marks the player bike; opponent grid-slot mapping is still open — discovery order shifts as cars spawn/despawn and riders reorder by position, so a stable cross-run opponent key is plan 10 task 6 work (never assume list order matches). Slice pointers must be converted to ring indexes in the hook (process-specific addresses); the current hook dumps them raw as `slice_ptr` for debugging only. Stamina/weapon/AI words in the plan's field table live in the rider half of the bike+rider pair (`*(bike+0x2fc)`; the hook currently omits them (native test skips absent fields).

**Field selection.** Bike words come from the existing `Bike::i32/u8` accessors (`src/bike.hpp:37`): `+0x1c` position, `+0x28` lateral, `+0xec` speed, `+0x23c` gear, `+0x2f0` condition, `+0x2f4` crash, `+0x140` slice, `+0xc0` flags, plus vertical height/velocity. Rider members map from `Rider` (`src/race.hpp:62`): stamina current/max, weapon `+0x1c8`, AI state words. Car fields map from `TrafficCar` (`src/traffic.hpp`): `+0x1c`, `+0x28`, `+0xec`, `+0x73` kind, `+0x7a` lane, `+0x7f` vehicle, `+0x71` phase.

## Free traffic (October 11, 2026, plan 05 task 2)

**Vehicle resources** (`0x004465f0`, see `docs/ACTORS.md` for the census):
`DATA/CARS/<CourseName>.CAR` holds `ANIM` (RRAN) and `CANS` (SNAC wrapper)
entries. `CANS` is **not** a flat cel: payload magic `SNAC`, size word +4,
type word +8, an embedded sub-container at +size whose own size word leads
(`0x0040b5f0` with index 0) to the cel block at `base + sub[+4] + 0xc`; the
vehicle mask `0x004b8fe0[i]` is that block's CCB flags word at +4
(`0x0040b620`). Mask bits `0x1c0000` are speed classes: `0x40000` slow,
`0x80000` oncoming, `0x100000` same-direction. The RRAN frames carry their
own indexed textures, so drawing does not need the CANS pixels.

**Ring cursors** (`0x004552e0`): `DAT_004b6e50` = front (base + 0x5000),
`DAT_0049f240` = back (base − 0x3000), `DAT_004b6e5c` = camera; both scroll
by the camera delta in `0x00454120`. The `0x0044dc10` window test
`[back, front)` applies only to objects with the knock-off flag `+0x1cc`
(riders); free cars always think, and `0x00437e60`'s tail parks them once
outside the window.
  - Ring plumbing REA re-confirmation (October 12, 2026; plan 10/17 port input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00454120` advance (1113 chars: front/back/camera scroll, 0x100 slice rebuild via `0x00453030/0x00450f90/0x00454c80`, split-sample flag clear), `0x00449910` ring walk (750: `+0x10/+0x20` link steps clamped to front/back) and `0x00449990` positioned index (1265: in-window pass-through else forward/backward search with ring-wrap guards). Files: `analysis/oracle/rea-454120-ringadv.md`, `rea-449910-ringidx.md`, `rea-449990-posidx.md` (disc-derived, git-ignored).
  - Merged `TrackRing` verification (October 12, 2026; plan 10 Task 5 merge `cbd1b54`): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00454c80` slice build (9674 chars) and `0x00453030` walker step (13951 — largest confirmation to date). Both are the RNG-consuming core the merged `src/track_ring.*` ports (terrain/BORR/DZHR iterator draws); all 20 cited addresses have project exports. The merge's draw-count evidence (K = 2363, 11,461-draw setup stream, anchor closure) is behavioral and stands on its own test (`track_ring_test`, `ring_probe`); this note covers export fidelity only, not the counts. Files: `analysis/oracle/rea-454c80-slicebuild.md`, `rea-453030-walkstep.md` (disc-derived, git-ignored).

**Spawn timers** (`0x0044dd00`/`0x0044dd90`, driven per step by
`0x0044b3b0`): rate 2500 = 0x19·100, density default 6, reloads
`(rate·density·60) >> 8` toward zero. Oncoming spawns kind 2 at front−1
sample with lane `−1 − rand % left_lanes` (sub-slice +0x30); "behind"
spawns kind 0 at front−1 with right lanes (+0x2c); "ahead" spawns kind 0 at
back+1 while the rider is slow. Lane counts use the signed-char semantics

**Placement** (`0x0044df60`): lateral `(road centre + lane·250 + 125)·256`;
vehicle type filtered by mask (loop excludes the last vehicle) and picked
with `rand() % accepted`; four random bytes (phase `+0x71` is a full int
slot 192..319 — a byte truncation freezes the car); serial `DAT_004b7e98`.
  - Spawn chain REA re-confirmation (October 12, 2026; plan 24 Task 1 Step 5 input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0044df60` placement (3472 chars) and `0x0044dd90` timers (1866). `0x0044dd90` owns three countdowns — oncoming (`+0x90`, kind 2 at front−1, lane `−1 − rand%left`), behind (`+0x94`, kind 0 at front−1, right lanes, clamped at 2× reload), ahead (`+0x80`, kind 0 at back+1 when slow); each reload adds its period and each spawn draws exactly one `rand()` for the lane. Suite green 70/70 at this commit. Files: `analysis/oracle/rea-44df60-xing.md`, `rea-44dd90-spawntimer.md` (disc-derived, git-ignored).

**Car think/move** (`0x0044e710`/`0x0043adc0`): speed accumulates through
`0x004498c0` (`+0xec += dt·rate`, clamped) toward the phase-scaled cruise
`((DAT_004b7e8c = 25) · phase >> 8) · 100` — note 25, not the timer rate
2500; rate `phase·0x12 >> 8`; obstacle following/lane changes
(`0x0044a070`/`0x00449e60`) not ported. Horn `0x0044efa0` → `0x00436830`
(0x12) when the look-ahead target is the player and `rand()%10 == 1`.
Native integration: `Race::set_traffic` steps the timers after the camera
update with the per-step camera advance; drawing reuses the verified
scenery projection with the depth-table scale at each car's Q8 depth.
Frame cycling and car CCB world-size scaling are native stand-ins.
  - Lane-validity completion (October 26, plan 10): the block above is
    now ported (`TrafficSystem::plan_speed`, new `front_q8` parameter),
    including the asymmetric bounds and the step-then-recheck shape
    (`+0x2c` right-lane word, `+0x30` left-lane word, both from the
    ring slot). Car template words read from the EXE: `+0x0c` draw
    `0x00446830`, `+0x10` think `0x0044e710`, `+0x14` move
    `0x0043adc0`; per-kind `0x00475700+kind*0x18` blocks carry the
    `0x0044efa0` contact callback. Mover `0x0043adc0` is a thin
    dispatcher (`0x004390c0` servo, `0x00439920` integration,
    `0x0043a610` lane/steer, conditional `0x0043a190`); of these only
    `0x0043a610` (lane/steer with a low-speed stop branch) and the
    conditional `0x0043a190` remain unported, and both are proven
    no-ops on the traced removal runs. Evidence in the L5 handoff.

Actor animation playback and descriptor recovery (October 11 continuation):

- REA (`rea-agents` 6.4.0, Ghidra provider) agrees with the exported
  `0x0043ae90` pseudocode. Original-function fixtures verify 2,048 playback
  steps independently. The selected draw-frame pointer is resolved before
  advancing; keeping only the post-advance frame changes the displayed frame.
- Duration is a signed byte, deadline comparisons are unsigned, and one call
  takes at most one transition. Deadlines accumulate from the old deadline.
- CANS runtime descriptors are 20 bytes, separate from RRAN graphics. The
  bounded `actor_descriptors` decoder reproduces all 194 descriptors built by
  `0x00445a60` (59 bindings). Metadata comes from CANS and 8-byte imported EXE
  source descriptors; only the initializer's relocation schema is native code.
- Rider/cop banks at `0x00474d78` each have 52 code slots, with 59 populated
  sequences total. Rider codes 20/21 refer to BSS records not initialized by
  the loader; direct selection rejects their zero draw pointers.

Owned actor graphics selection: CANS SNAC group records use signed high-16-bit one-based RRAN indices in their first word. The following attribute block supplies per-cell flags. `0x0040b6f0` combines `0x0040b730` and `0x0040b620`, then selects the RRAN frame through `0x0040b750`. All 672 cells match runtime helper calls. `select_actor_view` implements the angle-dependent portion of `0x0043af60`, including its unusual 0xff0000 wrap, signed-byte counts, yaw reversal flag 8, low-resolution span and full-circle formulas, elevation clamp, and roll correction limited to ±7. Across 9,312 original calls the selected RRAN index, flags, roll and imported hotspots match. Orientation calculation `0x00413ae0`, live animation triggers, shadow remaps and depth dispatch are separate remaining work.
  - Actor dispatch REA re-confirmation (October 12, 2026; plan 11a Task 3 input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0043af60` view selection (3487 chars — the `select_actor_view` source, 9,312-call fixture) and `0x0044ba40` billboard emit (3686). Suite 71/71 green on re-run at this commit (`oracle-sim` flaked once under full-run ordering, passes isolated + re-run). Files: `analysis/oracle/rea-443af60-selview.md`, `rea-44ba40-billboard.md` (disc-derived, git-ignored).

Actor billboard dimensions: the RRAN frame width/height are logical view units. HI Bob images commonly double those dimensions, with odd dimensions rounded down (e.g. logical 14×33 maps to HI 27×65). `actor_node` therefore projects RRAN dimensions and samples the owned HI texture, preserving its own bounds. Empty shadow frames may have RRAN 1×1 dimensions but zero-size Bob pages, so they contribute no pixels. `ActorNodes` preserves shadow/body ordering and ground versus vertical Q8 displacement. Normal player riding animation `0x00407f30` has 1,024 matching captured steps; distance-based AI selection accepts a caller-supplied original distance, while crash-specific dispatch remains separate.

Napa vehicle CANS inheritance verified: all ten masks, all 207 view cells and all 30 decoded descriptors match original `0x004465f0` resources and `0x0040b200`/`0x0040b6f0` calls. The last four vehicles inherit the sixth CANS resource when their own sequential CANS lookup fails. Live car frame selection and actor projection can now use the same bounded CANS/EXE decoding as riders.

Crash actor behavior: `0x00403d60` copies current and previous motion from the bike to a separate rider, zeroes rider roll and several forces, sets drag 95 on the bike/70 on the rider, applies separate optional velocity impulses, raises the rider by 0x4b00 Q8 and bike by its contact-height constant, sets crash reason/state and collision dimensions, then chooses rider animation 29 for reason 2, 35 for reasons 4/5, otherwise 33 with queued -1. The numerical offline branch matches 64 captures across 49 fields per actor; route/network ownership and landmark traversal are deliberately separate. REA independently reproduces the existing crash-entry decompile (`analysis/oracle/rea-crash-entry.json`). The rider template at 0x463c10 has class 8 and update callback 0x406890; it uses `0x0043adc0` for independent crash motion. Crash animation branches `0x00407a90` (bike) and `0x00407f30` (rider) match 1,024 and 2,048 runtime steps respectively. `0x0040a600` handles walking, pickup, remount and wreck outcomes after stopping; the existing native crash timer remains a stand-in until those are ported.
- Crash-entry tail (October 26, 2026; L5d 2267 fix): `0x00403d60:135` ends with `thunk_FUN_00407410(body)` on the crash body (`analysis/decompiled/00403d60.c:135`). When the finish is known (`DAT_004b6e58 != 0`) and the body holds a slice (`+0x2c != 0`, i.e. live `bike_slice_present`), `0x00407410:20-24` takes the direct path `finish - body+0x1c`, discarding any split-branch walk offset. Native `crash_entry`/`Race::crash` now do this (`finish_q8_ - body_pos`); verified on L5d tick 2267 (branch `-23` -> trunk exact). REA 6.4.0 `function 0x00407410` (Ghidra 12.1.4) confirms the direct branch; disassembly `4060e4-4060f2`/`406165-40616e` shows the `0x004060b0` same-node gate compares `*(slice_ptr)` (first DWORD = next link), not node IDs -- that slice-identity port stays open (plan-17). `0x00406890:15-16` crashed-half `+0x1f8 += +0x30 - +0x1c` was already ported as `body_before - body_after` in `crash_body_motion`.
- Crashed car contact (October 26, 2026; L5d 2278 fix): `0x004368f0` (`analysis/decompiled/004368f0.c`) gates pair separation only on `+0x10`/`+0xc0`/geometry/callbacks, never on `+0x2f4`; `0x004049c0` handles crashed (`+0xf4 += 0x150`, line 219, then nudge + steer tail with no `0x004052c0`). Native `collide_traffic` early-returned for crashed (wrongly citing rejection); `contact_car` missed the bump, called `knock_off` (true via already-crashed, suppressing the kick and adding `+shit/256`), bled speed `|184|/4` and shoved steer-rate `shit/2` (855 vs -2 kick). Native now runs separation + `contact_car` for crashed with the bump, no knock-off/speed-bleed/shove, and tail kick baseline `magnitude` (`full>>10`) vs `+0xf0`. Verified on L5d tick 2278 (car `13e2038` kind 0 vs crashed rider 9 bike: lateral +12032, velocity +330, speed bleed -427, kick 855 vs -2 -- all gone). REA 6.4.0 `0x0043a580` shows the riderless-bike move (prep/integrate/gears/road-move, then branch-gated `0x0043a610` walls); the walls gate did not fire here (routes 0/0). `+0x324` type-word gap (native never sets 0/1/2) stays open.
- Bike type-word (October 26, 2026; walls groundwork): `0x00405990:20` sets bike `+0x324` to `param_5` (0 player incl. the `-1` spare case, 1 opponent, 2 cop). Native now sets `+0x324 = (slot == 0 ? 0 : 1)` in `place_rider` (cop already 2 in `police.cpp:90`); `+0x328` stays 0 for all, so the `0x0043a580` branch gate (`DAT_004c4fb0[+0x328]` vs player) still always takes the walls path. Write-only to date (no reader yet); L5d/L1 stay green. `+0x328` + `DAT_004c4fb0` mapping stays open (plan-17).
- Solo contact gates, flash, half entry and crash brain (October 26, 2026; split 845 -> 2056): `0x004368f0` (`analysis/decompiled/004368f0.c:21,31`) gates the outer walk on `+0x10 == tick` with `+0xc0 & 6 == 6` and the inner walk on `+0xc0 & 5 == 5`, unconditionally -- the native solo path skipped both flag checks, contacting a frozen `c0=4` node the original ignores (split-left tick 845: player 31432/70 vs frozen 31488/15360, marginal 70 overlap). Native now enforces both in solo (frozen outer skip kept; inner check unconditional). `0x00407a90:14-25` (`analysis/decompiled/00407a90.c`) refreshes the scenery-mark flash outside crash states 1/2 (`+0x1e9` survives while bike `+0x1c>>8 == half +0x1ec` and `(bike +0x10 - half +0x1f0) < 12`, advancing `(gap>>2)+1`; `+0x81` copies it) -- the solo uncrashed rider never ran it (native `step_rider` has no bike think; live runs it in `run_bike_node`, crashed-solo in `crash_bike_animation`). Native `step_rider` now refreshes it pre-move with `ticks_` for `+0x10`. `0x00403d60:52-112` copies the bike words into the half (`iVar2 = *(bike+0x2fc)`, incl. `+0x160` ground) and sets half `+0xc0 |= 3` -- native `begin_crash` copied only to the crash body; `crash_entry`/`crash` now copy the entry set to `half_object` too (split 2055: half flags 4->7, ground 37->61). `0x00407bd0` (via half think) transitions crashed riders to state 8/9 with the state clock -- native solo `step_crash` never did (split 2056: state 0 vs 9); it now runs `ai_transition` + `state_ticks`/`replan_ticks` + crash-gated stamina (dispatch no-op for 8/9). `anim_deadline` is `uint32_t`; the golden dumps `-1`, so the snapshot emits `int32_t` (harness sign artifact at the 2055 sentinel). All from project exports, no fresh REA fetch (REA env documented in the handoff). Open: tick-2056 riderless-bike motion (`0x0043a190` vertical absent from `crash_bike_motion`; `0x0043a610` walls are stand-ins, plan 11 Task 4).

  - Crash sequence REA re-confirmation (October 12, 2026; plan 11 Task 4 port input): pinned REA 6.4.0 pseudocode is whitespace-identical to the project exports for `0x00403d60` crash entry (4716 chars), `0x0040a600` run-back/recovery (4422) and `0x0043a610` wall/terrain collision (9756) — the largest single-function confirmation to date. `0x0043a610` (not ported; the stand-in clamps at the shoulder) resolves the surface class, shoulder/rough drag, wall proximity per side and the crash-vs-grunt impulse gates; `0x0040a600` owns states flying/sliding/standing/walking/remount with the 1200 walk speed and the ±6001/±9 proximity box already ported in `recover_crash_actor`. Files: `analysis/oracle/rea-403d60-crashentry.md`, `rea-40a600-runback.md`, `rea-43a610-wall.md` (disc-derived, git-ignored).

`remount_crash_actor` implements the local numeric reset from `0x00404560` and its copy from `0x004067d0`, including the detail that +0x104 is NOT copied by the latter. Original probes restore the linked rider's stamina from +0x38 to +0x3c; Race maps that to `RiderStats::max/current`. Linked route/landmark and network calls remain caller responsibilities. The fixture's private bike/rider/stamina/ring links avoid the live object heap.
  - Remount/reset REA verification (October 12, 2026; plan 11 Task 4 `remount_crash_actor` input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00404560` remount reset (1532 chars: `+0x2f4 = 0`, anim clears, stamina `+0x3c = +0x38`, `+0xc0 &= ~3`, `+0x067d0` copy-back, `0x00407410` re-anchor), `0x004067d0` copy-back (1535: the `+0x104`-skipping field copy already documented above) and `0x0040a9d0` walk speed (20 chars: `return 0x4b0` = 1200, the constant the port already uses). Files: `analysis/oracle/rea-404560-remount.md`, `rea-4067d0-copyback.md`, `rea-40a9d0-walkspeed.md` (disc-derived, git-ignored).

Police scope for opening Napa: `0x00407f30` invokes `0x00406b80` when owner is 0, rider flag 0x10 is clear, no cop exists and `(tick & 0xfff)==0`; no level guard appears. `0x00406b80` constructs the cop 0x2700 Q8 units behind the player using templates 0x464310/0x464378 and rider row 15 at 0x464038. Personality 9 is copied by `0x004078f0` to runtime 0x4c7d64. It cycles speech IDs 350..357, chooses bike from 0x4641c8+15*5+level, resets current relationship columns to their defaults and replaces player-target relationships using place/difficulty. Original level-1 constructor evidence lives in `analysis/oracle/napa-police-spawn.tsv`. This is an explicitly induced constructor capture, separate from an unmodified 4096-tick spawn trace. REA export for 0x406b80 exactly matches the existing decompile.

  - Police chain REA re-confirmation (October 12, 2026; plan 11 Task 6 port input): pinned REA 6.4.0 pseudocode is whitespace-identical to the project exports for `0x00406b80` cop constructor (3676 chars), `0x00407860` cop-find (614) and `0x00407f30` arrest+rider-anim dispatch (5040). `0x00406b80` builds via `0x00405990` (templates `0x464310/0x464378`, row 15, param_5 = 2), single-cop guard on `DAT_004642dc`, video-slot cycle 0–7, 10-column relationship reset + level-scaled rows, cop-becomes-owner, speed `+0x1a4` from `+0x1bc[level]`; `0x00407860`/`0x00407f30` own the ≥0xb DZHR spawn branch, proximity gates and the busted outcome 4 path. Live spawn/chase/siren tick integration and the `napa-cops`/`city-cops` goldens remain open (need working captures). Files: `analysis/oracle/rea-406b80-copspawn.md`, `rea-407860-copfind.md`, `rea-407f30-arrest.md` (disc-derived, git-ignored).

Cop actor allocation: `0x00405470` allocates 0x32c-byte bikes; `0x00405490` allocates 0x208-byte riders. Constructor capture must not apply bike-only offsets >=0x208 to the rider. Native `make_police_actor` retains imported virtual callback addresses only as opaque numeric metadata; native code never executes them. It imports cop-bank playback (bank 1), applies per-level bike selection/throttle percentages and reduced AI gears, initializes condition/stamina/weapon and pursuit relations, with ground/road target supplied by the caller. Captured level-1 numeric fields match; route/scheduler ownership, dynamic AI replanning and display-list integration remain open.

## Napa playtest fixes (October 11, 2026, plan 11a)

### Engine rpm (tachometer) call chain

`0x004349c0` updates the bike's rpm field `+0x2dc` — the value the dashboard
needle reads — from the gear ratio and speed. `+0x2e8` caches the speed and
`+0x2e0` is the fixed volume `0x9ffe`. It is an engine-update routine, not a
bike-think routine, and is called once per tick from:

- `0x00434ce0` — the player's engine (after the distance/doppler `0x00434db0`);
- `0x00434fd0` — each of the two nearest audible vehicles, bikes only
  (`+0xc4 == 4`) and only when the doppler volume `+0xcc` is nonzero;
- `0x00434980` — the not-racing branch, for the player bike only (`DAT_004642d8`).

The `+0x2e8` cache means the needle is recomputed only when the speed changes,
is zero, or the bike is airborne (`+0x20 >= 0x65`, which instead adds `+0x1000`
capped at `0xffff` and does **not** refresh `+0x2e8`). The earlier port kept
this arithmetic privately in `sound.cpp` (`rpm_`/`rpm_speed_`), so the bike's
`+0x2dc` stayed at its initial `0x2000` and the dashboard needle was frozen.
The fix ports `0x004349c0` literally as `update_engine_rpm(Bike&)` in
`bike.cpp` and calls it from `SoundSystem::update` (the native equivalent of
`0x00434980`/`0x00434c40`), so the needle and the engine pitch read the same
`+0x2dc`.

### Dashboard distance readout (layout entry 30)

The centre distance is **not** bike `+0x44` (nothing native writes it, which is
why it showed a constant `25.5`). The dashboard draw `0x00446c30` prints it
from the global `DAT_004b8a34` with the `%d%c%03d` format (i.e. "d.mmm"),
recomputed each tick by `0x00438550` as the signed distance to the nearest
opponent:

- For each opponent, `d = player.+0x1f8 − opponent.+0x1f8`; `+0x1f8` is the
  cumulative distance along the route written by `0x00407410` (walked node
  lengths, `+0x1e0` node, `·0x100`).
- `d < 0` is an opponent ahead; the minimum ahead distance is kept, otherwise
  the minimum behind distance. If the nearest-ahead is less than half the
  nearest-behind, the ahead opponent is the target and the value stays
  positive; otherwise the behind opponent becomes the target and the value is
  negated.
- Display is `(|value| × 1000) / (metric ? 3279 : 5280)` — 3279 feet/km and
  5280 feet/mile, so `+0x1f8` is in feet.

Porting this needs `0x00407410`'s cumulative-distance walk (the native tracks
`+0x1c` in Q8 samples, a different scale) and an oracle pixel capture to
confirm the calibration; the readout is not yet ported. The sign (ahead/behind)
also drives a separate indicator colour/string (`0x00446c30`), not yet ported.

### Dashboard opponent target (October 12, 2026)

Correction to the section above: layout entry 30 prints `DAT_0049e115`, the route distance from bike `+0x44` (a per-frame copy of `+0x1c` made by `0x00438450`). `DAT_004b8a34` is a different value, the opponent gap.
- `0x00438550` (called from `0x00438450` every frame) scans the 15 opponent slots `DAT_004c7f74..` (slot 14 = `DAT_004642dc`). It computes `d = player.+0x1f8 − opponent.+0x1f8`, where `+0x1f8` is the Q8 distance still to the finish (`0x00407410`: `DAT_004b6e58 − +0x1c` once the finish is known, otherwise node lengths ·0x100 walked to the end).
- The nearest behind (`d < 0`) becomes the target only when twice its distance is less than the nearest ahead. Otherwise the nearest ahead is the target and `DAT_004b8a34` is negated. With no target, `DAT_004b8a30 = 0`.
- `0x00446b80` copies the name (`0x00446bf0`: personality string for offline riders) into `DAT_0049e0e0` and the stamina meter `((+0x3c·2−2)·15)/(+0x34−1)+1` into `DAT_0049e119`.
- `0x00446c30` draws the name at entry 5 and the gap at entry 10 as `%d%c%03d` of `(|gap|·1000 >> 4)/(metric ? 3279 : 5280)` (one sample is 16 feet). Colours are COLORREF `0x10e7` when the target is ahead (gap ≤ 0) and `0x1ca328` when behind.
- The arrow is Wingdings (`0x00442990`: height 12 low / 18 high) glyph 0xe9 (ahead) or 0xea (behind), in entry 11's rect offset by (−12, +11/+9) in high resolution, drawn through `0x00447b20`.
  - HUD target REA re-confirmation (October 12, 2026; plan 11a/16 port input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00438550` nearest-opponent scan (784 chars: 15-slot walk, ahead/behind minima, 2×-behind rule, `DAT_004b8a30/34` writes), `0x00446b80` name+stamina copy (506) and `0x00446a40` distance readout (249). No new semantics beyond this section — the ported `Hud::state` name/gap/meter path was built from these exports. Files: `analysis/oracle/rea-438550-nearopp.md`, `rea-446b80-tgtname.md`, `rea-446a40-distread.md` (disc-derived, git-ignored).

### Depth table padding (October 12, 2026)

`0x0046f098` holds 4352 positive entries followed by two zero words (`0x004734a0` holds other data). Building forward heights (`0x00454240`) index `depth+256` and `depth+shoulder`, which reach entry 4352 at slice 32. The original reads the zero there, giving scale 0.

### Traffic zones and intersections (October 12, 2026)

- `0x00454240`'s third argument is the traversal's byte +10: 1 for the forward traversal `0x0049ee30` and 0 for the backward one. With it set, an event whose `0x00452710` flag is `0x80000000` (the end sample, types other than 4, 5 and 7) calls `0x0041ec70(record)`. That function calls `0x0041eca0` and then, in single player, `0x0044dd00(record byte +10)`.
- `0x0044dd00`: a negative value sets cruise `DAT_004b7e8c = −d` and rate `DAT_004b7e84 = DAT_004b7ea4 = −d·100`. Otherwise density is d (0 → 6, ×4 in multiplayer), both reloads become `(rate·d·60) >> 8`, and the oncoming timer restarts. Behind/ahead timers are untouched.
- Race start order (`0x0044b250`): `0x0044db80` (density 6), then `0x004552e0` with ring camera 0x3200, which builds slices back to front from 0x200 for 0x8000 with the forward traversal, so zones in the first window apply at the grid. The view camera starts at 0x3100 and `0x0044b3b0` moves both by the same delta, so the ring front is view camera + 0x5100.
  - Start chain REA re-confirmation (October 12, 2026; plan 10/17 port input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0044db80` density init (311 chars: car-pool alloc 16×0x210, `0x004465f0` vehicle load, `0x0044dd00(−0x19)` cruise 25 + rate 2500 then `0x0044dd00(6)` density 6, timer presets), `0x0044dd00` density set (567: negative → cruise/rate, 0 → 6, ×4 multiplayer, reloads `(rate·d·60)>>8`) and `0x004552e0` ring fill (2664: cursor init 0x3200/0x3100, 0x8000 back-to-front slice build with forward traversal so grid zones apply). No RNG in start setup — the 11,461-call setup stream noted in the index comes from rider building (`0x00405600`), not from these. Files: `analysis/oracle/rea-44db80-densinit.md`, `rea-44dd00-density.md`, `rea-4552e0-ringfill.md` (disc-derived, git-ignored).
- `0x0041eca0` (types 1–3 only) places an intersection controller: pool `0x00468ee4` (10 × 0x114), template `0x00468f10`, type 0x400, think `0x0041f040`, interval 0x1e, teardown `0x0041f1f0`. Its position is the zone-end slice clamped into the ring window.
  - Controller words: `+0x108` = event start ·0x100 and `+0x10c` = end ·0x100 (route position via the node delta); `+0x110` = event type (1..3); `+0x111` = record byte +9, the cross road's lane span.
  - Phases: {600, 180, 780}, or {75, 0, 240} when the zone-end slice is dual (ring slice `+0x1c` = 2) on terrain type 4 and the flag byte +11 is 0.
- Signals (`0x0041ef60`): pool `0x00468ee0` (40 × 0x108), template `0x00468f30`, whose type word is rewritten to `0x40 << index` (`0x00468f00[index]` → `0x00468f34`).
  - Each sits at the corner slice + fraction 0x50/0xb0. Laterally it is in the middle of its side's shoulder (side 0: `(+0xe0 + +0xe8)/2`, side 1: `(+0xe4 + +0xec)/2`), interpolated by the same fraction toward the linked sub-slice.
  - Creation order and NULL-`goto` (the last call is unchecked):

    | Index | Side | Fraction | Slice | Condition | Lane byte | Slot |
    | --- | --- | --- | --- | --- | --- | --- |
    | 0 | 1 | 0x50 | end − 1 | start right lanes > 0 | zone length − 2 | `+0xec` |
    | 2 | 0 | 0xb0 | start | end − 1 left lanes > 0 | zone length − 2 | `+0xf4` |
    | 3 | 0 | 0x50 | end − 1 | not the short case; variant 1/2 | start lanes | `+0xf8` |
    | 1 | 1 | 0xb0 | start | not the short case; variant 1/3 | start lanes | `+0xf0` |

    (An earlier version of this note had the slots as 0/2/3/1, the fractions as laterals, and the lane byte as a lane count.)
  - Phase starts `(~index & 1)·2`; accumulator 0x30c.
- **Controller think** `0x0041f040`, every 0x1e ticks with dt `+0x18`. Slots run in order 0..3; each accumulator gains dt. Inside `[back+0x100, front−0x200]` the phases wrap 0 → 1 → 2 with catch-up.
- **Crossing spawn**, when an odd signal wraps to 0 on a tick with bit 3 set:
  - variant 1: kind = signal index, range = lanes/2 (offset by range for index 3);
  - variant 2: kind 3; variant 3: kind 1, range = lanes;
  - lane = offset + `rand() % range`;
  - slice = start + (lane+1)·0x100.
- **`0x0044df60` for odd kinds:** lateral = ±0x78000 + the slice centre (world units, added raw); this is an offset, not a speed. Fraction 0x80; vehicle class 0x40000. Copies the controller start/end/lanes to `+0x1d0..+0x1d8` and the signal to `+0x1ec`. Per-kind template words `0x00475700 + kind·0x18` give kinds 1/3 half-width 0xa000.
- **Car think `0x0044e710`.** Kind 0/2 cars latch the nearest signal of type `0x00468f00[kind]` (index 0 / 2) within `(iVar15+0x80)·16` in the driving direction.
  - Every car follows its nearest lane obstacle (`0x0044a070`, mask 0x3f, within `iVar7 << 5`; odd kinds look across the same sample) through `0x00449a80`.
  - Stop line: kind 0 at start + 0xa0, kind 2 at end − 0xa0, crossing cars at the road edge ∓0x60.
  - Red (phase 2) and close-enough yellow brake to 0 at the line; a car already past the line hurries (double speed, accel 0x24).
  - Car think REA re-confirmation (October 12, 2026; plan 24 Task 1 input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0044e710` car think (10170 chars — second-largest confirmation to date), `0x0041f1f0` teardown (253) and `0x0041f220` signal draw (1548, dead in the original: heads never drawn). No new semantics beyond this section — the port was built from these exports; only the Step-5 capture + pixel frame remain open. Files: `analysis/oracle/rea-44e710-carthink.md`, `rea-41f1f0-teardown.md`, `rea-41f220-sigdraw.md` (disc-derived, git-ignored).
- **Pedestrian signal `0x004415b0`.** Only on slices marked 8/0x10. Signal index 0 (lane ≥ 0) or 2, the nearer of ahead/behind within 0x1e0 (behind wins ties), when within 0x801.
  - Controller chain REA re-confirmation (October 12, 2026; plan 24 Task 1 Step 5 input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x0041eca0` placement (2945 chars), `0x0041ef60` signal setup (910) and `0x0041f040` think (2022). No new semantics beyond the section above — this closes the "trace" half of Step 5's acceptance prerequisites; only the `city-intersection` capture + pixel frame remain open. Files: `analysis/oracle/rea-41eca0-ctlplace.md`, `rea-41ef60-signal.md`, `rea-41f040-think.md` (disc-derived, git-ignored).

### Actor census (October 12, 2026, static)

| Pool | Count × size | Template | Type `+0xc4` | Role |
| --- | --- | --- | --- | --- |
| `0x00463ad0` / `0x00463ad4` | `DAT_004642d4` × 0x32c / 0x208 | per rider | 4 / 8 | bikes / rider halves |
| `0x0049e654` | 16 × 0x210 | `0x00475698` | 0x10 | cars |
| `0x004c1274` | 15 × 0x468 | `0x0046dde0` | 0x20 | pedestrians (DZHR actors, type < 0xb) |
| `0x00468ee4` / `0x00468ee0` | 10 × 0x114 / 40 × 0x108 | `0x00468f10` / `0x00468f30` | 0x400 / 0x40 | intersection controllers / signals |
| `0x00476ce8` | 20 × 0xec | `0x00476cb8` | 1 | in-road BORR solids (no draw) |

The `+0xc4` values tested anywhere are 1, 2, 4, 8, 0x10, 0x20, 0x40 and 0x400. Police bikes are riders (`0x00406b80`), not a separate pool.
  - Object-iterator REA re-confirmation (October 12, 2026; plan 10 Task 5 `TrackRing` input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00451e20` BORR step (1217 chars), `0x00451cb0` slot walk (2385: twice per slice build, one draw per random-spaced object) and `0x00452230` DZHR index (869: may end one past the last record). Confirms the remaining iterator draw sites the merged `TrackRing` ports. Files: `analysis/oracle/rea-451e20-borstep.md`, `rea-451cb0-borslots.md`, `rea-452230-dzhridx.md` (disc-derived, git-ignored).

**Pedestrians (corrected October 12, 2026).** Pool `0x004c1274` is pedestrians, not cops.
- `0x004520d0` (slice build, DZHR iterator at section `+0x1b8`) spawns a DZHR record with word0 bit 31 set:
  - type `word0 & 0xf` < 0xb goes to `0x004409e0` with family selector `(word0 >> 8) & 0xff`;
  - the position is `(slice+0xcc + sign12(word1)) · 0x100 − 2s + rand() % (4s + 1)`, where `s = (word1 & 0xff0000) >> 14`, and the fourth argument is `word1 >> 24`;
  - type ≥ 0xb instead spawns a police bike 0x2000–0x2800 Q8 from the player (two `rand()` draws, `0x00449910`, `0x00406b80`);
  - DZHR spawn REA re-confirmation (October 12, 2026; plan 24 Task 4 input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x004520d0` iterator (2219 chars) and `0x004409e0` setup (1828). `0x004520d0` iterates the DZHR section (`+0x1b8`) matching `sample>>8`, skips consumed records (`>>8 & 0x3f == 0x3f`), packs non-actor records (bit 31 clear, max 2: side bit `0x80`, side `>0 → |2`, lateral `slice+0xcc + sign12`) and fires actor records (bit 31 set) through the formula above with exactly one `rand()` per record; `0x004409e0` builds the ped object (`0x004387a0` pool `0x004c1274`, lateral copy `+0x1c..+0x30`, family `+0x1dc`, type `+0x1e4`, side 3/1, trigger `player + rand%0x4fb1` — second draw — pool serial `0x004c1278`) or the cop branch (two more draws, `0x00449910`, `0x00406b80`). No new semantics beyond this section; the RNG order (one draw per record + trigger draw per ped + two per cop) is the replay-critical detail. Files: `analysis/oracle/rea-4520d0-dzhr.md`, `rea-4409e0-setup.md` (disc-derived, git-ignored).
- The think `0x00440dd0` has crossing types 4–7, a performer type 10 (animation 6), and standing types otherwise. States: 0 wait (trigger at the player position + `rand() % 0x4fb1`), 1 cross, 3 hit (animation 2, sound event 0x16), 4/5 get up, 7 performing.
- Artwork is FAMILIES.RSC category 2 (`0x0044acd0(+0x1dc, 2, n)`). Family 101 is the woman with a walker. About 50 people animations have walk, hit, fall and get-up frames.
- Per-course actor events (pedestrian / police): CITY 449/12, HIWAY 93/11, MEDLY 476/17, NAPA 85/20, CANYN 43/12.
  - Solid roadside objects (October 16, 2026; plan 16 Task 2, [handoff](agent-handoffs/human702-20261016.md)): **supersedes "outside-edge BORR slots are draw-only"**. `0x004373f0` (called from `0x004368f0` when the rider's fraction is within `+0x178` of mid-slice) sweeps the rider's own sub-slice (`+0x140`): every BORR slot (`+0x60` count, laterals `+0x6c`, units incl. `+0xcc`) against the box list `+0x68`/`+0x61`, then the billboards (`+0x3d`, entry `+0x40+16n`: `+2` type, `+3` box count, `+4` boxes, `+8` lateral). Boxes `{left, bottom, right, top}` are relative to the slot lateral·0x100; one-box slots with |lateral| ≥ 0x400 (and sub-slice `+0xac` bit 0x4000 clear, never written by the ring) are three times as wide. `0x00451cb0` zeroes `+0x61/+0x68`; the renderer fills them on first draw: `0x0044b5f0` → `0x0044b630(+0x60, i, 0)` copies frame 2's `+0x10` count and `+0xc` box pointer and sets `+0x64 |= 4` (billboards: `0x0044b980`, `0x44ba0d`). Frame 2 = `0x0040b750(0x0044ac00(selector, 0), 2)` (BORR, family category 0) or `0x0044acd0(selector, 4, 0)` (billboard). Billboards (`0x00437750`) of types 1/2 lower the box bottom to top − 0x2000 / − 0x3fff; types ≥ 3 take the marking branch. A hit builds the static `0x004c3e90` (`0x00437660`): position = rider sample + 0x80, lateral = slot·0x100, `+0xc4` 1, `+0x174` box width, `+0x17c` box height, `+0x178` 0, `+0x180` 0xc0, `+0x194` 0x1000000, speeds 0; then `0x00436a40` (32-bit `imul` products wrap with this mass) and the rider's `0x004049c0`. The static's lateral is in the slice/route frame (no replay `road_base` offset; verified at ticks 702–705). The class-1/2 crash vector at `0x404eeb` is rider `{0,0,0x200}`, bike `{0,0,0x100}` (all three native call sites now share `kContactCrashRiderImpulse`/`kContactCrashBikeImpulse`; see the October 17 crash-vector audit below).
  - Window-walk draws in the ring setup (October 16, 2026; plan 10 Task 5, [handoff](agent-handoffs/ring-terrain-20261016.md)): `0x00453e60` calls `0x00453b60` on each split (+1) and merge (−1) before recursing (visit-once). It builds a scratch walker with `0x00452f50(node+0x10, 0)` (section enter at 0) and re-enters `0x00452cb0(node+0x10, dir<0)` (split: section end; merge: 0), then makes exactly 0x51 `0x00453030(±0x100)` calls. Step 1 reaches the node (`0x00452e10` returns 2): a split enters +8/+12 at 0 and steps both by `outer & 0xff` = 0; a merge enters +8/+12 at their ends (`param_3` 0) and steps both by −0x100. Later steps move the half `(char)node+0x19` first, then the other (its return ignored). Only the terrain iterators draw (`0x004510c0` init, `0x004516e0` step); the BORR/DZHR/other iterators of `0x00452cb0`/`0x00452e10` are draw-free (static call graph). `DAT_00476cd4` is 0 during setup, so `0x00452fd0` never collapses, and the `+0x14 = 0x5100` limit is not reached in 81 steps. Assembly `0x452f1d`: a backward crossing into a previous road node enters it at its end and steps by minus the old position (literal, not a decompiler artefact). Napa: 3 splits + 3 merges, 9,089 draws from the old start state, previously absorbed into a fitted `ring.pre_draws` of 9,098; the setup actually starts at srand(1) with 0 pre draws (`ring-probe` K = 11,461 → state 1; frame-0 walker terrain exact). Earlier suspects eliminated: per-step `0x00451380` calls (native already calls on every forward step while a next record exists), BORR random gate (`0x00451cb0` draws only when word bit 11, byte +0x1c, is set; byte +0x14 only picks the side), second `0x00454240` (only for two-road slices).
  - DZHR jitter/sort REA re-confirmation (October 12, 2026; plan 10 Task 5 `TrackRing` input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00453e60` DFS driver (1356 chars: visit-once flag, per-type section enter, child recursion), `0x00451ee0` record jitter (1140: TWO draws per record — sample offset `rand % (spread+1)` plus level-gate `rand % 10` that consumes (`|= 0x3f00`) failing records) and `0x00451fc0` pairwise sort (906: bubble by `& 0x7fff0000` sample key). The level-gate draw is the RNG detail the merge's draw-count audit must include per record. Files: `analysis/oracle/rea-453e60-dfs.md`, `rea-451ee0-recjit.md`, `rea-451fc0-sort.md` (disc-derived, git-ignored).
- Ported October 12 (`src/race_pedestrians.cpp`). Object words: `+0x1c8` anchor (0x00440990 snaps back beyond 0x1ff), `+0x1dc` selector, `+0x1e0` loaded, `+0x1e4` type, `+0x1e8` state, `+0x1ec` side (index into the heading table `0x0046df30` = {0, 0x400000, −0x800000, −0x400000}), `+0x1f0` heading, `+0x1f4` pace (speed cap/accel ×100 via `0x004498c0`), `+0x1f8` trigger, `+0x1fc` lane, `+0x200` voice tick, and `+0x208..+0x220` code 0–6 sequences.
- Template `0x0046dde0` → object (`0x004387a0` mapping, template +0x1c.. → +0x148, +0x14c, +0x150, +0x154, +0x160, +0x168, +0x16c, +0x1b4, +0x1b8, +0x170, +0x174, +0x178, +0x17c, +0x180, +0x194, +0x1a4, +0x1a8, +0x1ac): half-width 0x1000, half-length 0x10, half-height 0x1e00, mass 100, drag +0x154 = 5 (0x19 once knocked down), first think interval 0x3c.
- Lane-clear test `0x00441500`: the nearest bike or car (mask 0x14) in the lane is found by `0x0044a070` (ahead for negative lanes). Clear when `((|v|&~0xff0)>>4 + (|v|&~0x1fe)>>1 + (|v|&~0x7f)·2)/(cruise+1) · ((factor·0x191)>>8) & ~0xf0, >>4` ≤ `|gap| & ~0xf`. The crossing factor is 0xff/0x80/1 for types 5/6/7, and `(factor>>2)+0x10` for the mid-crossing recheck.
- Voices: category-2 resources 2/3 are `MROF` chunks whose big-endian word +8 is the speech argument (0x64…).
  - Ped helper REA re-confirmation (October 12, 2026; plan 24 Task 4 input): pinned REA 6.4.0 pseudocode whitespace-identical for `0x004415b0` signal query (895 chars: lane-of via `0x0044a020`, nearer of backward/forward `0x00449ce0` mask `0x00468f00[lane≥0?0:2]` within 0x1e0, phase word `+0xf0` when within 0x801, else −1), `0x004414a0` arrival (406: shoulder-midpoint ×0x100 vs lateral, side-gated direction), `0x0044a020` lane-of (303: `(lateral − centre·0x100)/0xfa` signed, −1 for bit-0 params) and `0x004498c0` accel (385: heading wrap, `+0xec += +0x18·rate` clamp cap, `+0x5c = +0x128`). No RNG in any helper. Files: `analysis/oracle/rea-4415b0-pedsignal.md`, `rea-4414a0-arrival.md`, `rea-44a020-laneof.md`, `rea-4498c0-accel.md` (disc-derived, git-ignored).

### Crash run-back input (October 12, 2026, static)

In `0x0040a600`, a rider with `+0x324 == −1` (remote network) goes to `0x0040a9e0`, which reads its input packet through `0x00440410`. In the single-player branch, at animation 38, the player's input word `DAT_004b7ed0 & 0x7800` (racing state `DAT_004c5d7c == 1`, demo hold clear) steers the walk and zeroes the 1,200-tick timer. Bits 0x800, 0x1000, 0x4000 and 0x2000 come from the same keys as accelerate, brake, left and right (`0x00465fb9` codes 0x94/0x96/0x93/0x95). With none held, the rider heads for the bike at speed 1200 (`0x0040a9d0`). For the player on slice type 1, a bike or rider lateral outside `[+0x14c, +0x158]` ends the race (`0x00416010(5, 0xb4)`); the port does not have this yet.
  - Walk-input REA check (October 12, 2026; plan 24 Task 3 input): `0x0040a9e0` has no decompile export and no REA procedure entry (network-only helper) — port from the `0x0040a600` parent (already confirmed) + disassembly. `0x00440410` packet reader confirmed whitespace-identical (241 chars). Re-fetches of `0x0040a600`/`0x00403d60` are byte-identical to the prior turn's files (deduped), proving toolchain determinism across turns. File kept: `analysis/oracle/rea-440410-netpkt.md` (disc-derived, git-ignored).
  - On-foot contact REA (October 12, 2026; plan 24 Task 3 input — corrects the prior bullet's claim that `0x004046b0` is export-less): `analysis/decompiled/004046b0.c` exists, and pinned REA 6.4.0 pseudocode is whitespace-identical to it (3854 chars: type-8 rider-half contact — car-kind short-circuit, `0x00436830(4)` thud, `+0x17c·2` vertical set, speed-proportional `+0xf4` shove with 0x150 clamp, `+0xec` quarter-bleed, impulse zeroing + clamps, `0x00407410` re-anchor when crashed). Only `0x0040a9e0` remains export-less/network-only. File: `analysis/oracle/rea-4046b0-onfoot.md` (disc-derived, git-ignored).
  - Bounds-wreck disassembly verification (October 12, 2026; plan 24 Task 3 input — no REA needed for a region check): `original-disassembly.txt:12289+` lines 70–92 show the exact sequence — slice-type-1 gate (`cmpb $0x1` at `40a704`), bike lateral `+0x28>>8` vs `[+0x14c, +0x158]` (`40a709–40a721`), then rider lateral via `+0x140`-slice vs the same bounds (`40a723–40a741`), either out-of-range → `0x00416010(5, 0xb4)` (`40a743–40a74d`) + `+0x2f0 = 0` (`40a752`). Matches the paragraph above instruction-for-instruction; the port's missing piece is confirmed missing, not misdescribed.
  - Walk-steer gate disassembly verification (October 12, 2026; plan 24 Task 3 input — no REA needed): `40a835–40a85d` show `DAT_004b7ed0` tested with `testb $0x78, %ah` (= bits 0x800/0x1000/0x2000/0x4000, i.e. the documented 0x7800), then three gates in order — player-bike identity (`0x004642d8`, `40a843–40a84a`), demo-hold clear (`0x004753c8`, `40a84c–40a853`), racing state == 1 (`0x004c5d7c`, `40a855–40a85d`) — before the per-bit steer branches (`40a85f+`: `testb $0x10, %ah` = accelerate-bit path first). The Task-3 capture trilogy (a/b/c) tests exactly this gate: (a) no bits → auto-walk, (b)/(c) held bits → directed. Porter implements the gate chain verbatim in `step_crash`.
  - Timer + police-owner disassembly verification (October 12, 2026; plan 24 Task 3 input — no REA needed): `40a649–40a657` load the `+0x138[state]` elapsed word and compare against `0x4b0` (1200); over-time falls to `40a65f–40a670`, where a live cop owner (`DAT_004642dc != 0` and its `+0x2fc` == the walking rider-half) skips via `je 0x40a6d5` — the documented police exemption, verbatim. Otherwise `+0x2f0 != 0` continues the walk and the player-bike check at `40a67c–40a694` wrecks via `0x00416010(5, 0xb4)` when the timer expires. Porter: 1200-tick auto-remount timer + cop-owner skip in `step_crash`.
  - Proximity-box disassembly verification (October 12, 2026; plan 24 Task 3 input — no REA needed): `40a785–40a7c9` compute `Δpos = bike − rider` and `Δlat − 0x1770`, then require `|Δpos| ≤ |Δlat|/128` (sign-fixed `sarl $0x7` with `0x7f` rounding at `40a7a0–40a7b9`) and `|Δlat − 0x1770| ≤ 0x1770` (`40a7bb–40a7c9`) — i.e. longitudinal closeness scaled by lateral/128 and lateral within 6000 of the bike after the 6000 offset, matching the ported `recover_crash_actor` box (±9/±6001 with the `0x00449c20` sign quirk, which this region explains: the `subl $0x1770` precedes the absolute value). `+0x2f0` tested at `40a7cb–40a7d3` before the `0x25`-case remount path. Porter: already ported; this closes the last unverified constant in the box.
  - Bike think outside crash states 1/2 (October 24, 2026): `0x00407a90`
    writes bike +0x7c = 0 (animation frame pointer; the same word
    `0x00404560` resets) and returns before the `0x0043ae90` advance, after
    copying half +0x1e9 to +0x81. During state 3 the bike is therefore not
    drawn; rider animation 41 shows the lift. Ported in
    `Race::crash_bike_animation`.
  - Bike-think disassembly (October 25, 2026; `original-disassembly.txt`):
    `407a91-407aa7` calls `0x0040b1a0` only when the bike equals
    `DAT_004642d8` (player identity; native: `&r == &riders_[0]`).
    `407abc-407b0b`: +0x7c clear, then if half +0x1e9 is nonzero, keep it
    only while bike +0x1c arithmetic `>>8` equals half +0x1ec and the
    unsigned bike +0x10 minus half +0x1f0 is below 12, setting
    +0x1e9 = (gap>>2)+1 else 0; bike +0x81 copies the half byte.
    `437e69-437e6f` stores the current tick into node +0x10 immediately
    before think, so +0x10 already equals the tick inside `0x00407a90`.
    `0x0040b1a0` (`40b1a0-40b1fa`) drains `DAT_004c5f10` by `dt<<8`
    (dt = bike +0x18) while boosting (stop at <1 via `0x0040b170`) and
    recharges by `dt * DAT_00465a28` while the
    `(charge + signbias)>>8 < DAT_00465a30`; native `nitro_charge_tick`
    matches this on the per-rider charge (player only, so equivalent).
    REA 6.4.0 `function 0x0040b1a0` is whitespace-identical to the project
    export (438 chars); `0x00407a90` has no REA procedure entry (gap #8),
    so that slice rests on the project export plus the disassembly above.
  - Scheduler-word lineage (October 25, 2026): the raw Bike word +0x10 is
    never stored in native (no `i32(0x10)` write exists); the +0x10 image
    is `Rider::sched_last`, which the field replay compares against golden
    `bike_10` tick-for-tick and which bike think stores to `ticks_` just
    before use. Native `r.sample` equals bike +0x1c>>8 at every bike think
    over the 2400-tick field replay (scratch probe, 0 mismatches, reverted).
    The flash therefore reads `r.sched_last`/`r.sample`: behavior-identical
    to the disassembly words at think time.
  - Remount-gate disassembly verification (October 12, 2026; plan 24 Task 3 input — no REA needed): `40a990–40a9b4` show the walk-function exit — state-3 (`cmpl $0x3`) + anim-frame `0x29` gate, then `call 0x00404560` (remount reset) and return; the `0x26`-anim early-return branch sits just above (`40a992–40a9a5`). With this, `0x0040a600` is verified end-to-end: entry/network/contact branches (REA parent), timer/police, bounds-wreck, proximity, steer-gate, and now the remount exit. The Task-3 porter owns the full function body; only captures + port remain.

### Rider–vehicle contact (October 12, 2026, plan 11a Task 1)

Detection is the scheduler contact pass `0x004368f0`, called once per tick
from the scheduler driver `0x004383b6` (after the `0x004383ab` ring resort; a
second resort follows when any contact fired). A node participates as the
primary only when its schedule word `+0x10` equals the tick `DAT_004b7ecc`
and `(byte)+0xc0 & 6 == 6`; the inner loop walks forward while
`(byte)+0xc0 & 5 == 5`, breaking at the first pair whose longitudinal gap
exceeds the summed half-lengths (`+0x178` each). Lateral must be within the
summed half-widths (`+0x174` each), the slice-half bits (`+0x36` of the
`+0x2c` node, masked with the primary's, `& 3`) must share a roadway, and
height within the summed half-heights (`+0x17c` each). Split-roadway
exclusion is that `+0x36 & 3` shared-half test: rider and car on different
roadways never collide (the native `Race::step` pair loop already has this
as `sa.dual && a.route != b.route`). On overlap both `+0x1a0` links are set,
`0x00436a40` separates them, then each side's `+0x1a4` handler runs (bike:
`0x004049c0`; car: `0x0044efa0`, horn only).

Separation `0x00436a40` uses masses `+0x194` (bike 256, car 1800), impulses
`+0x184`/`+0x188`/`+0x18c`, contact flags `+0x19c` (bit `0x40` = "other width
contains ours", set in the forward-overlap prologue), and previous-position
copies `+0x30..+0x44` to choose the separation axis. Longitudinal moves go
through course-ring traversal `0x00439270`, which has no scratch equivalent,
so the oracle fixture pre-seeds impulses instead (documented approximation).
  - Motion-pair REA verification (October 12, 2026; plan 10 Task 5/6 merge `36c03dc`): pinned REA 6.4.0 pseudocode whitespace-identical for `0x00439920` motion integration (2526 chars: impulse halves into `+0xf0/+0xf4/+0xec`, heading-split `+0x108/+0x10c`, position history shift) and `0x00439270` road advance (1439: course-ring traversal the fixture pre-seeds around). (Requested `0x00439a80`, but that address resolves to `0x00439920` in REA's database and the file was renamed to `rea-439920-motion.md` to match content; the actual `0x00449a80` follow choke was confirmed in an earlier turn.) The merge's contact-walk motion rests on these. Files: `analysis/oracle/rea-439920-motion.md`, `rea-439270-roadadv.md` (disc-derived, git-ignored).

The bike-side handler `0x004049c0` against cars (`+0xc4 == 0x10`): crashed
riders (`+0x2f4 != 0`) never take a new crash. Live riders with the tall-car
height (`+0x17c = 8960 >= 0x2000`) take class 2 when `+0x19c & 0x40` is set,
class 3 otherwise (class 1 needs a short other object and never fires for
cars). Player-only sounds via `0x00436830` (two `0x00441750` draws, same as
the bike-bike path). Class 2/3 reach the knock-off decision `0x004052c0`
(class 2 scales `local_20` by 7/4, class 3 by 1/8; stamina damage
`0x004050c0`, bike damage `0x004051e0`), then `0x00403d60` with reason 2
(animation 29) on knock-off — no `crash.cpp` change needed since reason is a
caller parameter. Class 2 without knock-off transfers speed when the gap is
`< 0x1771`; class 3 shoves laterally and cuts speed.

Car dimensions come from template `0x00475698` via `0x004387a0`: `+0x174 =
16384`, `+0x178 = 64`, `+0x17c = 8960`, `+0x180 = 192`, `+0x194 = 1800`.
The car reaction `0x0044efa0` is horn-only (sound 18, player-gated): no stop,
swerve, or speed change. Fixture `analysis/oracle/napa-car-contact.tsv`
(git-ignored): 256 isolated `0x004049c0` calls — 32 fast-contained crashes
(reason 2, anim 29), 32 slow-contained speed transfers, 64 side shoves, 128
crashed-rider rejections. Open: stamina words rise in the crash cases with
no writer found yet; the golden captures it and the port must reproduce it.
End-to-end parity needs the plan 10 `napa-crash-car` tick scenario.

Contact-class correction (October 14, 2026; plan 10 Task 5, REA 6.4.0
re-confirmation of `0x004049c0`/`0x004052c0` identical to the exports):
contained bike-bike hits are class 1, not class 2 — both bikes are tall
(`+0x17c = 0x2300 >= 0x2000`) but the other bike is short-ish
(`0x2300 < 0x2800`) and unflagged (`+0xc4 & 0x10 == 0`), so lines 77–88
route there; the bike caller (`+0xc4 & 0xc != 0`) passes stamina param 0
and bike param `(mag+bias)>>4`. Sounds, class routing and knock magnitudes
read the unscaled impulses; elasticity (`0x180` = 192) applies after,
including `0x18c` scaling. Gap crashes (`0x00403d60` via the class ladder)
use the handler's `local_18` pair (rider `{0,0,0x200}`, bike `{0,0,0x100}`),
then fall through to the nudge + `0x00404f93` tail with no kick. Tick-208
proof: the class-3 `knock_off(0, swing, 3)` tail kick closes steer 2437 to
exactly 2392 in `sim-napa-solo-human.tsv`. **Correction (October 17, 2026):
this pair was recorded here as `{0,0x200,0}`; the disassembly stack stores
prove the second component is zero (see the crash-vector audit entry).**

Crash-vector audit (October 17, 2026; plan 10 Task 5/6 fidelity). Every call
to the crash entry `0x00403d60` was re-read from
`analysis/original-disassembly.txt` and checked against the native sites.
`0x00403d60` takes its impulse arrays as `__fastcall` param_2 = the rider
half (`param_1+0x2fc`) and param_3 = the bike, and adds component `[i]` to
`+0xec`/`+0xf0`/`+0xf4` in that order (decompile lines 99-106), so a
component's axis is observable in the actor's speed/steer/vertical words.

| site | reason | rider (param_2) | bike (param_3) | native |
| --- | --- | --- | --- | --- |
| `0x404eeb` (`0x004049c0` class 1/2 gap) | 1/2 | `{0,0,0x200}` | `{0,0,0x100}` | `contact_static`/`contact_car`/`contact` — now the shared constants |
| `0x4053b2` (`0x004052c0`) | 4 | `{-80,0,0}` | `{0,0,0}` | `knock_off` reason 4 |
| `0x4053b2` (else) | other | `{-256,0,1792}` | `{0,0,1024}` | `knock_off` default |
| `0x434825` | 6 | none | none | not modeled |
| `0x438e34` | 6 | `{-0x600,0,0}` | `{0x400,0,0}` | not modeled |
| `0x43a50b` (`0x0043a190` high-jump wreck) | 6 | `{0,0,-0x50}` | `{0,0,0x50}` | not modeled |
| `0x43ac62` (`0x0043a610`) | 3 | none | none | `walls` reason 3 |
| `0x43ad96` (`0x0043a610`) | 6 | `{0,0,0x50}` | `{0,0,0x10}` | `walls` reason 6 |

Finding: the class-1/2 rider impulse was written `{0,0x200,0}` at the two
`contact_car`/`contact` sites (`contact_static` already had `{0,0,0x200}`),
which put the whole 0x200 on the half's steer rate `+0xf0` instead of its
vertical velocity `+0xf4`. The `0x404eeb` stores are
`movl $0x200, 0x28(%esp)` after `pushl %edi`, i.e. `[esp+0x24]`, the third
word of the rider struct at `[esp+0x1c]`; and `movl $0x100, 0x34(%esp)` the
third word of the bike struct at `[esp+0x28]`. Fixed and pinned by
`rr::kContactCrashRiderImpulse`/`kContactCrashBikeImpulse`; the unmodeled
sites `0x434825`, `0x438e34` and the `0x0043a190` high-jump wreck remain
(fall/high-jump and network/attack crash paths) and are listed here rather
than silently waved through.

Previous-copy sync (October 14, 2026; plan 10 Task 6 fix order #1a):
`0x00438930` copies words `0x1c..0x2c` to `0x30..0x40` (the int-indexed
`piVar1[0xc..0x10]` loop — five words, not six), and `move()` re-syncs the
same five every tick, so the build values matter for tick 1 and for frozen
riders. `place_rider` now does the same copy. Effect: the demo golden's
tick-208 contact disappears (lateral gate culls with true prev-laterals)
and its first divergence moves to tick 222 with rng in sync.

### Options screens and pause (October 12, 2026, plan 18 Task 1)

Tables dumped from the reference EXE (image base `0x400000`): screen table
`0x0046a390`, item rows 0x38 bytes. Restroom `0x10` has 13 items at
`0x00473860` — the sound mix (handler `0x00401df0`) and Configure Input init
(`0x0040fd90`) live here, NOT under Graphics Options. Jukebox `0x11` has 9
slots (8 live + 1 dead). Configure Input `0x12` has 22 slots (9 rebind rows
via `0x0040e580`, 2 defaults rows, Done, spacer, 9 key labels);
`0x0040fd90` is device init returning the target, not the screen itself.
Graphics Options `0x13` has 8 slots (Resolution/Bikers/Videos live, 4 dead).
The sound mix is a Win32 modal dialog (resource id 300), not a menu screen:
seven `msctls_trackbar32` sliders, range 0-100 step 1 stored as `100 - pos`, defaults 100/80/80/80/80/80/80,
mute bit `0x00463030`. CORRECTION (October 12, 2026; export-grounded `00401e20`/`00401ee0`/`004020a0`/`004022d0`, REA gap — `0x00401e20` not in REA's procedure DB): every scroll AND edit-box change live-applies via `PostThreadMessageA(thread 0x4753f4, msg 0x4c8018)`; test button `0xbc1` posts `0x4c801c`. The earlier "no preview sound" claim was wrong. OK (id 1) harvests sliders + mutes + master into `0x00463030..`, writes 64-byte REG_BINARY `Mixer`, posts `0x4c800c`, EndDialog(1); Cancel (id 2) skips harvest/registry (live-applied values NOT rolled back) but still posts `0x4c800c`, EndDialog(2). Plan 18's native Sound screen must live-apply to active mixer voices (already required by the jukebox-handoff research) AND match the no-rollback-on-cancel semantics.
Item-table detail (October 12, 2026; EXE dumps): `0x10` rows carry desc slots 65–87 with handlers `0x445150/0x445370/0x445310/0x445460/0x445490/0x445560/0x445390/0x445590` (restroom services), sound mix row (cap 82, `0x401df0`) and controls row (cap 84 → target 18 = screen `0x12`, `0x40fd90`); quit row `0x4453c0`, Done row `0x41f4f0`. `0x12` rows: 9 rebind (caps 256–264, `0x40e580`), 2 defaults (382/383, `0x40fdf0` — caption `0x17e/0x17f` selects keyboard/joystick table `0x466618/0x466630`), Done, spacer, 9 key labels (flags `0x10000`, caps 160–168, filled by `0x40e7b0`). `0x11` rows now carry descs 231–236/496 (band/track names). Controls `0x0044f800` ORs keyboard and
joystick bits into `0x004b7ed0` once per frame. There is NO original in-race
pause screen: the frame driver `0x00442e10` has no pause branch; pause is the
modal `MessageBoxA` `0x00443120` and quit-confirm is the modal pump
`0x004431d0`. F10 cycles `MiniDash` 0-1-2-0 (full/mini/off); PrintScreen sets
the screenshot flag. Open: the menu key(s) reaching the `0x4235f8`/`0x423625` cycle handlers, and the Escape race-quit path.
  - Rebind flow + conflict rule (October 12, 2026; plan 18 Task 1 — export `0040fbf0.c`, native `settings.cpp`/`frontend.cpp`): `0x0040fbf0(new_row)` scans all 11 rows at `0x465fb8` (stride 2); any OTHER row with the same 2-byte code is zeroed + relabeled via `0x0040e7b0` (rows 9/10 and 2/3 take extra label-refresh side paths only). CONFIRMED PORTED: native `apply_rebind` zeroes same-code rows and stores the new code (tested: unbinds, never swaps); `0x0040fdf0` defaults-copy and `0x0040e580` row-arm are ported (`reset_keys`/`read_control_tables`, tested against EXE tables). Key labels (`0x0040e7b0`, 61 KB formatter) still uncompared — label TEXT fidelity open.
  - MiniDash/OpponentList dispatch: SUPERSEDED by the Menu-key-dispatch bullet below (October 12, 2026): `0x423777` (`/` key) toggles `OpponentList`, it is not ON-only; `0x474f48` is the HUD opponent-gap mode byte, not the OpponentList setting. (Original text kept for trail: registry strings `MiniDash`=`0x467ac4`, `OpponentList`=`0x467ab4`; in-race F10 cycle at `0x4432b8` confirmed.)
  - Menu key dispatch (October 12, 2026; plan 18 Task 1 — disasm `0x4232a7–0x423f74` wndproc + jump tables): key messages dispatch via table `0x423f80` (0x100/0x104 key-down → `0x4232f9`: Shift/Ctrl/Alt set latch flags `0x496a08/0x496008/0x496098`; 0x101/0x105 key-up → `0x423345`: ESC toggles byte `0x46affc`; 0x102/0x106 → `0x423649` name-entry). Sub-switch at `0x42371d` (after `0x402e80`): `+`/`-` halve/double dword `0x475648`, `/` (`0x2f`) → `0x423777` TOGGLES `OpponentList` (`0x4753a8` 0↔1 via `sete`, clears `0x46afdc`). PORTED October 12, 2026: native `toggle_opponent_list` + menu `/` key wiring (`85b09d1`, `d68d44b`; pinned by `frontend_test`). `0x4235f8`/`0x423625` fall-through pair (CTRL-latch ? cycle `0x474f48` [HUD opponent-gap mode, `0x447383` draws it] : cycle MiniDash) is entered from the nearby `0x4235xx` table (file offset `0x233d0`); the exact command-ID→handler row mapping stays open but is no longer needed for the toggle semantics.
Plan 18's screen map is corrected accordingly: Sound hangs off Restroom as a
native look-alike of dialog 300, Pause is purely native.
  - Pause-pump keys (October 12, 2026; plan 18 Task 1 Step 3 — exports `004431d0.c`/`00443120.c` + disasm call sites): modal pump `0x004431d0` exits on key-up/`syskey-up` (`0x101/0x105`): F10 (`0x79`) cycles MiniDash, PrintScreen (`0x2c`) sets the screenshot flag, any other key exits 2 (resume); with param_1≠0 Escape (`0x1b`) exits 1, else 2. WM_TIMER + joystick-button exits 2. Three call sites: A (param 1, string +0x4f0) — return 1 → `PostQuitMessage(0)` + outcome `0x4c5d7c = 5`; B (param 0, +0x4f4); C (param 0, custom format +0x5f0). Native gap: race loop has no F10/PrintScreen handling (`cycle_mini_dash` is menu-test-only); the opponent toggle is PORTED with menu `/` wiring (`toggle_opponent_list`, `d68d44b`) — plan 18 Task 6/7 port scope for the rest. Escape race-entry point still open.
  - Escape race-entry (October 12, 2026; plan 18 Task 1 — time-boxed, entry point still open): eliminated the run-back control dispatch (`0x40a835+` reads control-word bits `0x78/0x10/0x08/0x40/0x2000` for on-foot steering, not quit) and the key-table at `0x436352` (rebind display codes). The race-side call site invoking quit-confirm pump variant A (param_1 = 1 → outcome 5) was not found within the box. Native behavior already matches the confirmed outcome-5 rule (ESC → quitting → second ESC quits wrecked). Per the plan's trace rule: stopping here and reporting rather than guessing.
  - Press-a-key capture (October 13, 2026; plan 18 Task 7 Step 1 — disasm `0x0040e690`/`0x0040e760` + exports `0040fb50.c`/`0040f6d0.c`): `0x0040fb50` drains WM_KEYDOWN–KEYUP then scans the 256-byte `GetKeyboardState` for a pressed key; `0x0040f6d0` polls joystick (X<15000 → `0x28`, X>45000 → `0x26`, Y<15000 → `0x25`, Y>45000 → `0x27`, R with flag → `0x2a/0x29`, else buttons via `0x0040f7a0`, 0 when idle/`0x465fe0` clear). `0x0040e760` loops both; `0x1b` (Escape) skips the store → cancel with relabel only, no change. Keyboard codes store low-byte-first (`key,0`), joystick high-byte-first (`code,0`). Registry `Controls` = 0x16-byte `0x465fb8` via `0x0040e710` (`KEY_ALL`, value `0x466648`); read back by `0x0040e5c0`.
  - Action table `0x00476c00` (11 rows × 8 bytes: u32 control bit + u32 analog flag): 0 accel `0x1` dig, 1 brake `0x2` ANALOG, 2 left `0x4` dig, 3 right `0x8` ANALOG, 4 punch `0x10` ANALOG, 5 kick `0x20` ANALOG, 6 backhand `0x40` ANALOG, 7 look-back `0x80` ANALOG, 8 unused `0x100` ANALOG, 9 nitro `0x200` dig, 10 camera `0x400` ANALOG. Row 8 (bit `0x100`) has no native control bit and no game binding — dead in the original too. Analog rows use dead zones `0x3fff/0xbffd`, digital rows `0x6fff/0x8fff` (`0x0044f880` branch, matches the `steer_dead_zone` comment).

### Rider/bike/shadow nodes (October 12, 2026, plan 11a Task 3 trace)

Start-grid ordering follow-up (October 12): pinned `npx rea-agents@6.4.0`
on the hash-verified disc EXE confirms `0x00443af0`, `0x00443cd0` and
`0x00438610`; all three pseudocode outputs exactly match the existing exports.
  - Independent re-confirmation (October 12, 2026): same three functions re-fetched via pinned REA 6.4.0 — `0x00443af0` (376 chars), `0x00443cd0` (533), `0x00438610` (1587) — all whitespace-identical to the project exports a second time. Files: `analysis/oracle/rea-443af0-order.md`, `rea-443cd0-worldobj.md`, `rea-438610-halfdispatch.md` (disc-derived, git-ignored).
The original emits the complete next nearer world strip (lanes and terrain,
both roads and join when split) before the farther slice's object pass.
The unsigned rendered-position low-byte `<0x80` test inserts scenery between
actors; it does not delay only front-half actors past a road strip. Saved
`napa-start` and `napa-drive` frame-0 tails place the near shadow/body after
the nearer road, with body bottom y 164.5 below the road seam y 152.5.
The native front-half-only delay remains an approximation, including its
placement before nearer hills/buildings. Full trace, exact call sites, masks,
evidence limits and correction sequence:
[start-grid order research](research/2026-10-12-start-grid-order.md).
This confirms the original rule and captured near-grid layering, not native
constructed-list/full-frame parity.

The bike+shadow dispatch `0x00446250` emits shadow first (clip 0, ground
without vertical) then body (clip 1, ground + vertical>>8), re-running view
selection `0x0043af60` after the `+0x90` swap on each side. The armed variant
`0x004463e0` adds a weapon overlay: a third `0x0044ba40` node with the
`0x0040b750` frame selection — native `rider_nodes` emits only 2 nodes, so
the reported club-without-attack is a missing overlay, not a wrong base
frame. Shadow colour `0x00446350` is `0xdf` only when the body colour is cop
(`0xde`), else `0xe0` (no live-race effect: the game only calls with bank
0). The `0x0044d300` dispatch has NO shadow darkening/translucency mode:
shadows take the same textured path with plain index-0 transparency
(candidate cause closed by code reading; pixel proof still needs the frame
match). Orientation math (`0x00413ae0` vs `actor_angles`) matches on
reading. Scene dump v4 (October 12) extends the proxy with
`dump_rider_objects` (full `0x340` bike + rider-half per ring node), and
`napa-drive-v4` recaptures frames 0/150/300/750 with rider object state;
v4 pixel mode still passes (0 differing pixels). `--native-actors`
(oracle-scene-test) rebuilds actor nodes from the v4 objects and CONFIRMS
FAILING 4/4 frames — isolated to view/subframe selection base (plan
candidate 2): native `read_actor_sequence` stores the descriptor-table
index into `render[0..3]`, but the original `0x0040b6f0` group lookup
resolves frames 47/54 at CANS group id 28 (native `views[27]`), unreachable
from the stored base (view clamps to `byte(d[6])-1`). REA second opinion on
`0x00446250` backs the dispatch structure; the suspicious piece is
`0x0040b200`'s `+0` store (the `0x40b5a0` directory search is id-keyed, so
the base must be a group id, not a descriptor index). Next: hook
`0x0043af60`+`0x0044ba40` entries via trampolines, log per-frame, recapture,
fix `read_actor_sequence`/`select_actor_view` against ground truth.
BONUS: opponent colour is the 1-based grid slot in `DAT_004c7f74` (frame 750
shows colour 0xa = slot 10); player colour is the live rider-table row
byte+5 (writer unidentified).

### Playtest audio and AI corrections (October 12, 2026)

Reference `../cdrom/ROADRASH/ROADRASH.EXE` SHA-256
`73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`
matches `analysis/inventory.json`. No legacy Android executable was used.

- Listener: `0x00437c30` refreshes `DAT_004b7ef4` and derives camera speed
  from the previous position before `0x00434db0` attenuates vehicle sounds.
  Native `SoundSystem::update` previously calculated speed but never assigned
  the new listener position; the player left the 0x1e00 hearing radius.
- Overtake gate: assembly `0x00408fe7` uses `xor al,al`, including when the
  scan is null or equals brain+0x168. Caller `0x00409000` tests **AL**, not
  the other bytes returned by the decompiler. Native incorrectly returned
  true whenever a replan target existed, leaving the racing point stale.
  The distinct-candidate path `0x00408e90..0x00408fdd` writes brain+0x194
  (lateral), +0x198 (position) and conditionally +0x19c (speed). Those writes
  now exist natively: compare lateral +0x28 to lateral, apply half-node width
  0x3000 plus 0x5000, signed-byte lane bounds/side flip, then the far/close
  speed cases. This is an assembly-backed translation, not full-field oracle
  parity: scheduler scan order, target size assumptions, AI states and shared
  RNG acceptance remain open.
- Scalar-only `set_demo_hold` fixtures had all-zero per-level speed arrays;
  the sixty-tick replan replaced their scalar speed with zero. They now use
  the opponent-style fallback when the speed table is absent. Existing
  imported personality speed tables remain authoritative.
- Visual audit (superseded October 16, 2026 for the crash/countdown text: the
  original has no wipeout banner and the countdown is now the original digit;
  see "Race countdown banner" below): `draw_hud` still adds SDL debug text `WIPEOUT!` during crashes;
  `draw_pause` draws a translucent full-screen shade and SDL debug text.
  Neither is a pixel-exact original screen. The original pause message uses
  `MessageBoxA` in `0x00443120`; see the options/pause trace above. Native
  crash actor playback exists, but this audit did not capture original crash
  frames or establish complete crash-frame parity.

### Race view modes and low-detail bikers (October 12, 2026, plan 18 Task 4 Step 1)

- Mode select `0x00448870` (`src/view.hpp::view_mode`, already ported): HiRes on → 640×480 (Q16 shift 15, bit-0 clear); off → 320×240 (shift 16); inset centers the 320×240 view at (160,120) in a 640×480 display. Choice `0x004457f0` maps 1/2/3 → low fullscreen / low inset / high fullscreen.
- Dashboard `0x00416310` (export `analysis/decompiled/00416310.c`): HiRes off → horizon art `Images/Horizons/L`, meter `LilMeter` (`IMAGES/LILMETER.BOB` on disc), dash BOB `Images/Bikes/<class>/kLoDash.BOB` (kilometers) or `mLoDash.BOB` (miles, `0x0042f490` metric check). HiRes on → `Images/Horizons/`, `BigMeter` (`IMAGES/BIGMETER.BOB`), `kHiDash.BOB`/`mHiDash.BOB`. Low mode uses SEPARATE low-resolution artwork, not a scaled high dash. `Images/Bikes/*/KLODASH.BOB` + `MLODASH.BOB` exist on disc for RAT/SPORT/SUPER.
- Bob file select (`0x4436a0` region disasm): HiResBikers (`0x00475384`) nonzero → `Data/Bikers/Hi<X>.DAT`, else `Data/Bikers/Lo<X>.DAT` (strings `0x46f080/0x46f070` + `.DAT`), with fallback to Lo when the Hi open fails (`0x00443380`). Disc holds `HIBOB.DAT` + `LOBOB.DAT` (plus HI/LO SHADOW/SHADOC/COP). Disasm `0x448c8a` forces HiResBikers = 1 after the Graphics Options bikers popup applies. Low mode uses SEPARATE low-resolution sprites, not halved high sprites.
- Native gaps (this task): game always renders high view (`view_mode(true)` hardcoded in `game_main.cpp`), always loads `HIBOB.DAT`, `hud.cpp` implements only the high dashboard. Captures `napa-drive-low` / `napa-drive-inset` (Step 2) pending at trace time — see handoff.

### Oracle capture outage (October 12, 2026, evening — blocks plan 18 Task 4 Step 2 and all captures)

- Signature: every capture since ~12:22 faults identically — page fault read `000001FE` at `7BF63CC2`, 2 sim frames dumped, `winedbg` wedges the run (exit 124; exit 5 when the crash dialog is dismissed, as the owner did at 23:33 — the dialog screenshot shows the game DID reach Demo Mode countdown, so this is a race-start crash, not bootstrap). Confirmed across 10+ failed captures in `analysis/oracle/failed-captures/`.
- Last successes: `napa-drive-v6.tsv` etc. at 06:10–07:52 (900–87,738 frames). Zero successes after the 11:43 `AWEMAN32.DLL` rebuild (`0ab97ab`, car fields + ring cursors).
- Eliminated by test: parent-commit DLL rebuilt and probed — same fault, so NOT the `0ab97ab` proxy change. Staged-controls side effects ruled out (prefix copy removed; registry restored byte-identical to backup; no HiRes/Inset keys remain). Drive-2 vs replay-input modes both fault identically.
- Not the cause either: leftover `winedbg` processes were cleared between probes; failures persist on a clean prefix.
- Open suspects: Wine Staging 11.18 behavior change, `AWEMAN_REAL.DLL`/install drift, or a game-side time bomb (midday→evening wall-clock? No — failures span 12:22–23:38 while mornings worked; a date check is unlikely but untested). Next step for the oracle owner: run the game once WITHOUT the proxy (rename `AWEMAN32.DLL`) to separate a game/Wine fault from a proxy fault; if it still crashes, bisect Wine behavior (virgin prefix + `oracle/install.sh`); if clean, bisect proxy hook sites (`FRAME_HOOK`/`hook_draw` first — the fault address is Wine-space, consistent with a bad display-list pointer from `dump_scene`).
- Prefix/DLL/registry left exactly as found (current DLL hash `c474993f…`, restored after the parent test; registry byte-identical to pre-diagnosis backup; no staged `.controls` files remain).

## Resolution assumptions (October 13, 2026, plan 19 Task 1)

- Fixed screen sizes in `src/` (logical = 320×240 view space, stays; surface = must scale with render scale `s`):
  - Logical: `ActorProjection::occlusion = 240*65536` and `actor_node(..., logical_width=320)` (`actor_render.hpp`); `ceiling = 240*65536` and `MarkedGroundInput` width 320 (`course_scene.cpp:170,246`); `RgbImage` 640×480 menu background + title/description/dialog boxes keyed to 640/480/320 (`frontend.cpp:189,268,279,1167,1309-1316,1378,1410` — Task 5 scales these); dashboard artwork validation 640/320/190×64/95×32 (`hud.cpp:33-34`, separate low art per the Task 4 trace above — Task 5 scales the compose).
  - Surface: `TextureEdge` rows `std::array<TextureEdge,480>` (`texture.hpp:17`, `texture.cpp:30,60` — Task 3 replaces with `RasterScratch` sized to surface height; the wide path `TextureVertexWide`/`RasterScratch64` already exists); `CourseScene::draw_horizon` requires a 640×480 surface and writes rows 0..319 from the 720×320 horizon (`course_scene.cpp:161-164`); race `Framebuffer{640,480}`, rgba buffers, SDL textures, clip rect and pause/menu textures (`game_main.cpp:484-512,566,734-745,989-993,1140,1341,1361`); `road_draw.cpp` debug clip/quad (viewer-only).
  - `display.apply(480)` calls set the 640×480 logical presentation; Task 8 moves these to `640s×480s` (510s race).
- Precision (Step 2): over the Napa sweep's 2,509 views, 4,807,112 of 5,392,376 node coordinates (89.15 %) carry non-zero low 15 bits — real sub-pixel content, so higher scales gain genuine geometric precision rather than just sharper sampling. The original high mode discards these bits (shift 15 + bit-0 clear, `0x00410160`/`0x0040ffc0`); `view_mode_scaled` (Task 2, landed) keeps them for `s = 2..4`.
- Render-time randomness (Step 3): none. Scenery placement (`roadside_objects`, `place_dzhr` in `scenery.cpp`) and BORR hazard spans (`road_hazard_spans` in `race.cpp`) consume `random()` only during course setup (`CourseScene` ctor per section with an isolated MSVC-recurrence stream, `course_scene.cpp:55-78`; hazards at load via `set_hazards`). `draw_nodes`/`render_nodes`/`draw_horizon` consume no RNG, so rendering extra or fewer frames cannot move any random stream — Task 7's independence test needs no RNG relocation.
- `0x0040e7b0` label formatter (REA `rea-agents@6.4.0` Ghidra second opinion, evidence `ev_5dfb0c1d`): row index = `(param - 0x465fb8) >> 1`; rows < 9 write the word at `0x466670` into the 0x40-byte slot at `0x481bc8`; other rows sprintf the keyboard/joystick code names into the slot at `0x481a08`. Callers `0x40e760/0xfbf0/0xfc70/0xfd90` confirmed. No impact on plan 19 beyond documenting the label path Task 7 ported.

## Plan 19 Task 9: side-culling trace — Hor+ widescreen answer (October 13, 2026)

**Question:** how does scene construction cull nodes at the left/right of the 320-wide logical view, and can the logical width grow (e.g. to 426 for 16:9) without the original building nodes at different times?

**Evidence:** REA 6.4.0 Ghidra traces of `0x00443af0` (scene build), `0x00443b40` (per-slice half-select), `0x00443cd0` (world-object dispatch), `0x00443c00` (category-5 dispatch), `0x00438610` (scenery half-dispatch), `0x0044b980` (billboard visibility), `0x0043bef0` (road panel dispatch), `0x0040bde0` (road panel clip), `0x0044b630` (billboard clip), `0x0044ba40` (actor projection + clip), plus project exports `analysis/decompiled/00443af0.c`, `00443b40.c`, `00443cd0.c`, `00438610.c`, `0044ba40.c`, `0044b630.c`, `0044fe30.c`, `00438450.c`, `004385f0.c`, `00438600.c`, `004387a0.c`, `00444630.c`, and EXE-data reads (`DAT_0046667c` = 320, `DAT_00466678` = 240, `DAT_00475618` = 448).

**Answer by cull point:**

1. **Road panels clip against `DAT_0046667c` (320), not build anything new.** `0x0040bde0` (road panel builder, reached via `0x00443cd0` → `0x0043bef0` → type-0 branch) takes an early-out per half: half 0 returns unseen when left edge < 0 or right edge > `DAT_0046667c·0x10000`; half 1 mirrors it. Fully-off-screen strips emit nothing; partially-visible strips proceed to `0x0040bd80`/`0x0040b9b0` and `0x0044c0e0`/`0x00450720`. Growing 320 → 426 only widens the early-out: strictly more strips emitted, none at different times, none reordered.
2. **`0x00443af0` subtree builds nothing width-dependent.** `0x00443af0` walks 0x1f+1 projected world strips (`0x004c0a5c`, stride 0x224): per strip `0x00443cd0` (world objects) + `0x00443b40` (scenery halves) + `0x00438600` (cursor advance). No width constant, no left/right test anywhere in the loop — iteration count and order are fixed by the route/camera depth window (`0x00438450`: register while depth < camera+0x2000, cap 0x32 nodes). The `0x00443b40` half-select compares the two scenery halves' mean depth against `(DAT_0046667c>>1)·0x10000` (screen centre: which side the viewer is on) and dispatches each half to `0x00443c00` (category 5: bucket table at `0x00473528/30/38`, bytes `04 03 02 01 00` / `00 01 04 03 02` / `00 01 02 03 04`) or `0x00438610(half,-10000,10000)`. The ±10000 here are dispatch-bucket IDs (bucket field +0x50 selects the callback at +0xdc), **not** screen coordinates.
3. **Scenery visibility is bucket + vertical-window, never horizontal.** `0x00438610` iterates the depth-sorted node array (`0x004c3d30`, cursor `0x004977bc` set by `0x004385f0`, advanced per strip by `0x00438600`); per node it breaks on depth mismatch, checks family id and the `0x80`-flagged front-half bit, then fires the node's callback if its bucket (`+0x50`) lies in `[param_2,param_3)`. Horizontal culling happens one level down, inside each callback: `0x0044b980` (billboards) and `0x0044b5f0` route through `0x0044b630`, whose clip test is against `DAT_0046667c·0x10000` horizontally and `DAT_00466678·0x10000` vertically: mirrored-branch logic keeps a billboard iff `0 < left` / `left < 320` in Q16 (flag-dependent direction). Widening 320 → 426 widens both comparisons; node build order (depth-sorted array, fixed cursor protocol) is untouched.
4. **Actor (rider/car) visibility is the same shape.** `0x0044ba40` projects via `DAT_0046667c` as screen centre (`(w&~1)·0x8000` added to the projected x) and as the right-edge cull (`0 < right && left < w·0x10000`, mirrored for flag-1 sprites), with vertical cull against the occlusion row. Centre-shift plus edge-widen: actors outside [0,320) today are still built by the sim every tick (rider slots, car pool) — only their quads are skipped. Growing the width un-skips them; build timing never changes.
5. **Scale global `DAT_00475618` (448) is not width — do not touch it.** It feeds only the size-0 actor scale path (`((w/2·0x270>>8)·proj)/sprite_w`); it is a fixed tuning constant, independent of `DAT_0046667c`. A Hor+ port keeps it at 448 (vertical FOV unchanged).
6. **Registration is depth-windowed, not width-windowed.** `0x00438450` registers world objects with depth < camera+0x2000 (cap 50); nothing tests lateral position at registration. Side objects already exist in the node array — they are only skipped at emit. So no "different times" risk: the wide view reveals already-built nodes.

**Port mapping (for plan 21 Task 8):** `road.hpp:view_width` (=320) feeds `RoadSlice::screen_x` centre; `course_scene.cpp` hardcodes `160·65536` (building transition) and `320` (marked-ground width); `actor_render.cpp` takes `logical_width` as a parameter already and culls `right<=0 || left>=w`. The Hor+ change is: `logical_width` 320→426 (or 384/427/match), `origin_q16_x = (w−320)/2·65536` subtracted in `map` before scaling, and every `DAT_0046667c` cull widened. Vertical FOV unchanged. Native probe (Napa @50.5 samples: 236 nodes, 75 spanning an edge) confirms side content exists to reveal.

**Decision input for the owner:** road and hills extend naturally (same strips, wider clip); scenery/riders at the sides appear rather than pop in — they are sim-built every tick and only emit-skipped today, so no pop-in mechanism exists beyond the edge; HUD is a centred 4:3 dashboard with runtime-generated side panels (plan 21 Task 8). Recommend proceeding on explicit yes.

## Plan 21 Task 4: palette-change trace (October 13, 2026)

**Question:** does the original change the palette during a race, or use palette ranges for shading? (Determines whether the RGB path needs a `palette_generation` counter with per-generation texture re-conversion.)

**Evidence:** REA 6.4.0 trace of `0x00448f90` (palette/audio init); project exports `00448f90.c`, `00415e90.c`, `00415e9c.c`, `00415d90.c`, `004163d0.c`, `0041e710.c`, `00442e10.c`, `004431d0.c`.

**Answer:** the palette is loaded once and never animated during a race.
- `0x00448f90` reads 1,024 palette bytes via `0x0044c730` into `DAT_004b8490`, zeroes entry 0, and builds the half-brightness copy at `DAT_004b8030` (`>> 1` per channel). One-shot init.
- Runtime `SetEntries(0,0,0x100,...)` calls: `0x004431d0` (main loop: full-bright `0x4b8030` at startup, half-bright `0x4b8490` after MiniDash F10 switches), `0x004163d0` (race setup paths), `0x00442e10` (race loop: restores full palette only when the `0x4753c0` fade timer expires — a screen-fade transition, not per-frame shading).
- `0x00415e90`/`0x00415e9c`/`0x00415d90` are the 0x40-step crossfade workers (copy base to `0x482358`, then float-scale each step, uploading per step): menu/screen transitions, never invoked per race tick.
- No crash/arrest/sky code writes palette memory. No palette-range shading.

**Port rule:** `palette_generation` increments only on (a) course load (new base palette), (b) MiniDash mode switch (full/half select), (c) fade-transition completion. RGB mip caches key on the generation; no per-frame invalidation. The race steady state never re-converts.

## Plan 21 Task 6: 33-slice limit trace (October 13, 2026)

**Question:** how is the 33-slice limit built in, and can the depth table be extended with the original's own law (exact fit required)?

**Evidence:** REA 6.4.0 trace of `0x00443a00` (projection driver); EXE-data reads of the depth table at `0x0046f098` (4,352 int32 entries); port `read_depth_table` corroboration (`scale[0] = 163840`, `scale[256] = 32768`, strictly decreasing, positive).

**Findings:**
- The slice count is a call parameter: `0x00443a00` calls `0x00443d70(..., 0x21 /* 33 */, ...)`. The scene-build loop `0x00443af0` walks a fixed 0x1f+1 strips per call (depth-windowed registration in `0x00438450`, cap 50 nodes) — widening slices would need registration + cursor changes too.
- The depth table has 4,352 entries for depth indices 0..4351 (depth Q8 = index*2). Slice `n` uses `index = depth&~1/2` with `depth = 256-frac+256n`: 33 slices reach index ~4224; the table covers exactly one more slice (index 4352 is out of bounds for frac=0). The table's natural limit IS 33 slices.
- No exact law: near field clamped at 163840 to k=138; mid-range linear-reciprocal fit (`1/scale = 1.98e-7·k + 1.74e-6` over k=256..2048) errs up to 7.2% (13,704 absolute at k=256). Two-point `K/(k+c)` errs 1.3%. The table is empirical/tuned, not a closed form.
- Per the plan's own trust rule ("maximum error must be zero"), the table MUST NOT be extended with invented values.

**Conclusion (stop-and-report per Step 3):** 48/64 slices are infeasible without fabricating projection data. Task 6 cannot proceed as specified. Options: (a) drop it (recommended — fog's far-fade already softens the 33-slice pop-in); (b) revisit only with an oracle-measured extension (out of scope).

## Car knock-off tail continuation (October 14, 2026)

`0x004049c0` keeps `local_21` from `0x004052c0`, then routes class-2/3
knock-offs through `0x00404f4d` (lateral separation), `0x00404f93` (skip
steering kick when `local_21` is true), and `0x00404feb` (impulse/flag clear
and clamps). A newly set `+0x2f4` does not return early. `0x00405042` stores
-1000 when `+0xec` is below -1000. The native car handler previously
returned before cleanup and incorrectly floored negative speed at zero.

Evidence: address-keyed export `analysis/decompiled/004049c0.c`, original
disassembly, and `npx --yes rea-agents@6.4.0 function
../cdrom/ROADRASH/ROADRASH.EXE 0x004049c0 --format md` (exit 0), retained at
`analysis/codex-contact-20261014/rea-004049c0.md`. The reference SHA-256 is
`73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`, matching
`analysis/inventory.json`. Synthetic public `Race::step` regressions cover
contained and glancing car knock-offs, tail clearing, separation and reverse
speed floor. This is translation/boundary evidence, not live original parity.
The earlier `sim-fault-2.md` claim that an early return implements only the
steering-kick guard is superseded; it also dropped required cleanup.

## MiniDash numeric speed (October 14, 2026)

When MiniDash is enabled, `0x004468e0` writes numeric speed to
`DAT_0049e10c`, which `0x00446c30` prints at the mini speed rectangle.
Imperial: `(v >> 12) + (v >> 7) + (v >> 9)` for signed bike `+0xec`.
Metric: mask `v` with `0xfffff0ff`, `0xffffff87`, `0xfffffe1f`, arithmetic
shift by 8, 3, 5 respectively, add and divide by 10 with truncation toward
zero. Clamp negative output to zero. The full-dial branch still computes
an angle separately; travelled distance is not the MiniDash numeric value.

Evidence: `004468e0.c`, `00446c30.c:177-188`, disassembly
`0x0044693a..0x004469a4`, and independent REA 6.4.0 function export at
`analysis/codex-contact-20261014/rea-004468e0.md` (exit 0, same inventory EXE
checked above). This supports the numeric state/composition correction; it
does not establish original font/layout, needle-pen or opponent-list pixel
equality.

## SIM capture corruption and frozen scheduler evidence (October 14, 2026)

The failed SIM captures were caused by oracle instrumentation. The billboard
wrapper replacing four `0x0044ba40` calls neither forwarded the original call
nor checked its 512-record buffer. SIM mode skips `dump_frame`, where the
actor counts had been reset, so records accumulated across presents. In the
previous installed DLL, `g_bb=0x62f2e080`, stride 80, `g_pixels=0x62f519b4`
and `g_frame=0x62f519b8`: record 1821 writes its `occ_owner`/`occ_row` at
exactly the two corrupted globals. This explains the observed frame59→6
and pixels0→8347995 corruption; timer/environment hypotheses are superseded.

The wrappers now live in `oracle/actor_record_hooks.h`, forward every call
even when output is disabled or full, and cap records at512. Every frame
resets actor counters before original presentation. REA6.4.0 plus original
disassembly confirm `0x0044ba40` fastcall ECX/EDX plus five stack arguments
and `ret0x14`. Snapshot inputs precede the forward; emitted-node count follows
it. Captures made with the missing-forwarding proxy need fresh actor-node
evidence before they can establish actor rendering equality.

A first repaired run exited cleanly but requested250 presented frames held
only70 SIM ticks. The SIM limit now counts completed tick dumps, independent
of presented-frame count; non-SIM keeps its original presented-frame limit.
An independently validated900-tick run exits0 with all ticks1..900 and end
marker. A300-tick forensic follow-up also completes and measures demo_hold=1.
Neither capture is full simulation acceptance.

Header-only bike fields now capture actual previous copies, velocities, flags
and crash state for type4 nodes independently of think callback, plus lane,
bias and next-record centre/width/eight bends. Existing v1 numeric parsing is
retained. Replay hydrates declared frozen state once, validates pointer-linked
type8 halves and rejects missing/ambiguous state before changing any rider.
Old markerless demo captures retain their previous fallback; no RNG reanchor
or moving-player state overwrite is added.

New forensic capture disproves stale frozen velocity as the tick210 cause:
slot2 has zero108/10c/110, dimensions7680/64/8960, elasticity192, mass256 and
slice(+2c)+36 mask7. Instead its original dt(+18) is4 at contact: lateral
19200→15360→11520 at ticks208/209. Native dt1 nudges only960 each contact,
leaving a spurious tick210 overlap. `0x00437e60` updates dt and last-tick
BEFORE checking null think/move; null move still schedules next=tick+interval
at `0x00438108`. Freezing callbacks does not freeze scheduler metadata.
Next work must capture/hydrate +0c/+10/+14/+18 once and model that cadence;
initial dt5 alone and hardcoded4 are both insufficient. Runtime metadata
records last ticks1,5,...205,209. This is the next native translation gap.

Evidence: retained private worktree analysis/sim-forensics and validated
`codex-sim-frozen900` / `codex-sim-mask300` receipts, worker handoffs and
synthetic actual-wrapper mutation tests. Commercial traces stay local.

## Scheduler metadata capture spec (October 15, 2026, plan 10 Task 6 fix order #1c)

REA 6.4.0 confirms both scheduler functions against the project exports
(`0x00437e60` body `0x437e60-0x43816d`, 782 bytes; `0x00438170` all 89 lines).
All four header words are 32-bit signed ints; gates are signed compares
(`cmpl` + `jle`/`jge`/`jl`); `+0xc4` masks are unsigned byte tests.
Element scaling verified: byte-explicit offsets in both functions,
int-indexed `[3]/[4]/[5]/[6]` in `0x00438930` are bytes `0x0c/0x10/0x14/0x18`.

- `+0x0c` next: `0x00437e60:153` writes `+0x14 + tick` (32-bit wrap,
  `438108-438113`); driver tail `0x00438170:79-80` also rewrites the player
  bike `DAT_004642d8` word every frame.
- `+0x10` last: `0x00437e60:11-12` writes `tick` on every scheduled run,
  including frozen (NULL think/move) nodes; init `0x00438930.c:23`.
- `+0x14` interval: init from template-image dword +8 (`0x00438930.c:22`,
  `43897f-438984`); live recompute in the `0x00437e60` tail, skipped when
  frozen. Actual value comes from the capture, never hardcoded.
- `+0x18` dt: `0x00437e60:13` writes `tick - old_last` (signed `subl`,
  `437e69`); init 0.

The oracle now dumps per-tick `sched.<addr>.next/last/interval/dt` for
type-4 nodes plus `rider.<slot>.bike_c/bike_10/bike_14` for slotted riders.
Expected solo-freeze cadence (unproven until captured): interval 4 with
lasts 1,5,...,205,209 on the primary `+0x0c <= tick` gate. The native
translation waits for that record; no dt value is assumed in `src/race.*`.

Evidence: `docs/agent-handoffs/sched-cadence-20261015.md`,
`sched-interval-writer-20261015.md`, `sched-metadata-trace-20261015.md`.

## Frozen scheduler cadence translated; tick-376 traffic-timer draw (October 15, 2026, plan 10)

`sched900-20261015` (course 4, seed 1, drive 2, solo, SIM, 900 ticks,
proxy `1b8e2357`) proves the predicted cadence: all 14 frozen type-4 bikes
hold interval 4 with lasts 1,5,...,205,209 and dt 4 (first window dt 5 from
the build `+0x10 = tick - interval` init). The live player keeps interval 1.
Native `Rider` now carries `sched_last/next/interval` plus a `sched_live`
gate, stepped for frozen riders in `step()`/`countdown_step()` before the
early return (traffic/pedestrian gate shape); `tests/frozen_state.hpp`
hydrates them plus bike `+0x18` (dt) from per-tick `sched.<addr>.*` keys.
Legacy captures without sched keys keep `sched_live` false and dt 1, so
their signatures are byte-identical. Solo replay detaches native traffic:
the oracle zeroes the pool count (`RD32(0x0049e654) = 0`), so native
spawns (first at tick 319, 6 draws, no golden counterpart) are isolation
breaks, not model gaps. Replay moved 210 -> 319 -> 376 on sched900.

Tick 376 is a single original draw with no visible state change anywhere in
the dump. Mechanism, read from the exports (no guessing): `0x0044dd90`
(free-traffic spawn timers, three distance-driven branches) draws one lane
pick `FUN_00441750() % lanes` whenever a timer expires with lane count > 0,
then calls `0x0044df60`, whose FIRST statement (`0044df60.c`) returns on the
nulled pool (`DAT_0049e654 == 0`) before any draw. So the expiry consumes
exactly one stream draw and places no car. Porting the timer state
(`DAT_004b7e80/90/94`, rates `88/9c/a4`, tap-point lane words) is plan 10
Task 7 work; the live `0x00437e60` cadence for frozen nodes is done.
The spare player half (think `0x00434660`, slot rider.1) stays live in solo
captures with interval 1, mirroring the bike; it drew nothing through
tick 375 and has no native counterpart (open).

## Player-control branch gates (October 16, 2026, plan 10 Task 5 Step 2a)

`DAT_004753c8` (demo hold) is read by eight exports; six matter in-race
(all `== 0` selects the human side). Only writer in the exports is
`0x00421430:30` (`= 0`, standings path, off the race); the `= 1` comes
from unexplored menu/bootstrap code, which is why the oracle bootstrap
stays on attract and `RR_ORACLE_HUMAN=1` clears it at race start
(seed-hook pattern, nothing re-arms it mid-race):

- `0x00434660:25` — player think: control-word branch vs demo-AI branch.
- `0x0040ad30:8,16` — steer authority: human uses `DAT_004b7e7c`-based
  authority, demo fixed `-/+0x20`. Native `steer()` has no demo_hold gate.
- `0x00438c70:67` — traction skid flag for the player bike only when
  human. Not gated natively.
- `0x004395e0:115` — move push: human uses `iVar6 * 800` direct, demo the
  `dt`-scaled form. Native `move()` always uses the scaled form.
- `0x00438170:78` — scheduler tail copies rider-half `+0xc` to bike `+0xc`
  only when human. Not modeled natively.
- `0x0040a600:78` — wreck attack-bit rule needs human + finish latch.
  Native wreck arms lack the hold gate.
- `0x0041c1b0:7` — menu string selector (menu-only).

`RR_ORACLE_HUMAN=1` + per-tick `player.demo_hold` dump give instrumented
branch proof (`control_branch: human` iff all-zero). The spare player
half (think `0x00434660`, solo `rider.1`) stays live with interval 1 and
mirrors the bike; call-graph BFS (310 functions) proves its think cannot
draw with no attacks/contact/crash, and bike think `0x00407a90` needs
crash states -- both benign for RNG in clean captures. It has no native
counterpart (halves take neither collide role: `rider_or_ped` needs
type 4). Evidence: `docs/agent-handoffs/spare-half-trace-20261016.md`,
`sched-human-20261016` (1431 ticks, hold 0 throughout).

## Human launch ramp and tick-149 wall scrape (October 16, plan 10)

Drive-ramp ground truth (`rider.0.bike_240/114/260`, sched-human capture):
cap `+0x260` = 9188, ramp `+918`/`tick` (`+0x264`), gear-0 table `+0x1d0` =
248 (native spec words match: 9160/916/248 -- the 0.3% cap/rate deltas are
truncation-absorbed through saturation, proven tick by tick, benign).
Player dt series 2,1,1,... (interval 1): tick-2 drive consumes the previous
run's dt (ramp `2*918 = 1836`) while contact reads the fresh dt, so native
steps scheduler metadata after drive but before collide. No tick-2 contact
occurred (impulses/flags zero), leaving the contact-side order as the
frozen-consistent choice, not a second measurement.

Tick 149 is the first human-specific physics gap: full-throttle entry into
the left wall (lateral 16123 -> 15123 -> 13412..., steer-rate pinned
-1200/-1440, no crash) with scrape speed-bleed 3351 -> 2739 -> 2242 ->
1839 and a gear 1 -> 0 drop at 151. Native `Race::walls` is the documented
stand-in (no type dispatch, wall faces, scrape branch, or thresholds) and
over-brakes to 843. Porting `0x0043a610` (292 lines) with its sub-slice
plumbing (`+0x37/+0x38/+0x3a/+0x3b/+0x3c/+0x14c...+0x160`) is plan 11
Task 4 / plan 16 Task 1 scope; the offroad trace (`offroad-trace-20261015`)
holds the branch map. Demo solo stands at tick 376 (spawn-timer lane draw).

## Contact class routing follows separate()'s 0x40 flag (October 16, plan 10)

`0x004049c0` LAB_00404c2d routes bike-bike class by separate()'s width
flag, not lateral containment: other `+0x17c < 0x2000` -> class 0; else
self `+0x19c & 0x40 == 0` -> class 3 (glance); else other short-ish
(`+0x17c < 0x2800`) and unflagged (`+0xc4 & 0x10 == 0`) -> class 1, else
class 2. The port had invented a geometric `contained` predicate, routing
the tick-149 human contact to class 1 (`ec += 184`) instead of class 3
(`ec -= shit/4`, `f0` kick). Fixed to the flag; tick-149 consequences now
match exactly (speed 2739, steer -1911). Bike-bike others always carry
`+0x17c 0x2300`/`+0xc4 & 0x10 == 0`, so only classes 1/3 occur there.
Tick-149 forensics: same contact, same sounds, same push on both sides;
impulse -3370 from `(0 - 3370) * 2 * mass/mass`.

Tick 189 is a terrain-jitter count gap at a ring back-crossing
(Napa sample ~134, section 0): original 24 draws, native 27, all inside
`TrackRing` stepping (BORR walks + DZHR jitter + terrain iterator share
one stream). Same slice built both sides (front_/back_ track exactly);
BORR is stateless and DZHR consumption matched through tick 188, leaving
the terrain iterator's per-step jitter draw count as the prime suspect
(live cache/random integration was already open). Next slice.

## Tick-189 terrain-jitter gap, 0x3c staleness note (October 16, plan 10)

Same slice built both sides at a ring back-crossing (Napa sample ~134),
yet 24 vs 27 draws, all inside `TrackRing` stepping (BORR walks, DZHR
jitter and the terrain iterator share one stream). BORR is stateless and
DZHR consumption matched through tick 188, implicating the terrain
iterator's per-step jitter draw count (section-0 hills) -- the known-open
live cache/random integration. No golden edits.

Separately: native bike `+0x3c` is never written (stays build 0; frozen
riders hydrate lane-like constants 8192-40960 from capture). separate()'s
lateral gate reads it, so any future overlap with a stale player 0x3c takes
a different gate than the original. Untriggered so far (tick-149 contact
ran with matching geometry); maintenance semantics still open -- do not
paper over with the lateral value until traced.

## Tick-189 terrain-step draw gap, precisely bounded (October 16, plan 10)

Same slice built both sides at a ring back-crossing (Napa front 34552,
sample 134, section-0 hills), yet 24 vs 27 draws, all inside `TrackRing`
stepping. Eliminated: BORR (stateless; recount 0 both), DZHR (recount 0
eligible both; consumption matched through tick 188), slice identity
(front_/back_ track exactly), boundary timing (single crossing both).
Remaining: the two walkers' hill `hill_slopes` (8 points x up to 3 draws,
early return on zero length) and wall jitter compositions differ -- i.e.,
walker-side record-advance timing or point-window state drifted silently
earlier (walker positions/record indices are never dumped, heights stayed
flat-insensitive). Needs per-walker draw attribution plus walker-state
oracle keys (new slice); do not touch hill/wall stepping until then.
Native `hill_slopes`/early-return structure matches 0x00451380 on
inspection (length/x-slope/height-slope per point, whole-function abort
on zero length); init matches 0x00450fc0 (16 draws).

## Tick-189 walker state at the crossing (October 16, plan 10)

Pre-step walker dump at the tick-189 back-crossing (temporary probe,
reverted): back node 20 pos 1536 (sample 6) hillrec 0 (next record start
24); front node 20 pos 34304 (sample 134) hillrec 7 (next start 150).
Neither hill record advances (24 > 7, 150 > 135), BORR gates closed,
DZHR recount 0 eligible -- yet 27 native draws happen during this tick
against the original's 24. Open hypotheses, in test order: (a) a walker
section-enter re-running terrain init (16 hill draws) during its step
(enter resets borr/dzhr/terrain; lengths unchecked); (b) wall jitter on a
walker whose section differs from assumed; (c) a second draw site in the
step path. Needs per-site draw counters plus section-boundary data, then
the matching oracle walker keys. No hill/wall/stepping code touched.

## Tick-189 per-tick draw parity (October 16, plan 10)

Per-tick draw counts (native instrumented run vs golden f-chain) match
exactly on every drawing tick through 188 (80:24, 111:21, 135:27,
149-151:2s, 160:30) -- no masked cancellation. The tick-189 gap (24 vs
27) is therefore a single-tick, single-slice event, not drift fallout.
Walker pre-step state there (back node20/pos1536/hillrec0,
front node20/pos34304/hillrec7, next starts 24/150) shows no hill record
advance under the native rules, so the 27 native draws are unattributed
by every known ring site (BORR/DZHR recount 0, no wall section, no
re-entry). Prime suspects left: a walker section-enter re-running
terrain init, or walker positions having drifted silently earlier
(positions/indices are never dumped). Next: per-site draw counters plus
walker-state oracle keys. Probes reverted; tree clean.

## Spawn gating with nulled pool; timer-schedule fork at tick 319 (October 16, plan 10)

`TrafficSystem` gained `spawn_enabled` (default on): with spawns off, each
gated timer expiry still draws the lane pick but skips placement, serial
bump and motion -- exactly the oracle-nulled-pool behavior (`0x0044dd90`
draws, `0x0044df60` returns first-statement; same shape verified for the
`0x0041f040` crossing lane pick). Draw-before-place order and the
pool-exhausted path are unchanged for live game and field replays.

Attached (spawn-disabled) solo replay regressed demo 376 -> 319, so the
harness keeps traffic detached for now: the native behind-timer fires at
tick 319 on its self-consistent density-40 schedule (tap sample 138,
lane count 1) while the original stays silent there. Behind evolution
(init 0, first fire ~tick 2 on reload 3515, density later 40 without a
behind reset) checks out arithmetically, so the fork is in timer inputs:
tap-lane words (`+0x30/+0x2c` vs `PathSample` lane Q8), the distance-delta
source (`DAT_004b7ef4-004b7f08` vs camera advance), or density/zone timing
-- Task 7 core, with the full branch/init account in
`traffic-timer-trace-20261016.md`. The tick-376 lane pick itself is
uncontested. No golden edits.

## Tick-189 fixture gap: constant-rand terrain coverage (October 16, plan 10)

The 864-slice oracle comparison uses three FIXED random values, so hill
point windows/slopes collapse to bound cases. Real captures draw varied
values, exercising clamp/skip/advance interactions the fixtures cannot
reach (early return on zero length, window-skip strictness, record-start
boundaries under varied lengths). Per-tick counts match through tick 188,
ruling out masked drift; per-site tags were polluted by non-race streams
(scene iterators share the call sites) and reverted. Decisive next
experiments, in order: (1) randomized differential terrain-step harness
through the isolated oracle function hooks (same 3x3x2x48 shape, varied
draws); (2) walker-state oracle keys (positions, hill/wall record indices)
to hand-simulate the tick-189 crossing. No stepper code touched.

## Tick-319 timer fork refined: tap gates agree, camera update conditional (October 16, plan 10 Task 7)

Two follow-up traces closed the tap-word and delta-source questions:
- Tap words: `+0x2c/+0x30` come from NALR via `0x004528f0`, equal to native
  `left_q8/right_q8 >> 8` sample-for-sample on forward single roadways
  (five-course census: no negative accumulators; smoothing is
  count-invariant). At the tick-319 tap (sample 138) and tick-376 tap
  (sample 142) both sides see count 1: the 319 silence is timer-state
  (behind never went < 0), not a lane gate.
- Delta source: `DAT_004b7ef4` is the view camera (writers: init + camera
  update only), `DAT_004b7f08` its per-tick watermark; timers take the
  full-tick delta with no residual carryover (corrects the earlier note).
  Native `camera - previous_` already equals it exactly.
- Prime suspect for the fork: the original camera update is conditional
  (skipped unless the player scheduler object's last-run tick == current
  tick), banking +9/tick into the behind timer on skipped ticks while
  native decays it unconditionally. Needs capture proof (countdown/green
  alignment); native formula otherwise unchanged.
Evidence: `timer-tap-trace-20261016.md`, `timer-delta-trace-20261016.md`.

### Countdown timer integration (October 15 continuation, plan 10 Task 7)

`0x0044b3b0` calls `0x0044dd90` on every simulation tick, including
stationary hold ticks. It passes the full camera delta after ring builds;
no residual carry is involved. Initial `0x0044db80` sets ahead=-3515,
behind=0; the ring's density-40 event resets both reloads and oncoming
to 23437 while preserving ahead/behind current values. At rate 2500,
59 zero-distance ticks therefore leave oncoming=22906, behind=531,
ahead=-2984. No lane expiry occurs during that hold.

Native countdown now reproduces this history. Because hydrated frame 0
is post-tick-1, replay banks that tick without rerunning hydrated brain
state. Solo replay retains lane draws while suppressing car placement.
The existing demo trace now agrees through tick 574; first divergence
is target identity resolution at 575: original pointer 20976340 remains
nonnull, but the harness searches by initial position against moving native
riders. A stable address-to-rider mapping is the next correction. This is
measured replay progress,
not full traffic parity. Human still diverges at terrain step tick 189.

Independent reference-hash check and pinned REA 6.4.0 `0x0044b3b0`
spot-check agree with the address-keyed export. See
[independent review](agent-handoffs/timer-independent-20261015.md) and
[countdown driver trace](agent-handoffs/countdown-driver-trace-20261016.md).

Terrain follow-up: matching per-tick draw totals through 188 constrains
stream position at tick boundaries, but does not exclude earlier
within-tick draw assignment differences affecting hidden point windows.
Current terrain fixtures compare only six of eight points and no CRT
state. Expand isolated seeded fixture state before guessing a hill-step
change; exact eight-point and race-walker offsets are in
[terrain probe handoff](agent-handoffs/terrain-next-20261015.md).

### Demo tick576: detached rider-half scan state

The tick575 target-pointer report was fixed by stable initial actor identity.
At576, golden brain+180 selects pointer20988784, the type8 half linked from
bike20976340. Its target position22208 minus the recovered overtake
offset960 directly establishes half position21248; target lateral-16384
from half lateral16384 matches the recovered avoidance clearance32768.
The bike had meanwhile moved away from21248 under contact. Native scan
objects copied that moving bike's position/speed into the half despite
retaining a separate frozen half lateral.

Solo instrumentation explicitly nulls both think/move callbacks on frozen
nonplayer bikes and halves (`sim_freeze_others`), and type8 halves are not
outer participants in native/original bike contact. The recovered half scan
state must therefore retain its own captured position and speed. Frame0
`nodeaddr.<bike>.half108` is the linked half's own speed; the half node's
`.half108` instead describes its absent linked half and is a sentinel.
No change to attached live half synchronization or type-mask semantics is
justified by this finding. See
[576 trace](agent-handoffs/demo576-next-20261015.md).

### Seeded hill hidden-state component evidence

Actual original0x004510c0/0x004516e0 calls match native hidden state across
960 snapshots with6 CRT seeds, all8 points and forward/reverse/roundtrip
paths.18 final CRT states also match, covering last-step draws. Tested
synthetic LIHR data and every start/end/x/height/dx/dh word agree. This
strengthens component evidence without proving Napa walker initialization
or shared draw assignment; compare paired race walkers from setup next.
Separate fixture schema is terrain.seeded.schema1; source remains
synthetic, captured original output stays local.

Fresh sched600 demo capture exposes original force/throttle at584:
force26/throttle7352 (native25/7330). Original581–585 throttle is
9188,8576,7964,7352,6740, with cap9188 and gear2. The one-unit speed
gap is thus consistent with different drive-ramp inputs; no evidence
justifies changing integrate_speed's faithful rounding.

## Frozen scan slice validity (October 15, 2026; plan 10)

Original00449ce0 checks self+0x2c at449ce1..449cee and candidate+0x2c
at449d1f..449d24. A NULL self returns NULL; a NULL candidate is skipped
while preserving position-window traversal. Independent REA6.4.0 output
is whitespace-identical to the address export (783chars); assembly is the
field-width/branch authority. Live captured frozen bike/half metadata
w2c/half2c is independently nullable, so native route reconstruction must
not invent membership. sched900 tick735 entered the2400-unit overtake
window for NULL-slice nodepos27648, producing false lateral22784; retaining
membership advances same single-anchor replay through892. Road-half flags
and later contact eligibility remain separate coverage.

This supersedes the native scan assumption that every reconstructed frozen
node has a valid single-road slice. It does not supersede independent
frozen-half position/speed state (needed at576). Optional missing pointer
metadata explicitly keeps older fixture behavior; declared contradictory
linked-half pointers reject before state mutation. See
[full probe and REA evidence](agent-handoffs/codex-sim-research-20261015.md).

## Race countdown banner (October 16, 2026)

Evidence: project exports `00442dd0.c`, `00442e10.c`, `0044c780.c`,
`00446c30.c`, `00447b20.c`, `00442990.c`, `004163d0.c`, `00448f90.c`,
`004431d0.c`, `00416010.c`; ENU.DLL string table; EXE data `0x466654` = `"%d"`.

- Countdown `DAT_004753c0` = `DAT_004753bc` = 0xb4 (network 0x168) at
  `0x00442dd0`; `0x00442e10` subtracts the frame's steps and skips the
  stepper `0x0044b2e0` while positive; at expiry uploads `DAT_004b8490`.
- `DAT_004b8030` is the half-brightness palette (`0x00448f90`, channels
  `>> 1`); `DAT_004b8490` the full one. (Corrects the plan 21 Task 4 note
  above, which swapped the two labels.) The race starts on the half palette
  (`0x004163d0` fade or direct upload), so the countdown view is half-bright.
- `0x0044c780` skips `0x00446c30` while the timer equals its start value.
- `0x00446c30` draws `"%d"` of `t/0x3c + 1` (179..120 → 3, 119..60 → 2,
  59..1 → 1) in Badloc `DAT_004c11ec` (0x80/0x48), rect top = view top +
  `0x78 >> (1-HiRes)`, DrawText 0x4851 via `0x00447b20` (black, then white at
  −(HiRes+1)), independent of MiniDash.
- `DAT_004c5bd0` has no other single-player in-race writer except the pause
  message `0x004431d0`. There is no wipeout/crash or finish banner; ENU
  strings 176–183 (`WRECKED!` ...) belong to the results screen.
- Native port: `countdown_banner`/`Hud::draw_countdown` (`src/hud.*`),
  `CourseCanvas::draw` (`src/race_caches.hpp`).
- Palette fades (`0x00415f70`, disassembly 0x415d90..0x415f6a): 14 ms
  periodic timer, 63 uploads then release on the 64th callback, blocking.
  Fade-in `0x00415d90`: table `0x467860` = 0.96^i (double, iterated from
  1.0, factor at `0x462000`), upload n = 63..1 is `trunc(half × table[n−1])`.
  Fade-out `0x00415e90`: from the half palette, cumulative
  `x = trunc(x × 0.96)` per upload. The loop clock (`0x004163b0`) starts
  after the fade-in. Native: `fade_in_lut`/`fade_out_lut`, `RaceScreen`.
- In-race pump `0x004431d0` callers (wndproc key-up, disasm `0x423345..`):
  Escape → `0x423408` string 316 (exit 1 → `PostQuitMessage(0)`, outcome 5
  while racing; `0x00442e10` ends on `WM_QUIT` at once, no 0xb4 run-on),
  `VK_PAUSE` → `0x4234f7` string 317 (countdown off its start, mode 1/2),
  `V` → string 380. The message is drawn by `0x00446c30` in the countdown
  rect with small Badloc `DAT_004c1210`, over the half palette.
- Original countdown pixels (`napa-dash-solo` post-dash d2/d36/d70): white
  index 255, shadow index 0, digit box x 303–331 / 312–321, y 143–220.

## Race palette uploads, runtime evidence (October 16, 2026)

Proxy `RR_ORACLE_PALETTE=1` patches `IDirectDrawPalette::SetEntries` (vtable
+0x18 of `DAT_00475424`) and logs every upload with its return address.
Upload call sites (disassembly; all pass flags 0, start 0, count 0x100):

| Call | Return | Source | Role |
| --- | --- | --- | --- |
| `0x415e69` | `0x415e6c` | `0x482358` | fade-in worker `0x00415d90` |
| `0x415f49` | `0x415f4c` | `0x482358` | fade-out worker `0x00415e90` |
| `0x41642a` | `0x41642d` | `0x4b8030` half | race start, no-fade branch (`0x482b68`) |
| `0x416664` | `0x416667` | `0x482760` | `0x482b68` branch after the race loop (not traced) |
| `0x442f2c` | `0x442f2f` | `0x4b8490` full | countdown expiry `0x00442e10` |
| `0x44322d` | `0x443230` | `0x4b8030` half | `0x004431d0` entry when countdown is 0 |
| `0x44334d` | `0x443350` | `0x4b8490` full | `0x004431d0` exit 2 when countdown is 0 |
| near `0x41e76e` | not traced | stack copy | `0x0041e710` (front end) |

Capture `napa-palette` (Napa, solo, seed 1, drive 2, attract bootstrap;
see VERIFICATION) recorded 130 uploads, in order: 63 fade-in (counter
`0x482350` 63..1, countdown 180, before race frame 2), full at the frame the
countdown reaches 0 (frame 181), half when the Pause pump opens (frame 221),
full when it closes on a key (251), half when the Escape quit prompt opens
(281), then 63 fade-out uploads right after the second Escape (311). Every
entry equals the formulas above applied to `DATA/PALETTE.RAW` with entry 0
forced black: fade-in `trunc((c >> 1) × 0.96^(n−1))`, fade-out cumulative
`trunc(x × 0.96)` from `c >> 1`, so the first fade-out upload is already
`trunc(half × 0.96)` even when the full palette was live. Uploads 56..63 of
the fade-out are all black. Upload spacing: 867 ms over 62 intervals for
both fades (14 ms timer).

Further runtime facts from the same proxy:
- The `RR_ORACLE_COURSE` bootstrap races in game mode `0x004753b0` = 0
  (attract). In mode 0 `VK_PAUSE` is ignored and Escape (branch `0x423450`)
  ends the race at once with the fade-out and no `0x004431d0` prompt; the
  bootstrap then starts the next race with a new fade-in (probe run).
- Race frames 0 and 1 (`0x004163fa`, `0x00416406`) render before the first
  fade-in upload, so two race frames are presented under whatever palette
  preceded the race. That palette was not logged (no upload between hook
  installation at course entry and the fade-in). Native presents nothing new
  before the first upload; not compared.
- `0x0049e138` (the `palette.N` field) is not the live palette: it stays
  zero through every upload.

- Nitro configuration (October 17, 2026): `0x00405600` calls
  `0x0040b0e0(*(0x0047ea40[row+5]) + 0x14c)`, copying six words (recharge,
  boost, maximum, charges, minimum, tail) into `DAT_00465a28..3c` and
  setting the charge `DAT_004c5f10 = maximum << 8`; zero words mean no
  nitro. `+0x14c` is BIKESPEC word 83 (bikes 7–10: 256, 288..384,
  180..240, 10, 120, 15). Riding animation: the advancing block is the
  rider half's `+0x70` (`0x00434660` line 13, `0x00407f30` → `0x0043ae90`);
  the bike's block is only set at spawn/crash and its `+0x81` mirrors
  `half+0x1e9` while riding (`0x00407a90`). Stamina is in the 0x00463c78
  row `*(half+0x1e0)` (`+0x38` maximum, `+0x3c` current).

- Scheduler node list and parking (October 17, 2026). The 0x00437e60 object
  list (`S = [0x0046dcc8]`) holds one node per bike (type 4), one per rider
  half (type 8), pedestrians (type 0x20) and the intersection controller and
  signals (types 0x40..0x400). `+0xdc` is the **draw** callback and `+0xe0`
  the **think** (the older note calling `+0xdc` the think callback was
  wrong); `+0xe4` is move. After the think/move calls, `+0xc = +0x14 + tick`
  (`0x00438108`..`0x00438113`). When `+0xe4` is NULL the function jumps
  straight to `0x00438108` (line 17), so a frozen node keeps its `+0x14` and
  only advances `+0xc` and runs the group test. With `param_3` set, nodes
  whose `+0xc4 & 0xc` is nonzero (bikes/halves) are parked when
  `DAT_004b6e50 - pos < 0x1e00` or `pos - DAT_0049f240 < 0x800`; other nodes
  use 0/0 and unlink. Parking calls `+0xe8 = 0x00406940`, which saves
  `+0xdc`/`+0xe0` to `+0x31c`/`+0x320`, nulls draw/think, clears `+0x2c` on
  bike and half and masks `+0xc0` with 0xfc. The activation code at
  `0x00406700` swaps the saved callbacks back, so `+0x31c/+0x320` is the
  park/wake state (native `Rider::parked` for frozen solo riders only).
- The proxy's solo freeze zeroes `+0xe0`/`+0xe4` before each step, so frozen
  pedestrians neither think nor move; they hold their spawn position and are
  unlinked on a scheduled run once the ring back passes them (the two behind
  the player leave at tick 1382 = 2 + 23×60 in `sched-human-cov2-20261017`).

## Live rider scheduler, parking and waking (October 17, 2026; plan 10 Task 6)

Measured on `napa-field-l1-20261017` (2400 ticks, demo, no solo freeze);
ported in `src/race_schedule.cpp` (`Race::set_live_schedule`).

- Driver: `0x0044b3b0` calls `0x00438170` with `ecx = 0`
  (`0x44b3fc xorl %ecx,%ecx`), so the zone walk of `0x00438170` is unused:
  every node with `+0x0c <= tick` runs `0x00437e60(node, tick, +0x2c != 0)`,
  front to back. After the pass `0x004383e0` insertion-sorts the list by
  `+0x1c` (swap only on strictly smaller, so equal positions keep their
  previous order), `0x004368f0` runs the contacts, the list is sorted again,
  and `0x00407610` closes the pass. The AI scans (`0x00449ce0`) walk links,
  so mid-pass they see the order of the last sort, not current positions.
- Templates (`.data`): opponent bike `0x00463ad8` = flags 3, type 4,
  interval 4, draw `0x00446250`, think `0x00407a90`, move `0x004060b0`, park
  `0x00406940`; opponent half `0x00463b40` = type 8, interval 4, draw
  `0x004463e0`, think `0x00407bd0`, move `0x00406890`, park `0x00406b40`;
  player half `0x00463c10` interval 1 (think `0x00434660`); the player bike
  runs interval 1. `0x00438930` builds `+0x10 = tick - interval`, `+0x0c =
  tick` at tick 0, so tick 1 runs everything with dt 5 (opponents) and 2
  (player). The free-car template `0x00475698` is type `0x10`, interval 10,
  think `0x0044e710`, move `0x0043adc0`.
- Interval tail for `+0xc4 & 0xc` (bikes and halves): 30 with no slice;
  else `d = (pos - DAT_004b6e5c) / 256` (signed), `v = 1` for `-2 <= d < 4`,
  `(-2 - d)/4 + 1` below, `(d + 1)/4 + 1` above; before tick 300 a
  non-player object with `+0xec < 0x1800` adds 1, `< 0x1000` 2, `< 0x800` 4;
  capped at 8. Measured at tick 1: 8, 7, 6, 5 by grid distance, 1 for the
  player bike and half. The half node uses its own `+0x1c/+0xec/+0x2c`.
- Calls through `+0xe8`: the slice-end test and the window test pass
  `edx = 0` (park), the `|lateral| > 0xc8000` test `edx = 1` (clamp). For a
  half, `0x00406b40` only clamps; halves are parked by their bike.
- `0x004060b0` live branch: physics `0x0043a580` (the countdown ticks run it
  too: tick 1 settles `+0x160` 37 -> 74 with no throttle), then for an
  uncrashed bike `0x004067d0` copies `+0x1c..+0x2c`, `+0x30..+0x40`, slices,
  `+0x58..+0x60` and `+0xec..+0x110` to the half; half `+0x1f8 += old - new`
  on the same node, else `0x00407410` recomputes it; `0x004075f0` refreshes
  the place; `+0x310 += distance`, `+0x30c += dt`. `0x00405c80` starts them
  at 600 and `((level*5 + 45)*200 >> 8)*600` (21000 at level 0).
  `0x004049c0` (rider contact) also ends with `0x00407410` for an uncrashed
  rider, from the half's position.
- `0x00439390` (sample crossing) copies the new slice's node and local
  offset into `+0x304/+0x308` for type-4 objects, so a bike's virtual
  position tracks its slice while live.
- Parking `0x00406940` (live, `+0x324 != 0`): `+0x318` crash delay (crashed
  only), pace `+0x314 = ((+0x310 << 8) / +0x30c) * 95/100` with the player
  behind it (`DAT_00464304 = 0x5f`) or `* 105/100` (`DAT_00464308 = 0x69`),
  `0x00431960(bike, 0)` clears `+0x300`, `+0xdc/+0xe0` saved to
  `+0x31c/+0x320` (half: `+0x200/+0x204`), `+0xc0 &= 0xfc` on the bike only,
  both `+0x2c` nulled.
- Parked branch: step `dt * pace >> 8` (player crashed: level 3 `3/4`,
  level 4 full, else half pace), added to `+0x308`, bike and half `+0x1c`,
  and subtracted from half `+0x1f8`; node walk over `+0x304` links (types
  0/1/2, end types 3/4 clamp and zero the pace); virtual finish when the node
  carries a finish (`node+0x10 -> +0x2c`) and `+0x1f8 < 1`: `0x00437800`,
  `+0x308 = finish*256 + rand()%0x701 + 0x100`.
- Ring trackers (`0x00450d30`, two per ring, `0x00450dc0` steps them):
  `0x004552e0` creates both on the back slice and steps tracker 0 by 14 and
  tracker 1 by 91 slices; `0x00454120` steps both with every slice crossing.
  Each writes its slice into its node's `+0x18` (tracker 0) or `+0x1c`
  (tracker 1) while inside that node. Wake test: a parked bike in a node
  holding a tracker wakes when `lo <= +0x308 <= hi` (`lo` = tracker-0 slice
  local or 0, `hi` = tracker-1 slice local or `len*256 - 1`); it lands on
  `0x00449910` from the nearer tracker slice at `sample*256 + 0x80`, road
  centre, takes `0x004091f0` then `0x00408e40` targets (`+0x28` = target x),
  speed = pace, `+0xc0 |= 3`, callbacks restored, `0x00431960(bike, 1)`,
  `0x004067d0`, `0x00407550`. Parking happens within 0x1e00 of the front or
  0x800 of the back; waking needs the tracker window (back + 14 .. back +
  91 samples), which gives the hysteresis.
- `0x00407610` (every 60 ticks from `DAT_004642e4 = 0`, set by `0x00405600`):
  parked halves by place; the three nearest ahead of the player lose
  60/30/15 of pace (`DAT_004642e8`, floor 3000), the three nearest behind
  gain 60/30/15 (`DAT_004642f8`, cap 30000). A rider is skipped only when its
  `+0x304` node and the player's lie on the two branches of one split
  (`0x00454040` node flags: trunk 0xc0, next branch 0x40, previous branch
  0x80; section `+0x20` = the split).
- Measured: the three front riders park on tick 1 (pace 8440 from 21000/605,
  band -15/-30/-60 on tick 1), advance 263/262/261 on tick 9 and 987 on tick
  39; rider 1402c34 parks on tick 337 (front distance 7659 < 0x1e00) and
  wakes on tick 765.
- Brains of rider rows 9..14 (`0x00407990`: `0x004c4cfc + row*0x204`, i.e.
  `0x004c5f20 + (row-9)*0x204`) are personality 0 with the five level speeds
  `+0x1bc` jittered by `rand()%13 - 6` in `0x004078f0`, whose 30 draws precede
  the oracle's `srand`; the replay loads the dumped levels.
- Setup tick: the field capture's setup pedestrians move in tick 1 (+36 =
  one move with dt 1 + interval) while its intersection objects tear down on
  tick 392 = 2 + 13×30 (next = 2, as on the solo captures). Why the two
  differ is open; the replay sets them separately.
- `0x00409ca0` (follow) and `0x004092e0` (can-overtake) read the target's
  half (`+0x168 -> +0x2fc`), whose words lag the bike between its moves and
  contacts.
- Free-car spawns (`0x0044df60` -> `0x004387a0`): position is the spawn
  slice's sample * 256 with fraction 0 (kinds 0/2) or 0x80 (crossing kinds).

### Car cadence / think-to-move correction (2026-10-17)

`0x00438930` initializes car next = spawn tick, last = tick - template
interval (10), dt = 0. Since the driver spawns after the scheduler, the
first timer car runs next tick with dt 11. `0x00437e60`'s exact type-0x10
branch uses signed division by 256 for camera distance, relative forward
velocity < 2000 for kind 0 near the camera, a slow-speed increment below
2000, a near-band cap 6, and far-band factor four/cap 60; odd kinds take
the separate crossing branch.

`0x004498c0` first adds dt * rate and caps to target, then `0x004390c0`
applies drag; do not cap again after the mover. The car template at
`0x00475698` has +0x150 = 50, +0x154 = 10, +0x160 = 111. Cars skip
`0x0043a190` (type 0x10), so ground stays nonzero. `0x00439920` uses
((forward_velocity*2 - acceleration*dt)*dt)>>9, including when speed
clamps to zero: large dt can produce a small forward creep from drag's
negative acceleration. The synthetic red-light fixture now exercises the
near-camera cadence; it must not assume per-tick scheduling in the far LOD.

Supersedes the drift-table interpretation in `car-think-20261016l.md`:
assembly `0x0044ee02` is `MOVSX ECX, byte ptr [EAX + 0x475694]`, with no
index scale. Original bytes `01 00 ff 00` give +800, 0, -800, 0. The old
word indexing accidentally read the car template for kinds 1..3. Evidence:
existing `analysis/oracle/rea-44e710-carthink.md` assembly and the disc EXE
SHA-256 `73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`
(rechecked against `analysis/inventory.json`). No Ghidra writes or Wine run.

The live-field capture's `ring.<car-address>.position/lateral/speed` words
are reliable; its legacy kind/phase offsets are not the car dword offsets
(e.g. tick 227 reports nonsensical phase). Compare only the measured motion
triples and state that limitation. At tick 308 they equal 35240/32000/1431,
while the first pedestrian node differs by one position unit.


### Live scheduler/contact continuation (2026-10-17, ped308)

Reference disc EXE SHA-256 rechecked against `analysis/inventory.json`:
`73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`.
Existing exports/assembly used; no Wine capture or Ghidra database writes.

- `0x00437e60` reads the pre-driver ring camera `DAT_004b6e5c`. The native
  `camera_position()` derives from the player's already-moved +0x138;
  reading it in the pedestrian pass made the tick-308 performer run a tick
  early (13605 rather than 13604). The ring cursor fixes the entire cadence:
  its first one-unit creep now occurs at 309, as captured.
- `0x0043a190` has an early `(type & 0x20)` branch after sampling ground:
  +0xf4 = 0, +0x20 = 0, +0x160 = `DAT_0046dcd8` (74). Pedestrians do not
  take the bike landing/bounce law, even when knocked down. `0x00437bb0`
  also sets scheduler flag bit 4 and clears bit 8 at insertion.
- `0x00437c10` calls `0x00437be0`: clear flag bit 4, set last/next to
  tick+100, mark the list dirty; then set bit 8. `0x00437df0` unlinks the
  node at the next scheduler prepass. Live capture: the retired pedestrian
  remains in the tick-685 list at position 13708 and disappears at 686.
  This supersedes the immediate live pool removal. The nulled-callback solo
  replay retains its separately measured same-step lifetime contract:
  strict human acceptance drops its two frozen nodes at tick 1382. The
  distinction is explicit in the frozen replay path, not a trace-specific
  tick check or comparison exclusion.
- `0x00438170` runs scheduled nodes before `0x004368f0` contacts; the driver
  then runs ring advance / `0x0044dd90`. Car moves and intersection think
  are now a separate phase from driver timers. Cars merge with rider nodes
  in descending position order, with obstacle snapshots taken at the car's
  turn. At 761, native rider 13 previously saw car position 39879 instead
  of its already-moved 39929, corrupting brain +0x198; interleaving fixes it.
- `0x004368f0` contacts walk ascending position. Only the outer (behind)
  node must have last-run == tick and flags & 6 == 6; its partner needs
  flags & 5 == 5. A sleeping car behind a due bike cannot scan backward
  into it. This explains the spurious tick-625 contact: before contacts,
  rider 14 already had the exact original lateral 9296 / steer -368;
  native's unconditional car pass changed them to -5722 / -611.
- Live car separation now reuses `0x00436a40` through the car's persistent
  object image: both previous positions, actual velocities, both masses,
  outer-node orientation, containment/fallback branches and impulses. The
  old scalar approximation first diverged at tick 647 (rider 14 position
  37958 vs original 38001); the shared separator passes that contact and
  preserves the car's deferred impulses for its next `0x00439920` move.
  Legacy unscheduled component fixtures keep their old scalar path.

Measured result: unchanged field trace `99e20344…` matches through tick 787,
including all compared rider/brain/half/scheduler/node-list and car motion
words, RNG, the tick-337 parking and tick-765 wake. No car hydration,
reanchoring or comparison exclusions. First remaining failure at 788:
RNG 2280515322 original / 34387072 native. From 1941267649, original takes
three draws and native one. `RR_FIELD_ALL=1` also exposes rider 12 entering
brain state 3, steer -537 / -1181, steer rate -240 / -480. The two omitted
`0x00409460` attack rolls and `0x004095d0` selection in the native state-3
arm are consistent with this failure; this is static attribution plus a
runtime mismatch, not a captured call-stack proof. Next combat work must
port the complete branch and measure it, not add two dummy draws.


## Full Napa live field: combat, car response and marking lifetime (October 18, 2026)

Baseline `992106d`; original disc hash rechecked against inventory. Existing
address-keyed exports were used; no Ghidra import or patched executable.

- `0x00409460` follows its target, tests `0x00409510`, rolls signed personality
  bytes `+0x1d0/+0x1d1` for kick/backhand, starts `0x004095d0` or resolves
  `0x004096f0`, then calls speed/steer again. `0x00409510` compares the half's
  current animation against all four weapon variants. Starting/hitting an
  attack applies `0x00408d50` events 0/1/3 and 2/4 respectively, each with
  three saturated relationship axes from brain `+0xf0`.
- The hit gate is **descriptor flags `&0x600000` OR half frame byte 2**.
  Preserve graphics flags on playback descriptors and the selected previous
  descriptor. The hit at812 lacked its relationship reaction in native, causing the
  visible836 replan rating error while RNG still matched. Descriptor-only
  hits are independently covered by regression; native now ports both event
  sources and the relationship writes.
  Victim selection409400 is nearest half along the road, backward wins ties;
  reach reads half motion. Repeat/taunt and steal animation queues are real
  selections, rather than discarded random decisions.
- `0x00434660` regenerates stamina before calling407bd0 for the demo player;
  `0x00407bd0` regenerates again after AI dispatch. The missing second pass
  surfaced at1232 after the first measured player car contact.
- Scheduler scans preserve list-sort keys even after a car moves (953).
  Time-to-meet uses signed object `+0x108`, including oncoming cars, rather
  than positive speed `+0xec` (1001). `0x00408ad0` handles a non-null target
  whose bike slice is null by returning to racing and clearing the target
  when changing state (1553).
- At1231 the separate startup-configured original call trace records45 draws:
  `0x00404b8c`, `0x00404bb1` (player contact sound), `0x0044efc6` (car callback),
  then14 triples `0x004513f3/0x00451479/0x004514e7` (terrain). Thus the missing
  draw was `0x0044efa0`, not terrain. It rolls rand%10 for a player-contact
  horn, sets car `+0x1f4=1`, and starts crossing-car `+0x1f8` at240 for a bike
  partner (60 for a non-bike). The timer decays by car dt and forces target
  speed0 while positive. Contact callback order follows outer then inner.
- `0x00437e60` moves an already-outside due car before its window teardown;
  `0x00437c10` clears live bit4, sets8 and last/next=tick+100. It remains in
  the list until next prepass. Immediate native removal first failed1803.
- `0x00437750` billboard types3+ dispatch a type2 marking object without
  separation. `0x004049c0` sets traction timer60/120/180 by type modulo3,
  records selector/frame in half1e8/1e9 plus sample1ec and tick1f0 above
  speed1000, zeros steering rate and adds half the steering. `0x00407a90`
  advances the mark frame every4 ticks, clearing it on sample change or12
  elapsed ticks. Collision boxes are loaded on first draw (44b630/44b980),
  not ring construction; native simulation prepares the original33-slice
  range independently of rendering frequency. This rejects a premature1129
  mark on the far-ahead rider and matches the original1293 contact.
- AI riding animation LOD407f30 reads pre-driver `DAT_004b7ef4`; using the
  moved player's view camera crossed the near/distant boundary early at1402.
  The expanded animation capture measures this independently of bike motion.

The old car probe used decompiler element indices as byte offsets. Correct:
kind1cc (not73), vehicle1fc (not7f), lane1e8 (not7a), phase1c4 (not71).
Proxy schema2 records the corrected offsets. Legacy aliases are explicitly
reported as malformed capture metadata, never compared as named car state.
Schema2 adds every AI animation and30 relationship words. The optional
RR_ORACLE_EXTRA_SPEEDS fixture sets six extra personalities' recorded three
level-speed values before race construction; it reproduces startup jitter
that precedes the race seed, without modifying a live step. This supersedes
the older SIM_SCENARIOS claim that differing captures prove animation pacing.

Measured: unchanged `99e20344…` legacy capture and `294343d5…` schema2 capture
both match all2400 ticks and shared RNG. The latter has no golden-only state
keys. Scope is this captured Napa level1 demo field, not all levels or races.

## Police: overtake latch, lane draw, post-arrest replan (2026-10-18)

- 0x00405990 draws the brain lane byte +0x18c as `rand() % 3` (offline). For
  the periodic cop at tick 4096 that is the only draw of the tick, measured on
  five captures. 0x004091f0 reads it for the straight target.
- 0x00449e90 (called by 0x00408e40 with 0x3f/0/0x96/0x960/0xc000) keeps the
  first candidate on equal time-to-meet. Order on the live list therefore
  decides between a bike and its half at the same position. The half node
  carries the crash body's +0x1c while crashed, and 0x004383e0 (strict <) never
  swaps equal keys, so after a remount the half stays first. A wholly-left
  object met before the target ends the walk with no latch (an oncoming car
  between the cop and the player).
- 0x00408e40 with a latch and gap <= 0x24 leaves +0x19c unchanged unless the
  heading is within 0xf. A latched cop therefore holds its last target speed,
  and the 0x00409ca0 downed-target caps are skipped.
- 0x00416010(4, 0xb4) (the arrest) sets DAT_004c5d7c = 4 and DAT_004753c4 =
  0xb4. At the next 0x004082f0 replan the cop drops a target whose +0x324 is
  0 (the human player). It is measured (`napa-cops-parked`: target 0 at 4572)
  and supersedes the earlier "two-player pursuit rule, not offline" note in
  `race.cpp`.
- The original's RNG stream before the cop varies between otherwise identical
  captures (first difference at tick 245). Traffic and the cop lane follow it.

## Run-to-run RNG variance: launch-time speed jitter (2026-10-18)

Supersedes the open note above ("the original's RNG stream before the cop
varies between otherwise identical captures") and identifies its cause.

- `0x00448200` (WinMain init) calls `srand(time(NULL))` at `0x448224`
  (`0x004581e0` is the CRT `time()`: GetLocalTime, then `0x0045ca30`), then
  `0x004490d0 -> 0x00442960` runs `0x004078f0`. `0x004078f0` copies the ten
  personalities to `0x004c6b40`, copies personality 0 into the six spare rows
  `0x004c5f20 + n*0x204` (rider rows 9..14, read by `0x00407990`) and adds
  `rand()%13 - 6` (via `0x00441750`) to each of their five `+0x1bc` level
  speeds, row-major: 30 draws. The other callers are `0x00445090` (mode
  change), `0x0041f630` (character/career restart) and `0x0042f110` (a valid
  save file only, at `0x42f1f2`).
- The oracle pins its seed later (menu entry), so these 30 draws use the
  wall-clock stream. Measured: brute-forcing the MSVC state from the 18
  dumped jitters (levels 0..2 of rows 9..14, frame 1) of two back-to-back
  captures gives exactly `1792371931` and `1792371960` (plus the `+2^31`
  aliases), i.e. the launch seconds 18:05:31 and 18:06:00. The state at
  `0x004078f0` entry is `time(NULL)` itself: no draw comes between.
- Those speeds move rows 9..14 differently from tick ~235, so the AI overtake
  rolls fire on different ticks: `0x004092e0` (call `0x409372`, return
  `0x409377`, `(rand & 0xff) < 0x00465a10[level] * dt`) and the state-3 rolls
  in `0x00409460` (`0x409487` kick `% +0x1d0`, `0x4094d3` backhand
  `% +0x1d1`). These are the only draws whose count varies (RANDLOG of
  `napa-randlog1/2-20261018`, 245..451); after tick ~490 every run draws the
  same count per tick, but from a shifted stream, so traffic lanes, vehicles
  and phases differ. Not frame pacing: everything after the jitter follows
  the 60 Hz tick.
- Human oracle captures (`RR_ORACLE_HUMAN=1`, level 3) build the ring and its
  intersection controllers with the tick counter at 0 (`0x00438930`: +0xc =
  0, +0x10 = -0x1e): the first crossing think falls on 1 + 30k (Napa tick 751,
  12 draws: `0x41f160` lane pick plus 5 placement draws per car). The demo and
  solo captures measured 2 (`TrafficSystem::setup_tick` default).
- Car think `0x0044e710`, lane change past a slower obstacle
  (`0x0044e85f..0x0044e909`, now ported): when the obstacle from `0x0044a070`
  is closer than `(iVar7*3)/2` and is not a car (`+0xc4 & 0x10`), a kind-0
  car whose obstacle `+0x108` is below its own steps one lane toward 0, or
  from lane 0 to lane 1 when the slice (`+0x140`) `+0x2c` has two lanes; a
  kind-2 car (obstacle not a bike/half, `+0xc4 & 0xc`) steps from below -1
  toward -1, or outward while `-lane` is below the `+0x30` count. That tick
  skips the follow-speed call, and `0x0044edfc` moves `+0x1dc` (lane-centre
  error) only halfway from its previous value. Napa example: car 13e2248
  (spawned tick 391 behind the grid) changes lane 0 -> 1 at 2140, 4.5 samples
  behind the parked player.
- Ported in `0x0044e710` (October 26, plan 10): the lane-validity
  block `0x0044e9bb..0x0044eb85` (a lane outside the slice's count
  is checked against the linked slice `+0x08` (forward, unless the
  `+0x28` slot reads 3) or `+0x18` (oncoming, unless 1), stepped
  toward validity, else the car stops with target 0). Kind 0 uses
  a non-strict bound (count <= lane is outside); kind 2 uses a
  strict bound (count < -lane is outside); the recheck keeps the
  stepped lane on failure and restores the original on success.
  The car reads its lane words from a scrolling-window ring slot
  (`+0x140` == `+0x2c`, newest same-residue sample at/under the
  ring front over the 0x8000 window): verified end to end
  against dumped per-tick slice words on the L5e kind-0 removal
  (lane 1 -> 0 with full stop), closing the L5 (1995) and L5e
  (2399) divergences and advancing L5d past tick 2002. The `+0x28`
  slot reads 0 in every dumped tick, so the link is always taken
  here. See `docs/agent-handoffs/l5-tick1995-trace-20261026.md`.
- Still unported: the backward ring step (`0x00454120` with a negative
  camera delta: `0x00453030(-0x100)` for both walkers, `0x00450f90`, and
  `0x00454c80` at the new back). A crash or arrest pulls the camera back one
  Q8 across a slice boundary, which draws 24 terrain values
  (`0x4513f3/0x451479/0x4514e7`) on the knock/arrest tick.
- City tick 235 draws in the pedestrian think at `0x4410b5`
  (`rand & 3` after the 0x0044a020 side check); native does not reach it.


## 2026-10-18 backward track-ring crossing (0x00454120)

- Negative camera slice crossing: back walker `0x0049ea20` steps `-0x100`
  through `0x00453030`, `0x00450f90(1,-0x100)` moves its slice cursors,
  `0x00454c80(new_back,0x0049ea20)` rebuilds the new back slice,
  `0x00450f90(0,-0x100)` moves the other cursors, then front walker
  `0x0049ee30` steps `-0x100`. The random-consuming native ring now follows
  this order and builds from the back walker on reverse crossings.
- `0x00452e10` enters a previous ordinary road with `0x00452cb0(...,0)`:
  the road/terrain position is `previous.length*256`, while BORR and DZHR
  initialize to `(their sample_count-1)*256`. The applied iterator delta is
  **minus the old road position**, not the overshoot (`0x00452f1d`). A missing
  previous link returns 3 without moving or permanently stopping the walker.
- `0x00451e20` compares sample/start as unsigned on reverse steps. Before
  start, it loads the preceding BORR word, sets end to the current sample and
  start to `sample-(period+1)*units`; before record zero it disables selector
  `0x3f`. `0x00451b60` at an end loads the last word with those same bounds.
- `0x00452230` rewinds while sample is below the **current** DZHR word's
  position, stopping at index zero. A forward walk can leave index equal to
  count: assembly `0x00452256` reads the physically following SGS word before
  decrementing. Preserve it in `CourseSection::scenery_following_word` rather
  than reading past a vector. Disc census: every Napa table is followed by
  `CESR` (`0x52534543`); the other courses also have `HTPR` and zero followers.
  Synthetic SGS resources ending exactly at DZHR have no following word and
  retain zero. BORR header sample counts equal HTPR counts on all five courses.
- A back-walker road transition can immediately build at `local == length`.
  `0x00451970(...,0)` uses HTPR header end height, `0x00451a40` reads
  the two bytes following its path samples, and lane/shoulder backwards
  initializers (`0x00452820`, `0x00452a00`) take the final record's end
  values. `CourseSection::backward_endpoint` preserves these directly rather
  than indexing past `samples`. For wall/building terrain `0x00451710` reads
  the selector pair at count, including padding/following SGS bytes; preserve
  those separately as `TerrainDefinition::end_selectors`. A synthetic back
  walker crosses a road boundary and rebuilds that endpoint without an
  out-of-range access. This endpoint case has assembly/synthetic evidence,
  not an original runtime node-transition capture.
- Terrain negative stepping was already implemented (`0x004516e0` /
  `0x004515a0` / `0x00451380`); the stepping algorithm was unchanged (only the end-selector read was added). New original
  RANDLOG evidence shows exactly 24 draws on knock tick 300, callers
  `0x4513f3`, `0x451479`, `0x4514e7`, eight triples. The native one-anchor
  replay now matches the complete 700-tick capture's RNG and opponent motion.
- The next long on-foot mismatch is separate: original RANDLOG at tick 4552
  calls `0x00409377` (AI overtake roll, `0x004092e0`); neither camera cursor
  crosses a slice. The companion run has the same RNG states at 4551/4552 as
  `napa-cops-foot`. Foot2/3 first differ by one draw at 4580 with stationary
  camera; their older traces do not contain caller logs. This does not claim
  their exact caller was directly measured. See VERIFICATION.md.
- Evidence: existing address-keyed `analysis/decompiled/{00454120,00452e10,
  00452cb0,00451e20,00451b60,00452230,00452060}.c`, assembly in
  `analysis/oracle/rea-452230-dzhridx.md`, unmodified EXE SHA256
  `73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2`
  verified against inventory. Existing dual-road split/merge ring traversal
  remains explicitly unsupported; these captures exercise ordinary-road walking.

### October 25, 2026: rural cross-road renderer and player swing lifetime

- `0x0043bef0` type 4 with marking flags `0x18` runs shoulder `0x0041d260`
  followed by `0x0041d110`, replacing `0x0041c390` hill bands. The latter
  selects category 5 frame 2 once through `0x0044a870`, walks outward from
  projected shoulder X (+0x60/+0x68), with far/near row +0x28/+0x38 and
  scale +0x20 times 256 (negated on side 0). `0x0043b940` returns 16 for
  terrain type 4; `0x00443990` rejects horizontally invisible tiles. Loads,
  shifts, negations, tile limit and dispatch verified against exported assembly
  at `0x0041d110–0x0041d25e`. Port now includes this formerly missing branch.
- `0x00434660` uses `0x00409510` attack-animation membership to decide when
  a player can start a swing. The native legacy 18-tick cap is not an original
  rule: with original club descriptors, it can restart a held swing before
  damage. Descriptor-backed player swing lifetime now follows animation
  completion. Static evidence plus controlled native live-scheduler damage
  tests, not original combat replay acceptance.
- [Investigation and evidence limits](agent-handoffs/user-road-combat-20261025.md).

### October 26, 2026: lane-grid origin across width changes

`0x00444630` updates the previous road record's far lane origin (+0x30)
using the previous sub-slice's +0xd8 when it differs from the newly projected
sub-slice. Assembly `0x00444c57–0x00444ca3` confirms dword loads, origin
comparison, previous origin plus current world X minus camera X, multiply by
current scale, add screen centre and store previous +0x30. `0x0043c170`
uses the previous strip's lane count at both ends. `CourseScene` formerly
used the next sample's rounded left lane origin at its far end, shifting the
whole far grid one lane when ceil(left lanes) changed. CITY sample 159
(left Q8 512 -> 533) exposed scenery through the right lane. Projecting the
near strip's origin at the far scale (including sub-slice centre offset)
restores the original construction. [Reproduction](agent-handoffs/city-width-gap-20261026.md).

### October 26, 2026: inward hills approaching merges

`0x00443cd0` separates exterior side rendering from the inner gap dispatcher
`0x0043be80`. For type-4 hills the latter uses `0x0041d500` with bounded band
counts; exterior profiles alone use the fourth band extending to the viewport.
`0x00454890` joins the two inward world-space profiles in an approaching merge,
collapsing surplus points to a shared ridge and recording per-side band limits.
`0x00454c30` places that ridge at the midpoint of the preceding X endpoints and
evaluates the first profile's signed Q16 slope there (fourth argument unused).
Disjoint terminal profiles use count 4 and `0x0041d620`'s connecting band-2
quad, with neighboring count maxima from `0x0041d500`. Signed compare and divide
sequences checked in exported assembly. Native previously extended each inner
profile to the viewport, producing the reported Napa green curtain; the
approaching-merge path now follows these rules in local render copies.
[Trace and acceptance limits](agent-handoffs/napa-merge-terrain-20261026.md).

## Menu title and description placement (October 26, 2026 follow-up)

The screen-9 capture `analysis/oracle/menu-09-20261026.bmp` exposes layout
errors previously hidden by the broad text/chrome characterization mask.
Assembly, rather than Ghidra's overlapping stack-local names, resolves them:

- `0x0041fa52` tests the saved **format bit 4**, not the measured text width
  suggested by the pseudocode. Ordinary left banners reach `0x0041faa1`:
  right edge minus 16, bottom edge minus 6, DrawText flags `0x802a`
  (right/bottom/single-line). Right banners use left plus 16 and `0x28`;
  format bit 1 uses `0x25` (or `0x15` for slot `0x178`); bit 4 uses `0x29`.
  The black pass is followed by `OffsetRect(-2,-2)` and the colour pass.
- `0x0042071d` tests the saved **description flag 0x1000**, not the RECT's
  right edge suggested by the pseudocode. That case uses `0x4851` and the
  fixed rectangle `(0,300)..(640,420)`; 420 is an absolute bottom coordinate.
  The ordinary path (`0x0042072f..0x00420795`) retains the caller alignment,
  adjusts the shadow rectangle by left +10 / right -6, and top +10 /
  bottom -6 (wide descriptions instead use top/bottom +2 and single-line).
  `0x4852` becomes `0x4850` without flag 0x80. The colour pass offsets -2.
- Measured orange ink bounds (half-open): original `(33,48)..(222,92)`;
  corrected native `(33,48)..(224,92)`; old native `(24,35)..(215,79)`.
  Final paragraph line starts at original x=18, corrected native x=17,
  old native x=115. Font hinting/advances and JPEG deltas remain open.

`src/frontend.cpp` shares these rectangle/flag rules between logical and
scaled drawing. This supersedes the former forced-centering implementation;
it does not establish whole-frame pixel parity or other-screen acceptance.

## Mano-a-mano network play (October 26, 2026, plan 34 input)

This is static analysis only; no capture has been run. Full detail, with
traced and inferred items labelled, is in [MULTIPLAYER.md](MULTIPLAYER.md).

- **Game modes:** mode 3 is networked. `DAT_004c5af9` marks the host,
  `DAT_004c5b00` the client.
- **Transport `DAT_0046c4cc`:** `0x184` modem (TAPI plus COMM handle, two
  players), `0x185` TCP/IP and `0x186` IPX. Both of the last two use WSOCK32
  datagram sockets, up to eight peer slots (`0x0049bd00`) and player table
  `0x00497304` (8 × 0x20). TCP/IP game traffic uses UDP 6996; discovery
  broadcast uses 6995.
- **Rider fields:** `+0x328` is the network slot; `+0x324` is 0 for a local
  human, 1 for AI and −1 for a remote rider.
- **Send cadence** (`0x0044b2e0`): `(DAT_004c5afc & tick) == 0`, with mask 1
  on TCP/IP and IPX and 3 on modem. It runs `0x0043cdb0` and `0x0044f040`.
- **Frames and records:**
  - frames are 0x4c bytes: kind `0x0a`, a presence mask, then three 24-byte
    records;
  - each record starts with a `type << 4 | slot` byte;
  - the receiver is `0x0043e9c0`;
  - clients send to the host, and the host relays (`0x0043c640`,
    `0x00430570`).
- **Not lockstep.** Each peer is authoritative for its own rider and the AI
  riders it owns (owner table `0x004c4fb0`). The host broadcasts the owner and
  interest tables. The heartbeat (`0x00442f60`, every 0x39 ticks) carries a
  per-slot handicap byte computed from progress `+0x1f8`.
- **Networked rule changes:**
  - countdown 0x168 (`0x00442dd0`);
  - traffic density ×4 (`0x0044dd00`);
  - odd-kind spawns suppressed, and spawns gated by the nearest remote riders
    (`0x0044df60`, `0x0044f0d0`);
  - `0x00441750` returns `DAT_0049bce8` in mode 3.
- **Menu:** screen 0x0b (Host `0x0042fc40`, Join `0x0042fef0`, Bulletin Board,
  Chat Messages, Restroom, Disconnect `0x00431600`); screen 0x0d holds F1–F8
  chat messages of 60 bytes each at `0x00497408`.
- **Gameplay rules for the port** (October 26, 2026, plan 34 Task 1; static,
  from the exports and disassembly; full text in
  [MULTIPLAYER.md](MULTIPLAYER.md) "Gameplay rules the port keeps"):
  - rider count `DAT_004642d4 − 1` from the A.I. bikers byte `DAT_004b88f8`:
    Full 15, Limited 8, None = highest player row + 1 (`0x00405600:21-31`);
    humans start on grid rows = network slot, A.I. behind; grid table
    `0x00464258` replaces `0x00464218` above four humans;
  - handicap: every 0x39 ticks over humans' `+0x1f8`, spread
    `(max−min)·0x40/0x4200` capped 0x40, applied only when positive, only to
    an uncrashed moving human, as `+0x108 = v + (b·v >> 8)` in `0x004395e0`;
  - chat log: six lines at `0x004c58c0`, no timeout, drawn at the top of the
    race view by `0x00446c30:378-425`;
  - networked finish: won only for first place (`0x00416010`); each machine
    leaves the race on its own outcome and the others keep racing; kind-10
    record removes the rider (`0x00432440`); 441/442 printed by `0x004314d0`;
    Escape shows 430 (host) or 316 (client) without pausing (`0x00423345`);
  - strings 427–429 are not referenced by the EXE; `DUEL` is not played by
    any mano result path (attract and jukebox only) — the earlier
    "DUEL is the mano cutscene" reading is withdrawn;
  - restroom patterns 5/6/7 for mode 3 unconnected/host/client
    (`0x00444fb0`).
- **Correction.** Plan 09 assumed two-player lockstep exchanging controls. The
  code replicates state (snapshots and events), and up to eight humans can
  play over TCP/IP or IPX.
