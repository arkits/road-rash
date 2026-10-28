# Pixel USB Android smoke test — 2026-10-27

Baseline: d64bdc1, shared tree dirty. Existing CMake/JPEG, oracle, RRI and frontend acceptance edits belong to other workers and were not modified. This tests that working tree, not pristine HEAD. Owned files: this handoff only. No source edits, build or device processes held exclusively; app left installed and running.

## Device and build

ADB serial 68070DLDV007AH; Pixel 11 Pro XL (kodiak), Android 17; physical display 1080x2404. Debug ARM64 package org.roadrash.nativeport. Build log: analysis/pixel-20261027-build.log (local).

Initial assembleDebug failed in the in-flight rr-jpeg-build ExternalProject: nested CMake could not find Ninja. Retried successfully with SDK CMake bin on PATH:

```sh
env PATH="$PWD/.tools/android-sdk/cmake/3.22.1/bin:$PATH" JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home android/gradlew --no-daemon -p android assembleDebug
adb -s 68070DLDV007AH install -r android/app/build/outputs/apk/debug/app-debug.apk
adb -s 68070DLDV007AH shell am start -n org.roadrash.nativeport/.MainActivity
```

Build successful, including verifyDebugNoGameData; installation successful. No native test suite run (device smoke test, no source edits).

## Observations

- First-run import screen rendered; tapping Choose ISO opened Android DocumentsUI.
- Original workspace disc ISO copied to /sdcard/Download/roadrash-test.iso; selected through Downloads search in SAF. Progress visibly advanced (98/341 captured), staging subsequently committed to files/game/ROADRASH. 342 files present; successful importer completion establishes its inventory check, not an independent device hash audit. Import selection at device 18:59:14; audio startup after completion at 18:59:55, approximately 41 seconds.
- Touch activated Thrash Mode, then Napa Valley; loading screen, video frames, road, scenery, opponents, player bike and HUD rendered. Held left touch changed bike position toward the roadside. No claim of complete touch/combat validation or frame-rate measurement.
- Android Back opened the native pause menu. Resume, Home, then bringing activity forward returned to pause, with explicit resume required. Screenshots captured after transitions settled.
- Force-stop/relaunch used new PID 13648 (previous 11969), reused imported data and reached frontend without reimport. Career/settings persistence was not exercised.
- No Road Rash crash observed or present in captured crash buffer. App-scoped logs saved locally. Audio stream initialized via AAudio; audible quality was not assessed.

## Issues and limits

1. **Orientation:** all captured app screens remained portrait 1080x2404 with a small centered 4:3 play area and large bars, despite AndroidManifest.xml requesting landscape. Root cause not established. Next action: inspect SDL Android orientation selection/hints and test landscape startup and rotation on this Pixel.
2. **Build environment:** the nested JPEG project needs Ninja discoverable on PATH; SDK Ninja was not found automatically by nested CMake. Coordinate any cmake/jpeg_scalar.cmake fix with its current owner.
3. Not covered: multitouch attacks/brake, physical gamepad, audible audio correctness, frame pacing/performance, full race completion, save/load career, import cancellation/low-space, release/owner signing or MANGMI controls. No full hardware acceptance claim.

Local evidence: analysis/pixel-20261027-{first,import,menu,course,race,touch,pause,resume,relaunch}.png and corresponding build/app/relaunch logs. Commercial screenshots/data remain local and are not committed. Test ISO remains in Downloads; app and imported data remain available for owner playtesting.
