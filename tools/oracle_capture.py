#!/usr/bin/env python3
"""Validate or capture oracle traces; failed captures never replace accepted output."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import tempfile
import time
import uuid
from agent_common import ROOT, ResourceClaims, atomic_json, sha256


def validate_trace(path, simulation=False, expected_exe=None, expected_frames=None):
    """Validate the complete format, sequence and (for simulation) required tick data."""
    lines = Path(path).read_text().splitlines()
    if not lines or lines[0] != '# rr-oracle 1':
        raise ValueError('bad oracle magic')
    header, frames, current, ended = {}, [], None, False
    for line in lines[1:]:
        if not line or line.startswith('#'):
            if any(x in line.lower() for x in ['patch verification failed', 'patch installation failed',
                                               'ordinal 2 not found', 'invalid rr_oracle']):
                raise ValueError('oracle instrumentation failure: ' + line)
            continue
        if ended:
            raise ValueError('data after end marker')
        if line == 'end':
            ended = True
            continue
        if '\t' not in line:
            raise ValueError('missing tab separator')
        key, value = line.split('\t', 1)
        if key in {'exe', 'scenario'}:
            if current is not None or key in header or not value:
                raise ValueError('invalid or duplicate header')
            header[key] = value
        elif key == 'frame':
            if set(header) != {'exe', 'scenario'} or value != str(len(frames)):
                raise ValueError('invalid frame sequence/header')
            current = {}
            frames.append(current)
        else:
            if current is None or not key or key in current or not re.fullmatch(r'-?\d+', value):
                raise ValueError('invalid/duplicate integer field')
            n = int(value)
            if not -(2**63) <= n < 2**63:
                raise ValueError('integer outside int64')
            current[key] = n
    if not ended or not frames:
        raise ValueError('truncated/empty capture: require frames and end marker')
    if not re.fullmatch(r'[0-9a-f]{64}', header.get('exe', '')):
        raise ValueError('invalid executable hash')
    if expected_exe and header['exe'] != expected_exe:
        raise ValueError('different reference executable')
    if expected_frames is not None and len(frames) != expected_frames:
        raise ValueError(f'frame count {len(frames)} != expected {expected_frames}')
    tick0 = frames[0].get('tick')
    for i, frame in enumerate(frames):
        if simulation and not {'tick', 'rng', 'controls', 'rider.0.position', 'rider.0.speed'} <= frame.keys():
            raise ValueError(f'simulation frame {i} lacks required fields')
        if tick0 is not None and frame.get('tick') != tick0 + i:
            raise ValueError(f'tick not contiguous at frame {i}')
    branches = {f.get('player.demo_hold') for f in frames}
    # Evidence comes from instrumented values in the trace, never an env flag/name.
    branch = 'human' if branches == {0} else 'demo' if branches == {1} else 'unverified'
    return {'schema': 'rr-oracle 1', 'exe_sha256': header['exe'],
            'scenario': header['scenario'], 'frames': len(frames), 'tick_start': tick0,
            'tick_end': frames[-1].get('tick'), 'seed': frames[0].get('seed'),
            'control_branch': branch, 'trace_sha256': sha256(path), 'complete': True}


def reference_hash():
    inventory = json.loads((ROOT / 'analysis/inventory.json').read_text())
    return next(x['sha256'] for x in inventory['files'] if x['path'] == 'ROADRASH.EXE')


def publish_capture(trace, output, receipt, companions=()):
    """Write unique immutable run, then atomically switch the accepted manifest pointer."""
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    runs = output.parent / (output.stem + '.captures')
    runs.mkdir(exist_ok=True)
    run = runs / uuid.uuid4().hex
    run.mkdir()
    dest = run / output.name
    shutil.copy2(trace, dest)
    for path in companions:
        shutil.copy2(path, run / Path(path).name)
    receipt = dict(receipt, trace=str(dest.resolve()), artifacts=[str(x.resolve()) for x in run.iterdir()])
    atomic_json(run / 'receipt.json', receipt)
    # Legacy consumers retain the flat path. Companions are only copied after validation.
    for path in run.iterdir():
        if path.name == 'receipt.json':
            continue
        fd, name = tempfile.mkstemp(dir=output.parent, prefix='.' + path.name + '.')
        os.close(fd)
        try:
            shutil.copy2(path, name)
            os.replace(name, output.parent / path.name)
        finally:
            if os.path.exists(name):
                os.unlink(name)
    atomic_json(str(output) + '.receipt.json', receipt)
    return receipt


def capture(args):
    workspace = args.workspace.resolve()
    prefix = args.prefix.resolve() if args.prefix else workspace / 'prefix'
    install = args.install.resolve() if args.install else prefix / 'drive_c/ElectronicArts/RoadRashOracle'
    output = args.output.resolve() if args.output else ROOT / 'analysis/oracle' / (args.scenario + '.tsv')
    wine = args.wine or Path.home() / 'Applications/Wine Staging.app/Contents/Resources/wine/bin/wine'
    assets = workspace / 'cdrom/ROADRASH'
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.scenario):
        raise ValueError('invalid scenario name')
    if args.timeout <= 0:
        raise ValueError('timeout must be positive')
    if not assets.is_dir() or not install.is_dir() or not Path(wine).is_file():
        raise ValueError('missing mounted/extracted assets, oracle install or Wine; run doctor/install first')
    expected = reference_hash()
    if sha256(assets / 'ROADRASH.EXE') != expected or sha256(install / 'ROADRASH.EXE') != expected:
        raise ValueError('reference EXE hash mismatch')
    dll = install / 'AWEMAN32.DLL'
    tag = 'capture-' + uuid.uuid4().hex
    trace = prefix / 'drive_c' / ('oracle-' + tag + '.tsv')
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG=os.environ.get('WINEDEBUG', '-all'),
               WINEDLLOVERRIDES='ddraw=n,b;aweman32=n', RR_ORACLE_OUT='C:\\' + trace.name,
               RR_ORACLE_SCENARIO=tag)
    record = bool(env.get('RR_ORACLE_RECORD'))
    if record:
        env['RR_ORACLE_RECORD'] = 'C:\\' + tag + '.controls'
    claims = ResourceClaims()
    with claims.hold(args.session, [prefix, install, output.parent]):
        dll_hash = sha256(dll)
        mapping = prefix / 'dosdevices/d:'
        mapping.parent.mkdir(parents=True, exist_ok=True)
        if mapping.exists() or mapping.is_symlink():
            if not mapping.is_symlink() or mapping.resolve() != assets.parent:
                raise ValueError('D: mapping differs; configure it explicitly before capture')
        else:
            mapping.symlink_to(assets.parent)
        input_controls = None
        if env.get('RR_ORACLE_INPUT'):
            value = env['RR_ORACLE_INPUT']
            match = re.fullmatch(r'([CcDd]):\\(.+)', value)
            if not match:
                raise ValueError('control input must be a resolvable C:\\ or D:\\ path for provenance')
            input_controls = (prefix / 'drive_c' if match[1].lower() == 'c' else assets.parent) / match[2].replace('\\', '/')
            if not input_controls.is_file():
                raise ValueError('control input file missing')
        input_hash = sha256(input_controls) if input_controls else None
        with tempfile.TemporaryDirectory(prefix='rr-oracle-') as tmp:
            log = Path(tmp) / 'wine.log'
            started = time.time()
            with log.open('w') as f:
                group = ({'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == 'nt'
                         else {'start_new_session': True})
                proc = subprocess.Popen([str(wine), str(install / 'ROADRASH.EXE')], cwd=install, env=env,
                                        stdout=f, stderr=subprocess.STDOUT, **group)
                try:
                    code = proc.wait(timeout=args.timeout)
                except (subprocess.TimeoutExpired, KeyboardInterrupt):
                    # Only the process group created for this capture is stopped.
                    if os.name == 'nt':
                        proc.terminate()
                    else:
                        os.killpg(proc.pid, signal.SIGTERM)
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill() if os.name == 'nt' else os.killpg(proc.pid, signal.SIGKILL)
                        proc.wait()
                    code = 124
            failed_dir = output.parent / 'failed-captures' / tag
            try:
                if code != 0:
                    raise ValueError(f'Wine exited {code}; capture rejected')
                if sha256(dll) != dll_hash:
                    raise ValueError('oracle DLL changed during capture')
                if input_controls and sha256(input_controls) != input_hash:
                    raise ValueError('control input changed during capture')
                info = validate_trace(trace, bool(env.get('RR_ORACLE_SIM')) and env['RR_ORACLE_SIM'] != '0',
                                      expected, int(env['RR_ORACLE_FRAMES']) if env.get('RR_ORACLE_FRAMES') else None)
                companions = list((prefix / 'drive_c').glob(tag + '-*.raw'))
                controls = prefix / 'drive_c' / (tag + '.controls')
                if record and not controls.is_file():
                    raise ValueError('recorded controls missing')
                if controls.is_file():
                    companions.append(controls)
                info.update({'requested_scenario': args.scenario, 'proxy_sha256': dll_hash,
                             'process_exit_code': code, 'duration_seconds': time.time() - started,
                             'controls_sha256': sha256(controls) if controls.exists() else input_hash,
                             'environment': {k: v for k, v in env.items() if k.startswith('RR_ORACLE_')}})
                # Raw companions retain the DLL tag in immutable run; flat replay names need output stem.
                staged = Path(tmp) / 'artifacts'
                staged.mkdir()
                renamed = []
                for path in companions:
                    name = path.name.replace(tag, output.stem, 1)
                    target = staged / name
                    shutil.copy2(path, target)
                    renamed.append(target)
                receipt = publish_capture(trace, output, info, renamed)
                shutil.copy2(log, Path(receipt['trace']).parent / 'wine.log')
                print(json.dumps(receipt, indent=2))
            except Exception as e:
                failed_dir.mkdir(parents=True, exist_ok=True)
                shutil.copy2(log, failed_dir / 'wine.log')
                if trace.exists():
                    shutil.copy2(trace, failed_dir / 'partial.tsv')
                atomic_json(failed_dir / 'failure.json', {'error': str(e), 'exit_code': code})
                raise


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='action', required=True)
    v = sub.add_parser('validate')
    v.add_argument('trace', type=Path)
    v.add_argument('--simulation', action='store_true')
    v.add_argument('--frames', type=int)
    c = sub.add_parser('capture')
    c.add_argument('scenario')
    c.add_argument('--workspace', type=Path, default=ROOT.parent)
    c.add_argument('--prefix', type=Path)
    c.add_argument('--install', type=Path)
    c.add_argument('--wine', type=Path)
    c.add_argument('--output', type=Path)
    c.add_argument('--timeout', type=float, default=600)
    c.add_argument('--session', default='oracle-capture')
    args = p.parse_args()
    try:
        if args.action == 'validate':
            print(json.dumps(validate_trace(args.trace, args.simulation, reference_hash(), args.frames), indent=2))
        else:
            capture(args)
    except (OSError, ValueError, RuntimeError) as e:
        p.exit(1, str(e) + '\n')


if __name__ == '__main__':
    main()
