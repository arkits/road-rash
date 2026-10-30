#!/usr/bin/env python3
"""Generate stable source/config identity for binaries and packaging."""
import argparse
import hashlib
import json
from pathlib import Path
from agent_common import ROOT, atomic_json, source_identity


def identity(config, compiler, cache=None):
    settings = {}
    if cache and Path(cache).exists():
        for line in Path(cache).read_text().splitlines():
            if '=' not in line or ':' not in line or line.startswith(('#', '//')):
                continue
            key_type, value = line.split('=', 1)
            key = key_type.split(':', 1)[0]
            if (key.startswith(('CMAKE_C_FLAGS', 'CMAKE_CXX_FLAGS', 'CMAKE_EXE_LINKER_FLAGS', 'CMAKE_SHARED_LINKER_FLAGS'))
                    or key in {'CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER', 'CMAKE_BUILD_TYPE',
                               'CMAKE_OSX_ARCHITECTURES', 'CMAKE_OSX_DEPLOYMENT_TARGET', 'CMAKE_SYSTEM_NAME',
                               'CMAKE_ANDROID_ARCH_ABI', 'CMAKE_ANDROID_NDK', 'RR_BUNDLE_SDL', 'RR_DEV_ASSETS'}):
                settings[key] = value
    config_hash = hashlib.sha256(json.dumps(settings, sort_keys=True).encode()).hexdigest()
    return dict(source_identity(), configuration=config, compiler=compiler,
                build_configuration=settings, configuration_sha256=config_hash)


def write_header(path, info):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(info, sort_keys=True)
    content = '#pragma once\nnamespace rr { inline constexpr const char* build_info_json = ' + json.dumps(payload) + '; }\n'
    if not path.exists() or path.read_text() != content:
        path.write_text(content)


def release_version():
    """(major, minor, patch) from the newest v* tag; 0.0.0 before the first release."""
    import re
    import subprocess
    try:
        tag = subprocess.run(['git', 'describe', '--tags', '--abbrev=0', '--match', 'v[0-9]*'], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return 0, 0, 0
    m = re.match(r'v(\d+)\.(\d+)(?:\.(\d+))?', tag)
    return (int(m[1]), int(m[2]), int(m[3] or 0)) if m else (0, 0, 0)


def write_version_header(path, info):
    """Plan 33b Task 3: macros for the Windows VERSIONINFO resource."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    major, minor, patch = release_version()
    commit = (info.get('commit') or 'unknown')[:12]
    dirty = '+dirty' if info.get('dirty') else ''
    lines = [f'#define RR_VERSION_MAJOR {major}', f'#define RR_VERSION_MINOR {minor}',
             f'#define RR_VERSION_PATCH {patch}', f'#define RR_VERSION_STRING "{major}.{minor}.{patch}"',
             f'#define RR_PRODUCT_VERSION_STRING "{major}.{minor}.{patch} ({commit}{dirty})"']
    content = '\n'.join(lines) + '\n'
    if not path.exists() or path.read_text() != content:
        path.write_text(content)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', default='Debug')
    p.add_argument('--compiler', default='unknown')
    p.add_argument('--cache', type=Path)
    p.add_argument('--header', type=Path)
    p.add_argument('--manifest', type=Path)
    p.add_argument('--version-header', type=Path, help='Windows VERSIONINFO macros (plan 33b)')
    p.add_argument('--check', type=Path, help='verify manifest source matches current tree')
    args = p.parse_args()
    info = identity(args.config, args.compiler, args.cache)
    if args.check:
        saved = json.loads(args.check.read_text())
        for key in ['commit', 'source_sha256']:
            if saved[key] != info[key]:
                p.exit(1, f'stale build: {key} differs; rebuild\n')
        print('PASS source identity')
    else:
        if args.header:
            write_header(args.header, info)
        if args.manifest:
            atomic_json(args.manifest, info)
        if args.version_header:
            write_version_header(args.version_header, info)
        print(json.dumps(info, sort_keys=True))


if __name__ == '__main__':
    main()
