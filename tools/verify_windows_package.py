#!/usr/bin/env python3
"""Plan 33b Task 5: verify a Windows portable zip (and optionally the installer).

Checks, without establishing gameplay fidelity:
- the zip layout ("Road Rash Native\\" with rr-game.exe, README.txt, portable.txt,
  licenses\\) and no other files;
- rr-game.exe: machine type (AMD64/ARM64), GUI subsystem, imports only on the
  system-DLL allowlist, embedded manifest (UTF-8 code page, PerMonitorV2,
  longPathAware), icon, version resource naming the expected commit;
- no game data: disc file extensions, the 341 reference SHA-256 hashes, ISO
  and AVI magic;
- with --run (Windows host, or --wine): `rr-game.exe --console --version` from
  the extracted zip reports a Release build of the current source, and portable
  mode put its log in the "data" folder beside the exe;
- with --installer: the setup is a PE with the same version; with
  --install-test (Windows only) a silent per-user install, Start-menu
  shortcut, identical exe, launch, silent uninstall that keeps game data, and
  a reinstall + `/PURGE=1` uninstall that removes it.
Requires pefile (python -m pip install pefile).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile

import pefile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from agent_common import ROOT, source_identity  # noqa: E402

PRODUCT = 'Road Rash Native'
MACHINES = {'x64': 0x8664, 'arm64': 0xAA64}
# System DLLs the packaged exe may import (static CRT: no VC++ redistributable).
# Reviewed against the MSVC x64/ARM64 and MinGW builds' actual imports
# (`--print-imports`); SDL3 loads XInput, DirectInput, D3D, Vulkan and
# WASAPI/DirectSound at run time, so they are not import-table entries.
ALLOWED_IMPORTS = {
    'kernel32.dll', 'user32.dll', 'gdi32.dll', 'shell32.dll', 'ole32.dll', 'oleaut32.dll', 'advapi32.dll',
    'imm32.dll', 'winmm.dll', 'version.dll', 'setupapi.dll', 'cfgmgr32.dll', 'hid.dll', 'ws2_32.dll',
    'iphlpapi.dll', 'crypt32.dll', 'bcrypt.dll', 'dwmapi.dll', 'uxtheme.dll', 'shlwapi.dll',
}
# MinGW links the system C runtime (msvcrt, or the Windows 10 UCRT through its
# api-ms-win-crt-* API sets); MSVC release builds use the static CRT instead.
MINGW_CRT = {'msvcrt.dll', 'ucrtbase.dll'}
DISC_SUFFIXES = {'.EXE', '.RSC', '.RRI', '.BOB', '.CRS', '.DAT', '.WAV', '.RRA', '.AVI', '.ISO', '.MID', '.SBK',
                 '.DLL', '.BIN', '.CUE', '.IMG'}
REQUIRED = {'rr-game.exe', 'README.txt', 'portable.txt'}
LICENSES = {'SDL-LICENSE.txt', 'stb-LICENSE.txt', 'libdatachannel-LICENSE.txt', 'libjuice-LICENSE.txt',
            'usrsctp-LICENSE.md', 'plog-LICENSE.txt', 'OpenSSL-LICENSE.txt'}


def fail(message):
    raise SystemExit(f'FAIL: {message}')


def reference_hashes():
    text = (ROOT / 'src/reference_hashes.inc').read_text()
    hashes = set(re.findall(r'"([0-9a-f]{64})"', text))
    if len(hashes) < 300:
        fail('reference hash table not found')
    return hashes


def imports(pe):
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],
                                           pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT'],
                                           pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
    names = {e.dll.decode().lower() for e in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', [])}
    names |= {e.dll.decode().lower() for e in getattr(pe, 'DIRECTORY_ENTRY_DELAY_IMPORT', [])}
    return names


def resources(pe, type_id):
    root = getattr(pe, 'DIRECTORY_ENTRY_RESOURCE', None)
    for entry in root.entries if root else []:
        if entry.id == type_id:
            for name in entry.directory.entries:
                for lang in name.directory.entries:
                    rva, size = lang.data.struct.OffsetToData, lang.data.struct.Size
                    yield pe.get_data(rva, size)


def version_strings(pe):
    if not hasattr(pe, 'DIRECTORY_ENTRY_RESOURCE'):
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
    strings = {}
    for group in getattr(pe, 'FileInfo', []) or []:
        for info in group:
            for table in getattr(info, 'StringTable', []):
                strings.update({k.decode(): v.decode() for k, v in table.entries.items()})
    return strings


def check_exe(path, arch, commit, allow_mingw_crt):
    # Parse from bytes: a memory-mapped file stays locked on Windows and would
    # block running and deleting it.
    pe = pefile.PE(data=path.read_bytes(), fast_load=True)
    if pe.FILE_HEADER.Machine != MACHINES[arch]:
        fail(f'{path.name}: machine 0x{pe.FILE_HEADER.Machine:04x}, expected {arch}')
    if pe.OPTIONAL_HEADER.Subsystem != pefile.SUBSYSTEM_TYPE['IMAGE_SUBSYSTEM_WINDOWS_GUI']:
        fail(f'{path.name}: not a GUI-subsystem executable')
    names = imports(pe)
    allowed = ALLOWED_IMPORTS | (MINGW_CRT if allow_mingw_crt else set())
    extra = sorted(n for n in names if n not in allowed
                   and not (allow_mingw_crt and n.startswith('api-ms-win-crt-')))
    if extra:
        fail(f'{path.name}: imports outside the system-DLL allowlist: {extra}')
    manifests = [m.decode('utf-8', 'replace') for m in resources(pe, 24)]
    if len(manifests) != 1:
        fail(f'{path.name}: expected one embedded manifest, found {len(manifests)}')
    for element, value in [('activeCodePage', 'UTF-8'), ('dpiAwareness', 'PerMonitorV2'), ('longPathAware', 'true')]:
        if not re.search(rf'<{element}\b[^>]*>\s*{value}\s*</{element}>', manifests[0]):
            fail(f'{path.name}: manifest lacks {element}={value}')
    if not list(resources(pe, 14)):
        fail(f'{path.name}: no icon group')
    strings = version_strings(pe)
    product = strings.get('ProductVersion', '')
    if commit[:12] not in product:
        fail(f'{path.name}: ProductVersion {product!r} does not name commit {commit[:12]}')
    if '+dirty' in product:
        fail(f'{path.name}: built from a modified tree ({product!r})')
    return dict(machine=arch, imports=sorted(names), product_version=product,
                file_version=strings.get('FileVersion'))


def check_no_game_data(files, hashes, suffixes=True):
    for name, data in files:
        if suffixes and Path(name).suffix.upper() in DISC_SUFFIXES and not name.endswith('/rr-game.exe'):
            fail(f'unexpected disc-type file: {name}')
        if hashlib.sha256(data).hexdigest() in hashes:
            fail(f'{name} matches an original disc file')
        if data[32769:32774] == b'CD001' or (data[:4] == b'RIFF' and data[8:12] == b'AVI '):
            fail(f'{name} carries disc/ISO or AVI data')


def run_version(exe, wine, expected):
    command = [wine, str(exe)] if wine else [str(exe)]
    env = dict(os.environ)
    env.pop('RR_PREF_OVERRIDE', None)  # portable mode must decide the location
    # Output goes to files, not pipes: under Wine a lingering wineserver can
    # hold a pipe open long after the game exited.
    with tempfile.TemporaryFile('w+') as stdout, tempfile.TemporaryFile('w+') as stderr:
        code = subprocess.run([*command, '--console', '--version'], stdout=stdout, stderr=stderr,
                              timeout=120, env=env).returncode
        stdout.seek(0)
        stderr.seek(0)
        text, errors = stdout.read(), stderr.read()
    if code != 0:
        fail(f'{exe} --version exited {code}: {errors.strip()[-400:]}')
    match = re.search(r'\{.*\}', text, re.S)
    if not match:
        fail(f'{exe} --version printed no JSON: {text[-400:]!r}')
    info = json.loads(match.group(0))
    for key in ['commit', 'source_sha256']:
        if info.get(key) != expected[key]:
            fail(f'{exe} {key} {info.get(key)!r} != current source {expected[key]!r}; rebuild')
    if info.get('configuration') != 'Release':
        fail(f'{exe} is a {info.get("configuration")} build, not Release')
    return info


def check_signature(path):
    """Authenticode signature valid and timestamped (Windows only)."""
    script = (f"$s = Get-AuthenticodeSignature -LiteralPath '{path}'; "
              "Write-Output \"$($s.Status) $([bool]$s.TimeStamperCertificate)\"")
    out = subprocess.run(['pwsh', '-NoProfile', '-Command', script], capture_output=True, text=True).stdout.split()
    if out != ['Valid', 'True']:
        fail(f'{path.name}: Authenticode signature {out}; expected Valid with a timestamp')


def wait_gone(path, seconds=60):
    deadline = time.time() + seconds
    while path.exists() and time.time() < deadline:
        time.sleep(0.5)
    return not path.exists()


def install_test(installer, zip_exe, expected):
    """Silent per-user install/uninstall on a Windows host (CI runner)."""
    appdata, localappdata = Path(os.environ['APPDATA']), Path(os.environ['LOCALAPPDATA'])
    roaming, local = appdata / 'RoadRashNative', localappdata / 'RoadRashNative'
    if (roaming.exists() or local.exists()) and os.environ.get('CI') != 'true':
        fail('install test deletes %APPDATA%/%LOCALAPPDATA%\\RoadRashNative; run it on a clean CI runner')
    shortcut = appdata / 'Microsoft/Windows/Start Menu/Programs' / f'{PRODUCT}.lnk'
    target = localappdata / 'Programs' / PRODUCT
    flags = ['/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART']
    report = {}
    for round_, purge in [(1, False), (2, True)]:
        work = Path(tempfile.mkdtemp(prefix='rr-install-'))
        subprocess.run([str(installer), *flags, f'/LOG={work / "install.log"}'], check=True, timeout=300)
        exe = target / 'rr-game.exe'
        if not exe.is_file():
            fail(f'installer did not create {exe} (expected the per-user default folder)')
        if (target / 'portable.txt').exists():
            fail('installed copy contains portable.txt')
        if exe.read_bytes() != zip_exe.read_bytes():
            fail('installed rr-game.exe differs from the zip')
        if not shortcut.is_file():
            fail(f'no Start-menu shortcut at {shortcut}')
        run_version(exe, None, expected)
        log = local / 'RoadRash' / 'rr-game.log'
        if not log.is_file():
            fail(f'installed game wrote no log at {log}')
        uninstallers = sorted(target.glob('unins*.exe'))
        if not uninstallers:
            fail(f'no uninstaller in {target}: {sorted(p.name for p in target.iterdir())}')
        args = [str(uninstallers[-1]), *flags] + (['/PURGE=1'] if purge else [])
        subprocess.run(args, check=True, timeout=300)
        # The uninstaller returns at once and finishes from a temporary copy
        # of itself; the install folder disappears only when it is done, so
        # wait for that before the next round reinstalls.
        if not wait_gone(target, 120) or not wait_gone(shortcut):
            fail(f'uninstall left the program folder or its shortcut behind: {target}')
        if purge and (not wait_gone(local) or not wait_gone(roaming)):
            fail('/PURGE=1 uninstall kept game data')
        if not purge and not log.is_file():
            fail('silent uninstall removed game data it must keep')
        report[f'round{round_}'] = 'purged game data' if purge else 'kept game data'
        shutil.rmtree(work, ignore_errors=True)
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('zip', type=Path)
    p.add_argument('--arch', choices=sorted(MACHINES), required=True)
    p.add_argument('--commit', help='expected commit (default: HEAD)')
    p.add_argument('--installer', type=Path)
    p.add_argument('--run', action='store_true', help='launch the zip exe (Windows host or --wine)')
    p.add_argument('--wine', help='Wine binary for --run on macOS/Linux (supplementary only)')
    p.add_argument('--install-test', action='store_true', help='silent install/uninstall (Windows CI only)')
    p.add_argument('--allow-mingw-crt', action='store_true', help='MinGW cross build check: allow msvcrt.dll')
    p.add_argument('--print-imports', action='store_true')
    p.add_argument('--require-signature', action='store_true',
                   help='exe and installer must carry a valid timestamped Authenticode signature (Windows)')
    args = p.parse_args()

    commit = args.commit or subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    expected_name = re.compile(rf'RoadRashNative-\d+\.\d+\.\d+(-g[0-9a-f]+)?-windows-{args.arch}\.zip$')
    if not expected_name.match(args.zip.name):
        fail(f'unexpected archive name {args.zip.name}')
    hashes = reference_hashes()
    report = {'archive': args.zip.name}
    with tempfile.TemporaryDirectory(prefix='rr-winpkg-') as tmp:
        tmp = Path(tmp)
        with zipfile.ZipFile(args.zip) as z:
            names = [n for n in z.namelist() if not n.endswith('/')]
            if any(not n.startswith(PRODUCT + '/') or '..' in n for n in names):
                fail(f'files outside "{PRODUCT}/": {names}')
            relative = {n[len(PRODUCT) + 1:] for n in names}
            expected_files = REQUIRED | {f'licenses/{x}' for x in LICENSES}
            if relative != expected_files:
                fail(f'zip contents differ: missing {sorted(expected_files - relative)}, '
                     f'extra {sorted(relative - expected_files)}')
            check_no_game_data([(n, z.read(n)) for n in names], hashes)
            z.extractall(tmp)
        folder = tmp / PRODUCT
        exe = folder / 'rr-game.exe'
        report['exe'] = check_exe(exe, args.arch, commit, args.allow_mingw_crt)
        if args.print_imports:
            print('\n'.join(report['exe']['imports']))
        if args.require_signature:
            if os.name != 'nt':
                fail('--require-signature runs only on Windows')
            check_signature(exe)
            if args.installer:
                check_signature(args.installer.resolve())
            report['signature'] = 'valid, timestamped'
        if args.run:
            if os.name != 'nt' and not args.wine:
                fail('--run needs a Windows host or --wine')
            expected = source_identity()
            if expected['commit'] != commit:
                fail(f'HEAD {expected["commit"]} != expected commit {commit}')
            report['run'] = run_version(exe, args.wine, expected)['commit']
            if not (folder / 'data' / 'rr-game.log').is_file():
                fail('portable mode did not put rr-game.log in the data folder beside the exe')
            report['portable_log'] = 'data/rr-game.log'
        if args.installer:
            setup = pefile.PE(data=args.installer.read_bytes(), fast_load=True)
            strings = version_strings(setup)
            version = re.match(r'RoadRashNative-(\d+\.\d+\.\d+)', args.installer.name)
            if not version or not strings.get('FileVersion', '').startswith(version.group(1)):
                fail(f'installer version {strings.get("FileVersion")!r} does not match its name')
            check_no_game_data([(args.installer.name, args.installer.read_bytes())], hashes, suffixes=False)
            report['installer'] = args.installer.name
            if args.install_test:
                if os.name != 'nt':
                    fail('--install-test runs only on Windows')
                report['install_test'] = install_test(args.installer.resolve(), exe, source_identity())
    print(json.dumps(report, indent=2))
    print(f'PASS: {args.zip.name} ({args.arch}), {len(report["exe"]["imports"])} system DLLs, no game data'
          + (', launched' if args.run else '') + (', install/uninstall' if args.install_test else ''))


if __name__ == '__main__':
    main()
