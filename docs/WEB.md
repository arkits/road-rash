# Web port (plan 23): toolchain, build and measurements

The current native game is playable at https://road-rash.archit.xyz, hosted
on the owner's Dokku VPS. Game files load automatically on demand and cache
locally; no disc selection is required. Pushes to main build/deploy through
GitHub Actions. See [WEB_HOSTING.md](WEB_HOSTING.md) for the current deployment,
persistent assets, TLS and operations.

Build locally with `tools/build_web.sh`. An asset-free local server still
supports original PC disc ISO/folder import. The Vercel deployment and
local-only asset policy described below are historical, superseded for the
VPS deployment by the owner's explicit October 25 hosting request.

## Playable browser implementation — October 25, 2026

**Historical deployment:** [Vercel preview](https://road-rash-o4j8h7nq2-arkits.vercel.app) — READY, engine/config
shell commit `ba1fe64`, engine `096bff2` (implementation `8673a75`). This project requires Vercel
sign-in for previews. Import your disc on this origin, then choose Quick race.
Framework: static HTML/JavaScript + WebAssembly; prebuilt upload, no remote C++
build. Vercel does not report a build duration for this upload.

- `RR_WEB_BROWSER=ON` builds `rr-game.js` + `rr-game.wasm` from the same
  `game_main.cpp`, `RaceScreen`, simulation, renderer, frontend and audio code
  used natively. The separate default Emscripten configuration runs tests in Node.
- **Architecture decision:** Asyncify replaces the proposed all-platform callback
  rewrite for this release. SDL 3.4.8 already yields inside `SDL_RenderPresent`
  and `SDL_Delay`; explicit extra frame yields cause blank frames and must not
  be added. Asset reads use `EM_ASYNC_JS`. The browser canvas retains its WebGL
  drawing buffer so asynchronous reads cannot discard the last displayed frame.
- The canvas fills the play area. SDL's window (and buffer, x devicePixelRatio)
  follows the canvas CSS box, and SDL letterboxes 4:3 or widescreen output
  inside it. `Widescreen: match` fills any aspect up to 16:9 (October 26, 2026).
  No JSPI, pthreads, SharedArrayBuffer or COOP/COEP headers are required.
- `platform_web.cpp` exports the existing `ImportJob` request/supply protocol.
  It verifies the same 341 reference hashes for folder and ISO imports. The
  browser streams ISO ranges, writes verified files into a new OPFS generation,
  then atomically publishes `active.json` only after every file succeeds.
  Cancellation, quota errors and bad discs preserve the prior committed disc.
- `Assets::read` loads local OPFS files on demand. No full-disc MEMFS preload or
  hosted game data is needed. Startup validates required paths, hashes in the
  marker and file sizes; missing/evicted data returns to the importer.
- Saves/settings use `/persist`, an IDBFS mount with `autoPersist`, restored
  before the native main function starts. The existing atomic-file writer is
  unchanged. Disc and save data belong to the deployment origin: changing the
  URL requires a new import; browser eviction/private-mode cleanup can remove it.
- The launch gesture creates/resumes the SDL audio context. Keyboard, existing
  SDL gamepad/touch handling, fullscreen and native pause remain shared code.
  `visibilitychange` pushes SDL lifecycle events, pausing audio/simulation and
  requiring an explicit resume after returning.
- Non-pthread wasm builds run requested renderer bands inline, including when
  settings request multiple workers. Native worker behavior is unchanged.
- `web/` contains the launcher, progress/cancel/retry UI, course selector,
  controls, non-affiliation notice, canvas and error recovery. **Quick race**
  (course and rider pickers, `--race --course N --character C`) starts the
  race directly as a Thrash race and finishes on the original results and
  standings screens; **Start game** runs the original frontend. Enter skips the
  opening movie/credits. All five native course choices were browser-tested.

### Rider picker — October 27, 2026

The Quick Race rider is a chip (portrait thumbnail, name, level-1 bike) that
opens a modal modelled on the original character select (`0x00421df0` case
`0xe`): the `IMAGES/CHARS/<rider>-N.RRI` portrait (the `.RRI` is a JPEG, shown
through a `data:` URL: the deployed CSP `img-src 'self' data:` blocks `blob:`), the orange Badloc name, `Height/Weight/Bike/Cash` and the
bio from `TEXT/ENU.DLL` string `40+i` (heights/weights `0x00463c78` +8/+0xc,
cash `0x00469000`, bike `0x004641c8[row*5]` named through slot `0x80+i`,
imperial formats 4000/4001), and three columns of names with the yellow marker.
A side rail draws the in-race sprite: `DATA/BIKERS/HIBOB.DAT` riding frames
3/6/5/8/9 through the bike's remap `0x0046e158 + bike*256` and
`DATA/PALETTE.RAW`, as the race does (`0x00446190`). Everything is parsed in
`web/app.js` at run time from the verified hosted or OPFS files; nothing
disc-derived is committed. Arrow keys/Home/End move the radio group, Enter or
double-click rides, Esc goes back; at ≤720 px the picker is full screen with a
two-column, 44 px-row roster. The choice is remembered in `localStorage`.

### Course picker — October 27, 2026

The Quick Race course is a chip of the same kind (postcard thumbnail, name,
length) opening a modal modelled on the original course select: the 243-wide
`IMAGES/POSTCARD.RRI` strip with `IMAGES/POSTCARD/<course>.RRI` right of it
(`0x0041e240`, `0x0041dbc0`; the unqualified postcards, not the `Q*` stamped
ones or the alternate Sierra), the orange name, the blurb and `Length: %s %s`
from `TEXT/ENU.DLL` strings `21+i`/27 with the level-1 length `0x00468f58`
(tenths of a mile, `[course*5 + level]`, as `0x00420810`), "Level 1", and the
names in the original two-row, three-column order (City, Sierra, Peninsula,
Napa, PCH). The rail pans the course's in-race horizon
`IMAGES/HORIZONS/<name>.BOB` (720x320 indices, name through `0x00467aa0`,
`DATA/PALETTE.RAW`). Roster buttons carry the launcher's `--course` number
(Napa 0, City 1, Sierra 2, PCH 3, Peninsula 4), so launch args are unchanged.
Keyboard, touch, phone layout and `localStorage` follow the rider picker;
`course_select` telemetry is sent on confirm. `tests/web_browser_smoke.cjs`
now picks each course through the modal.

The page also loads the original Badloc/Futura TrueType files from the verified
local disc after import/restore. Browser FontFace uses in-memory bytes from OPFS;
no game fonts are published. Badloc headings and Futura controls/body replace the
system-font approximation. Before import the page uses system fallbacks. Browser
smoothing differs from the native thresholded text rasteriser.

### Online launcher panel — October 27, 2026

The launcher has a **Multiplayer** panel (plan 34) below Start Game and Quick
Race. It does not speak the binary protocol; it polls same-origin
`/net/health` every 8 s while the launcher is visible. `rr-netd` now adds
`public_rooms` to that JSON: the same entries a `RoomList` reply carries
(code, host alias, level, humans, seats, `lobby`/`full`/`racing`), at most 32,
public rooms only, without the build filter. Production already proxies
`/net/health`, so no Nginx change is needed.

Buttons start the same `rr-game` with `--room`. A room code joins;
`@watch:CODE` joins as a spectator; `@host` opens the Host popup and `@rooms`
the in-game list (`Frontend::net_join_invite`; `@` never occurs in a code or
link). The page also passes `--net-server` as `wss://<page host>/net` (`ws://`
on plain-HTTP localhost), so the game connects to the server the panel lists.
An invite link's `?room=` shows a banner and is removed from the address so
Start Game keeps its meaning.

Local end-to-end test with a real server:

```sh
RR_NETD_PORT=18080 RR_NETD_HTTP_PORT=18081 RR_NETD_BIND=127.0.0.1 build/server/rr-netd
node tools/web_dev_server.mjs build-agent-browser/dist --assets /path/to/hosted-game-assets --port 8766
```

`web_dev_server.mjs` serves the bundle (and optional hosted assets) on
loopback and forwards `/net` (WebSocket) and `/net/health` to `rr-netd`, like
the production Nginx locations. Without `rr-netd` the panel shows "Server
offline" and an explanation; `tests/web_browser_smoke.cjs` checks that state,
then a routed `/net/health` list, the invite banner and code validation.

### Build, serve, test and deploy

```sh
tools/build_web.sh
python3 -m http.server 8765 --directory build-agent-browser/dist
# In another terminal; use an installed Playwright through NODE_PATH if needed:
node tests/web_browser_smoke.cjs
# Optional ISO fixture, kept outside the served bundle:
RR_WEB_ISO=/absolute/path/to/disc.iso node tests/web_browser_smoke.cjs
# Authenticated, linked Vercel project; preview by default:
vercel deploy --prebuilt --yes
```

The build uses the already installed/pinned emsdk 6.0.10 and SDL 3.4.8. It
stages Vercel Build Output API files under `.vercel/output/`. Deploy **prebuilt**:
the remote builder does not have the local emsdk. `RR_WEB_BUILD_DIR` selects a
private build tree and `RR_BUILD_JOBS` controls parallelism.
`prepare_web_deploy.py` permits only nine code/metadata/license/preview files and rejects
any file matching a disc hash. The wasm is about 3.2 MiB raw; the tested race used
about 187 MiB of wasm memory. Executable URLs carry content hashes and cache immutably; the HTML entrypoint
uses no-store. Pinned Sentry/PostHog SDKs are bundled with npm and esbuild; see
[WEB_TELEMETRY.md](WEB_TELEMETRY.md). The output also sets MIME sniffing,
referrer and content-security policies. No game data enters the upload.

### Verification scope and remaining limits

- Native Debug: **106/106 CTests passed**, zero skips (165.35 seconds on the
  final native gate). Browser-specific changes do not change simulation rules.
- Node wasm: **101 passed, 2 skipped, 1 failed** of 104. `race-screen` and
  `mixer-device` require browser SDL devices and skip 77 under Node. The large
  PCH terrain oracle needs a 4 GiB Node-test memory ceiling (2 GiB caused
  `std::bad_alloc`); the browser retains its default memory ceiling.
- `raster-rgb-alloc` still differs from its native pre-change checksum in the
  optional full-colour enhancement. Its allocation and frame-stability checks
  pass. The golden has **not** been changed and full enhanced-pixel parity is
  **not** claimed. Original indexed rendering, hires fixtures and simulation
  oracle checks pass. This portability difference remains open.
- Local Chromium/Playwright covers folder and ISO import, malformed ISO,
  cancellation, quota failure, disc/save reload, five courses, keyboard driving,
  nonzero audio output under normal autoplay policy, pause, the visibility
  bridge, fullscreen, frontend and missing-file recovery. The same flow is
  checked with deployment CSP headers. Screenshots/network evidence are local
  under `analysis/browser-20261025/`, never committed. No asset requests/uploads
  or uncaught JavaScript errors were observed.
- Browser hardware gamepads/touch, Firefox/Safari, mobile devices, a whole career
  save round trip and long-session performance remain unverified. The original
  native game's known fidelity gaps still apply. Optional threaded web rendering
  and a future callback refactor remain separate work, not release prerequisites.


## Toolchain (Task 1, Step 1)

- **emsdk 6.0.10**, installed locally at `.tools/emsdk` (clone, `--depth 1
  --branch 6.0.10`), installed and activated there; nothing installed
  globally. Owner-approved (plan's Global Constraints) on October 12, 2026.
- Pinned by the plan's rule (newest release at least two weeks old on
  October 12, 2026): 6.0.10 was released 2026-09-21; 6.0.11 (2026-10-06)
  and 6.0.12 (2026-10-08) were too new.
- `emcc 6.0.10 (d6c521a7f05449857c76bd99e396895583cf2083)`.
- **Node v24.19.0** bundled with the emsdk
  (`.tools/emsdk/node/24.19.0_64bit/bin/node`); the system Node (v25.8.1)
  is not used. `CMAKE_CROSSCOMPILING_EMULATOR` points at the bundled Node.
- **SDL 3.4.8**, the same pinned release archive and SHA-256 as the macOS
  bundle build (`cmake/sdl3_static.cmake`, reused verbatim). Emscripten
  backend drivers detected at configure time: video `dummy emscripten
  offscreen`, render `ogl_es2`, audio `disk dummy emscripten`, joystick
  `emscripten virtual`.
- Requires Python ≥ 3.10 to run emsdk itself (the Xcode `/usr/bin/python3`
  is 3.9); Homebrew Python works, and emsdk then uses its own Python 3.13.

## Build (Task 1, Step 2)

- `cmake/emscripten.cmake` holds the web settings; `CMakeLists.txt` has an
  `if(EMSCRIPTEN)` branch. Configure with:
  `emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release`
  (source `.tools/emsdk/emsdk_env.sh` first).
- Task 1 scope: `rr-resource` and the tests only. The game executables
  (`rr-game`, `rr-viewer`, `rr-import`, `rr-course`) keep blocking main
  loops and are skipped for `EMSCRIPTEN` until the SDL main-callback
  rework (Task 3).
- Release flags: `-O3 -flto -msimd128`, `-sALLOW_MEMORY_GROWTH=1`,
  `-sENVIRONMENT=node`, `-sDISABLE_EXCEPTION_CATCHING=0`.
- Compile issues found and fixed:
  1. **Exception catching must be a compile option, not just a link
     option.** Emscripten 6.0 emits no landing pads unless
     `-sDISABLE_EXCEPTION_CATCHING=0` is on the `em++` command line at
     compile time (verified: objects lack `__cxa_begin_catch` /
     `__cxa_find_matching_catch` references without it). With the flag at
     link time only, every `catch` in the ported code is dead and any
     thrown `std::runtime_error` escapes to JS as an unhandled promise
     rejection. Fixed in `cmake/emscripten.cmake` (`add_compile_options`
     and `add_link_options`).
  2. No ported-code changes were needed otherwise: `std::filesystem`
     (`importer.cpp`), `SDL_IOStream`, fixed-width integers and the ISO
     parser all compile for wasm32 as-is.
- Known 32-bit risk for Task 2/3 (not yet hit): `long` is 32 bits under
  wasm32 vs 64 on macOS ARM64. `play_video(..., long ticks)` and
  `run_game(..., long menu_frames)` in `src/game_main.cpp` should move to
  explicit widths when Task 3 touches those signatures. `Iso9660::ReadAt`
  already uses `uint64_t` offsets.

## Tests under Node (Tasks 1–2)

Task 1 (October 12, 2026) ran 63 tests: 46 pass, 12 skip, 5 fail (table
below, kept for the record). Task 2 closed all five and un-skipped the
twelve; the suite is now **68/68** (67 pass + `mixer-device` skipped 77).

| Test | Task 1 symptom under Node | Task 2 fix |
| --- | --- | --- |
| `oracle-sim` | `verify_sim.py` executed the `.js` directly (`Permission denied`) | Script runs `.js` through Node (`run_binary`/`node_prefix`: `RR_WEB_NODE`, emsdk bundle, `PATH`); binary links `-sNODERAWFS=1`. Diagnostic records the pinned `known-divergence`, same as native. |
| `race` | Stack-cookie overwrite before any output; native passes | Call chains need more than the 64 KiB default stack. `-sSTACK_SIZE=1MB` for all web links. No smash: ASan natively clean, and 1 MiB passes. |
| `sim_fields` | `RuntimeError: divide by zero`; native prints "sim field tables agree" | Genuine UB: `apply_spec` (`src/bike.cpp:104-115`) divides by zero on zeroed specs. UBSan natively reports 6 hits. Every divisor guarded to zero (= ARM64 result); real specs unaffected. Pinned by a zero-spec block in `tests/bike_test.cpp`. |
| `importer` | `FAIL create nonseekable source` | No FIFOs/pthreads in the single-threaded build. FIFO block gated out under `__EMSCRIPTEN__`; web nonseekable input arrives via the Task 4 `ImportJob`. |
| `mixer-device` | "No audio context available" | Binary exits 77 under Emscripten (no audio clock under Node) with a message; ctest maps 77 to skip. Native dummy-driver coverage unchanged. |

Former skips (all exit 77 for invisible disc/goldens) now pass via
`-sNODERAWFS=1` on every test executable (`rr_web_hostfs` in
`cmake/emscripten.cmake`, called from `CMakeLists.txt`; per-target so the
future browser game keeps the sandboxed filesystem):
`oracle-patch/road/scene/raster`, `polygon`, `text`, `frontend`, `sound`,
`music`, `pe-strings-corpus`, `career-corpus`, `wav-corpus`. Native-only by
design: `first-run-smoke` (excluded for `EMSCRIPTEN` in `CMakeLists.txt`).

Step 3 audit: `Iso9660::ReadAt` offsets are `uint64_t`; no `size_t`
file-offset arithmetic above 2 GB. The only `long`-as-counter uses are the
`game_main.cpp` tick/frame locals, owned by the Task 3 refactor. `long()`
casts elsewhere convert bounded in-memory sizes.

`tools/ci.sh` runs the web tree behind `RR_WEB=1` (`RR_WEB_DIR` overrides
the build directory).

## Import job (Task 4, October 12, 2026)

`src/import_job.hpp/.cpp`: `ImportJob` (table validation, ISO descriptor →
directory → ≤1 MiB file-chunk walk, per-file SHA-256 before delivery,
commit-only-when-clean) plus `ImportSink`. `src/sha256.cpp` grew a
`Sha256Stream` (the one-shot `sha256()` is now a thin wrapper; same
vectors). `src/importer.cpp` reimplements `import_game` as a sync driver
over the job (`FilesystemSink` keeps staging/marker/rename/cancel
semantics; cancellation still surfaces the progress callback's message).
`tests/import_job_test.cpp` covers the plan's six cases; the existing
`importer_test.cpp` passes unchanged. Full gates: native 70/70, web 69/69
(new `import_job` test in both), ASan/UBSan clean for
`importer/import_job/sha256/iso9660`. Task 3 stays blocked on another
worker's uncommitted `game_main.cpp` changes.

## Measurements (Task 1, Step 4)

`rr-bench-web` (`tests/bench_web.cpp`, synthetic only): 20,000 renders of
the `world_renderer_test` scene plus 20,000 decodes of a synthetic 4×4
Cinepak frame. Built natively (`-DRR_BENCH_WEB=ON`, Release, Apple Clang)
and for wasm (`-O3 -flto -msimd128`).

| Case | Native (Release) | Wasm (Node 24.19.0) | Ratio |
| --- | --- | --- | --- |
| render 20,000× | 19 ms | 22 ms | ~1.2× |
| cinepak 20,000× | 254 ms | 162 ms | ~0.6× (wasm faster) |

Checksums identical (render 9593448, cinepak 24000000): the wasm build
computes exactly what the native build computes. The ratio is far below
the plan's 3× stop threshold, so no performance task reordering is
needed; real frame-time measurement stays in Task 7.

Sizes (`build-web/rr-bench-web.*`, code only — the bench links the whole
`rr-resource` but only runs two subsystems):

| File | Raw | gzip -9 | brotli -11 |
| --- | --- | --- | --- |
| `rr-bench-web.js` | 14,125 B | — | — |
| `rr-bench-web.wasm` | 54,538 B | 23,151 B | 19,879 B |

## Historical deployment scaffold (October 12, 2026)

Early Task 9 Steps 1–2 scaffold; the playable game target does not exist
yet (Tasks 3–6 open), so there is nothing playable to deploy.

- `vercel.json` (repo root): `buildCommand: tools/build_web.sh`,
  `outputDirectory: build-web/dist`, static (`framework: null`), caching
  headers for future hashed `.js`/`.wasm`, no-cache `index.html`. No
  COOP/COEP headers on purpose: the baseline single-threaded build works
  on any static host without them; Task 7 adds them with the threaded build.
- `tools/build_web.sh`: writes a status placeholder `index.html` (with the
  non-affiliation notice the plan requires) plus `LICENSES.txt`, then runs
  the no-game-data bundle check (game extensions + SHA-256 against
  `src/reference_hashes.inc`). `RR_WEB_WASM=1` is reserved for the Task 3+
  game target. Verified locally: bundle builds, `vercel.json` parses,
  placeholder serves 200 over a local static server.
- `.vercelignore`: excludes local-only trees from uploads (`.tools/`
  ~50k files, `build*/`, `analysis/`, Android build outputs); without it
  `vercel` fails with "files should NOT have more than 15000 items".
  `installCommand` is a no-op (no npm dependencies).
- Not done: preview-deploy check, custom domain, `--prod`. Per plan 23,
  deploying publicly needs the owner's explicit yes (Task 9 Steps 3–4),
  including how the page names the project.

## Notes for later tasks

- Every test binary exits through Emscripten's async `run()` glue; a
  nonzero `main` return (including skip-77) still propagates correctly
  under both Node 24 and 25, but `--output-on-failure` logs are huge:
  Node prints the entire single-line `.js` as the error's source
  context. Grep past it.
- `build-web/` is the web build directory (git-ignored, like the other
  `build*/` trees). `RR_BENCH_WEB` stays OFF everywhere except the web
  toolchain file and explicit bench configures.
