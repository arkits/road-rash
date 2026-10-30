#!/bin/sh
# Plan 22 Task 2 Step 3: builds the libFuzzer tree and runs every fuzz target.
# Usage: tools/fuzz.sh [--seed-from-disc] [seconds-per-target] [target...]
#   --seed-from-disc  copy the user's imported files into analysis/fuzz/<target>
#                     (local only, never committed) before running.
#   seconds-per-target defaults to 30 (RR_FUZZ_SECONDS overrides); pass target
#                     names (e.g. fuzz_settings) to run a subset.
# Each target runs with -timeout=2 -rss_limit_mb=512 over fuzz/seeds/<target>
# (committed, synthetic) plus analysis/fuzz/<target> (local). Crashes land in
# analysis/fuzz/crashes/<target>/. Exits 1 if any target finds a crash.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISC="$ROOT/../cdrom/ROADRASH"
BUILD="$ROOT/build-fuzz"
LOCAL="$ROOT/analysis/fuzz"
SECONDS_PER_TARGET="${RR_FUZZ_SECONDS:-30}"
SEED_FROM_DISC=0
TARGETS=""

for arg in "$@"; do
    case "$arg" in
        --seed-from-disc) SEED_FROM_DISC=1 ;;
        *) case "$arg" in
               ''|*[!0-9]*) TARGETS="$TARGETS $arg" ;;
               *) SECONDS_PER_TARGET="$arg" ;;
           esac ;;
    esac
done

if [ "$SEED_FROM_DISC" = 1 ]; then
    if [ ! -d "$DISC" ]; then
        echo "fuzz: --seed-from-disc needs the disc at $DISC" >&2
        exit 1
    fi
    seed_find() {
        # seed_find <target> <find predicates...>: copy up to 100 matches, tolerate none.
        target="$1"; shift
        mkdir -p "$LOCAL/$target"
        find "$DISC" "$@" -type f 2>/dev/null | head -100 | while IFS= read -r f; do
            cp -f "$f" "$LOCAL/$target/" 2>/dev/null || true
        done
    }
    seed_find fuzz_resources -maxdepth 2 -name '*.RSC'
    seed_find fuzz_cel -maxdepth 2 -name '*.RSC'
    seed_find fuzz_family -maxdepth 2 -name '*.RSC'
    seed_find fuzz_animation -maxdepth 2 -name '*.RSC'
    seed_find fuzz_course -path '*/COURSES/*.CRS'
    seed_find fuzz_terrain -path '*/COURSES/*.CRS'
    seed_find fuzz_cars -path '*/CARS/*.CAR'
    seed_find fuzz_bobs '(' -name '*.BOB' -o -path '*/BIKERS/*.DAT' ')'
    seed_find fuzz_avi -name '*.AVI'
    seed_find fuzz_audio '(' -name '*.WAV' -o -name '*.RRA' ')'
    seed_find fuzz_music '(' -iname '*.mid' -o -iname '*.sbk' ')'
    seed_find fuzz_pe -path '*/TEXT/*.DLL'
    seed_find fuzz_rri -name '*.RRI'
    mkdir -p "$LOCAL/fuzz_exe_tables"
    cp -f "$DISC/ROADRASH.EXE" "$LOCAL/fuzz_exe_tables/" 2>/dev/null || true
    echo "fuzz: disc seeds copied (fuzz_iso/fuzz_save/fuzz_settings/fuzz_unpack have no disc source)"
fi

# Plan 33 Task A9: Homebrew LLVM on macOS, otherwise $LLVM_BIN or the clang on PATH.
if [ -z "${LLVM_BIN:-}" ]; then
    if [ -x /opt/homebrew/opt/llvm/bin/clang++ ]; then LLVM_BIN=/opt/homebrew/opt/llvm/bin
    elif command -v clang++ >/dev/null 2>&1; then LLVM_BIN=$(dirname "$(command -v clang++)")
    else LLVM_BIN=/opt/homebrew/opt/llvm/bin; fi
