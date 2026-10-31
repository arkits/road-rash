#!/usr/bin/env python3
"""Plan 33a Task 2: stage rr-game into the Linux tarball and AppImage.

Run by tools/build_linux_package.sh inside the sniper SDK container (Python
3.9). Produces in --out:
  RoadRashNative-<version>-linux-<arch>.tar.gz  road-rash-native/ (rr-game, fonts/, licenses/, ...)
  RoadRashNative-<version>-<arch>.AppImage      the same tree as an AppDir, static type-2 runtime
  version-<arch>.txt                             <version>, for the caller
No game data is staged; tools/verify_linux_package.py checks that and the rest.
"""
import argparse
import gzip
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from agent_common import ROOT  # noqa: E402
import build_info  # noqa: E402

APP_ID = 'org.roadrash.nativeport'
LICENSES = {
    'SDL-LICENSE.txt': 'third_party/SDL-LICENSE.txt',
    'stb-LICENSE.txt': 'third_party/LICENSE',
    'libdatachannel-LICENSE.txt': 'third_party/libdatachannel-LICENSE.txt',
    'libjuice-LICENSE.txt': 'third_party/libjuice-LICENSE.txt',
    'usrsctp-LICENSE.md': 'third_party/usrsctp-LICENSE.md',
    'plog-LICENSE.txt': 'third_party/plog-LICENSE.txt',
    'OpenSSL-LICENSE.txt': 'third_party/OpenSSL-LICENSE.txt',
}
FONTS = ['DejaVuSans.ttf', 'DejaVuSans-Bold.ttf']
# Fixed archive metadata keeps the tarball reproducible for one executable.
MTIME = 1767225600  # 2026-01-01T00:00:00Z
APPRUN = '''#!/bin/sh
here=$(dirname "$(readlink -f "$0")")
exec "$here/rr-game" "$@"
'''


def package_version():
    """X.Y.Z when HEAD carries the v* tag, else X.Y.Z-g<commit> (a development build)."""
    major, minor, patch = build_info.release_version()
    base = f'{major}.{minor}.{patch}'
    tags = subprocess.run(['git', '-C', str(ROOT), 'tag', '--points-at', 'HEAD', '--list', 'v[0-9]*'],
                          capture_output=True, text=True).stdout.split()
    if tags:
        return base
    commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', '--short=7', 'HEAD'], text=True).strip()
    return f'{base}-g{commit}'


def desktop_entry(exec_line):
    text = (ROOT / 'linux/road-rash-native.desktop.in').read_text()
    return text.replace('@EXEC@', exec_line).replace('@ICON@', APP_ID)


def stage_tree(stage, exe, fonts_tar):
    (stage / 'licenses').mkdir(parents=True)
    (stage / 'fonts').mkdir()
    shutil.copy2(exe, stage / 'rr-game')
    subprocess.run(['strip', '--strip-unneeded', str(stage / 'rr-game')], check=True)
    shutil.copy2(ROOT / 'linux/README.txt', stage / 'README.txt')
    shutil.copy2(ROOT / 'linux/road-rash-native.desktop.in', stage / 'road-rash-native.desktop.in')
    shutil.copy2(ROOT / 'linux/install-desktop-entry.sh', stage / 'install-desktop-entry.sh')
    shutil.copy2(ROOT / 'assets/app-icon/roadrash.png', stage / 'icon.png')
    for name, source in LICENSES.items():
        shutil.copy2(ROOT / source, stage / 'licenses' / name)
    with tarfile.open(fonts_tar) as t:
        members = {Path(m.name).name: m for m in t.getmembers() if m.isfile()}
        for name in FONTS:
            (stage / 'fonts' / name).write_bytes(t.extractfile(members[name]).read())
        (stage / 'licenses' / 'DejaVu-LICENSE.txt').write_bytes(t.extractfile(members['LICENSE']).read())
    for p in stage.rglob('*'):
        executable = p.name in ('rr-game', 'install-desktop-entry.sh', 'AppRun')
        p.chmod(0o755 if p.is_dir() or executable else 0o644)


def write_tarball(stage, path):
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode='w', format=tarfile.PAX_FORMAT) as t:
        for p in [stage, *sorted(stage.rglob('*'))]:
            info = t.gettarinfo(str(p), p.relative_to(stage.parent).as_posix())
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mtime = MTIME
            if p.is_file():
                with p.open('rb') as f:
                    t.addfile(info, f)
            else:
                t.addfile(info)
    with path.open('wb') as f, gzip.GzipFile(filename='', mode='wb', fileobj=f, mtime=0, compresslevel=9) as g:
        g.write(raw.getvalue())


def build_appimage(stage, appdir, path, tool, runtime, arch):
    shutil.copytree(stage, appdir, symlinks=True)
    for name in ('road-rash-native.desktop.in', 'install-desktop-entry.sh'):
        (appdir / name).unlink()
    (appdir / 'AppRun').write_text(APPRUN)
    (appdir / 'AppRun').chmod(0o755)
    (appdir / f'{APP_ID}.desktop').write_text(desktop_entry('rr-game'))
    shutil.copy2(ROOT / 'assets/app-icon/roadrash.png', appdir / f'{APP_ID}.png')
    os.symlink(f'{APP_ID}.png', appdir / '.DirIcon')
    metainfo = appdir / 'usr/share/metainfo'
    metainfo.mkdir(parents=True)
    shutil.copy2(ROOT / f'linux/{APP_ID}.metainfo.xml', metainfo / f'{APP_ID}.appdata.xml')
    env = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN='1', ARCH=arch, SOURCE_DATE_EPOCH=str(MTIME))
    subprocess.run([str(tool), '--no-appstream', '--runtime-file', str(runtime), str(appdir), str(path)],
                   check=True, env=env)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--arch', choices=['x86_64', 'aarch64'], required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--fonts', type=Path, required=True, help='dejavu-fonts-ttf-2.37.tar.bz2')
    p.add_argument('--appimagetool', type=Path, required=True)
    p.add_argument('--runtime', type=Path, required=True, help='type-2 runtime for the AppImage')
    args = p.parse_args()

    exe = args.build_dir / 'rr-game'
    if not exe.is_file():
        raise SystemExit(f'rr-game not found in {args.build_dir}; build the linux-release preset first')
    version = package_version()
    out = args.out.resolve()
    work = out / f'stage-{args.arch}'
    shutil.rmtree(work, ignore_errors=True)
    stage = work / 'road-rash-native'
    stage_tree(stage, exe, args.fonts)

    tarball = out / f'RoadRashNative-{version}-linux-{args.arch}.tar.gz'
    write_tarball(stage, tarball)
    appimage = out / f'RoadRashNative-{version}-{args.arch}.AppImage'
    appimage.unlink(missing_ok=True)
    build_appimage(stage, work / 'AppDir', appimage, args.appimagetool.resolve(), args.runtime.resolve(), args.arch)
    (out / f'version-{args.arch}.txt').write_text(version + '\n')
    for a in (tarball, appimage):
        print(a)


if __name__ == '__main__':
    main()
