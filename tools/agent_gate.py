#!/usr/bin/env python3
"""Logged verification profiles with source identity and separate coverage outcomes."""
import argparse
import json
import os
from pathlib import Path
import platform
import sys
import time
import uuid
import xml.etree.ElementTree as ET
import zipfile
from agent_common import ROOT, ResourceClaims, atomic_json, run_logged, source_identity


def ctest_counts(path):
    root = ET.parse(path).getroot()
    counts = dict(passed=0, skipped=0, failed=0)
    for test in root.iter('testcase'):
        if test.find('skipped') is not None:
            counts['skipped'] += 1
        elif test.find('failure') is not None or test.find('error') is not None:
            counts['failed'] += 1
        else:
            counts['passed'] += 1
    return counts


def run_gate(args):
    build = args.build_dir.resolve()
    output = (args.output or ROOT / 'analysis/agent-gates' / (args.profile + '-' + uuid.uuid4().hex)).resolve()
    output.mkdir(parents=True, exist_ok=False)
    identity = source_identity()
    report = {'profile': args.profile, 'source': identity, 'build_directory': str(build),
              'configuration': 'Debug', 'started_at': time.time(), 'steps': [], 'coverage': {}, 'status': 'running'}
    claims = ResourceClaims()
    resources = [build]
    if args.profile.startswith('android'):
        resources = [ROOT / 'android']
    elif args.profile == 'package':
        resources = [build]
    env = dict(os.environ)
    homebrew_java = Path('/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home')
    if homebrew_java.is_dir():  # plan 33 A9: only where it exists (other hosts use their JAVA_HOME)
        env.setdefault('JAVA_HOME', str(homebrew_java))
    try:
        with claims.hold(args.session, resources):
            commands = []
            if args.profile in {'native', 'sanitizers', 'oracle'}:
                configure = ['cmake', '-S', str(ROOT), '-B', str(build), '-DCMAKE_BUILD_TYPE=Debug']
                configure += ['-DRR_BUNDLE_SDL=OFF']
                if args.profile == 'sanitizers':
                    flags = '-fsanitize=address,undefined -fno-omit-frame-pointer'
                    configure += ['-DCMAKE_CXX_FLAGS=' + flags, '-DCMAKE_C_FLAGS=' + flags,
                                  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
                commands.append(configure)
                command = ['cmake', '--build', str(build), '--parallel', str(args.jobs)]
                if args.profile == 'oracle':
                    command += ['--target', 'oracle-sim-test']
                commands.append(command)
                if args.profile == 'oracle':
                    commands.append([sys.executable, ROOT / 'tools/verify_sim.py', '--binary', build / 'oracle-sim-test',
                                     '--mode', 'acceptance', '--report', output / 'simulation.json'])
                else:
                    test = ['ctest', '--test-dir', str(build), '--output-on-failure', '--output-junit', str(output / 'ctest.xml')]
                    if args.tests:
                        test += ['-R', args.tests]
                    commands.append(test)
                    assets = ROOT.parent / 'cdrom/ROADRASH'
                    if args.profile == 'native' and not args.tests and assets.is_dir():
                        commands.append([build / 'rr-viewer', '--assets', assets, '--smoke'])
                        commands.append([build / 'rr-course', '--exe', assets / 'ROADRASH.EXE',
                                         *sorted((assets / 'DATA/COURSES').glob('*.CRS'))])
                        report['coverage']['imported_assets'] = 'viewer smoke and course sweep'
            elif args.profile.startswith('android'):
                release = args.profile == 'android-release'
                report['configuration'] = 'RelWithDebInfo' if release else 'Debug'
                command = [ROOT / 'android/gradlew', '-p', ROOT / 'android', 'assembleRelease' if release else 'assembleDebug']
                if release:
                    command += ['-PreleaseDebugSigning=true']
                    report['signing'] = 'local debug key; not owner-signed release'
                commands.append(command)
            elif args.profile == 'windows-package':
                # Plan 33b Task 5. On Windows: the MSVC preset's Release build,
                # its CTest, then package and launch. Elsewhere: an existing
                # MinGW cross build (--build-dir), packaged and checked; that
                # is supplementary evidence, never Windows acceptance.
                report['configuration'] = 'Release'
                sys.path.insert(0, str(ROOT / 'tools'))
                import build_windows_package
                arch = 'arm64' if platform.machine().lower() in {'arm64', 'aarch64'} and os.name == 'nt' else 'x64'
                if os.name == 'nt':
                    preset = 'windows-msvc-arm64' if arch == 'arm64' else 'windows-msvc'
                    build = ROOT / f'build-{preset}'
                    report['build_directory'] = str(build)
                    commands += [['cmake', '--preset', preset],
                                 ['cmake', '--build', str(build), '--config', 'Release', '--parallel', str(args.jobs)],
                                 ['ctest', '--test-dir', str(build), '-C', 'Release', '--output-on-failure',
                                  '-j', '4', '--output-junit', str(output / 'ctest.xml')]]
                    verify_flags = ['--run']
                    report['coverage']['install_test'] = 'CI only (it removes per-user game data)'
                else:
                    systems = [p.read_text() for p in build.glob('CMakeFiles/*/CMakeSystem.cmake')]
                    if not any('set(CMAKE_SYSTEM_NAME "Windows")' in text for text in systems):
                        raise RuntimeError('windows-package off Windows needs --build-dir of a MinGW cross build')
                    commands.append(['cmake', '--build', str(build), '--target', 'rr-game', '--parallel', str(args.jobs)])
                    verify_flags = ['--allow-mingw-crt'] + (['--run', '--wine', env['RR_WINE']] if env.get('RR_WINE') else [])
                    report['coverage']['windows'] = 'cross-built and checked off Windows; not Windows acceptance'
                version, _ = build_windows_package.package_version()
                dist = output / 'dist'
                commands.append([sys.executable, ROOT / 'tools/build_windows_package.py', '--build-dir', build,
                                 '--arch', arch, '--out', dist])
                commands.append([sys.executable, ROOT / 'tools/verify_windows_package.py',
                                 dist / f'RoadRashNative-{version}-windows-{arch}.zip', '--arch', arch, *verify_flags])
                report['artifact'] = str(dist / f'RoadRashNative-{version}-windows-{arch}.zip')
            elif args.profile == 'package':
                env['RR_PACKAGE_BUILD_DIR'] = str(build)
                commands.append([ROOT / 'tools/build_macos_app.sh'])
                report['configuration'] = 'Release'
            for i, command in enumerate(commands):
                step = run_logged(command, output / f'{i:02d}.log', env=env)
                report['steps'].append(step)
                atomic_json(output / 'receipt.json', report)
                print(f'{args.profile}: step {i + 1}/{len(commands)} exit={step["exit_code"]}; log={step["log"]}', flush=True)
                if step['exit_code'] != 0:
                    raise RuntimeError(f'command failed ({step["exit_code"]})')
            if (output / 'ctest.xml').exists():
                report['coverage']['ctest'] = ctest_counts(output / 'ctest.xml')
                if not sum(report['coverage']['ctest'].values()):
                    raise RuntimeError('CTest ran no tests')
                sim = build / 'oracle-diagnostic.json'
                if sim.exists() and sim.stat().st_mtime >= report['started_at']:
                    report['coverage']['simulation_diagnostic'] = json.loads(sim.read_text())
            if args.profile.startswith('android'):
                variant = 'release' if args.profile.endswith('release') else 'debug'
                apk = ROOT / f'android/app/build/outputs/apk/{variant}/app-{variant}.apk'
                with zipfile.ZipFile(apk) as z:
                    if any(x.startswith('assets/game/') for x in z.namelist()):
                        raise RuntimeError('commercial game data packaged in APK')
                    # Plan 20 Task 1 Step 4: upscaled cache entries are generated
                    # locally and must never ship in the APK.
                    if any('upscaled/' in x or x.endswith('.rrup') for x in z.namelist()):
                        raise RuntimeError('upscaled cache entry packaged in APK')
                report['artifact'] = str(apk)
                report['coverage']['hardware'] = 'not run'
                manifests = ROOT.glob(f'android/app/.cxx/{report["configuration"]}/*/*/build-info.json')
                report['binary_provenance'] = [json.loads(x.read_text()) for x in manifests]
            elif args.profile == 'package':
                report['artifact'] = str(build / 'Road Rash Native.zip')
            if (build / 'build-info.json').exists() and not args.profile.startswith('android'):
                report['binary_provenance'] = json.loads((build / 'build-info.json').read_text())
            after = source_identity()
            if any(after[k] != identity[k] for k in ['commit', 'source_sha256']):
                raise RuntimeError('source changed during gate; receipt is not valid for a stable source snapshot')
            report['status'] = 'passed'
    except (RuntimeError, OSError, ValueError) as e:
        report.update(status='failed', error=str(e))
    finally:
        xml = build / 'package-ctest.xml' if args.profile == 'package' else output / 'ctest.xml'
        if xml.exists() and xml.stat().st_mtime >= report['started_at']:
            report['coverage']['ctest'] = ctest_counts(xml)
            sim = build / 'oracle-diagnostic.json'
            if sim.exists() and sim.stat().st_mtime >= report['started_at'] and args.profile != 'oracle':
                report['coverage']['simulation_diagnostic'] = json.loads(sim.read_text())
        atomic_json(output / 'receipt.json', report)
    print(json.dumps({'status': report['status'], 'receipt': str(output / 'receipt.json'), 'coverage': report['coverage']}, indent=2))
    return 0 if report['status'] == 'passed' else 1


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('profile', choices=['native', 'sanitizers', 'oracle', 'android-debug', 'android-release', 'package',
                                       'windows-package'])
    p.add_argument('--build-dir', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    p.add_argument('--tests', help='CTest regex for focused iteration')
    p.add_argument('--session', default='agent-gate')
    args = p.parse_args()
    args.build_dir = args.build_dir or ROOT / ('build-agent-' + args.profile)
    raise SystemExit(run_gate(args))


if __name__ == '__main__':
    main()
