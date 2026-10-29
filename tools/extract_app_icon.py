#!/usr/bin/env python3
"""Extract the reference EXE icon and derive launcher/favicon assets.

Requires pefile and Pillow (python -m pip install pefile Pillow).
Only the explicitly requested application icon is exported, never game data.
"""
import hashlib
import io
import json
from pathlib import Path
import struct
import sys

import pefile
from PIL import Image

root = Path(__file__).resolve().parents[1]
exe = Path(sys.argv[1]) if len(sys.argv) > 1 else root.parent / 'cdrom/ROADRASH/ROADRASH.EXE'
data = exe.read_bytes()
inventory = json.loads((root / 'analysis/inventory.json').read_text())
expected = next(row['sha256'] for row in inventory['files'] if row['path'] == 'ROADRASH.EXE')
if hashlib.sha256(data).hexdigest() != expected:
    raise SystemExit('Expected the unmodified original disc executable')
pe = pefile.PE(data=data)
resources = {}
for kind in pe.DIRECTORY_ENTRY_RESOURCE.entries:
    if kind.id not in (3, 14):
        continue
    for entry in kind.directory.entries:
        resource = entry.directory.entries[0].data.struct
        resources[kind.id, entry.id] = pe.get_data(resource.OffsetToData, resource.Size)
group_id = min(key[1] for key in resources if key[0] == 14)
group = resources[14, group_id]
reserved, kind, count = struct.unpack_from('<HHH', group)
header = bytearray(struct.pack('<HHH', reserved, kind, count))
payload = bytearray()
for index in range(count):
    offset = 6 + index * 14
    width, height, colours, zero, planes, bits, size, icon_id = struct.unpack_from('<BBBBHHIH', group, offset)
    icon = resources[3, icon_id]
    if len(icon) != size:
        raise SystemExit('Icon resource size mismatch')
    header.extend(struct.pack('<BBBBHHII', width, height, colours, zero, planes, bits, size,
                              6 + count * 16 + len(payload)))
    payload.extend(icon)
ico = bytes(header + payload)
output = root / 'assets/app-icon'
output.mkdir(parents=True, exist_ok=True)
(output / 'roadrash.ico').write_bytes(ico)
image = Image.open(io.BytesIO(ico)).convert('RGBA')
image.save(output / 'roadrash.png')
# Retain the original pixel art at each Android launcher density.
for density, size in [('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)]:
    directory = root / 'android/app/src/main/res' / ('mipmap-' + density)
    directory.mkdir(parents=True, exist_ok=True)
    image.resize((size, size), Image.Resampling.NEAREST).save(directory / 'ic_launcher.png')
# The 16/32/48 pixel multi-size ICO works in desktop and mobile browsers.
image.resize((48, 48), Image.Resampling.NEAREST).save(
    root / 'web/favicon.ico', format='ICO', sizes=[(16, 16), (32, 32), (48, 48)],
    bitmap_format='bmp')
print(f'Extracted icon group {group_id}: {count} image(s), source {image.size}, EXE SHA-256 {expected}')
