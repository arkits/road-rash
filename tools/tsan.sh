#!/bin/sh
# plan 22 Task 4: ThreadSanitizer tree. Configures build-tsan with Homebrew
# clang (-fsanitize=thread, RelWithDebInfo), builds, runs ctest with TSan
# options, then runs threaded scenarios. Darwin ARM64 per Clang TSan docs.
# Does NOT fix races found here; findings are reported in analysis/tsan-notes.md.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
# Plan 33 Task A9: Homebrew LLVM on macOS, otherwise $LLVM_BIN or the clang on PATH.
if [ -z "${LLVM_BIN:-}" ]; then
    if [ -x /opt/homebrew/opt/llvm/bin/clang++ ]; then LLVM_BIN=/opt/homebrew/opt/llvm/bin
    elif command -v clang++ >/dev/null 2>&1; then LLVM_BIN=$(dirname "$(command -v clang++)")
    else LLVM_BIN=/opt/homebrew/opt/llvm/bin; fi
fi
build_dir=${RR_TSAN_BUILD_DIR:-"$root/build-tsan"}
if [ ! -x "$LLVM_BIN/clang++" ]; then
    echo "tsan.sh: LLVM clang++ missing ($LLVM_BIN/clang++); install LLVM or set LLVM_BIN" >&2
    exit 77
fi
cmake -S . -B "$build_dir" -DRR_TSAN=ON -DRR_BUNDLE_SDL=OFF \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER="$LLVM_BIN/clang" \
    -DCMAKE_CXX_COMPILER="$LLVM_BIN/clang++"
cmake --build "$build_dir" --parallel "${RR_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
export TSAN_OPTIONS=${TSAN_OPTIONS:-"halt_on_error=1:second_deadlock_stack=1"}
# Race-relevant suites: tests that exercise threads/audio/import lifecycle.
# RR_TSAN_TESTS is a ctest -R regex. (Plan 33a: it used to carry "-R '...'"
# with the quotes, which reached ctest as one argument or as a regex with
# literal quotes, so no test ran; --no-tests=error now catches that.)
ctest --test-dir "$build_dir" --output-on-failure --no-tests=error \
    -R "${RR_TSAN_TESTS:-mixer|importer|wav|avi|cinepak|video_player|session|actor_traffic|traffic|render_threads|race-cache|raster-rgb-threads|file_browser}" \
    ${RR_TSAN_CTEST_ARGS:-}
# Threaded scenarios (Review Focus 4). Best-effort: each runs only if the
# binary/asset fixture exists; skips are recorded, not failures.
status=0
run_scenario() {
    desc=$1; shift
    if "$@" >>"$build_dir/tsan-scenarios.log" 2>&1; then
        printf 'tsan scenario OK: %s\n' "$desc"
    else
        code=$?
        printf 'tsan scenario exit=%d (skip or finding, see tsan-scenarios.log): %s\n' "$code" "$desc"
        if [ "$code" != 77 ]; then status=$code; fi
    fi
}
: >"$build_dir/tsan-scenarios.log"
if [ -x "$build_dir/mixer-test" ]; then
    run_scenario "mixer device (audio callback thread)" \
        env SDL_AUDIODRIVER=dummy "$build_dir/mixer-test" --device
fi
if [ -x "$build_dir/rr-game" ]; then
    run_scenario "first-run import smoke (import worker thread)" \
        "$build_dir/rr-game" --first-run-smoke
    # NOTE: the headless tick loop exits only with --steer-into-traffic
    # (without it, --ticks falls through to the interactive run_race loop).
    if [ -d ../cdrom/ROADRASH ]; then
        run_scenario "headless race 1800 ticks (sim+traffic threads)" \
            env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$build_dir/rr-game" \
            --race --course 3 --steer-into-traffic --ticks 1800 --assets ../cdrom/ROADRASH
    else
        printf 'tsan scenario SKIP (no disc assets): headless race\n'
    fi
else
    printf 'tsan scenario SKIP (rr-game not built in tsan tree)\n'
fi
exit $status
