#!/usr/bin/python3 -I
"""Forced SSH command: deploy only road-rash-net, retaining rolling health checks.

Each generation gets one distinct, identical inside/outside UDP port from the
approved range. No fixed range publication, skipped checks or STUN mapping is
needed. The server multiplexes peers on that port and advertises its public IP.
"""
import fcntl
import json
import gzip
import io
from pathlib import Path
import socket
import subprocess
import sys
import tarfile
import tempfile

APP = 'road-rash-net'
PUBLIC_IP = '5.161.142.20'
PORT_BEGIN, PORT_END = 40000, 40099
KEYS = ('RR_NETD_RTC', 'RR_NETD_PUBLIC_IP', 'RR_NETD_UDP_BEGIN',
        'RR_NETD_UDP_END', 'RR_NETD_UDP_PUBLISHED')


def command(*args, optional=False):
    result = subprocess.run(['/usr/bin/dokku', *args], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode and not optional:
        raise RuntimeError('dokku command failed: ' + ' '.join(args) + '\n' + result.stderr)
    return result.stdout.strip() if not result.returncode else ''


def mapping(port):
    return f'--publish={PUBLIC_IP}:{port}:{port}/udp'


def validate_archive(path):
    with gzip.open(path, 'rb') as compressed:
        raw=compressed.read(17*1024*1024+1)
    if len(raw)>17*1024*1024:
        raise ValueError('expanded archive too large')
    with tarfile.open(fileobj=io.BytesIO(raw), mode='r:') as archive:
        files = {m.name.removeprefix('./'): m for m in archive.getmembers() if m.isfile()}
        if set(files) != {'Dockerfile', 'app.json', 'rr-netd'}:
            raise ValueError('expected code-only rr-netd archive')
        if any(not m.isfile() and not (m.isdir() and m.name in ('.', './')) for m in archive.getmembers()):
            raise ValueError('links and extra directories are not deployment inputs')
        if sum(m.size for m in files.values()) > 16 * 1024 * 1024:
            raise ValueError('deployment archive too large')
        if len(archive.getmembers()) != 4 or not files['rr-netd'].mode & 0o111:
            raise ValueError('duplicate archive member or non-executable daemon')
        binary = archive.extractfile(files['rr-netd']).read(20)
        if binary[:4] != b'\x7fELF' or binary[4] != 2 or int.from_bytes(binary[18:20], 'little') != 62:
            raise ValueError('expected Linux x86-64 executable')
        checks = json.load(archive.extractfile(files['app.json']))['healthchecks']['web']
        if not any(c.get('type') == 'startup' and c.get('port') == 8081 and c.get('path') == '/net/health' for c in checks):
            raise ValueError('startup healthcheck required')


def reserve_port(previous):
    for port in range(PORT_BEGIN, PORT_END + 1):
        if str(port) == previous:
            continue
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.bind((PUBLIC_IP, port))
            return port, sock
        except OSError:
            sock.close()
    raise RuntimeError('approved UDP range has no free port')


def configure(run, values):
    present = [f'{k}={v}' for k, v in values.items() if v]
    absent = [k for k, v in values.items() if not v]
    if present:
        run('config:set', '--no-restart', APP, *present)
    if absent:
        run('config:unset', '--no-restart', APP, *absent)


def container_id():
    return subprocess.check_output(['/usr/bin/docker', 'ps', '--filter',
        r'name=^/road-rash-net\.web\.1$', '--format', '{{.ID}}'], text=True).strip()


def stream_command(args, *, stdin=None, cwd='/', check=False):
    # Preserve live build output without retaining an unbounded log in memory.
    tail=''
    with subprocess.Popen(args, stdin=stdin, cwd=cwd, text=True, errors='replace',
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as child:
        while chunk:=child.stdout.read(4096):
            print(chunk,end='',flush=True)
            tail=(tail+chunk)[-4096:]
        result=subprocess.CompletedProcess(args,child.wait(),stdout=tail)
    if check:
        result.check_returncode()
    return result


def publish_archive(path, current=container_id, invoke=stream_command):
    before=current()
    with open(path, 'rb') as archive:
        result=invoke(['/usr/bin/dokku', 'git:from-archive', APP, '--'], stdin=archive,
                      cwd='/')
    unchanged=current()==before
    # Dokku 0.38.25 returns 1 for its explicit identical-tree skip. Accept only
    # that exact outcome, with the old generation still running; real errors
    # must propagate and restore the previous configuration.
    skipped=(result.returncode==1 and unchanged
             and 'No changes detected, skipping git commit' in result.stdout
             and "Call 'ps:rebuild' on app to rebuild the app from existing source" in result.stdout)
    if result.returncode and not skipped:
        result.check_returncode()
    # Even a successful no-op needs a new container for the selected UDP port.
    if unchanged:
        invoke(['/usr/bin/dokku', 'ps:rebuild', APP], cwd='/', check=True)


def deploy(path, run=command, reserve=reserve_port, publish=None):
    validate_archive(path)
    old = {key: run('config:get', APP, key, optional=True) for key in KEYS}
    previous = old['RR_NETD_UDP_PUBLISHED']
    if previous and (not previous.isdecimal() or not PORT_BEGIN <= int(previous) <= PORT_END):
        raise ValueError('invalid managed UDP port')
    port, reservation = reserve(previous)
    added, removed = False, False
    try:
        if previous:
            run('docker-options:remove', APP, 'deploy', mapping(previous))
            removed = True
        run('docker-options:add', APP, 'deploy', mapping(port))
        added = True
        configure(run, dict(RR_NETD_RTC='1', RR_NETD_PUBLIC_IP=PUBLIC_IP,
                            RR_NETD_UDP_BEGIN=str(port), RR_NETD_UDP_END=str(port),
                            RR_NETD_UDP_PUBLISHED=str(port)))
        reservation.close()  # release immediately before Docker binds it
        if publish:
            publish(path)
        else:
            publish_archive(path)
    except BaseException:
        # These options configure future containers only. The old healthy
        # generation retains its original UDP binding and running config.
        if added:
            run('docker-options:remove', APP, 'deploy', mapping(port))
        if removed:
            run('docker-options:add', APP, 'deploy', mapping(previous))
        configure(run, old)
        raise
    finally:
        reservation.close()


def main():
    state=Path('/var/lib/road-rash-net-deploy')
    state.mkdir(mode=0o700, exist_ok=True)
    if state.is_symlink() or state.stat().st_uid!=0 or state.stat().st_mode & 0o077:
        raise ValueError('deployment lock directory must be root-private')
    with open(state/'lock', 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        with tempfile.TemporaryDirectory(prefix='road-rash-net-deploy-') as directory:
            archive = Path(directory) / 'code.tar.gz'
            # A deploy credential must not cause an unbounded disk write.
            data = sys.stdin.buffer.read(16 * 1024 * 1024 + 1)
            if len(data) > 16 * 1024 * 1024:
                raise ValueError('compressed deployment archive too large')
            archive.write_bytes(data)
            deploy(archive)


if __name__ == '__main__':
    main()
