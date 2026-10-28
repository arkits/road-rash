# loading-recovery-fixture-20261028

Task: Fix deployment recovery fixture for loading progress DOM API

Baseline: c3583a72c6f34eb66d414e3eff27a15fca78a5d2

Worktree: `/Users/archit/Dev/rr/native-port`
Build directory: `/Users/archit/Dev/rr/native-port/build-agent-loading-recovery-fixture-20261028`
Owner PID: 95735
Resource token: `98cfaca6-f426-40ca-8892-dd9baf594056`

Owned files:

- `/Users/archit/Dev/rr/native-port/tests/web_recovery.cjs`

## Progress / evidence

CI failed in `tests/web_recovery.cjs` with `TypeError: track.removeAttribute is
not a function`, from `updateLoading`. The browser game compiled successfully;
the test's synthetic DOM lacked attribute removal and style properties needed
by the new measured loading bar. Failure run: 40643481163 (header commit), also
40643435849 (progress commit).

Updated only the recovery fixture: real attribute storage/get/remove operations
and a style object. Added assertions for exact measured progress, clearing
accessible values/count on unknown stages, and hiding progress on terminal errors.
Production DOM code stays unchanged.

Checks: `node tests/web_recovery.cjs` PASS, including all existing recovery cases
and new progress assertions. Against the previously built web bundle:
`NODE_PATH=.tools/browser-tests/node_modules RR_WEB_DIST=build-agent-loading-copy-20261027/dist node tests/web_bundle_cache.cjs`
PASS; same environment with `tests/web_telemetry.cjs` PASS. RTC test already passed
in the failing CI run; no transport changes. `git diff --check` PASS.
No native build needed for a JavaScript test-fixture change.

Push will trigger the normal deployment workflow; verify its result before closing.

## Checks / remaining work

Run 40648592205 confirmed recovery/cache/telemetry checks PASS, deployment and
revision/health PASS, normal live gameplay PASS. A separate 90-second cold-start
timeout in the storage-blocked live test remains; see
`hosted-cold-start-20261028.md` for its measured reproduction and fix.

## Live processes / resources

No live local processes. Release claim after push; monitor CI independently.
