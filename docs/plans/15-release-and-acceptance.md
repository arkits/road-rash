# Release and Final Acceptance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Read [00-INDEX.md](00-INDEX.md) first. Requires plans 10–14.

**Goal:** Prove the definition of done in 00-INDEX.md from a clean checkout, produce the final macOS and Android release artefacts, and settle the remaining owner decision on macOS distribution signing. Multiplayer was decided on October 26, 2026 (plan 34).

**Architecture:** No new subsystems. This plan adds an acceptance script that runs every gate in sequence and a final documentation pass. Each owner decision is recorded in the docs.

**Tech Stack:** Existing `tools/ci.sh`, `tools/build_macos_app.sh`, `tools/verify_macos_app.py`, Gradle, CTest.

**Spec:** [00-INDEX.md](00-INDEX.md) "Definition of done".

## Global Constraints

- **Clean checkout:** acceptance runs from a fresh `git clone` of the repo into a temporary directory, with only the user's ISO, the Wine prefix (for oracle goldens) and the Android SDK available.
- **No original data in any artefact.** `verifyNoGameData` and `tools/verify_macos_app.py` must pass.
- **Owner-only steps** (Developer ID, notarisation credentials, keystore) are performed by the owner. Claude never handles their passwords.

## Review Focus

1. **Missing goldens on a clean checkout:** oracle tests skip (77) and do not fail when goldens are absent. The acceptance script must therefore treat any skip of a required golden test as a failure. Task 1 requires `analysis/oracle` to be regenerated or provided, and checks that zero required tests were skipped.
2. **The first launch of the macOS `.app` from a quarantined download:** Gatekeeper behaviour differs between ad-hoc and notarised builds. Task 2 tests a ZIP downloaded through a browser.
3. **Upgrading over an earlier development install** (old pref-path data or save format): the game must keep or cleanly migrate saves and imported data. Task 3.
4. **A machine with no game controller and no audio device:** the game must start and play silently. Task 1 runs one headless smoke with `SDL_AUDIO_DRIVER=dummy`.
5. **Non-English system locale:** the language follows the original rule (`0x0042f340`). Task 1 runs the frontend smoke under each of the five languages.

---

### Task 1: Acceptance script

**Files:**
- Create: `tools/acceptance.sh`
- Modify: `docs/VERIFICATION.md`

- [ ] **Step 1: Write the script.** It exits non-zero on the first failure and logs to `analysis/acceptance-<date>.log`.

```sh
#!/bin/sh
# Usage: tools/acceptance.sh <path-to-ROADRASH-folder>
set -eu
ASSETS="$1"
LOG="analysis/acceptance-$(date +%Y%m%d).log"
mkdir -p analysis
run() {
cmake -S . -B build-accept -DCMAKE_BUILD_TYPE=Release
cmake --build build-accept --parallel
# Required oracle tests must run, never skip.
ctest --test-dir build-accept --output-on-failure --output-junit junit.xml
if grep -q 'status="skipped"\|<skipped' build-accept/junit.xml; then
    echo "FAIL: required tests were skipped (missing goldens or assets)"; exit 1
fi
build-accept/rr-viewer --assets "$ASSETS" --smoke
build-accept/rr-course --exe "$ASSETS/ROADRASH.EXE" "$ASSETS"/DATA/COURSES/*.CRS
for course in 0 1 2 3 4; do
    SDL_AUDIO_DRIVER=dummy build-accept/rr-game --assets "$ASSETS" --race --course "$course" --ticks 60000 --autopilot \
        --screenshot "analysis/accept-course$course.bmp"
done
for lang in en fr de es it; do
    SDL_PREFERRED_LOCALES="$lang" SDL_AUDIO_DRIVER=dummy build-accept/rr-game --assets "$ASSETS" --menu-frames 120 \
        --screenshot "analysis/accept-menu-$lang.bmp"
done
tools/ci.sh
echo "ACCEPTANCE OK"
}
run 2>&1 | tee "$LOG"
grep -q '^ACCEPTANCE OK$' "$LOG"
```

  Fish users run it through `sh tools/acceptance.sh …`. `set -e` inside `run` stops at the first failure, and the final `grep` turns a missing `ACCEPTANCE OK` into a non-zero exit even though `tee` ends the pipe.
