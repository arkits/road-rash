#!/usr/bin/env bash
# Plan 33a Task 3: build the Flatpak bundle from linux/org.roadrash.nativeport.yml.
#
#   tools/build_linux_flatpak.sh [x86_64|aarch64] [--out DIR] [--no-docker] [--commit SHA] [--url URL]
#
# Builds the committed source at --commit (default HEAD; uncommitted changes
# are not included) from --url (default: this checkout) with flatpak-builder,
# exports RoadRashNative-<version>-<arch>.flatpak to DIR (default
# build-linux-package/), then installs the bundle into a scratch user
# installation and runs `rr-game --version` and `--first-run-smoke`
# (SDL dummy drivers) through `flatpak run`. With Docker, flatpak-builder runs
# in the pinned flathub-infra image, which needs --privileged for bubblewrap;
# --no-docker uses the host's flatpak-builder (a CI job inside that image).
set -euo pipefail

IMAGE=ghcr.io/flathub-infra/flatpak-github-actions:freedesktop-24.08@sha256:66eb59fd5d923833e8e4142f60e542156aad4b8b1d5db38149d814c659e274bc

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
arch=$(uname -m)
[[ $arch == arm64 ]] && arch=aarch64
out=$root/build-linux-package
docker=1
inside=0
commit=
url=
while [[ $# -gt 0 ]]; do
    case $1 in
        x86_64|aarch64) arch=$1 ;;
        --out) out=$2; shift ;;
        --no-docker) docker=0 ;;
        --inside) inside=1; docker=0 ;;
        --commit) commit=$2; shift ;;
        --url) url=$2; shift ;;
        *) echo "usage: $0 [x86_64|aarch64] [--out DIR] [--no-docker] [--commit SHA] [--url URL]" >&2; exit 2 ;;
    esac
    shift
done
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)
commit=${commit:-$(git -C "$root" rev-parse HEAD)}
url=${url:-$root}
major_minor_patch=$(cd "$root/tools" && python3 -c 'import build_info; print("%d.%d.%d" % build_info.release_version())')
if git -C "$root" tag --points-at "$commit" --list 'v[0-9]*' | grep -q .; then
    version=$major_minor_patch
else
    version=$major_minor_patch-g$(git -C "$root" rev-parse --short=7 "$commit")
fi

if [[ $docker == 1 ]]; then
    platform=linux/amd64
    [[ $arch == aarch64 ]] && platform=linux/arm64
    mounts=(-v "$root:$root:ro" -v "$out:$out")
    if [[ -f $root/.git ]]; then
        common=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)
        mounts+=(-v "$common:$common:ro")
    fi
    exec docker run --rm --privileged --platform "$platform" "${mounts[@]}" \
        -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e 'GIT_CONFIG_VALUE_0=*' \
        -w "$root" "$IMAGE" \
        bash "$root/tools/build_linux_flatpak.sh" "$arch" --out "$out" --inside --commit "$commit" --url "$url"
fi

work=$out/flatpak-$arch
mkdir -p "$work"
manifest=$work/org.roadrash.nativeport.yml
sed -e "s|@SOURCE_URL@|$url|" -e "s|@SOURCE_COMMIT@|$commit|" "$root/linux/org.roadrash.nativeport.yml" > "$manifest"
bundle=$out/RoadRashNative-$version-$arch.flatpak
flatpak-builder --arch="$arch" --disable-rofiles-fuse --force-clean --state-dir="$work/state" \
    --repo="$work/repo" "$work/build" "$manifest"
flatpak build-bundle --arch="$arch" "$work/repo" "$bundle" org.roadrash.nativeport
echo "$bundle"

# Install into a scratch user installation and run the headless smoke.
export FLATPAK_USER_DIR=$work/user
rm -rf "$FLATPAK_USER_DIR"
# The 24.08 runtime is already in the builder image's system installation.
flatpak --user install -y --noninteractive "$bundle"
smoke_home=$(mktemp -d)
run=(env HOME="$smoke_home" flatpak --user run --env=SDL_VIDEODRIVER=dummy --env=SDL_AUDIODRIVER=dummy
     org.roadrash.nativeport)
"${run[@]}" --version | tee "$work/version.json"
grep -q '"configuration": *"Release"' "$work/version.json"
grep -q "\"commit\": *\"$commit\"" "$work/version.json"
"${run[@]}" --first-run-smoke
test -f "$smoke_home/.var/app/org.roadrash.nativeport/data/RoadRashNative/RoadRash/rr-game.log"
echo "flatpak smoke: OK ($bundle)"
