#!/bin/sh
# Build the browser engine. Hosted disc assets are provisioned separately.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="${RR_WEB_BUILD_DIR:-$root/build-agent-browser}"
dist="$build/dist"
if ! command -v emcmake >/dev/null 2>&1; then
    if [ ! -f "$root/.tools/emsdk/emsdk_env.sh" ]; then
        echo 'Install the pinned emsdk documented in docs/WEB.md before building.' >&2
        exit 1
    fi
    . "$root/.tools/emsdk/emsdk_env.sh"
fi
cd "$root"
emcmake cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DRR_WEB_BROWSER=ON -DBUILD_TESTING=OFF
cmake --build "$build" --target rr-game --parallel "${RR_BUILD_JOBS:-8}"
mkdir -p "$dist"
cp "$root/web/index.html" "$root/web/style.css" "$root/web/app.js" "$root/web/og-image.png" "$root/web/favicon.ico" "$build/rr-game.js" "$build/rr-game.wasm" "$dist/"
python3 - "$root" "$dist" <<'PYTHON'
import json, re, sys
from pathlib import Path
root, dist = map(Path, sys.argv[1:])
rows = re.findall(r'\{"([^"}]+)",\s*"([0-9a-f]{64})"\}', (root / 'src/reference_hashes.inc').read_text())
if len(rows) != 341:
    raise SystemExit('Unexpected import reference table')
(dist / 'references.json').write_text(json.dumps([dict(path=p, hash=h) for p, h in rows]))
PYTHON

# Licences (mirrors the macOS bundle rule: third-party notices ship).
{
    echo "THIRD-PARTY LICENCES (web bundle)"
    echo "=================================="
    for f in "$root/third_party/SDL-LICENSE.txt" "$root/third_party/LICENSE"; do
        if [ -f "$f" ]; then
            echo; echo "--- $(basename "$f") ---"; echo; cat "$f"
        fi
    done
} > "$dist/LICENSES.txt"

# Bundle pinned telemetry SDKs locally; no third-party script hosts at runtime.
npm ci --prefix "$root/web" --ignore-scripts
RR_TELEMETRY_OUTPUT="$dist/telemetry.js" npm run --prefix "$root/web" build
# Only known code/metadata files may be published.
python3 "$root/tools/fingerprint_web_bundle.py" "$dist"
python3 "$root/tools/prepare_web_deploy.py" "$dist" --validate-only
echo "Playable web bundle ready in $dist"
