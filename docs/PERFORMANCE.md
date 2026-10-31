# Race frame performance

How to measure the race loop's frame time, and what the numbers were on
October 24, 2026 (MacBook, Apple Silicon, 120 Hz display, Metal renderer,
7 render workers).

## Running the game fast

`./run-macos.sh` builds an optimized **Release** `rr-game` in `build-play/`.
`./run-macos.sh --debug` builds the unoptimized Debug tree in `build/`. The race
renderer runs on the CPU, so Debug is 4–7x slower and will not hold 60 fps
with enhancements on. Use it only for debugging.

## Benchmark tooling

```sh
tools/bench_game.sh                         # your settings.txt, Release build
tools/bench_game.sh user original enhanced  # several presets in one run
tools/bench_game.sh --render-scale 2 enhanced
tools/bench_game.sh --debug --frames 300 user
tools/bench_report.py analysis/bench/<stamp>/*.json
tools/bench_report.py --compare old.json new.json
tools/bench_report.py --min-fps 60 report.json   # exit 1 below target
```

- `rr-game --bench FRAMES` runs an autopilot quick race (`--course`, default
  Napa = 3) with the settings file, so widescreen, the enhancement filters,
  fog, grade, post effects and the render scale all apply. The run skips
  `--bench-warmup` frames (default 60; the script uses 90 to cover the fade-in),
  then measures FRAMES frames and exits. It writes JSON to stdout and to
  `--bench-out PATH`, labelled by `--bench-label`. Vsync is off unless you pass
  `--bench-vsync`, which turns it on whatever the settings file says, so the
  number is throughput. The race scale follows the
  settings (`effective_render_scale`) unless you pass `--render-scale`.
- Presets: `user` copies your real `settings.txt`. `original` turns every
  enhancement off at 4:3. `enhanced` turns every enhancement on with widescreen
  match. You can also pass a path to any `settings.txt`. Each preset runs under
  `RR_PREF_OVERRIDE` in `analysis/bench/<stamp>/pref-<preset>/`, so your real
  settings and saves are never written. The real `upscaled/` cache is
  symlinked in when it exists.
- Pacing (plan 33b Task 4): the report's `pacing` block has wall time,
  expected ticks, `drift_ticks`, start-to-start frame intervals (p50/p99/max),
  a 0/1/2/3+ step histogram and `clock_resets`. Drift balances ticks plus the
  clock's sub-tick remainder against wall time × 60, so it reads 0.000 unless
  the race clock really lost time (a stall past the 20-step cap). Frames where
  the race resets its clock on purpose (focus regained, resume) are counted,
  not charged. A 10-minute vsync check at the display's rate:
  `rr-game --bench 72000 --bench-vsync` (120 Hz; 86400 at 144 Hz), then
  `tools/bench_report.py --max-drift 1 REPORT.json`.
- The report includes the build type, renderer, window and pixel size,
  surface size, worker count, the cost-relevant settings, the mean fps, frame
  mean/p50/p95/p99/max, and the mean and max per phase. It also lists up to 32
  frames over 33 ms, each with its worst phase.
- Phases (`src/frame_profile.hpp`) are wall-clock scopes in `RaceScreen` and
  `CourseCanvas`:
  - `sim`: the simulation ticks.
  - `scene_nodes`: scene node construction.
  - `horizon`: the horizon and its RGB conversion.
  - `raster`: world rasterization on the worker pool.
  - `grade`: the colour grade.
  - `post`: motion blur, bloom and vignette.
  - `dash`: dashboard compose and composite.
  - `remap`: the fade/half palette, or indexed to RGBA.
  - `upload`: texture upload and draw.
  - `sprites_hud`: SDL sprites and HUD.
  - `present`: `SDL_RenderPresent`.

  The profiler only reads clocks. It never touches simulation state or RNG.

## Findings (October 24, 2026)

The owner reported 10 fps from `run-macos.sh` with these settings: windowed
x2, widescreen match (852×480 surface, since `hires=0` keeps the race at scale
1), MMPX texture detail (no upscale cache was present, so it cost nothing),
interpolation, perspective, trilinear filtering, fog 40, dusk grade, bloom,
vignette and motion blur.

Baseline per-phase means in ms (`analysis/bench/baseline-*`, local):

| Build | fps | raster | grade | post | horizon | dash |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Debug (old `run-macos.sh`) | 9.7 | 18.65 | 49.64 | 25.22 | 4.26 | 3.05 |
| Release | 69.0 | 6.29 | 4.92 | 1.96 | 0.55 | 0.30 |

Causes:

1. `run-macos.sh` built Debug (`-O0`). That alone was about 7x.
2. The colour grade ran single-threaded on the main thread. Each pixel did a
   double-precision trilinear 17³ LUT lookup, and the LUT was rebuilt every frame.
