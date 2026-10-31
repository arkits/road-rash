# Tooling notes

Environment setup, analysis-tool findings and workflow lessons that are not
part of the port itself. Cross-references: [ACTORS.md](ACTORS.md),
[REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md),
[VERIFICATION.md](VERIFICATION.md).

## clangd / C++ LSP

OMP's workspace-root `../lsp.json` points clangd at the native repository's
markers. The October 12 OMP setup session verified status, symbols, diagnostics,
definition and references through `xd://lsp`. That is historical client evidence;
a new session must check its own server status. Other clients may expose a
different LSP interface.

Check the database without modifying it:

```sh
python3 tools/agent_lsp.py
python3 tools/agent_lsp.py --check src/bike.cpp
```

The check reports missing source entries and CMake files newer than the database.
A successful check does not establish exact flag freshness or client loading.
Configure a build directory you own, then point the ignored repository symlink
at it (coordinate replacing the symlink if another client is using it):

```sh
cmake -S . -B build-agent-MYSESSION -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ln -sfn build-agent-MYSESSION/compile_commands.json compile_commands.json
```

Reconfigure after adding/removing sources or changing flags. In OMP, reload
`xd://lsp`, then check status → diagnostics → definition → references. If the
client has no LSP channel, `clangd --check` is a fallback, not proof of that
client's integration. Do not delete another session's build to refresh LSP.

## REA (`npx rea-agents@6.4.0`)

REA 6.4.0 ("Reverse engineer anything") is used as an **independent second
opinion** on the project's own Ghidra export. It re-imports and re-analyzes
the reference executable ephemerally; it never reads or modifies
`analysis/ghidra-project/`.

### Setup on this Mac

`rea doctor` reports the requirements. Two were missing by default:

1. **Node:** REA accepts Node 22.x (>= 22.19), 24.x (>= 24.11) or 26+. The
   system default is 25.8.1 (an unsupported non-LTS line). Use the nvm
   install:
   ```sh
   export PATH="$HOME/.nvm/versions/node/v24.13.0/bin:$PATH"   # or v22.23.1
   ```
2. **Ghidra provider:** Homebrew's Ghidra 12.1.4 needs the *libexec* root
   (the directory that contains `Ghidra/application.properties`), plus JDK 21:
   ```sh
   export GHIDRA_INSTALL_DIR=/opt/homebrew/Cellar/ghidra/12.1.4/libexec
   export JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home
   ```

`rea doctor` still prints `healthy: false` overall: Hopper and IDA are
optional analysis engines that are not installed, and the per-agent-client
registrations are unconfigured. Neither blocks the Ghidra-backed commands;
the `ghidra*` scope checks all report healthy with the two exports above.
`rea setup --install-hopper --yes` would add Hopper, but the project's
pipeline does not need it.

### Commands used

```sh
npx rea-agents@latest doctor
npx rea-agents@latest function "../cdrom/ROADRASH/ROADRASH.EXE" 0x44dd90 --format md
npx rea-agents@latest function "../cdrom/ROADRASH/ROADRASH.EXE" 0x44dd90 \
    --format md --filter-output raw_result.value.pseudocode
```

A `function` call takes roughly one to two minutes on this machine (fresh
ephemeral analysis each time).

### Evidence value and limits

- The `0x44dd90` (traffic spawn timers) pseudocode from REA's Ghidra 12.1.4
  backend was **identical** to the project's own `tools/decompile.sh` export,
  which independently supports the fidelity of the existing export pipeline.
- Good for: spot-checking a suspicious decompile before trusting it; getting
  a second rendering of a function whose Ghidra output looks wrong (see the
  gotcha list below).
- Not good for: bulk export (the project pipeline is faster, address-keyed
  and already done) or anything needing the project's manual annotations.

## Analysis gotchas

Lessons from the traffic work (October 11, 2026) that generalise to the rest
of the port:

1. **Ghidra pointer arithmetic is word-scaled.** `piVar3[0x71]` and
   `piVar3 + 0x1c` on an `int *` are byte offsets `0x1c4` and `0x70`. Every
   object-field mapping must multiply the bracket by 4.
2. **Ghidra can mis-decompile small helpers.** `0x0040b5f0` decompiled to an
   empty loop returning void; the real code (from
   `analysis/original-disassembly.txt`) walks the `SNAC` wrapper chain. The
   disassembly is authoritative — check it whenever a decompiled function's
   control flow looks meaningless.
