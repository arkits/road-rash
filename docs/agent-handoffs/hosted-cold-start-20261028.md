# hosted-cold-start-20261028

Task: Give cold hosted startup a measured bounded network budget and diagnostics

Baseline: dfbf1e12d86c5464201496f6695441322a60840a

Worktree: `/Users/archit/Dev/rr/native-port`
Build directory: `/Users/archit/Dev/rr/native-port/build-agent-hosted-cold-start-20261028`
Owner PID: 95735
Resource token: `c2aa9655-c338-47f9-a0cc-1a2acbbb98ab`

Owned files:

- `/Users/archit/Dev/rr/native-port/tests/web_hosted_smoke.cjs`

## Progress / evidence

After fixing the recovery fixture (dfbf1e1), deployment run 40648592205 passed
compilation, all pre-deployment checks, deployment, revision/health, normal live
race/audio/controls/cache checks. It then timed out after 90 seconds waiting for
the storage-blocked session to finish startup.

Measured the actual deployed game in a fresh headless Chromium session with
OPFS and IndexedDB blocked and telemetry intercepted. Manifest: 341 files,
466.8 MiB. Probe `/tmp/rr-restricted-probe.cjs`; log
`/tmp/rr-restricted-probe.log`: 70% (240/341) at 90s, 95% at 160s,
341/341 at 180s, live race with overlay cleared by 200s, no engine/page errors.
Screenshot: `analysis/restricted-probe-20261028.png` (local evidence).
This reproduces healthy forward progress beyond the former timeout.

The hosted smoke test now gives actual game startup a bounded five-minute
network budget. Assertions about audio, rendered race, storage fallback and
errors are unchanged. Launcher/error checks retain their existing time limits.
On startup timeout, save stage/count/engine error/visibility and screenshot
in the uploaded evidence. This distinguishes a stall from a slow cold download.

`node --check tests/web_hosted_smoke.cjs` and `git diff --check`: PASS.
No production source changes; no native build required. Full deployment
workflow will validate the updated real-browser check after push.

## Checks / remaining work

Complete: deployment workflow 40649895689 SUCCESS (6m9s), code revision c87e916.
All build/package/recovery/cache/telemetry checks passed. Deployment, revision
and health verification passed. Entire live hosted smoke suite passed, including
storage-blocked startup, audio/input, responsive/fullscreen rendering, cache
repair/reuse, C++ failure diagnostics and lifecycle callbacks.
Run: https://github.com/arkits/road-rash/actions/runs/40649895689
Site: https://road-rash.archit.xyz
Full local CI log: `/tmp/rr-cold-start-ci-full-20261028.log`.
No production source changes were needed for either failure.

## Live processes / resources

Probe browser closed; CI watcher exited successfully. File claim released.
No live local processes or held resources.
