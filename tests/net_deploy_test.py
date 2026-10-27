#!/usr/bin/env python3
"""Exercise failed-deployment restoration and archive rejection without root/Dokku."""
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('deploy_net', ROOT/'deploy/road-rash-net/deploy.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class FakeDokku:
    def __init__(self, old=True):
        self.config = {k: '' for k in module.KEYS}
        if old:
            self.config.update(RR_NETD_RTC='1', RR_NETD_PUBLIC_IP=module.PUBLIC_IP,
                               RR_NETD_UDP_BEGIN='40000', RR_NETD_UDP_END='40000', RR_NETD_UDP_PUBLISHED='40000')
        self.options = {module.mapping('40000')} if old else set()
        self.calls = []

    def __call__(self, *args, optional=False):
        self.calls.append(args)
        if args[0] == 'config:get':
            assert args[1] == module.APP
            return self.config[args[2]]
        if args[0].startswith('docker-options:'):
            assert args[1:3] == (module.APP, 'deploy')
            if args[0].endswith('add'):
                self.options.add(args[3])
            else:
                self.options.remove(args[3])
        else:
            assert args[1:3] == ('--no-restart', module.APP)
            for entry in args[3:]:
                if args[0] == 'config:set':
                    k, v = entry.split('=', 1)
                    self.config[k] = v
                else:
                    self.config[entry] = ''
        return ''


class Reservation:
    def close(self):
        pass


class Tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='rr-net-deploy-test-')
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)/'archive.tar.gz'
        binary = bytearray(20)
        binary[:4] = b'\x7fELF'; binary[4] = 2; binary[18] = 62
        self.files = {'rr-netd': bytes(binary), 'Dockerfile': b'FROM debian:bookworm-slim',
                      'app.json': json.dumps({'healthchecks': {'web': [dict(type='startup', port=8081, path='/net/health')]}}).encode()}
        self.write()

    def write(self, extra=None):
        with tarfile.open(self.path, 'w:gz') as archive:
            directory = tarfile.TarInfo('.'); directory.type = tarfile.DIRTYPE; archive.addfile(directory)
            for name, data in self.files.items():
                info = tarfile.TarInfo('./'+name); info.size = len(data); info.mode = 0o755 if name == 'rr-netd' else 0o644
                archive.addfile(info, io.BytesIO(data))
            if extra:
                archive.addfile(extra)

    def test_rolling_port_and_checks(self):
        fake = FakeDokku()
        def publish(path):
            self.assertEqual(fake.config['RR_NETD_UDP_BEGIN'], '40001')
            self.assertEqual(fake.options, {module.mapping(40001)})
        module.deploy(self.path, fake, lambda previous: (40001, Reservation()), publish)
        self.assertEqual(fake.config['RR_NETD_UDP_PUBLISHED'], '40001')
        self.assertFalse(any('checks:' in c[0] or c[0] == 'ps:stop' for c in fake.calls))

    def test_failed_health_restores_previous_config(self):
        fake = FakeDokku(); before = fake.config.copy()
        def fail(path):
            raise RuntimeError('new generation failed startup health')
        with self.assertRaisesRegex(RuntimeError, 'startup health'):
            module.deploy(self.path, fake, lambda previous: (40001, Reservation()), fail)
        self.assertEqual(fake.config, before)
        self.assertEqual(fake.options, {module.mapping(40000)})

    def test_first_failure_restores_unset_configuration(self):
        fake = FakeDokku(False); before = fake.config.copy()
        with self.assertRaises(RuntimeError):
            module.deploy(self.path, fake, lambda previous: (40000, Reservation()),
                          lambda path: (_ for _ in ()).throw(RuntimeError('failure')))
        self.assertEqual(fake.config, before); self.assertEqual(fake.options, set())

    def test_identical_archive_still_applies_new_port(self):
        calls=[]
        module.publish_archive(self.path, current=lambda:'old', invoke=lambda args,**kwargs:calls.append(args))
        self.assertEqual([c[1] for c in calls],['git:from-archive','ps:rebuild'])
        self.assertTrue(all(module.APP in c for c in calls))

    def test_changed_generation_does_not_rebuild_twice(self):
        ids=iter(('old','new'));calls=[]
        module.publish_archive(self.path, current=lambda:next(ids), invoke=lambda args,**kwargs:calls.append(args))
        self.assertEqual([c[1] for c in calls],['git:from-archive'])

    def test_links_rejected_before_mutation(self):
        extra = tarfile.TarInfo('./escape'); extra.type = tarfile.SYMTYPE; extra.linkname = '/etc/passwd'
        self.write(extra); fake = FakeDokku()
        with self.assertRaises(ValueError):
            module.deploy(self.path, fake)
        self.assertEqual(fake.calls, [])

    def test_missing_health_rejected(self):
        self.files['app.json'] = b'{"healthchecks":{"web":[]}}'; self.write()
        with self.assertRaisesRegex(ValueError, 'healthcheck'):
            module.validate_archive(self.path)


if __name__ == '__main__':
    unittest.main()
