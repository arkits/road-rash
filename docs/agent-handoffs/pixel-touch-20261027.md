# pixel-touch-20261027

Task: Pixel landscape and visible phone controls

Baseline: a134990cf253aa39378a542e0eb4905d444d4ca8

Worktree: `/Users/archit/Dev/rr/native-port`
Build directory: `/Users/archit/Dev/rr/native-port/build-agent-pixel-touch-20261027`
Owner PID: 95735
Resource token: `40dead68-b781-4928-b04d-e0f556ddac8e`

Owned files:

- `/Users/archit/Dev/rr/native-port/android/app/src/main/java/org/roadrash/nativeport/MainActivity.java`
- `/Users/archit/Dev/rr/native-port/android/app/src/main/AndroidManifest.xml`
- `/Users/archit/Dev/rr/native-port/src/input.cpp`
- `/Users/archit/Dev/rr/native-port/src/input.hpp`
- `/Users/archit/Dev/rr/native-port/src/race_screen.cpp`
- `/Users/archit/Dev/rr/native-port/src/race_overlay.cpp`
- `/Users/archit/Dev/rr/native-port/src/race_overlay.hpp`
- `/Users/archit/Dev/rr/native-port/tests/input_test.cpp`
- `/Users/archit/Dev/rr/native-port/docs/USER_GUIDE.md`

## Progress / evidence

Implemented landscape lock at the SDL Android activity boundary (SDL's resizable-window orientation request had overridden the manifest). Manifest uses sensorLandscape. Added visible full-window phone controls with shared drawing/hit geometry: LEFT, RIGHT, GAS, BRAKE, PUNCH, KICK, BACKHAND, PAUSE; pressed feedback; independent fingers, brake priority, mirrored handedness and settings opacity. Desktop zone controls remain as before. Android uses explicit gas rather than hidden auto-throttle. Pause clears held touches; a clean presentation redraw excludes controls from pause captures without simulation ticks. USER_GUIDE documents phone use.

Device changed during testing: original Pixel 11 Pro XL (68070DLDV007AH) disconnected, then Pixel 6 Pro (1A291FDEE000B4), Android 17, 1440x3120, connected. New APK installed and tested on Pixel 6 Pro: landscape screenshots are 3120x1440; first-run SAF ISO import completed; Napa and City rendered; GAS accelerated and highlighted; three injected fingers simultaneously held gas/left/punch, then changed to right/kick, then brake, then canceled. Screenshot shows rider punching/kicking and independent active-button highlights. On-screen PAUSE opens native menu; clean capture verified without ghost controls. Home/return retains landscape and paused state. No full race/performance/audio-quality/controller acceptance claim.

Local evidence: analysis/pixel6-{controls,gas-held,multitouch-left,multitouch-brake,final-controls,final-pause,final-lifecycle}.png; analysis/pixel6-multitouch.log; analysis/pixel6-final-app.log. The temporary /tmp/rr-touch-probe Java/D8/app_process harness injects real Android MotionEvents; initial attempt used the removed InputManager.getInstance API and failed in the probe process, not the game. InputManagerGlobal retry injected all events successfully (including final ACTION_CANCEL). Probe is not part of the app. Commercial screenshots/ISO stay local.

Concurrent ownership: preserved the pre-existing CMake/JPEG/oracle/RRI edits. src/import_ui.cpp changed concurrently to a broader centered-layout redesign after my narrow Android wording edit; retained it untouched and exclude it from this commit. It already contains phone-friendly Choose ISO wording. That external edit was included in the working-tree build, not claimed as this slice.

## Checks / remaining work

Android assembleDebug / verifyDebugNoGameData passed with SDK Ninja on PATH (analysis/pixel-touch-build.log). Debug app installed successfully. Full native Debug build passed in build-agent-pixel-touch-20261027 (analysis/pixel-touch-native-integration-build.log). Focused final input/race-actions/race-screen passed, including software-renderer overlay visibility in letterbox margins, presentation restoration, all action/handedness hit tests, simultaneous fingers, drag-off, cancel and brake priority (analysis/pixel-touch-focused.log). Full CTest: 106/106 passed, 749.71 seconds (analysis/pixel-touch-ctest.log); the final focused input/race-screen/race-actions rerun also passed after the clean-pause refactor. Full integration build then relinked every target successfully.

No live C++ LSP was exposed; agent_lsp.py confirmed the owned compile database is current. Apple's clangd --check produced ExtractFunction tweak failures (exit 3); no compile diagnostic was reported, and both native and Android compilers succeeded. This is a tooling limitation, not a successful LSP check.

Limits: physical controller, audible mix, frame timing, career saves, import low-space/cancel and reverse-landscape sensor rotation not validated. Both landscape choices are allowed by sensorLandscape; measured startup and app-return orientation only.

## Live processes / resources

Optimized assembleRelease -PreleaseDebugSigning=true also passed, including the asset-free APK check (analysis/pixel-touch-release-build.log). Installed over debug with the same local debug certificate; imported data survived. Force-stop and fresh launch used PID 30491 and reached the landscape frontend. This is a locally signed testing build, not owner release signing. App left installed on Pixel 6 Pro with imported game data. /sdcard/Download/roadrash-test.iso retained for owner. No Wine/Ghidra resources used. Temporary probe dex /data/local/tmp/rr-touch-probe.dex removed after testing. Resource claims released at completion.

## Upstream integration

Direct push rejected because upstream main advanced. Integrated both this slice and the earlier Pixel smoke-test note into an isolated shared clone at /tmp/rr-pixel-integrate-20261027 on upstream 73b7fd8. Conflicts preserve both standard includes, upstream network overlay, and upstream's rule preventing online simulation pause while clearing touch inputs. Shared dirty checkout was not rebased/reset/stashed. Validation on integrated source: native rr-game/input/race-actions/race-screen targets built; input and race-actions passed; race-screen passed with the original disc exposed at /tmp/cdrom (20.98 s). Android assembleDebug and asset check passed in 17 s with ANDROID_HOME pointing at the existing SDK. Network transports disabled for this integration compile; network acceptance not repeated. Full 106/106 gate and phone gameplay above cover the pre-integration working tree.