fi
CC_BIN="$LLVM_BIN/clang"
CXX_BIN="$LLVM_BIN/clang++"
if [ ! -x "$CXX_BIN" ]; then
    echo "fuzz: LLVM clang++ missing at $CXX_BIN; install LLVM (\`brew install llvm\` on macOS) or set LLVM_BIN" >&2
    exit 1
fi
mkdir -p "$LOCAL"
cmake -S "$ROOT" -B "$BUILD" -DRR_FUZZ=ON \
    -DCMAKE_C_COMPILER="$CC_BIN" -DCMAKE_CXX_COMPILER="$CXX_BIN" \
    -DCMAKE_BUILD_TYPE=Debug >"$LOCAL/build.log" 2>&1 || { tail -20 "$LOCAL/build.log"; exit 1; }
cmake --build "$BUILD" --parallel >>"$LOCAL/build.log" 2>&1 || { tail -20 "$LOCAL/build.log"; exit 1; }

"$BUILD/fuzz-seeds" "$ROOT/fuzz/seeds"
SEED_MANIFEST="$LOCAL/seed-manifest.txt"
find "$ROOT/fuzz/seeds" -type f | sort >"$SEED_MANIFEST"

if [ -z "$TARGETS" ]; then
    TARGETS="$(cd "$BUILD" && ls -d fuzz_* 2>/dev/null | grep -v '^fuzz-seeds$' || true)"
fi
if [ -z "$TARGETS" ]; then
    echo "fuzz: no targets built" >&2
    exit 1
fi

CRASH=0
for t in $TARGETS; do
    bin="$BUILD/$t"
    if [ ! -x "$bin" ]; then echo "fuzz: no such target $t" >&2; CRASH=1; continue; fi
    mkdir -p "$LOCAL/$t" "$LOCAL/crashes/$t"
    log="$LOCAL/$t.log"
    echo "fuzz: $t for ${SECONDS_PER_TARGET}s"
    set +e
    # Corpus order matters: new units are written to the FIRST corpus dir, so the
    # git-ignored local dir comes first and the committed synthetic seeds second.
    # -entropic=0 works around a Homebrew LLVM 22.1.4 runtime bug: with the
    # entropic schedule, loading 2+ corpus files aborts deterministically in
    # libFuzzer's own InputCorpus::AddRareFeature (heap-buffer-overflow in
    # vector relocate; reproduces with two empty files, no target code
    # involved). -reload=0 disables the periodic output-corpus reread, whose
    # ListFilesInDirRecursive hits the same class of abort once the local dir
    # holds several files (single-process runs need no reread).
    # Re-evaluate both when the LLVM bottle changes.
    "$bin" "$LOCAL/$t" "$ROOT/fuzz/seeds/$t" \
        -max_total_time="$SECONDS_PER_TARGET" -timeout=2 -rss_limit_mb=512 -entropic=0 -reload=0 \
        -print_final_stats=1 -artifact_prefix="$LOCAL/crashes/$t/" >"$log" 2>&1
    code=$?
    set -e
    tail -4 "$log" | sed 's/^/  /'
    if [ "$code" -ne 0 ]; then
        echo "fuzz: $t FAILED (exit $code); see $log" >&2
        CRASH=1
    fi
done
if [ "$CRASH" -ne 0 ]; then echo "fuzz: crashes found under $LOCAL/crashes/" >&2; exit 1; fi
if ! find "$ROOT/fuzz/seeds" -type f | sort | diff "$SEED_MANIFEST" - > /dev/null; then
    echo "fuzz: libFuzzer wrote generated units into the committed fuzz/seeds tree:" >&2
    find "$ROOT/fuzz/seeds" -type f | sort | diff "$SEED_MANIFEST" - >&2 || true
    exit 1
fi
echo "fuzz: all targets clean"