3. **Object slots are `int`s unless proven otherwise.** The car phase
   `+0x71` is a full int slot holding 192..319; storing it in a `uint8_t`
   wraps to 0..63 and silently freezes the car. The original writes
   `(rand & 0x7f) + 0xc0` without a byte truncate.
4. **Resource tags are reversed between disk and code.** The directory shows
   `ANIM`/`CANS`; the code searches for `0x414e494d`/`0x43414e53`
   (`"MINA"`/`"SNAC"` packed the other way). `rr-inspect` displays the
   readable form; match on the display form in native code.
5. **`CRSR`/`LBTR` container layout** (for building synthetic fixtures):
   24-byte header (`CRSR`, …, `LBTR` at +12, table offset at +16); the table
   is `LBTR` + count at +8, then 32-byte entries: tag, id, offset, size,
   16 pad bytes. Entry payloads start right after the table.
6. **Zero words in the raw EXE image are runtime-initialised.** The vehicle
   count `DAT_00474d88` and masks `0x004b8fe0` are zero on disk and filled
   from `DATA/CARS/*.CAR` at load (`0x004465f0`); the static car template and
   per-kind words at `0x00475698`/`0x00475700` *are* in the image and must be
   read from the user's executable at runtime (`read_exe_data`), never
   embedded.
7. **One shared rand stream.** The game's `rand()` is MSVC CRT
   (`seed*214013 + 2531011`, return `(seed >> 16) & 0x7fff`), shared by every
   system. Adding a consumer (traffic) shifts the stream for all other
   consumers — exactly as in the original. Consequence for verification:
   feature on/off frame diffs diverge globally, not locally, so pixel diffs
   cannot isolate a single subsystem's render effect.
8. **Numbers must be traced to their exact constant.** The traffic cruise
   uses `DAT_004b7e8c = 25`, not the spawn rate `DAT_004b7e84 = 2500`; the
   two live 0x18 bytes apart in the same initialiser (`0x0044dd00`). Reading
   "a rate-looking constant" from a nearby address produced a 100× speed bug
   caught only by headless probing.

## Portable C++ pitfalls (plan 33b)

- **Unsequenced reads.** In `f(a(), b())` or `a() + b()` the order is
  unspecified. Clang and GCC go left to right; MSVC may not. Two reads from a
  stream, a bit reader or the RNG in one expression must become separate
  statements. `explode.cpp` corrupted every packed resource under MSVC this
  way, and only real data caught it, because Windows CI has no disc and
  MinGW under Wine uses GCC's order. The cross-play workflow is what runs
  MSVC on real data.
- **Line endings.** Git on Windows checks out CRLF by default. Anything that
  hashes sources (the simulation fingerprint) must normalise them.
- **Static OpenSSL has no system roots.** Its default CA paths are the build
  machine's. TLS clients use `system_ca_file()` (`src/net/system_ca.cpp`).

## Linux gate in Docker (plan 33a)

- Image: `docker build --platform linux/arm64 -t rr-linux-gate:arm64 -f linux/gate.Dockerfile linux`
  (`linux/amd64` for x86-64; OrbStack runs it under Rosetta, fine for
  correctness, not for timing). Packages come from `linux/install-gate-deps.sh`,
  which CI uses too.
- Run: mount the checkout read-only, the disc so that `../cdrom/ROADRASH`
  resolves beside it, and a named volume for the build, then
  `tools/linux_gate.sh linux-gcc /build/linux-gcc` (presets `linux-gcc`,
  `linux-clang`, `linux-asan`). A worktree's `.git` is a file pointing into
  the main repository: mount that `.git` at its absolute host path and set
  `safe.directory` (`GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory
  GIT_CONFIG_VALUE_0=*`), or build provenance fails with "not a git
  repository". Mount the `analysis/oracle` symlink's target at its absolute
  path for the fixture tests.
- The source is mounted live: editing while a container builds mixes old
  and new files. Rerun the gate on the final commit.
