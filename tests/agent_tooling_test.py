#!/usr/bin/env python3
"""Regression checks for agent tooling failure contracts (synthetic traces only)."""
import argparse
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'tools'))
from agent_common import ResourceClaims, atomic_json, run_logged, source_identity
from agent_gate import ctest_counts
from agent_status import render
from build_info import identity, write_header
from oracle_capture import capture, validate_trace
from verify_sim import classify, verify

EXE = 'a' * 64


def trace(branch=None):
    result = '# rr-oracle 1\nexe\t' + EXE + '\nscenario\tsynthetic\n'
    for i in range(2):
        result += (f'frame\t{i}\ntick\t{i + 4}\nrng\t1\ncontrols\t1\n'
                   f'rider.0.position\t{i}\nrider.0.speed\t1\n')
        if branch is not None:
            result += f'player.demo_hold\t{branch}\n'
    return result + 'end\n'


class Tooling(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.env = patch.dict(os.environ, {'RR_AGENT_STATE': str(self.root / 'resources')})
        self.env.start()

    def tearDown(self):
        self.env.stop()
        self.tmp.cleanup()

    def write(self, name, text):
        p = self.root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
        return p

    def test_capture_sequence_and_branch_evidence(self):
        p = self.write('good.tsv', trace(0))
        info = validate_trace(p, simulation=True, expected_exe=EXE, expected_frames=2)
        self.assertEqual(info['control_branch'], 'human')
        p.write_text(trace())
        self.assertEqual(validate_trace(p, True)['control_branch'], 'unverified')
        # The name cannot substitute for an instrumented branch.
        p.write_text(trace().replace('synthetic', 'sim-napa-solo-human'))
        self.assertEqual(validate_trace(p, True)['control_branch'], 'unverified')

    def test_incomplete_duplicate_noncontiguous_and_wrong_exe_rejected(self):
        p = self.write('bad.tsv', '')
        variants = [trace().replace('end\n', ''), trace().replace('tick\t5', 'tick\t6'),
                    trace() + 'tick\t9\n', trace().replace('rng\t1', 'rng\t1\nrng\t1', 1),
                    trace().replace('frame\t1', 'frame\t2'), trace().replace('controls\t1\n', ''),
                    trace().replace('rider.0.speed\t1', 'rider.0.speed\t18446744073709551616')]
        for text in variants:
            with self.subTest(text=text[-100:]):
                p.write_text(text)
                with self.assertRaises(ValueError):
                    validate_trace(p, True)
        p.write_text(trace())
        with self.assertRaises(ValueError):
            validate_trace(p, True, 'b' * 64)
        with self.assertRaises(ValueError):
            validate_trace(p, True, expected_frames=3)

    def test_resource_overlap_live_owner_and_explicit_release(self):
        claims = ResourceClaims()
        token = claims.acquire('first', [self.root / 'build'])
        with self.assertRaises(RuntimeError):
            claims.acquire('second', [self.root / 'build/object'])
        with self.assertRaises(RuntimeError):
            claims.acquire('second', [self.root])
        with self.assertRaises(RuntimeError):
            claims.release(token, 'second')
        with self.assertRaises(RuntimeError):
            claims.release(token, reap=True)
        other = claims.acquire('second', [self.root / 'different-build'])
        claims.release(other, 'second')
        claims.release(token, 'first')
        self.assertEqual(claims.list(), [])

    def test_claim_exception_releases_only_own_resources(self):
        claims = ResourceClaims()
        token = claims.acquire('unrelated', [self.root / 'external'])
        with self.assertRaises(ValueError):
            with claims.hold('temporary', [self.root / 'build']):
                raise ValueError('failure')
        self.assertEqual([r['token'] for r in claims.list()], [token])
        claims.release(token, 'unrelated')

    def test_dead_owner_reaping_requires_pid_check(self):
        claims = ResourceClaims()
        token = claims.acquire('owner', [self.root / 'build'])
        with patch('agent_common.pid_alive', return_value=False):
            claims.release(token, reap=True)
        self.assertEqual(claims.list(), [])

    def test_subprocess_failure_preserved_and_log_complete(self):
        log = self.root / 'command.log'
        row = run_logged([sys.executable, '-c', 'print("failure evidence"); raise SystemExit(9)'], log, self.root)
        self.assertEqual(row['exit_code'], 9)
        self.assertEqual(log.read_text(), 'failure evidence\n')
        row = run_logged([self.root / 'missing-command'], log, self.root)
        self.assertEqual(row['exit_code'], 127)

    def test_exact_diagnostic_not_any_failure(self):
        output = 'first divergence at frame 1 (tick 5): rng original 1 native 2\nrng history (last 1 ticks, golden vs native): (1,2)\n'
        rule = {'trace_sha256': EXE, 'divergence': output.splitlines()[0],
                'output_sha256': hashlib.sha256(output.encode()).hexdigest()}
        self.assertEqual(classify(1, output, rule, EXE), 'known-divergence')
        for code, text, digest in [(1, output + 'unrelated error\n', EXE),
                                   (1, output.replace('native 2', 'native 3'), EXE),
                                   (-6, output, EXE), (1, output, 'b' * 64), (0, '', EXE)]:
            self.assertEqual(classify(code, text, rule, digest), 'unexpected-failure')

    def test_acceptance_missing_demo_and_stale_receipt_fail(self):
        fixture = self.write('human.tsv', trace(1))
        with patch('verify_sim.reference_hash', return_value=EXE):
            report, code = verify(self.root / 'binary', [self.root / 'missing.tsv'], self.root)
            self.assertEqual(code, 1)
            atomic_json(str(fixture) + '.receipt.json', dict(validate_trace(fixture, True), complete=True))
            report, code = verify(self.root / 'binary', [fixture], self.root)
            self.assertEqual(code, 1)
            self.assertIn('demo_hold', report['outcomes'][0]['error'])
            fixture.write_text(trace(0))
            report, code = verify(self.root / 'binary', [fixture], self.root)
            self.assertEqual(code, 1)
            self.assertIn('stale', report['outcomes'][0]['error'])

    def test_strict_acceptance_requires_replay_capability_and_match(self):
        fixture = self.write('human.tsv', trace(0))
        atomic_json(str(fixture) + '.receipt.json', validate_trace(fixture, True))
        def fake_replay(body):
            # A shebang script is the replay binary on POSIX; Windows runs a
            # .cmd wrapper around the same script (plan 33b).
            script = self.write('replay.py', '#!' + sys.executable + '\n' + body)
            script.chmod(0o755)
            if os.name != 'nt':
                return script
            return self.write('replay.cmd', f'@"{sys.executable}" "{script}" %*\r\n')
        binary = fake_replay('import sys\n'
                             'print("human-controls-single-anchor" if "--capabilities" in sys.argv else "2 ticks identical")\n')
        with patch('verify_sim.reference_hash', return_value=EXE):
            report, code = verify(binary, [fixture], self.root)
            self.assertEqual(code, 0)
            self.assertTrue(report['acceptance_met'])
            fake_replay('print("not a real replay")\n')
            report, code = verify(binary, [fixture], self.root)
            self.assertEqual(code, 1)

    def setup_fake_capture(self, body):
        workspace = self.root / 'workspace'
        install = workspace / 'prefix/drive_c/ElectronicArts/RoadRashOracle'
        install.mkdir(parents=True)
        (workspace / 'cdrom/ROADRASH').mkdir(parents=True)
        (workspace / 'cdrom/ROADRASH/ROADRASH.EXE').write_bytes(b'reference')
        (install / 'ROADRASH.EXE').write_bytes(b'reference')
        (install / 'AWEMAN32.DLL').write_bytes(b'proxy')
        digest = hashlib.sha256(b'reference').hexdigest()
        good = trace().replace(EXE, digest)
        wine = self.write('wine', '#!' + sys.executable + '\nimport os,time\nfrom pathlib import Path\n'
                          'out=Path(os.environ["WINEPREFIX"])/"drive_c"/os.environ["RR_ORACLE_OUT"].split("\\\\")[-1]\n' +
                          body.replace('GOOD', repr(good)))
        wine.chmod(0o755)
        args = argparse.Namespace(workspace=workspace, prefix=None, install=None,
                                  output=self.root / 'accepted/scenario.tsv', wine=wine,
                                  scenario='scenario', session='test-capture', timeout=2)
        return args, digest

    def test_failed_capture_preserves_accepted_and_releases_resources(self):
        for body in ['out.write_text("# rr-oracle 1\\n")\n',
                     'out.write_text(GOOD)\nraise SystemExit(8)\n']:
            # Independent fake prefix per case.
            with self.subTest(body=body), tempfile.TemporaryDirectory() as tmp:
                saved = self.root
                self.root = Path(tmp)
                args, digest = self.setup_fake_capture(body)
                args.output.parent.mkdir()
                args.output.write_text('previous accepted')
                try:
                    with patch('oracle_capture.reference_hash', return_value=digest), \
                         patch.dict(os.environ, {'RR_ORACLE_SIM': '1', 'RR_ORACLE_FRAMES': '2'}):
                        with self.assertRaises(ValueError):
                            capture(args)
                    self.assertEqual(args.output.read_text(), 'previous accepted')
                    self.assertEqual(ResourceClaims().list(), [])
                    self.assertTrue(list((args.output.parent / 'failed-captures').glob('*/failure.json')))
                finally:
                    self.root = saved

    def test_capture_publish_receipt_and_immutable_run(self):
        args, digest = self.setup_fake_capture('out.write_text(GOOD)\n')
        with patch('oracle_capture.reference_hash', return_value=digest), \
             patch.dict(os.environ, {'RR_ORACLE_SIM': '1', 'RR_ORACLE_FRAMES': '2'}), \
             contextlib.redirect_stdout(io.StringIO()):
            capture(args)
        receipt = json.loads(Path(str(args.output) + '.receipt.json').read_text())
        self.assertTrue(receipt['complete'])
        self.assertEqual(receipt['proxy_sha256'], hashlib.sha256(b'proxy').hexdigest())
        self.assertEqual(receipt['trace_sha256'], hashlib.sha256(args.output.read_bytes()).hexdigest())
        self.assertTrue(Path(receipt['trace']).exists())

    def test_source_identity_detects_untracked_changes_and_config(self):
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        subprocess.run(['git', '-C', str(self.root), '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                        'commit', '--allow-empty', '-qm', 'initial'], check=True)
        source = self.write('src/sample.cpp', 'int x;\n')
        first = source_identity(self.root)
        self.write('docs/note.md', 'documentation only')
        self.assertEqual(first['source_sha256'], source_identity(self.root)['source_sha256'])
        source.write_text('int y;\n')
        self.assertNotEqual(first['source_sha256'], source_identity(self.root)['source_sha256'])
        header = self.root / 'generated/info.hpp'
        write_header(header, first)
        previous = header.stat().st_mtime_ns
        write_header(header, first)
        self.assertEqual(previous, header.stat().st_mtime_ns)

    def test_ctest_coverage_keeps_skips_and_failures_separate(self):
        xml = self.write('tests.xml', '<testsuite><testcase name="ok"/><testcase name="skip"><skipped/></testcase>'
                         '<testcase name="failure"><failure/></testcase></testsuite>')
        self.assertEqual(ctest_counts(xml), {'passed': 1, 'skipped': 1, 'failed': 1})

    def test_configuration_fingerprint_distinguishes_sanitizers(self):
        cache = self.write('cache.txt', 'CMAKE_CXX_FLAGS:STRING=\nRR_BUNDLE_SDL:BOOL=OFF\n')
        with patch('build_info.source_identity', return_value={'commit': 'test', 'source_sha256': EXE}):
            normal = identity('Debug', 'test-compiler', cache)
            cache.write_text('CMAKE_CXX_FLAGS:STRING=-fsanitize=address,undefined\nRR_BUNDLE_SDL:BOOL=OFF\n')
            sanitizer = identity('Debug', 'test-compiler', cache)
        self.assertNotEqual(normal['configuration_sha256'], sanitizer['configuration_sha256'])

    @staticmethod
    def unlabeled_tests(payload):
        # Plan 30 Task 1: every test needs a label from {slow, sim, disc, unit}.
        # ctest --show-only=json-v1 lists properties as [{name, value}].
        bad = []
        for t in payload.get('tests', []):
            labels = set()
            for prop in t.get('properties', []):
                if prop.get('name') == 'LABELS':
                    v = prop.get('value', [])
                    labels.update(v if isinstance(v, list) else str(v).split(';'))
            if not (labels & {'slow', 'sim', 'disc', 'unit'}):
                bad.append(t.get('name', '?'))
        return bad

    def test_ctest_labels_cover_every_test(self):
        build = os.environ.get('RR_BUILD_DIR')
        if not build:
            # Synthetic: the checker flags the unlabeled entry only.
            payload = {'tests': [{'name': 'a', 'properties': [{'name': 'LABELS', 'value': ['slow', 'sim']}]},
                                 {'name': 'b', 'properties': []}]}
            self.assertEqual(self.unlabeled_tests(payload), ['b'])
            return
        out = subprocess.run(['ctest', '--test-dir', build, '--show-only=json-v1'],
                             capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, 'ctest show-only must succeed')
        bad = self.unlabeled_tests(json.loads(out.stdout))
        self.assertEqual(bad, [])

    def make_builds_repo(self):
        repo = self.root / 'repo'
        (repo / 'src').mkdir(parents=True)
        (repo / 'src' / 'x.cpp').write_text('x')
        import time
        old = time.time() - 10 * 86400
        for name in ['build-a', 'build-b', 'build-c']:
            d = repo / name
            d.mkdir()
            (d / 'f.bin').write_text('data')
        # build-a old + unclaimed; build-c fresh (now)
        os.utime(repo / 'build-a', (old, old))
        return repo

    def run_builds(self, repo, *args):
        import sys
        return subprocess.run([sys.executable, str(Path(__file__).resolve().parent.parent / 'tools' / 'agent_workspace.py'),
                               'builds', '--root', str(repo), *args],
                              capture_output=True, text=True, env={**os.environ, 'RR_AGENT_STATE': str(self.root / 'resources')})

    def test_builds_dry_run_lists_only_prunable(self):
        from agent_common import ResourceClaims
        repo = self.make_builds_repo()
        ResourceClaims().acquire('s', [repo / 'build-b'], os.getpid())
        out = self.run_builds(repo)
        self.assertEqual(out.returncode, 0, out.stderr)
        rows = json.loads(out.stdout)
        names = [r['name'] for r in rows if r['prunable']]
        self.assertEqual(names, ['build-a'])

    def test_builds_prune_removes_only_unclaimed_old(self):
        from agent_common import ResourceClaims
        repo = self.make_builds_repo()
        ResourceClaims().acquire('s', [repo / 'build-b'], os.getpid())
        out = self.run_builds(repo, '--prune', '--yes')
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertFalse((repo / 'build-a').exists())
        self.assertTrue((repo / 'build-b').exists())
        self.assertTrue((repo / 'build-c').exists())
        self.assertTrue((repo / 'src' / 'x.cpp').exists())

    def test_handoff_index_classifies_merged_and_open(self):
        from handoff_index import classify
        # merged: cited in docs text; open (resources): uncited + live PIDs.
        docs = 'see 25-1.md for the restart fix (PROGRESS.md)' 
        log = 'docs: apply 26-1.md factory order\n'
        self.assertEqual(classify('25-1.md', '# Handoff 25-1\nno pids here', docs, log), 'merged')
        self.assertEqual(classify('26-1.md', '# Handoff 26-1', docs, log), 'merged')
        self.assertEqual(classify('other.md', '# Notes\nPIDs: 1234, capture running', docs, log), 'open (resources)')
        self.assertEqual(classify('quiet.md', '# Notes\nnothing live', docs, log), 'open')

    def test_doc_checks_pass(self):
        import subprocess, sys
        out = subprocess.run([sys.executable, str(Path(__file__).resolve().parent.parent / 'tools' / 'doc_checks.py')],
                             capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout)

    def test_race_constructed_only_in_race_setup(self):
        # Plan 26 Task 4: the only production/replay Race construction lives in
        # src/race_setup.cpp; unit tests with synthetic routes are exempt.
        import re
        hits = [p for p in (Path(__file__).resolve().parent.parent / 'src').glob('*.cpp')
                if p.name != 'race_setup.cpp' and re.search(r'make_unique<\s*(rr::)?Race\s*>|\bRace\s+\w+\s*\(', p.read_text())]
        self.assertEqual(hits, [])

    def test_current_status_does_not_upgrade_fidelity_from_green_gate(self):
        output = render({'areas': [{'area': 'Physics', 'implementation': 'ported', 'verification': 'partial',
                         'remaining': 'human controls', 'evidence': []}], 'gates': {}})
        self.assertIn('partial; human controls', output)
        self.assertIn('No current receipts', output)


if __name__ == '__main__':
    unittest.main()