3. The post chain (motion blur, bloom, vignette) ran single-threaded. The
   vignette called `sqrt` per pixel every frame, and bloom allocated its buffers
   every frame.
4. The horizon palette conversion and the fade remap ran single-threaded.
5. RGB rasterization split rows into exactly one band per worker, and every
   band walked every polygon's edges over the full surface height. Busy road
   bands kept their workers working while sky bands left theirs idle.

Fixes, all bit-identical to the previous output:

- `run-macos.sh` defaults to Release; `--debug` gives the old build.
- `CourseCanvas::rows` runs the grade, horizon conversion and remap passes in
  row bands on the render pool. The grade table is cached per grade. The grade
  reuses the previous result for an identical input pixel; `grade_apply` is a
  pure function, so this is exact.
- `PostChain::apply(..., RenderThreads*)` runs every pass in row bands. Bloom
  reuses its buffers. The vignette caches its per-pixel gain for each surface
  size. A scratch harness compared HEAD's inline chain with the new one (hash
  `362dcc05` from both). `post-chain` now checks that the pooled chain equals
  the inline chain over four frames.
- `render_nodes_rgb_impl` uses 4x the worker count in bands, which the pool
  hands out on demand. It skips nodes whose mapped row range misses a band
  (`normalize_quad` keeps corners inside the original row range). `draw_rgb_impl`
  resets and walks edge rows only inside the band clip. Each row's edge value is
  computed directly from its vertex, so clipping does not change it.
  `raster-rgb-threads` (1 thread vs N, byte-equal) passes.

After (`analysis/bench/final-*`, local), ms:

| Preset | Build | Surface | fps | frame p99 | raster | grade | post | present |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| user | Release | 852×480 | 120.2 | 8.96 | 1.97 | 0.76 | 0.46 | 4.18 |
| original | Release | 640×480 | 120.9 | 9.01 | 0.18 | 0 | 0 | 5.97 |
| enhanced | Release | 852×480 | 120.2 | 8.93 | 1.97 | 0.75 | 0.46 | 4.20 |
| user | Release | 1704×960 (scale 2) | 72.2 | 15.08 | 6.97 | 2.48 | 1.35 | 0.04 |
| enhanced | Release | 1704×960 (scale 2) | 71.7 | 16.70 | 7.02 | 2.51 | 1.36 | 0.04 |
| user | Debug | 852×480 | 30.3 | 37.96 | 15.21 | 6.36 | 4.30 | 0.03 |

At scale 1, Release is capped by `SDL_RenderPresent` at the display's 120 Hz,
even with vsync off. The game's own work is about 4.1 ms per frame. At scale 2
(the owner's `render_scale=2` once `hires=1`), everything on runs at 72 fps.
Scale 3 (2556×1440) measured 31 fps before the raster banding change.

One user Release run showed a single 1.1 s frame inside `present`, a window or
OS stall that did not reproduce in the next run. Treat a single slow frame
with `worst: present` as environment noise; repeat the run.

## Rollback cost (October 26, 2026, plan 34 Task 3 Step 4)

`build-p34-rel/rollback-bench` (Release, Apple M3 Pro, NAPA L0 after 1200
race ticks, `tools/rollback_bench.cpp`):

| Field | Step p50 / p99 | Save p50 / p99 | Load p50 | Worst frame (restore + 16 re-simulated ticks with saves) p50 / max |
| --- | --- | --- | --- | --- |
| 2 humans, 15 riders | 8.2 / 22.7 µs | 5.7 / 14.0 µs | 6.6 µs | 236 / 775 µs |
| 8 humans, 8 riders | 6.1 / 20.2 µs | 7.0 / 20.7 µs | 7.8 µs | 216 / 246 µs |

Snapshots copy only mutable state: routes, hazard/DZHR tables and course
data are shared, and the ring statics, ring terrain and ring DZHR lists are
copy-on-write chunks (a `Race` copy went from 68 µs to 1.4 µs in Debug).
The budget (worst frame ≤ 4 ms) holds on the Mac with a 5× margin over the
p99 frame; the MANGMI AIR X measurement is **blocked** (no device attached),
so no fallback (save every 2nd tick, smaller window) has been needed or
chosen yet.

## Open

- The trilinear sampler and grade still do double-precision math per pixel.
  Integer or SIMD versions would help scale 3+ and Android, but they need a
  new exactness decision for the enhanced path. No device benchmark has been
  run on Android (plan 14).
- `draw_slices` is not passed to `render_nodes_rgb_fog` from the race canvas,
  so the fog far plane always uses 33 slices. This is a fidelity bug in the
  enhancement, not a performance issue. It is recorded here and not changed.
