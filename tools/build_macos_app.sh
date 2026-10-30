#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${RR_PACKAGE_BUILD_DIR:-"$root/build-release"}
cmake -S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DRR_BUNDLE_SDL=ON
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure --timeout 900 --output-junit "$build_dir/package-ctest.xml"
# Stamp the commit the bundle was built from (plan 11a Task 4), so the verifier
# can fail a stale app. The plist is part of the sealed resources, so re-sign.
app="$build_dir/Road Rash Native.app"
commit=$(git -C "$root" rev-parse --short HEAD)
plutil -replace RRBuildCommit -string "$commit" "$app/Contents/Info.plist"
python3 "$root/tools/build_info.py" --check "$build_dir/build-info.json"
cp "$build_dir/build-info.json" "$app/Contents/Resources/build-info.json"
codesign --force --sign - "$app"
python3 "$root/tools/verify_macos_app.py" "$app"
ditto -c -k --sequesterRsrc --keepParent "$app" "$build_dir/Road Rash Native.zip"
printf '%s\n' "$build_dir/Road Rash Native.zip"