- [ ] **Step 2:** Run it from a fresh clone: `git clone /Users/archit/Dev/rr/native-port /tmp/rr-accept && cd /tmp/rr-accept`. Symlink or provide `analysis/oracle`, then run `sh tools/acceptance.sh /Users/archit/Dev/rr/cdrom/ROADRASH`. Fix failures in their owning plan's code, not in the script.
- [ ] **Step 3:** Commit: `tools: whole-port acceptance script`.

---

### Task 2: macOS distribution

- [ ] **Step 1: Ask the owner** which distribution they want: ad-hoc signed (current; Gatekeeper needs right-click → Open) or Developer ID signed and notarised (needs the owner's Apple Developer account).
- [ ] **Step 2:** If Developer ID: add an optional `RR_CODESIGN_IDENTITY` and a `notarytool` profile name to `tools/build_macos_app.sh`. The owner runs signing and notarisation with their own credentials. Then verify with `spctl -a -vv`, and with `xcrun stapler validate` after stapling.
- [ ] **Step 3:** Download the ZIP through a browser (to get quarantine), unzip and launch. Record the result.
- [ ] **Step 4:** Commit: `release: macOS distribution signing`.

---

### Task 3: Upgrade and data migration

- [ ] **Step 1:** Install the last development build, import, and play to create a save and settings. Then install the release build over it, on macOS and on Android with the same signing key.
- [ ] **Step 2:** Imported data, settings and saves remain usable. If any format changed during plans 10–14, add a migration with a test first; otherwise record "no format changes".
- [ ] **Step 3:** Commit any migration: `settings: migrate development data`.

---

### Task 4: Multiplayer decision (plan 34, superseding plan 09)

- [x] **Step 1: Decided (October 26, 2026).** The owner chose: mano-a-mano is in, as online multiplayer with a modern design (plan 34), not a faithful port of the original netcode.
- [x] **Step 2:** Recorded in 00-INDEX.md (definition of done item 9) and PROGRESS.md.
- [ ] **Step 3:** At release, check plan 34's acceptance as part of the definition of done.

---

### Task 5: Final documentation and tag

**Files:**
- Modify: `README.md`, `docs/USER_GUIDE.md`, `docs/PROGRESS.md`, `docs/REVERSE_ENGINEERING.md`, `docs/VERIFICATION.md`, `docs/plans/00-INDEX.md`

- [ ] **Step 1:** Rewrite PROGRESS.md's header and status table as the final state. Every row is "done (verified by …)" or a named known deviation. Move "Assumptions" to a "Known deviations from the original" section listing only real deviations (GDI font hinting, zero-guarded out-of-resource reads, native touch/gamepad additions, the native save file list).
- [ ] **Step 2:** In the user guide: import on both platforms, controls (keyboard, gamepad, AIR X, touch), where data and saves live, how to uninstall cleanly, and the licence notices.
- [ ] **Step 3:** Mark plans 01–15 complete in 00-INDEX.md. Tag `v1.0.0` only after the owner confirms in chat, and build the tagged artefacts (the Android `versionName` comes from the tag).
- [ ] **Step 4:** Commit: `docs: final port status and user guide`.

### Owner decisions from plan 33 (recorded October 31, 2026)

- **Flathub submission** (plan 33a Task 3 Step 3): owner decision, because
  it publishes the project name and exposes the trademark question. The
  Flatpak bundle is built, verified and attached to releases either way.
- **AppImage update channel** (plan 33a Task 2 Step 3): none embedded by
  default; zsync update information only if the owner picks a channel.
- **Project licence:** the Linux and Windows packages carry every
  third-party licence; the port's own licence file waits for the owner.

## Exit criteria

- `tools/acceptance.sh` prints `ACCEPTANCE OK` from a fresh clone, and plan 14's device checklist is complete.
- Owner decisions on distribution and multiplayer are recorded, and release artefacts are built from the tag.
