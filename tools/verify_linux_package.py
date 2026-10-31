#!/usr/bin/env python3
"""Plan 33a Task 2: verify a Linux tarball (and optionally the AppImage).

Checks, without establishing gameplay fidelity:
- the tarball layout (road-rash-native/ with rr-game, README.txt, icon.png,
  road-rash-native.desktop.in, install-desktop-entry.sh, fonts/, licenses/)
  and no other files;
- rr-game: 64-bit ELF for the expected machine, NEEDED entries only on the
  system-library allowlist (SDL3 dlopens X11, Wayland, audio, udev and GL), and
  no versioned symbol newer than GLIBC_2.31 / GLIBCXX / CXXABI (libstdc++ is
  static);
- no game data: disc file extensions, the reference SHA-256 hashes, ISO and AVI
  magic, plus a size and magic check of every file over 64 KB;
- --appimage: an ELF type-2 AppImage whose squashfs holds the same rr-game,
  AppRun, the desktop entry, icon and AppStream metadata;
- --run (Linux host of the same architecture): `rr-game --version` from the
  unpacked tarball reports a Release build of the current source, and
  `--first-run-smoke` exits 0 under SDL's dummy drivers with a fresh HOME; with
  --appimage, the AppImage (extract-and-run) does the same.
Uses only the standard library (the ELF reader is below), so it runs on the
macOS host and in the Python 3.9 sniper container alike.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from agent_common import ROOT  # noqa: E402

MACHINES = {'x86_64': 62, 'aarch64': 183}
ALLOWED_NEEDED = {'libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0', 'librt.so.1',
                  'ld-linux-x86-64.so.2', 'ld-linux-aarch64.so.1'}
GLIBC_CEILING = (2, 31)
DISC_SUFFIXES = {'.EXE', '.RSC', '.RRI', '.BOB', '.CRS', '.DAT', '.WAV', '.RRA', '.AVI', '.ISO', '.MID', '.SBK',
                 '.DLL', '.BIN', '.CUE', '.IMG'}
TOP = 'road-rash-native'
REQUIRED = {'rr-game', 'README.txt', 'icon.png', 'road-rash-native.desktop.in', 'install-desktop-entry.sh',
            'fonts/DejaVuSans.ttf', 'fonts/DejaVuSans-Bold.ttf'}
LICENSES = {'SDL-LICENSE.txt', 'stb-LICENSE.txt', 'libdatachannel-LICENSE.txt', 'libjuice-LICENSE.txt',
            'usrsctp-LICENSE.md', 'plog-LICENSE.txt', 'OpenSSL-LICENSE.txt', 'DejaVu-LICENSE.txt'}
# Large files that are expected, by magic: the executable (ELF) and the fonts (TrueType).
LARGE_MAGIC = {'rr-game': b'\x7fELF', 'fonts/DejaVuSans.ttf': b'\x00\x01\x00\x00',
               'fonts/DejaVuSans-Bold.ttf': b'\x00\x01\x00\x00'}
APPDIR_REQUIRED = {'AppRun', 'rr-game', 'org.roadrash.nativeport.desktop', 'org.roadrash.nativeport.png',
                   'usr/share/metainfo/org.roadrash.nativeport.appdata.xml'}


def fail(message):
    raise SystemExit(f'FAIL: {message}')


def reference_hashes():
    hashes = set(re.findall(r'"([0-9a-f]{64})"', (ROOT / 'src/reference_hashes.inc').read_text()))
    if len(hashes) < 300:
        fail('reference hash table not found')
    return hashes


class Elf:
    """The parts of a little-endian ELF64 file this check needs."""

    def __init__(self, data):
        if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
            fail('not a little-endian ELF64 file')
        self.data = data
        self.machine = struct.unpack_from('<H', data, 18)[0]
        shoff, = struct.unpack_from('<Q', data, 40)
        shentsize, shnum = struct.unpack_from('<HH', data, 58)
        self.sections = []
        for i in range(shnum):
            name, kind, _, _, offset, size, link, info, _, entsize = struct.unpack_from(
                '<IIQQQQIIQQ', data, shoff + i * shentsize)
            self.sections.append(dict(kind=kind, offset=offset, size=size, link=link, info=info))

    def string(self, section, offset):
        start = self.sections[section]['offset'] + offset
        return self.data[start:self.data.index(b'\0', start)].decode()

    def needed(self):
        out = []
        for s in self.sections:
            if s['kind'] == 6:  # SHT_DYNAMIC
                for pos in range(s['offset'], s['offset'] + s['size'], 16):
                    tag, value = struct.unpack_from('<qQ', self.data, pos)
                    if tag == 0:
                        break
                    if tag == 1:  # DT_NEEDED
                        out.append(self.string(s['link'], value))
        return out

    def version_needs(self):
        """{library: [version names]} from .gnu.version_r."""
        out = {}
        for s in self.sections:
            if s['kind'] != 0x6ffffffe:  # SHT_GNU_verneed
                continue
            pos = s['offset']
            for _ in range(s['info']):
                _, count, file_off, aux, nxt = struct.unpack_from('<HHIII', self.data, pos)
                names = out.setdefault(self.string(s['link'], file_off), [])
                apos = pos + aux
                for _ in range(count):
                    _, _, _, name_off, anext = struct.unpack_from('<IHHII', self.data, apos)
                    names.append(self.string(s['link'], name_off))
                    apos += anext
                pos += nxt
        return out


def check_executable(data, arch):
    elf = Elf(data)
    if elf.machine != MACHINES[arch]:
        fail(f'rr-game: ELF machine {elf.machine}, expected {arch}')
    needed = elf.needed()
    extra = sorted(n for n in needed if n not in ALLOWED_NEEDED)
    if extra:
        fail(f'rr-game: NEEDED outside the allowlist: {extra}')
    versions = elf.version_needs()
    newest = (0, 0)
    for lib, names in versions.items():
        for name in names:
            if name.startswith(('GLIBCXX_', 'CXXABI_', 'GCC_')):
                fail(f'rr-game needs {name} from {lib}; libstdc++/libgcc must be static')
            m = re.fullmatch(r'GLIBC_(\d+)\.(\d+)(?:\.\d+)?', name)
            if m:
                newest = max(newest, (int(m[1]), int(m[2])))
    if newest > GLIBC_CEILING:
        fail(f'rr-game needs GLIBC_{newest[0]}.{newest[1]} (ceiling {GLIBC_CEILING[0]}.{GLIBC_CEILING[1]})')
    return dict(needed=sorted(needed), newest_glibc=f'{newest[0]}.{newest[1]}')


def check_no_game_data(files, hashes):
    for name, data in files:
        if Path(name).suffix.upper() in DISC_SUFFIXES:
            fail(f'unexpected disc-type file: {name}')
        if hashlib.sha256(data).hexdigest() in hashes:
            fail(f'{name} matches an original disc file')
        if data[32769:32774] == b'CD001' or (data[:4] == b'RIFF' and data[8:12] == b'AVI '):
            fail(f'{name} carries disc/ISO or AVI data')
        if len(data) > 64 * 1024:
            magic = LARGE_MAGIC.get(name)
            if magic is None or not data.startswith(magic):
                fail(f'{name}: unexpected large file ({len(data)} bytes)')


def read_tarball(path):
    files = {}
    with tarfile.open(path) as t:
        for m in t.getmembers():
            if not m.name.startswith(TOP + '/') and m.name != TOP:
                fail(f'{m.name} is outside {TOP}/')
            if m.issym() or m.islnk():
                fail(f'{m.name}: links are not expected in the tarball')
            if m.isfile():
                files[m.name[len(TOP) + 1:]] = (m.mode, t.extractfile(m).read())
    return files


def appimage_offset(data):
    """Offset of the squashfs payload: the end of the runtime ELF's section headers."""
    shoff, = struct.unpack_from('<Q', data, 40)
    shentsize, shnum = struct.unpack_from('<HH', data, 58)
    return shoff + shentsize * shnum


