#!/usr/bin/env python3
"""Separate exact known diagnostics from strict, complete simulation acceptance."""
import argparse
import glob
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from agent_common import ROOT, atomic_json, sha256
from oracle_capture import reference_hash, validate_trace


def node_prefix():
    """Node.js that runs a .js (Emscripten) replay binary. Plan 23 Task 2:
    verify_sim.py executes the binary directly, which fails for wasm output.
    Prefers RR_WEB_NODE, then the pinned emsdk bundle, then PATH."""
    env = os.environ.get('RR_WEB_NODE')
    if env:
        return [env]
    candidates = sorted(glob.glob(str(ROOT / '.tools/emsdk/node/*/bin/node')))
    if candidates:
        return [candidates[-1]]
    found = shutil.which('node')
    if found:
        return [found]
    raise FileNotFoundError('no Node.js for the .js replay binary (set RR_WEB_NODE)')


def run_binary(binary, args, timeout):
    cmd = [str(binary)] + args
    if Path(binary).suffix == '.js':
        cmd = node_prefix() + cmd
    elif os.environ.get('RR_SIM_EMULATOR'):  # plan 33b: cross builds (e.g. MinGW .exe under Wine)
        cmd = [os.environ['RR_SIM_EMULATOR']] + cmd
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def classify(code, output, rule, digest):
    if code == 0:
        return 'pass' if output.splitlines() and output.splitlines()[-1].endswith(' ticks identical') else 'unexpected-failure'
    if code == 77:
        return 'skip'
    if (code == 1 and rule and rule['trace_sha256'] == digest
            and rule['divergence'] in output.splitlines()
            and rule['output_sha256'] == hashlib.sha256(output.encode()).hexdigest()):
        return 'known-divergence'
    return 'unexpected-failure'


def verify(binary, fixtures, assets, mode='acceptance', rules=None, timeout=120, driver='demo'):
    outcomes = []
    for fixture in fixtures:
        row = {'fixture': str(fixture), 'mode': mode}
        try:
            info = validate_trace(fixture, True, reference_hash())
            row.update(info)
            if mode == 'acceptance':
                receipt_path = Path(str(fixture) + '.receipt.json')
                receipt = json.loads(receipt_path.read_text())
                if receipt.get('trace_sha256') != info['trace_sha256'] or not receipt.get('complete'):
                    raise ValueError('missing/stale capture receipt')
                if info['control_branch'] != 'human':
                    raise ValueError('capture does not prove player.demo_hold=0 throughout')
                # Current binary hardwires solo to demo-hold. Require an explicit
                # replay capability marker before claiming human-control acceptance.
                capabilities = run_binary(binary, ['--capabilities'], timeout)
                if capabilities.returncode != 0 or 'human-controls-single-anchor' not in capabilities.stdout.splitlines():
                    raise ValueError('replay binary has not declared human-controls-single-anchor support')
            result = run_binary(binary, [f'--driver={driver}', str(fixture), str(assets)], timeout)
            output = result.stdout + result.stderr
            row.update({'exit_code': result.returncode, 'output': output})
            row['status'] = classify(result.returncode, output, (rules or {}).get(Path(fixture).name), info['trace_sha256'])
            if row['status'] == 'pass' and output.splitlines()[-1] != f'{info["frames"]} ticks identical':
                row['status'] = 'unexpected-failure'
            if mode == 'acceptance' and row['status'] != 'pass':
                row['status'] = 'unexpected-failure'
        except FileNotFoundError as e:
            row.update(status='skip' if mode == 'diagnostic' else 'unexpected-failure', error=str(e))
        except (ValueError, OSError, subprocess.TimeoutExpired, json.JSONDecodeError) as e:
            row.update(status='unexpected-failure', error=str(e))
        outcomes.append(row)
    counts = {key: sum(x['status'] == key for x in outcomes)
              for key in ['pass', 'skip', 'known-divergence', 'unexpected-failure']}
    ok = not counts['unexpected-failure'] and (mode == 'diagnostic' or counts['pass'] == len(fixtures) > 0)
    return {'mode': mode, 'acceptance_met': mode == 'acceptance' and ok, 'counts': counts, 'outcomes': outcomes}, 0 if ok else 1


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mode', choices=['diagnostic', 'acceptance'], default='acceptance')
    p.add_argument('--driver', default=None,
                   help='driver axis for the replay binary (human|demo); default is '
                        'human for acceptance (which requires a human capture) and '
                        'demo for diagnostic')
    p.add_argument('--binary', type=Path, default=ROOT / 'build/oracle-sim-test')
    p.add_argument('--assets', type=Path, default=ROOT.parent / 'cdrom/ROADRASH')
    p.add_argument('--fixture', type=Path, action='append')
    p.add_argument('--rules', type=Path, default=ROOT / 'tools/sim_known_divergences.json')
    p.add_argument('--report', type=Path)
    args = p.parse_args()
    directory = Path(os.environ.get('RR_ORACLE_DIR', ROOT / 'analysis/oracle'))
    if args.mode == 'diagnostic':
        fixtures = args.fixture or [directory / 'sim-napa-solo.tsv']
        driver = args.driver or 'demo'
    else:
        # Strict acceptance needs a receipted, complete human capture and the
        # 0x00434660 player-control branch, so it replays as driver=human.
        fixtures = args.fixture or [directory / 'sched-human-cov2-20261017.tsv']
        driver = args.driver or 'human'
    rules = json.loads(args.rules.read_text())
    report, code = verify(args.binary.resolve(), fixtures, args.assets.resolve(), args.mode, rules, driver=driver)
    if args.report:
        atomic_json(args.report, report)
    print(json.dumps(report, indent=2))
    if args.mode == 'diagnostic' and report['counts']['skip'] == len(fixtures):
        return 77
    return code


if __name__ == '__main__':
    raise SystemExit(main())
