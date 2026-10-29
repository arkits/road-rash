# android-polish-20261028

Task: Android icon, credits touch skip, loading profile, attack layout and widescreen defaults

Baseline: 0b4ce7ced477d816932f697acfa217c14741d5da

Worktree: `/Users/archit/Dev/rr/native-port`
Build directory: `/Users/archit/Dev/rr/native-port/build-agent-android-polish-20261028`
Owner PID: 95735
Resource token: `9b2b07c5-3e1c-49d0-87bb-73d3429ac7da`

Owned files:

- `/Users/archit/Dev/rr/native-port/src/game_main.cpp`
- `/Users/archit/Dev/rr/native-port/src/input.cpp`
- `/Users/archit/Dev/rr/native-port/src/settings.hpp`
- `/Users/archit/Dev/rr/native-port/src/race_setup.cpp`
- `/Users/archit/Dev/rr/native-port/android/app/src/main/AndroidManifest.xml`
- `/Users/archit/Dev/rr/native-port/android/app/src/main/res`
- `/Users/archit/Dev/rr/native-port/web/index.html`
- `/Users/archit/Dev/rr/native-port/web/favicon.ico`
- `/Users/archit/Dev/rr/native-port/tools/build_web.sh`
- `/Users/archit/Dev/rr/native-port/tools/prepare_web_deploy.py`
- `/Users/archit/Dev/rr/native-port/tools/extract_app_icon.py`
- `/Users/archit/Dev/rr/native-port/assets/app-icon`
- `/Users/archit/Dev/rr/native-port/tests/settings_test.cpp`
- `/Users/archit/Dev/rr/native-port/tests/input_test.cpp`

## Progress / evidence

Implemented the original EXE icon extraction and committed-artifact pipeline,
Android launcher resources, web favicon and bundle validation; touch Select on
credit screens 7/0x23 (including letterbox taps); left-side attack buttons above
steering; Android match-display widescreen defaults (explicit saved choices still win).
The icon is the user's explicit narrow exception to the commercial-artwork rule.
Original EXE SHA-256 matches analysis/inventory.json. Extracted group 102 has four
images; source 48x48 preview is assets/app-icon/roadrash.png. No ImageGen used.

Loading diagnosis: loading screen 8 is an input gate, not a loading operation.
Before the fix it remained indefinitely until another touch on the bottom item.
Instrumented Debug NAPA setup on Pixel 6 Pro: course/tables 41.6 ms, scene 775.0 ms,
dashboard 0.5 ms, actors 32.7 ms, traffic 97.1 ms, personalities 0.1 ms, wiring
90 ms later. Android now presents the loading postcard once and selects it
automatically, after the Start video. SDL asset-stage timings stay Android-only
so oracle diagnostic exact stdout/stderr expectations remain unchanged.

Preserved pre-existing CMake/JPEG/RRI/oracle edits. Shared PROGRESS and VERIFICATION
are claimed by frontend-acceptance-all-20261026; this unique handoff records all
progress and checks instead of editing those files.

## Checks / remaining work

Native configure/full build passed in the owned build directory; full CTest
completed with --parallel 8 in 508.81 s: 120/121 passed, zero skips. It
identified two assertions within the frontend test
failures caused by applying the new default globally. Restricted widescreen's
new default to Android (matching the task); rebuilt all targets and reran
race-screen/input/frontend/settings/display: 5/5 passed (22.90 s). Desktop/web
retain their previous defaults. The remaining full-suite tests all passed,
including net_client (428.56 s). A final --rerun-failed confirms the repaired
frontend test separately. First profiling run was stopped after identifying that
added native SDL logs broke exact oracle diagnostic output. Limited stage logs
to Android, rebuilt and restarted the whole suite. The initial too-early build
attempt failed before configure completed; final full build passed.

Android assembleDebug + assembleRelease -PreleaseDebugSigning=true passed,
including both NoGameData checks. Local test signing only. Debug installed and
NAPA gameplay / relocated attack overlay / widescreen observed on Pixel 6 Pro
1A291FDEE000B4, Android 17. Release installed over it preserving import/settings;
automatic-loading test passed. Release NAPA stages: course/tables 9.1 ms,
scene 138.2 ms, dashboard 0.3 ms, actors 4.6 ms, traffic 3.4 ms, personalities
0.1 ms (155.7 ms total); race wiring 5 ms later. No second loading-screen tap.
Final rebuilt release installed on the same Pixel. Evidence remains local under
analysis/android-polish-*.png and analysis/android-polish-*-device.log.

Final Android Debug/Release reassembly and both asset checks passed after the
Android-only default change (27 s). aapt2 confirms the launcher icon is registered
at all five density sizes. Pillow verifies launcher dimensions/RGBA and favicon
16/32/48 images, exact file copied into the web bundle. Formatting and diff checks
passed. Android simpleperf profiling was unavailable on-device due to property
permissions; measured wall-clock stage logs were sufficient for this diagnosis.

Web tools/build_web.sh passed in build-agent-android-polish-web-20261028 with
10-file bundle validation; tests/web_bundle_cache.cjs passed (Wasm instantiated).
C++ client LSP unavailable. Owned compilation database preflight healthy;
clangd --check src/input.cpp exit 3 reports only Apple's ExtractFunction tweak
failures (10), matching prior sessions, no source parse diagnostics. Native,
Android and Wasm compilers passed. No original simulation fidelity claim. The
asset timings are specific to the imported files and this device, not an all-course performance guarantee.

## Live processes / resources

No capture or Ghidra resources used. The full integration CTest and all compiler processes have finished. No on-device profiling process
remains; restored security.perf_harden=1 after the profiling attempt. Gradle,
Wasm compiler and temporary browser servers finished. Git SSH fetch stalled;
stopped only owned SSH processes and verified main through the existing gh HTTPS
credential helper (HEAD equals fetched main). Will push by HTTPS using that helper.
Resource claims will be released after commit/push.

Final release repeat: selected Thrash then Napa and sent no further touch/key
input for 15 seconds. analysis/android-polish-final-auto-race.png shows gameplay;
analysis/android-polish-final-device.log records the asset-stage timings.
Tap in the left letterbox skipped the opening credits on the final APK.
Left installed with imported files/settings retained, paused before returning
Android Home. Web browser favicon check fetched the linked ICO successfully
(HTTP 200, valid three-image header); final stale-loader browser smoke passed.
