#!/usr/bin/env bash
# Plan 33a Tasks 2-3: portable Linux release build and packages.
#
#   tools/build_linux_package.sh [x86_64|aarch64] [--out DIR] [--no-docker] [--flatpak]
#
# Builds rr-game in the Steam Linux Runtime 3.0 ("sniper") SDK container,
# pinned by digest, so the binary needs glibc 2.31 at most and runs on
# SteamOS 3 and every current distribution. OpenSSL 3.5.9 is built static
# from its release tarball; SDL3 (pinned source) is static and dlopens X11,
# Wayland, PipeWire, PulseAudio, ALSA and udev; libstdc++/libgcc are static.
# Outputs in DIR (default build-linux-package/):
#   RoadRashNative-<version>-linux-<arch>.tar.gz
#   RoadRashNative-<version>-<arch>.AppImage    (static type-2 runtime, no libfuse2)
#   RoadRashNative-<version>-<arch>.flatpak     (--flatpak; see linux/org.roadrash.nativeport.yml)
# then runs tools/verify_linux_package.py on them. No game data is staged.
# --no-docker runs the container steps on this host (CI jobs that already run
# inside the sniper image). Downloads are cached in DIR/cache and checked
# against the SHA-256 values below.
set -euo pipefail

SNIPER_X86_64=registry.gitlab.steamos.cloud/steamrt/sniper/sdk@sha256:bb30053d3cd19d35c76ae69b2790dbaa1430448006f8240cb6d7e723f2d1940e
SNIPER_AARCH64=registry.gitlab.steamos.cloud/steamrt/sniper/sdk/arm64@sha256:966ad3ef70751b98253f031d22c5b75798ceb9c49612b0a3489fc5f8f58858fc
OPENSSL_URL=https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz
OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
DEJAVU_URL=https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2
DEJAVU_SHA256=fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7
APPIMAGETOOL_URL=https://github.com/AppImage/appimagetool/releases/download/1.9.1
APPIMAGETOOL_SHA256_x86_64=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
APPIMAGETOOL_SHA256_aarch64=f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158
RUNTIME_URL=https://github.com/AppImage/type2-runtime/releases/download/20251108
RUNTIME_SHA256_x86_64=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
RUNTIME_SHA256_aarch64=00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
arch=$(uname -m)
[[ $arch == arm64 ]] && arch=aarch64
out=$root/build-linux-package
docker=1
inside=0
flatpak=0
while [[ $# -gt 0 ]]; do
    case $1 in
        x86_64|aarch64) arch=$1 ;;
        --out) out=$2; shift ;;
        --no-docker) docker=0 ;;
        --inside) inside=1; docker=0 ;;
        --flatpak) flatpak=1 ;;
        *) echo "usage: $0 [x86_64|aarch64] [--out DIR] [--no-docker] [--flatpak]" >&2; exit 2 ;;
    esac
    shift
done
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)

fetch() { # url sha256 destination
    if [[ -f $3 ]] && echo "$2  $3" | sha256sum -c --status; then return; fi
    curl -fsSL --retry 3 -o "$3.part" "$1"
    echo "$2  $3.part" | sha256sum -c --quiet
    mv "$3.part" "$3"
}

build_inside() {
    local cache=$out/cache build=$out/build-$arch ssl=$out/cache/openssl-3.5.9-$arch
    mkdir -p "$cache"
    if [[ ! -f $ssl/lib/libssl.a ]]; then
        fetch "$OPENSSL_URL" "$OPENSSL_SHA256" "$cache/openssl-3.5.9.tar.gz"
        rm -rf "$cache/openssl-src-$arch"
        mkdir -p "$cache/openssl-src-$arch"
        tar -xzf "$cache/openssl-3.5.9.tar.gz" -C "$cache/openssl-src-$arch" --strip-components=1
        (cd "$cache/openssl-src-$arch" &&
            CC=gcc-14 ./Configure "linux-$arch" no-shared no-tests no-docs no-apps no-module \
                --prefix="$ssl" --libdir=lib --openssldir=/etc/ssl &&
            make -j"$(nproc)" >/dev/null && make install_sw >/dev/null)
        rm -rf "$cache/openssl-src-$arch"
    fi
    fetch "$DEJAVU_URL" "$DEJAVU_SHA256" "$cache/dejavu-fonts-ttf-2.37.tar.bz2"
    local tool_sha=APPIMAGETOOL_SHA256_$arch runtime_sha=RUNTIME_SHA256_$arch
    fetch "$APPIMAGETOOL_URL/appimagetool-$arch.AppImage" "${!tool_sha}" "$cache/appimagetool-$arch.AppImage"
    fetch "$RUNTIME_URL/runtime-$arch" "${!runtime_sha}" "$cache/runtime-$arch"
    chmod +x "$cache/appimagetool-$arch.AppImage"

    cd "$root"
    cmake --preset linux-release -B "$build" -DOPENSSL_ROOT_DIR="$ssl" -DBUILD_TESTING=OFF
    cmake --build "$build" --target rr-game --parallel "$(nproc)"
    python3 tools/stage_linux_package.py --build-dir "$build" --arch "$arch" --out "$out" \
        --fonts "$cache/dejavu-fonts-ttf-2.37.tar.bz2" \
        --appimagetool "$cache/appimagetool-$arch.AppImage" --runtime "$cache/runtime-$arch"
}

if [[ $inside == 1 || $docker == 0 ]]; then
    build_inside
else
    image=$SNIPER_X86_64 platform=linux/amd64
    [[ $arch == aarch64 ]] && image=$SNIPER_AARCH64 platform=linux/arm64
    mounts=(-v "$root:$root:ro" -v "$out:$out")
    # A git worktree's metadata lives in the main repository's .git; mount it
    # at the same path so build provenance can read the commit.
    if [[ -f $root/.git ]]; then
        common=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)
        mounts+=(-v "$common:$common:ro")
    fi
    docker run --rm --platform "$platform" "${mounts[@]}" \
        -e GIT_CONFIG_COUNT=1 -e GIT_CONFIG_KEY_0=safe.directory -e 'GIT_CONFIG_VALUE_0=*' \
        -e HOME=/tmp -u "$(id -u):$(id -g)" -w "$root" "$image" \
        bash "$root/tools/build_linux_package.sh" "$arch" --out "$out" --inside
fi

if [[ $inside == 0 ]]; then
    version=$(cat "$out/version-$arch.txt")
    if [[ $flatpak == 1 ]]; then
        "$root/tools/build_linux_flatpak.sh" "$arch" --out "$out"
    fi
    verify=(python3 "$root/tools/verify_linux_package.py" "$out/RoadRashNative-$version-linux-$arch.tar.gz"
            --arch "$arch" --appimage "$out/RoadRashNative-$version-$arch.AppImage")
    [[ $(uname -s) == Linux && $(uname -m) == "$arch" ]] && verify+=(--run)
    "${verify[@]}"
fi
