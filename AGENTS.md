# Road Rash native port — agent instructions

## Goal and working policy

The goal is a **100% complete, feature-complete port of Road Rash for modern
systems**: every course, level, mode, bike, opponent, weapon, hazard, cutscene,
sound, menu, option and save behaving like the original. Platform order: macOS
ARM64 first, then Android ARM64 and Linux, then Windows (plan 33). The port is
not done while any placeholder, stand-in or approximation remains.

- **Implement plans completely.** When you take a plan or task, finish all of
  it: code, tests, oracle evidence and docs. Do not defer parts to "later", leave
  TODO stand-ins, or mark a task done with known gaps. If the work is large,
  split it into sub-steps and keep going until it is finished. Open items belong
  in a plan only when blocked by an external fact (for example no device attached),
  and then you advance an independent slice.
- **Work autonomously.** Do not ask the owner whether to do something the port
  needs. Decide, do it, and record the decision in the docs. Ask only when
  a choice is genuinely the owner's, such as licensing or credentials.
- **Commit and push** finished work to `main` with explicit paths, without asking.
- Fidelity rules below still apply: never claim a match that was not measured.

## Scope and entry points

This Git repository is `native-port/`; its parent `rr/` is a workspace containing
the owner's disc, a legacy Wine install and a separate Wine-on-Android setup.
Run Git and native build commands from this repository.

The deliverable is native C++/SDL3 on macOS ARM64, Android ARM64, Linux
(including Steam Deck) and Windows (plan 33: builds, tests and packages done;
hardware acceptance open). Wine is
used only to inspect/capture the original for this port. The opening Napa race
is the current fidelity milestone, followed by all courses and the whole game; consult `docs/plans/00-INDEX.md` for current
dependencies before implementation or planning. Read `docs/CURRENT_STATUS.md` for the
latest evidence; dated plan overviews are historical snapshots. Match plans to the actual
interfaces before using their snippets. Plans 02–07 describe older architecture;
plan 12 is superseded by 17.

Use `README.md` for commands, `docs/TOOLING.md` for analysis/environment pitfalls,
`docs/REVERSE_ENGINEERING.md` for binary evidence, `docs/VERIFICATION.md` for
coverage, and the relevant plan for acceptance. Read what the task needs.

## Preserve concurrent work

- Check `git status --short` and recent commits before editing. Existing changes
  may belong to another agent. Preserve them; do not reset, restore or delete
  unfamiliar changes to make your task easier.
- OMP is the main development client. Check its latest handoff and file claims
  before taking a plan slice; read-only research can proceed independently. The
  owner has requested shared-tree work in some sessions: respect that choice and
  serialize overlapping files rather than silently moving the work elsewhere.
- Before parallel implementation, establish file ownership. Give each worker a
  separate worktree when practical. Serialize edits to shared simulation headers,
  `game_main.cpp`, the oracle proxy and shared documentation. One integrator
  updates shared status and accepts worker changes.
- Use a separate build directory per worker/worktree. A build lock protects the
  build directory, not source files, Wine installs or Ghidra databases. Coordinate
  those shared resources separately. Never remove another worker's lock merely
  because it is old; verify its owner is gone.
- Do not stash the whole shared tree (including `--include-untracked`) to test
  pristine HEAD, merge, or isolate your change. Use an isolated checkout for
  baseline comparisons; otherwise have the integrator coordinate the merge.
  A private build directory does not isolate edits to its source checkout.
- Re-read shared headers immediately before a narrow edit, inspect the resulting
  diff for dropped declarations/scopes, and compile an affected target before
  handing off. Register new CMake sources/tests only once their files exist.
- When committing is in scope, stage explicit paths and inspect the staged diff.
  Avoid `git add -A` and commits of another worker's in-flight changes. Do not
  rewrite shared history to untangle interleaved work.

## Reverse engineering and fidelity

- Use the unmodified disc binary at `../cdrom/ROADRASH/ROADRASH.EXE` as reference;
  check its hash against `analysis/inventory.json`. The parent's `android/`
  executable is patched and is not a reference.
- When evidence conflicts, read supersession notes and current recovered facts
  before reusing an old worker message. Validate actor identity through spawn,
  think/draw dispatch and asset family, not a guessed pool name or EXE string
  absence. Record which earlier conclusion changed and why.
