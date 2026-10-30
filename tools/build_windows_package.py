#!/usr/bin/env python3
"""Plan 33b Task 5: stage, sign and package rr-game.exe for Windows.

Produces, from an existing Release build:
  RoadRashNative-<version>-windows-<arch>.zip   portable ("Road Rash Native\\" with portable.txt)
  RoadRashNative-<version>-setup-<arch>.exe     per-user Inno Setup installer (when ISCC is found)
Signing goes through tools/sign_windows.ps1, which is skipped unless the owner
configured RR_SIGN_*. No game data is staged; tools/verify_windows_package.py
checks that and the rest. Written in Python (not the plan's .ps1) so the same
staging runs against a MinGW cross build on macOS/Linux.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from agent_common import ROOT  # noqa: E402
import build_info  # noqa: E402

PRODUCT = 'Road Rash Native'
LICENSES = {
    'SDL-LICENSE.txt': 'third_party/SDL-LICENSE.txt',
    'stb-LICENSE.txt': 'third_party/LICENSE',
    'libdatachannel-LICENSE.txt': 'third_party/libdatachannel-LICENSE.txt',
    'libjuice-LICENSE.txt': 'third_party/libjuice-LICENSE.txt',
    'usrsctp-LICENSE.md': 'third_party/usrsctp-LICENSE.md',
    'plog-LICENSE.txt': 'third_party/plog-LICENSE.txt',
    'OpenSSL-LICENSE.txt': 'third_party/OpenSSL-LICENSE.txt',
}
# Fixed zip timestamps keep the archive reproducible for one executable.
ZIP_TIME = (2026, 1, 1, 0, 0, 0)


def package_version():
    """X.Y.Z when HEAD carries the v* tag, else X.Y.Z-g<commit> (a development build)."""
    major, minor, patch = build_info.release_version()
    base = f'{major}.{minor}.{patch}'
    tags = subprocess.run(['git', '-C', str(ROOT), 'tag', '--points-at', 'HEAD', '--list', 'v[0-9]*'],
                          capture_output=True, text=True).stdout.split()
    if tags:
        return base, f'{base}.0'
    commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', '--short=7', 'HEAD'], text=True).strip()
    return f'{base}-g{commit}', f'{base}.0'


def find_executable(build, config):
    for candidate in [build / config / 'rr-game.exe', build / 'rr-game.exe']:
        if candidate.is_file():
            return candidate
    raise SystemExit(f'rr-game.exe not found in {build} ({config}); build the Release configuration first')


def sign(files):
    if os.name != 'nt':
        print('signing skipped: not a Windows host')
        return
    subprocess.run(['pwsh', '-NoProfile', '-File', str(ROOT / 'tools/sign_windows.ps1'), *map(str, files)], check=True)


def signing_configured():
    if os.name != 'nt':
        return False
    return subprocess.run(['pwsh', '-NoProfile', '-File', str(ROOT / 'tools/sign_windows.ps1'), '-Check']).returncode == 0


def find_iscc(explicit):
    if explicit:
        return Path(explicit)
    found = shutil.which('ISCC') or shutil.which('iscc')
    if found:
        return Path(found)
    for base in [os.environ.get('ProgramFiles(x86)'), os.environ.get('ProgramFiles'), os.environ.get('LOCALAPPDATA')]:
        if base:
            for sub in ['Inno Setup 6', 'Programs/Inno Setup 6']:
                candidate = Path(base) / sub / 'ISCC.exe'
                if candidate.is_file():
                    return candidate
    return None


def write_zip(stage, path):
    entries = sorted(p for p in stage.rglob('*') if p.is_file())
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in entries:
            info = zipfile.ZipInfo(p.relative_to(stage.parent).as_posix(), ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, p.read_bytes())


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--config', default='Release')
    p.add_argument('--arch', choices=['x64', 'arm64'], required=True)
    p.add_argument('--out', type=Path, help='output directory (default: <build>/package)')
    p.add_argument('--installer', choices=['auto', 'require', 'skip'], default='auto',
                   help='build the Inno Setup installer when ISCC is available (auto), always, or never')
    p.add_argument('--iscc', help='ISCC.exe path (otherwise PATH and the default install folders)')
    args = p.parse_args()

    build = args.build_dir.resolve()
    out = (args.out or build / 'package').resolve()
    version, numeric = package_version()
    exe = find_executable(build, args.config)

    stage_root = out / 'stage'
    shutil.rmtree(stage_root, ignore_errors=True)
    stage = stage_root / PRODUCT
    (stage / 'licenses').mkdir(parents=True)
    shutil.copy2(exe, stage / 'rr-game.exe')
    shutil.copy2(ROOT / 'windows/README.txt', stage / 'README.txt')
    for name, source in LICENSES.items():
        shutil.copy2(ROOT / source, stage / 'licenses' / name)
    sign([stage / 'rr-game.exe'])

    # The installer stages the same folder minus the portable marker.
    artifacts = []
    installer_name = f'RoadRashNative-{version}-setup-{args.arch}'
    iscc = None if args.installer == 'skip' else find_iscc(args.iscc)
    if args.installer == 'require' and not iscc:
        raise SystemExit('ISCC.exe (Inno Setup 6.3+) not found')
    if iscc:
        command = [str(iscc), '/Qp', f'/DAppVersion={version}', f'/DNumericVersion={numeric}', f'/DArch={args.arch}',
                   f'/DSourceDir={stage}', f'/DOutputDir={out}', f'/DOutputBaseFilename={installer_name}',
                   f'/DIconFile={ROOT / "assets/app-icon/roadrash.ico"}']
        if signing_configured():
            hook = f'pwsh -NoProfile -File "{ROOT / "tools/sign_windows.ps1"}" $f'
            command += [f'/Srrsign={hook}', '/DSignTool=rrsign']
        subprocess.run([*command, str(ROOT / 'windows/installer.iss')], check=True)
        installer = out / f'{installer_name}.exe'
        sign([installer])
        artifacts.append(installer)
    else:
        print('installer skipped: ISCC.exe (Inno Setup 6.3+) not found')

    shutil.copy2(ROOT / 'windows/portable.txt', stage / 'portable.txt')
    archive = out / f'RoadRashNative-{version}-windows-{args.arch}.zip'
    archive.unlink(missing_ok=True)
    write_zip(stage, archive)
    artifacts.insert(0, archive)
    for a in artifacts:
        print(a)


if __name__ == '__main__':
    main()
