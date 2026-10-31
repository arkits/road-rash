#!/usr/bin/env bash
# Plan 33a Task 2 Step 5: run the Linux tarball and AppImage on fresh
# distribution containers that have no development tools.
#   tools/linux_clean_host_smoke.sh <tarball> <appimage> [x86_64|aarch64] [image ...]
# Each container gets only libx11-6 and the ALSA library (what any desktop
# has); the game must start with SDL's dummy drivers, print a Release
# --version, run --first-run-smoke to exit 0 and write its log under a fresh
# HOME (the smoke itself logs nothing, so the file may be empty). The
# AppImage runs through APPIMAGE_EXTRACT_AND_RUN (no FUSE in containers). Prints one PASS/FAIL line per image; the exit status is the
# number of failures.
set -uo pipefail
tarball=$(realpath "${1:?tarball}")
appimage=$(realpath "${2:?appimage}")
arch=${3:-x86_64}
shift 3 2>/dev/null || shift $#
images=("$@")
if [[ ${#images[@]} == 0 ]]; then
    images=(ubuntu:20.04 debian:12 fedora:latest)
    [[ $arch == x86_64 ]] && images+=(archlinux:latest) # Arch publishes no arm64 image
fi
platform=linux/amd64
[[ $arch == aarch64 ]] && platform=linux/arm64
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
tar -xzf "$tarball" -C "$work"
cp "$appimage" "$work/game.AppImage"

inner='set -e
case $(. /etc/os-release; echo $ID) in
  ubuntu|debian) apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq libx11-6 libasound2 >/dev/null 2>&1 ;;
  fedora) dnf -y -q install libX11 alsa-lib >/dev/null ;;
  arch) pacman -Sy --noconfirm --needed libx11 alsa-lib >/dev/null ;;
esac
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy APPIMAGE_EXTRACT_AND_RUN=1
for game in /pkg/road-rash-native/rr-game /pkg/game.AppImage; do
  export HOME=$(mktemp -d) TMPDIR=$(mktemp -d)
  cd "$HOME"
  "$game" --version | grep -q "\"configuration\": *\"Release\"" || { echo "$game: --version is not a Release build"; exit 1; }
  "$game" --first-run-smoke >/dev/null 2>&1 || { echo "$game --first-run-smoke exited $?"; exit 1; }
  test -f "$HOME/.local/share/RoadRashNative/RoadRash/rr-game.log" || { echo "$game wrote no log"; exit 1; }
  echo "ok $game"
done
echo "glibc $(ldd --version 2>&1 | head -1 | grep -o "[0-9.]*$")"'

failures=0
for image in "${images[@]}"; do
    log=$work/$(echo "$image" | tr ':/' '__').log
    if docker run --rm --platform "$platform" -v "$work:/pkg:ro" "$image" sh -c "$inner" >"$log" 2>&1; then
        echo "PASS $image ($arch): $(tail -1 "$log")"
    else
        echo "FAIL $image ($arch):"
        sed 's/^/  /' "$log"
        failures=$((failures + 1))
    fi
done
exit "$failures"
