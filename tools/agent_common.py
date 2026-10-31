"""Small stdlib helpers shared by agent tooling; no automatic installation."""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parent.parent


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(fd, 'w') as f:
            json.dump(value, f, indent=2)
            f.write('\n')
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def git(*args, root=ROOT):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def source_identity(root=ROOT):
    """Hash source/config/tool inputs, including untracked inputs, excluding outputs/docs."""
    raw = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z',
                                   '--cached', '--others', '--exclude-standard'])
    files = sorted(set(os.fsdecode(p) for p in raw.split(b'\0') if p))
    h = hashlib.sha256()
    inputs = []
    for name in files:
        p = Path(name)
        if not (p.parts[0] in {'src', 'tests', 'tools', 'cmake', 'third_party', 'macos', 'windows', 'linux', 'oracle', 'android'}
                or name in {'CMakeLists.txt', 'Makefile'}):
            continue
        if 'build' in p.parts or '.gradle' in p.parts or '.cxx' in p.parts:
            continue
        full = root / p
        if full.is_file():
            digest = sha256(full)
            h.update(name.encode() + b'\0' + digest.encode() + b'\0')
            inputs.append(name)
        elif not full.exists():
            h.update(name.encode() + b'\0deleted\0')
    return {'commit': git('rev-parse', 'HEAD', root=root),
            'source_sha256': h.hexdigest(),
            'dirty': bool(git('status', '--porcelain', '--untracked-files=normal', root=root)),
            'input_count': len(inputs)}


def pid_alive(pid):
    if os.name == 'nt':
        # Plan 33b: os.kill(pid, 0) calls TerminateProcess on Windows; query instead.
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel32.OpenProcess.restype = wintypes.HANDLE
        handle = kernel32.OpenProcess(0x1000, False, int(pid))  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return ctypes.get_last_error() == 5  # ERROR_ACCESS_DENIED: exists, not ours
        try:
            code = wintypes.DWORD()
            return bool(kernel32.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259  # STILL_ACTIVE
        finally:
            kernel32.CloseHandle(handle)
    try:
        os.kill(int(pid), 0)
        return True
    except ProcessLookupError:
        return False
    except PermissionError:
        return True


def user_tag():
    """Per-local-user suffix for shared state directories (uid on POSIX, login name on Windows)."""
    if hasattr(os, 'getuid'):
        return str(os.getuid())
    import getpass
    return getpass.getuser()


class ResourceClaims:
    """One registry per local user; overlapping absolute paths conflict across worktrees."""
    def __init__(self, directory=None):
        self.directory = Path(directory or os.environ.get('RR_AGENT_STATE',
                                  str(Path(tempfile.gettempdir()) / f'rr-agent-resources-{user_tag()}')))
        self.path = self.directory / 'claims.json'

    @contextlib.contextmanager
    def transaction(self):
        self.directory.mkdir(parents=True, exist_ok=True)
        mutex = self.directory / 'mutex'
        deadline = time.monotonic() + 5
        while True:
            try:
                mutex.mkdir()
                break
            except FileExistsError:
                if time.monotonic() >= deadline:
                    raise RuntimeError(f'resource registry busy: {mutex}; inspect owner.json; no age-based removal')
                time.sleep(0.05)
        try:
            atomic_json(mutex / 'owner.json', {'pid': os.getpid()})
            rows = json.loads(self.path.read_text()) if self.path.exists() else []
            yield rows
            atomic_json(self.path, rows)
        finally:
            (mutex / 'owner.json').unlink(missing_ok=True)
            mutex.rmdir()

    def list(self):
        return json.loads(self.path.read_text()) if self.path.exists() else []

    @staticmethod
    def conflicts(a, b):
        a, b = Path(a), Path(b)
        return a == b or a in b.parents or b in a.parents

    def acquire(self, session, paths, pid=None):
        paths = sorted(set(str(Path(p).resolve()) for p in paths))
        pid = pid or os.getpid()
        if not pid_alive(pid):
            raise RuntimeError(f'owner PID {pid} is not alive')
        token = str(uuid.uuid4())
        with self.transaction() as rows:
            for row in rows:
                for wanted in paths:
                    if any(self.conflicts(wanted, held) for held in row['paths']):
                        raise RuntimeError(f'{wanted} held by {row["session"]} PID {row["pid"]} token {row["token"]}; '
                                           f'alive={pid_alive(row["pid"])}; release explicitly after verifying owner')
            rows.append({'token': token, 'session': session, 'pid': pid,
                         'paths': paths, 'created_at': time.time()})
        return token

    def release(self, token, session=None, reap=False):
        with self.transaction() as rows:
            row = next((r for r in rows if r['token'] == token), None)
            if not row:
                raise RuntimeError('unknown resource token')
            if reap:
                if pid_alive(row['pid']):
                    raise RuntimeError('cannot reap a live owner (including a reused PID)')
            elif session != row['session']:
                raise RuntimeError('session does not own this token')
            rows.remove(row)

    @contextlib.contextmanager
    def hold(self, session, paths):
        token = self.acquire(session, paths)
        try:
            yield token
        finally:
            self.release(token, session)


def run_logged(command, log, cwd=ROOT, env=None, timeout=None):
    log = Path(log)
    log.parent.mkdir(parents=True, exist_ok=True)
    started = time.time()
    with log.open('w') as f:
        try:
            result = subprocess.run([str(x) for x in command], cwd=cwd, env=env,
                                    stdout=f, stderr=subprocess.STDOUT, timeout=timeout)
            code = result.returncode
        except subprocess.TimeoutExpired:
            code = 124
        except OSError as e:
            f.write(str(e) + '\n')
            code = 127
    return {'command': [str(x) for x in command], 'cwd': str(cwd),
            'exit_code': code, 'duration_seconds': round(time.time() - started, 3),
            'log': str(log.resolve())}