- Sanitizer stacks need `llvm-symbolizer` (the image links llvm-19's); without
  it, `addr2line -Cfipe <binary> <offsets>` resolves the PIE offsets.
- Under ASan with 12 parallel jobs and Docker's 8.8 GB, the two
  `oracle-terrain*` tests can be killed for memory (no output, "Subprocess
  killed"); run them alone (`ctest -R oracle-terrain -j1`).
- A stale CMake cache in a reused build volume can keep backends from an
  earlier configuration (ALSA ON with no headers); `tools/determinism_matrix.py`
  configures with `--fresh`.

## Portable C++ pitfalls (plan 33a)

- **Plain `char` is unsigned on ARM64 Linux** (found in Part A).
- **Linux sanitizer runtimes own `operator new`.** TSan's static runtime
  defines it, so `tests/alloc_counter.hpp` turns its replacement off there;
  under ASan the replacement must cover the nothrow forms too, or
  `std::stable_sort`'s buffer pairs ASan's `new` with our `free`.
- **Static OpenSSL on Linux** looks for roots under the build container's
  `OPENSSLDIR`, which Fedora does not have; `system_ca_file()` picks
  `$SSL_CERT_FILE` or the distribution bundle.
- **Wayland scale arrives after the first commit.** A screenshot of a
  window's very first frame on a scaled Wayland output is laid out for scale
  1; capture after a few frames.

## Multi-session workflow

Two agent sessions have worked in this repository concurrently (October 11,
2026: the traffic/session-23 work and the plan-10 oracle work).

- `git add -A` sweeps the other session's in-flight files into your commit;
  this happened twice (`37c4597` contains the traffic sources, `51cb898`
  contains plans 10–15). Stage with **explicit pathspecs** whenever
  concurrency is possible, and check `git status` / recent `git log` first.
- `ls -laT` on the files you plan to touch reveals active writers (minute-old
  mtimes mean someone is mid-task).
- Rewriting shared history while another session works is riskier than
  leaving interleaved commits; leave the history and document the
  interleaving instead (see the session 23 entries in [PROGRESS.md](PROGRESS.md)).
- Before building or running `ctest`, expect the working tree to contain the
  other session's mid-edit code; a red build may be theirs, not yours.

REA actor animation analysis (October 11 continuation):
`npx --yes rea-agents@6.4.0 function ../cdrom/ROADRASH/ROADRASH.EXE
0x0043ae90 --format json` with the environment above produced canonical
evidence in `analysis/oracle/rea-actor-animation.json`. The pseudocode agrees with
the repository's Ghidra export. The playback port was separately validated
against 2,048 calls to the original function; REA output alone was not treated
as runtime proof.

## Emscripten (plan 23, web port)

Local install only, owner-approved October 12, 2026 (plan 23 Task 1 Step 1):

- `.tools/emsdk`, cloned `--depth 1 --branch 6.0.10`, installed and
  activated there; nothing global. 6.0.10 (released 2026-09-21) is the
  newest release at least two weeks old; `emcc 6.0.10
  (d6c521a7f05449857c76bd99e396895583cf2083)`.
- emsdk needs Python >= 3.10 (the Xcode `/usr/bin/python3` is 3.9); run
  its commands with Homebrew Python on `PATH`. Tests run under the
  bundled Node v24.19.0, never the system Node.
- Configure: `source .tools/emsdk/emsdk_env.sh` then
  `.tools/emsdk/upstream/emscripten/emcmake cmake -S . -B build-web
  -DCMAKE_BUILD_TYPE=Release`.
- Gotcha: `-sDISABLE_EXCEPTION_CATCHING=0` must be in compile options as
  well as link options, or no landing pads are emitted and every `catch`
  is dead (see [WEB.md](WEB.md)). The ported code throws
  `std::runtime_error` pervasively, so this bit every test at once.
- CTest logs under `--output-on-failure` are bloated by Node printing the
  whole single-line `.js` as error context; grep past it.


## Browser release tooling (October 25, 2026)

`tools/build_web.sh` uses the existing emsdk 6.0.10, bundled Node 24.19.0 and
SDL 3.4.8. No new SDK or browser download was needed. Browser integration used
Playwright 1.62.1 from the Codex bundled runtime and the existing Chromium build
1234 in `~/Library/Caches/ms-playwright`. `NODE_PATH` can point to that runtime's
`dependencies/node/node_modules`; no dependency or browser binaries are committed.
Vercel CLI 63.1.2 is authenticated locally for `arkits/road-rash`; the connector
required reauthentication, so preview upload uses `vercel deploy --prebuilt`.
See WEB.md for the asset-free build/output boundary and exact test coverage.