- Trace behavior to addresses and field widths. Decompiler pointer indexing is
  element-scaled; verify byte offsets, signedness and wraparound against assembly.
  Suspect or empty pseudocode needs a disassembly check.
- Use existing address-keyed exports first. REA is a spot-check, not runtime proof;
  use the tested version and environment recorded in `docs/TOOLING.md`. Keep
  speculative mappings explicitly labelled until validated.
- Keep the original 60 Hz simulation, integer semantics and shared MSVC RNG
  behavior intact. Rendering and optional visual enhancements must not consume
  simulation randomness or change race results; original rendering remains the
  fidelity baseline.
- Acceptance replays anchor RNG once and then free-run. `--reanchor` is diagnostic.
  A scenario called "human" must prove it reaches the player-control branch;
  attract-demo replay does not establish player-control fidelity.
- Distinguish component fixtures, original-list replay, native scene construction,
  simulation replay, live gameplay and device validation. State exactly what a
  comparison covers. Never change a golden just to make a test pass.
- Keep commercial assets, disc-derived dumps and upscaled artwork local and out
  of source/APK/app packages. Use synthetic committed parser fixtures.

## Verification and tools

- Prefer `rg` and `fd` for search. Use the client's C++ LSP for definitions,
  references, diagnostics and symbol renames when available. Check
  `python3 tools/agent_lsp.py`; refresh an owned compilation database after
  source/config changes. OMP's workspace `lsp.json` routes to `native-port/`;
  reload its LSP after marker changes. If LSP is unavailable, record the limit
  and use `clangd --check` plus text inspection; see `docs/TOOLING.md`.

- Normal native gate: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`,
  `cmake --build build --parallel`, then
  `ctest --test-dir build --output-on-failure`. Substitute your owned build directory.
  `make help` lists existing convenience targets; `make run` runs `rr-game`,
  while `make viewer` runs the research viewer.
- Run focused tests during iteration; run the integration gate after integrating
  code changes. `tools/ci.sh` includes Android debug assembly and asset checks;
  Android release assembly, codecs and standalone packaging have separate gates.
  Use sanitizer/oracle checks appropriate to the change. No build is needed for
  a documentation-only edit.
- Report skipped tests and known divergences separately. `oracle-sim` is an exact
  diagnostic check; `make verify-sim` is strict acceptance on the receipted
  human capture (`sched-human-cov2-20261017`, driver human) and currently
  passes; full human parity outside that capture and the live-AI systems are
  still open. A green native suite does not prove simulation parity. Never
  bless a new divergence just to pass CI.
- Preserve command exit codes. Log long commands to files and inspect the logs;
  a `tail`/`grep` pipeline or `|| true` must not turn a failed build into success.
- Oracle captures mutate shared state. Coordinate the prefix, DLL install, output
  name and process ownership before running them; stop only your own processes.
  Require a valid header, contiguous ticks and completion marker before using a
  capture. Do not overwrite a previous accepted capture with a partial run.
- `tools/decompile.sh` overwrites the analysis import. Preserve manual annotations
  before using it; serialize writes to the Ghidra project.
- APK assembly is not hardware validation. Check the selected `adb` serial and
  record what was actually observed for import, rendering, controls and lifecycle.

## Progress and handoffs

Document all progress in `docs/`. For implementation, update `docs/PROGRESS.md`
with the change and remaining gaps; update `docs/VERIFICATION.md` with commands,
outcomes, skips and evidence paths; put recovered facts in
`docs/REVERSE_ENGINEERING.md`. Update the applicable plan status when it changes.

During concurrent work, write a uniquely named `docs/agent-handoffs/<session>.md`
instead of competing to rewrite shared summaries. Include task/plan, baseline
commit, owned and changed files, evidence, checks, remaining work, live process
IDs and held resources. The integrator merges the handoff into shared docs.
Research notes under `analysis/` do not replace this documentation requirement.

Plans must describe portable actions and acceptance, with optional client-specific
skills. If a named skill is unavailable, carry out the equivalent workflow and
record the limitation. Continue authorized work through validation and documentation;
when interrupted, leave a concrete next action and distinguish partial work from
completion.

Keep a resumable handoff before a request/context budget stop. Include the first
failing command and unfinished edits, not just a success summary. If one slice
is blocked, advance an independent authorized slice; do not repeatedly emit
identical no-change audits. Do not expand scope or mark acceptance complete just
to avoid waiting.
