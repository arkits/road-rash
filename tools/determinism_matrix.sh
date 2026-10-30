#!/bin/sh
# Plan 34 Task 3 Step 5: cross-platform determinism matrix. Plan 33 Task A9
# moved the implementation to tools/determinism_matrix.py so it also runs on
# Windows hosts; this wrapper keeps the old command line working.
#   tools/determinism_matrix.sh [macos] [linux] [linux-arm64] [wasm] [android] [windows] [windows-arm64]
exec python3 "$(dirname "$0")/determinism_matrix.py" "$@"