def run(command, env, cwd=None):
    result = subprocess.run(command, capture_output=True, text=True, timeout=300, env=env, cwd=cwd)
    if result.returncode != 0:
        fail(f'{" ".join(map(str, command))} exited {result.returncode}: {(result.stderr or result.stdout)[-600:]}')
    return result.stdout


def smoke(command, expected):
    with tempfile.TemporaryDirectory(prefix='rr-linux-smoke-') as home:
        env = {k: v for k, v in os.environ.items() if not k.startswith(('XDG_', 'RR_'))}
        env.update(HOME=home, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', APPIMAGE_EXTRACT_AND_RUN='1',
                   TMPDIR=home)
        text = run([*command, '--version'], env, cwd=home)
        match = re.search(r'\{.*\}', text, re.S)
        if not match:
            fail(f'{command[0]} --version printed no JSON: {text[-400:]!r}')
        info = json.loads(match.group(0))
        for key in ['commit', 'source_sha256']:
            if info.get(key) != expected.get(key):
                fail(f'{command[0]} {key} {info.get(key)!r} != current source {expected.get(key)!r}; rebuild')
        if info.get('configuration') != 'Release':
            fail(f'{command[0]} is a {info.get("configuration")} build, not Release')
        run([*command, '--first-run-smoke'], env, cwd=home)
        log = Path(home) / '.local/share/RoadRashNative/RoadRash/rr-game.log'
        return dict(version=info.get('version'), log=log.is_file())


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('tarball', type=Path)
    p.add_argument('--arch', choices=sorted(MACHINES), required=True)
    p.add_argument('--appimage', type=Path)
    p.add_argument('--run', action='store_true', help='launch both packages (Linux host, same architecture)')
    p.add_argument('--commit', help='expected commit (default: HEAD)')
    args = p.parse_args()

    if not re.match(rf'RoadRashNative-\d+\.\d+\.\d+(-g[0-9a-f]+)?-linux-{args.arch}\.tar\.gz$', args.tarball.name):
        fail(f'unexpected archive name {args.tarball.name}')
    hashes = reference_hashes()
    files = read_tarball(args.tarball)
    names = set(files)
    expected_names = REQUIRED | {f'licenses/{n}' for n in LICENSES}
    if names != expected_names:
        fail(f'tarball layout: missing {sorted(expected_names - names)}, unexpected {sorted(names - expected_names)}')
    for name in ('rr-game', 'install-desktop-entry.sh'):
        if not files[name][0] & 0o111:
            fail(f'{name} is not executable in the tarball')
    check_no_game_data([(n, d) for n, (_, d) in files.items()], hashes)
    exe = files['rr-game'][1]
    report = dict(tarball=args.tarball.name, executable=check_executable(exe, args.arch), files=len(files))

    if args.appimage:
        data = args.appimage.read_bytes()
        if data[:4] != b'\x7fELF' or data[8:11] != b'AI\x02':
            fail(f'{args.appimage.name} is not a type-2 AppImage')
        if Elf(data).machine != MACHINES[args.arch]:
            fail(f'{args.appimage.name}: runtime is not {args.arch}')
        offset = appimage_offset(data)
        if data[offset:offset + 4] != b'hsqs':
            fail(f'{args.appimage.name}: no squashfs at offset {offset}')
        report['appimage'] = dict(name=args.appimage.name, size=len(data), squashfs_offset=offset)

    if args.run:
        if sys.platform != 'linux' or os.uname().machine != args.arch:
            fail('--run needs a Linux host of the package architecture')
        from agent_common import source_identity  # noqa: E402
        expected = source_identity()
        if args.commit:
            expected['commit'] = args.commit
        with tempfile.TemporaryDirectory(prefix='rr-linux-pkg-') as tmp:
            with tarfile.open(args.tarball) as t:
                t.extractall(tmp)
            report['tarball_run'] = smoke([str(Path(tmp) / TOP / 'rr-game')], expected)
            if args.appimage:
                extracted = Path(tmp) / 'appimage'
                extracted.mkdir()
                run([str(args.appimage.resolve()), '--appimage-extract'], dict(os.environ), cwd=extracted)
                root = extracted / 'squashfs-root'
                present = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() or p.is_symlink()}
                missing = APPDIR_REQUIRED - present
                if missing:
                    fail(f'AppImage lacks {sorted(missing)}')
                if (root / 'rr-game').read_bytes() != exe:
                    fail('AppImage rr-game differs from the tarball')
                check_no_game_data([(p, (root / p).read_bytes()) for p in sorted(present)
                                    if (root / p).is_file() and not (root / p).is_symlink() and p in files
                                    or p.endswith(('.desktop', '.xml', 'AppRun'))], hashes)
                report['appimage_run'] = smoke([str(args.appimage.resolve())], expected)
    print(json.dumps(report, indent=2, sort_keys=True))
    print('verify_linux_package: OK')


if __name__ == '__main__':
    main()
