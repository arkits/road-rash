#!/usr/bin/env bash
# Plan 33a Task 1: configure, build and test one Linux preset. Run it inside
# the linux/gate.Dockerfile image (or on any Linux host with the same tools):
#   tools/linux_gate.sh linux-gcc [build-dir] [-- extra cmake args]
# The build directory defaults to build-<preset>-<arch>. Tests run with SDL's
# dummy video and audio drivers; disc tests run when ../cdrom/ROADRASH exists
# and skip (exit 77) otherwise. The exit status is CTest's.
set -euo pipefail
preset=${1:?usage: tools/linux_gate.sh <linux-gcc|linux-clang|linux-release> [build-dir] [-- cmake args]}
shift
build="build-${preset}-$(uname -m)"
if [[ $# -gt 0 && $1 != -- ]]; then build=$1; shift; fi
[[ ${1:-} == -- ]] && shift
cd "$(dirname "$0")/.."
jobs=$(nproc 2>/dev/null || echo 4)
cmake --preset "$preset" -B "$build" "$@"
cmake --build "$build" --parallel "$jobs"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
ctest --test-dir "$build" --output-on-failure -j "$jobs" --timeout 1800
