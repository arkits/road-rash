#!/usr/bin/env python3
"""Validate the code-only bundle; optionally stage legacy Vercel output."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

root = Path(__file__).resolve().parents[1]
dist = Path(sys.argv[1]).resolve()
roles = ['index.html', 'app.js', 'telemetry.js', 'style.css', 'rr-game.js', 'rr-game.wasm', 'references.json', 'LICENSES.txt', 'og-image.png', 'favicon.ico']
files = {p.name for p in dist.iterdir() if p.is_file()}
allowed = set()
stable = {'index.html', 'LICENSES.txt', 'og-image.png', 'favicon.ico'}
for role in roles:
    stem, extension = role.rsplit('.', 1)
    matches = {name for name in files if name == role or (
        role not in stable and
        re.fullmatch(re.escape(stem) + r'\.[0-9a-f]{16}\.' + re.escape(extension), name))}
    if len(matches) != 1:
        raise SystemExit(f'Expected one bundle file for {role}: {matches}')
    for name in matches:
        if name != role and hashlib.sha256((dist / name).read_bytes()).hexdigest()[:16] != name.split('.')[-2]:
            raise SystemExit(f'Fingerprint does not match bundle content: {name}')
    allowed.update(matches)
if files != allowed or any(p.is_dir() for p in dist.iterdir()):
    raise SystemExit(f'Unexpected web bundle contents: {files ^ allowed}')
hashes = set(re.findall(r'[0-9a-f]{64}', (root / 'src/reference_hashes.inc').read_text()))
for name in allowed:
    if hashlib.sha256((dist / name).read_bytes()).hexdigest() in hashes:
        raise SystemExit(f'Game asset found in web bundle: {name}')
if '--validate-only' in sys.argv[2:]:
    print(f'Validated {len(allowed)} code/metadata files; no game assets.')
    raise SystemExit(0)
output = root / '.vercel/output'
static = output / 'static'
# Only this generated deployment directory is replaced. Project linkage stays intact.
if static.exists():
    shutil.rmtree(static)
shutil.copytree(dist, static)
# Vercel merges vercel.json with prebuilt routes. Use one header definition
# so the tested local policy also survives that merge on the deployment.
project = json.loads((root / 'vercel.json').read_text())
headers = {item['key']: item['value'] for item in project['headers'][0]['headers']}
config = {'version': 3, 'routes': [
    {'src': '/(.*)', 'headers': headers, 'continue': True},
    {'handle': 'filesystem'}
]}
(output / 'config.json').write_text(json.dumps(config, indent=2) + '\n')
print(f'Validated {len(allowed)} code/metadata files; no game assets. Vercel output: {output}')
