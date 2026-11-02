#!/usr/bin/env python3
"""Cooperative, cross-process admission budgets; never a CPU/memory quota.

Each lease retains kernel locks, so ordinary exits and crashes release permits.
Run adapters remain responsible for deadlines and cleanup of their own children.
Unwrapped work is outside this accounting. An admission estimate cannot prove
that a workload fits or that an abruptly killed adapter left no child processes.
"""
import argparse
import contextlib
import fcntl
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import selectors
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_POOL = Path.home() / 'Library/Caches/sl-remastered/parity-resource-pool-v1'
# Provisional interactive envelope, not a measured whole-game maximum. Memory
# permits are 512 MiB estimates. Keep Docker's observed 7.75 GiB ceiling separate.
CAPACITY = {'cpu': 12, 'host_memory': 64, 'vm_memory': 40,
            'renderer': 2, 'stock': 1, 'build_writer': 1}


GIB = 1024 ** 3


class ResourcePressure(RuntimeError):
    """Admission refused or observed execution stopped under host pressure."""


class OutputLimit(RuntimeError):
    """The supervised command exceeded its combined stdout/stderr budget."""


class ArtifactLimit(RuntimeError):
    """Declared artifact trees exceeded bounds or could not be observed safely."""


class ArtifactBudget:
    """Bounded, no-follow logical-byte accounting of declared private trees.

    This is sampled refusal, not a filesystem quota or write sandbox. Root
    identity is fixed at construction; disappearing/replaced entries are an
    unavailable observation, never an empty successful tree. Symlinks are never
    followed; explicit count policy charges link bytes only. This does not
    establish that link targets are private; adapters must validate isolation.
    Explicit socket count policy charges Unix namespace metadata only, without
    connecting or reading traffic. Other special files remain refused.
    """

    def __init__(self, roots, max_bytes, *, max_entries=100000, scan_seconds=1,
                 link_policy='reject', socket_policy='reject', retained=False):
        if type(max_bytes) is not int or max_bytes < 1:
            raise ValueError('artifact byte limit must be a positive integer')
        if type(max_entries) is not int or max_entries < 1:
            raise ValueError('artifact entry limit must be a positive integer')
        if not math.isfinite(scan_seconds) or scan_seconds <= 0:
            raise ValueError('artifact scan deadline must be positive and finite')
        if link_policy not in ('reject', 'count'):
            raise ValueError('artifact link policy must be reject or count')
        if socket_policy not in ('reject', 'count'):
            raise ValueError('artifact socket policy must be reject or count')
        self.roots = []
        for supplied in roots:
            path = Path(supplied).absolute()
            # Do not permit aliases, including symlinks in parent directories.
            if path.resolve(strict=True) != path:
                raise ValueError('artifact root must be canonical, without symlinks')
            if path == ROOT / 'ref' or ROOT / 'ref' in path.parents:
                raise ValueError('artifact roots must be outside ref/')
            if any(path == old or path in old.parents or old in path.parents for old, _ in self.roots):
                raise ValueError('artifact roots must not overlap')
            descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
            try:
                info = os.fstat(descriptor)
                self.roots.append((path, (info.st_dev, info.st_ino)))
            finally:
                os.close(descriptor)
        if not self.roots:
            raise ValueError('at least one artifact root is required')
        self.max_bytes, self.max_entries, self.scan_seconds = max_bytes, max_entries, scan_seconds
        self.link_policy = link_policy
        self.socket_policy = socket_policy
        self.last_sample = {}
        # Per-run limits alone allowed hundreds of multi-GiB captures to fill
        # the disk. Share a durable cumulative allowance for repository-local
        # captures; test temp directories and ordinary build outputs stay out.
        capture_roots = [path for path, _ in self.roots
                         if retained or path == ROOT/'local' or ROOT/'local' in path.parents]
        self.retention = None
        self.retention_descriptor = None
        if capture_roots:
            spec = importlib.util.spec_from_file_location('capture_storage', ROOT/'tools/parity/storage.py')
            storage = importlib.util.module_from_spec(spec); spec.loader.exec_module(storage)
            try:
                storage.prepare_state(storage.DEFAULT_STATE)
                self.retention_descriptor = _open(storage.DEFAULT_STATE/'lock')
                fcntl.flock(self.retention_descriptor, fcntl.LOCK_SH | fcntl.LOCK_NB)
                self.retention = storage.reserve_capture(capture_roots, max_bytes)
            except (OSError, ValueError) as error:
                self.close()
                raise ArtifactLimit('capture retention admission: ' + str(error)) from error

    def close(self):
        """Release maintenance exclusion only after the capture writers stop."""
        descriptor = getattr(self, 'retention_descriptor', None)
        if descriptor is not None:
            self.retention_descriptor = None
            os.close(descriptor)

    def __del__(self):
        self.close()

    def check(self):
        sample = {'logical_bytes': 0, 'entries': 0, 'max_bytes': self.max_bytes,
                  'symlinks': 0, 'symlink_bytes': 0, 'link_policy': self.link_policy,
                  'unix_sockets': 0, 'unix_socket_bytes': 0, 'socket_policy': self.socket_policy,
                  'roots': [str(path) for path, _ in self.roots], 'complete': False}
        self.last_sample = sample
        deadline = time.monotonic() + self.scan_seconds

        def scan(descriptor):
            with os.scandir(descriptor) as entries:
                for entry in entries:
                    sample['entries'] += 1
                    if sample['entries'] > self.max_entries or time.monotonic() >= deadline:
                        raise ArtifactLimit('artifact observation entry/time bound exceeded')
                    info = entry.stat(follow_symlinks=False)
                    if stat.S_ISDIR(info.st_mode):
                        child = os.open(entry.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                                        dir_fd=descriptor)
                        try:
                            opened = os.fstat(child)
                            if (opened.st_dev, opened.st_ino) != (info.st_dev, info.st_ino):
                                raise ArtifactLimit('artifact directory changed during observation')
                            scan(child)
                        finally:
                            os.close(child)
                    elif (stat.S_ISREG(info.st_mode) or
                          (stat.S_ISLNK(info.st_mode) and self.link_policy == 'count') or
                          (stat.S_ISSOCK(info.st_mode) and self.socket_policy == 'count')):
                        sample['logical_bytes'] += info.st_size
                        if stat.S_ISLNK(info.st_mode):
                            sample['symlinks'] += 1
                            sample['symlink_bytes'] += info.st_size
                        if stat.S_ISSOCK(info.st_mode):
                            # Namespace metadata only. Never connect/read: socket
                            # traffic is not a file's disk artifact payload.
                            sample['unix_sockets'] += 1
                            sample['unix_socket_bytes'] += info.st_size
                        if sample['logical_bytes'] > self.max_bytes:
                            raise ArtifactLimit(f'artifact bytes exceeded {self.max_bytes}')
                    else:
                        raise ArtifactLimit('artifact symlink or special file cannot be accounted for')
            # A final stat/open or even an empty-directory read can block past
            # the budget. Never label that late observation complete.
            if time.monotonic() >= deadline:
                raise ArtifactLimit('artifact observation time bound exceeded')
        try:
            for path, identity in self.roots:
                descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
                try:
                    info = os.fstat(descriptor)
                    if (info.st_dev, info.st_ino) != identity:
                        raise ArtifactLimit('artifact root identity changed')
                    scan(descriptor)
                    current = path.stat(follow_symlinks=False)
                    if (current.st_dev, current.st_ino) != identity:
                        raise ArtifactLimit('artifact root changed during observation')
                finally:
                    os.close(descriptor)
            if time.monotonic() >= deadline:
                raise ArtifactLimit('artifact observation time bound exceeded')
        except (OSError, RecursionError) as error:
            raise ArtifactLimit(f'artifact observation unavailable: {error}') from error
        sample['complete'] = True
        return sample


def sample_host(path):
    """Actual free disk on `path` and reclaimable memory. Raises OSError if unreadable."""
    disk = shutil.disk_usage(path).free
    if sys.platform == 'darwin':
        text = subprocess.run(['vm_stat'], capture_output=True, text=True, timeout=10, check=True).stdout
        size = int(re.search(r'page size of (\d+) bytes', text).group(1))
        pages = {name: int(value) for name, value in re.findall(r'^Pages ([a-z ]+):\s+(\d+)\.', text, re.M)}
        # Free + inactive + speculative are immediately reclaimable; active/wired are not.
        memory = size * (pages['free'] + pages['inactive'] + pages['speculative'])
    else:
        fields = dict(re.findall(r'^(\w+):\s+(\d+) kB', Path('/proc/meminfo').read_text(), re.M))
        memory = int(fields['MemAvailable']) * 1024
    return {'free_disk_bytes': disk, 'available_memory_bytes': memory}


class Pressure:
    """Optional admission gate on real host state. A threshold of 0 disables that check;
    an unreadable sample refuses admission (a missing observation is never a pass)."""

    def __init__(self, min_free_disk=10 * GIB, min_available_memory=1 * GIB, path=ROOT, sampler=None):
        if min_free_disk < 0 or min_available_memory < 0:
            raise ValueError('pressure thresholds must not be negative')
        self.min_free_disk, self.min_available_memory = min_free_disk, min_available_memory
        self.path = Path(path)
        self.sampler = sampler or sample_host

    def check(self):
        """Return (ok, sample, reasons)."""
        if not (self.min_free_disk or self.min_available_memory):
            return True, {}, []
        try:
            sample = self.sampler(self.path)
            disk, memory = sample['free_disk_bytes'], sample['available_memory_bytes']
            if type(disk) is not int or type(memory) is not int or disk < 0 or memory < 0:
                raise ValueError('invalid pressure sample')
        except (OSError, KeyError, ValueError, AttributeError, subprocess.SubprocessError) as error:
            return False, {}, [f'pressure sample unavailable: {error}']
        reasons = []
        if self.min_free_disk and disk < self.min_free_disk:
            reasons.append(f'free disk {disk / GIB:.1f} GiB < {self.min_free_disk / GIB:.1f} GiB')
        if self.min_available_memory and memory < self.min_available_memory:
            reasons.append(f'available memory {memory / GIB:.2f} GiB < {self.min_available_memory / GIB:.2f} GiB')
        return not reasons, sample, reasons


def _open(path):
    return os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)


def _root_exited_unreaped(process):
    """Observe exit while reserving the child's PID/PGID through group cleanup."""
    info = os.waitid(os.P_PID, process.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
    return info is not None and info.si_pid == process.pid


def _group_snapshot(group):
    """Read only PID/PGID/state, never command lines or environment values."""
    observed = subprocess.run(['/bin/ps', '-axo', 'pid=,pgid=,stat='],
                              stdin=subprocess.DEVNULL, capture_output=True,
                              text=True, check=True, timeout=2)
    if len(observed.stdout) > 4 * 1024 * 1024:
        raise RuntimeError('process-group census exceeded its output bound')
    members = []
    for line in observed.stdout.splitlines():
        fields = line.split()
        if len(fields) != 3 or not fields[0].isdigit() or not fields[1].isdigit():
            raise RuntimeError('incomplete process-group census')
        if int(fields[1]) == group:
            members.append({'pid': int(fields[0]), 'pgid': int(fields[1]), 'state': fields[2]})
    return members


def _signal_owned_group(process, signum, status):
    # Refuse a lost wait relationship (for example an inherited SIGCHLD-ignore
    # disposition) before sending any signal to a potentially recycled PGID.
    try:
        _root_exited_unreaped(process)
    except ChildProcessError as error:
        raise RuntimeError('owned root wait identity is no longer reserved') from error
    try:
        os.killpg(process.pid, signum)
    except ProcessLookupError:
        pass
    except PermissionError:
        # Darwin rejects signaling a zombie-only group with EPERM. Preserve
        # WNOWAIT's PID reservation; do not mistake that no-op for permission
        # to ignore a live member or an unavailable census.
        if sys.platform != 'darwin' or not _root_exited_unreaped(process):
            raise
        members = _group_snapshot(process.pid)
        if (not any(m['pid'] == process.pid for m in members) or
                any(not m['state'].startswith('Z') for m in members)):
            raise
        status.setdefault('zombie_group_signal_noops', []).append(
            {'signal': signum, 'members': members, 'root_unreaped': True})


def _console_guardian(config_fd, lifeline_fd, status_fd):
    """Own one console group independently of the admission launcher's lifetime.

    The command cannot inherit the lifeline or private control descriptors. Both
    it and this guardian retain leases; EOF before GO refuses execution.
    An escaped session or destruction of this guardian is outside this boundary.
    """
    status = {'returncode': None, 'kill_required': False, 'parent_lost': False}
    process = None
    try:
        with os.fdopen(config_fd) as stream:
            config = json.load(stream)
        with selectors.DefaultSelector() as selector:
            selector.register(lifeline_fd, selectors.EVENT_READ)
            if os.read(lifeline_fd, 1) != b'G':
                status['parent_lost'] = True
                return 2
            # Check for an already closed parent before creating any command.
            if selector.select(0):
                status['parent_lost'] = True
                return 2
            process = subprocess.Popen(config['command'], stdin=subprocess.DEVNULL,
                                       start_new_session=True, cwd=config['cwd'], env=config['env'],
                                       pass_fds=tuple(config['lease_descriptors']))
            status['root_pid'] = process.pid
            while not _root_exited_unreaped(process):
                if selector.select(.05):
                    status['parent_lost'] = True
                    break
    except BaseException as error:
        status['error'] = f'{type(error).__name__}: {error}'
    finally:
        try:
            if process is not None:
                _signal_owned_group(process, signal.SIGTERM, status)
                grace = time.monotonic() + 2
                while not _root_exited_unreaped(process) and time.monotonic() < grace:
                    time.sleep(.01)
                status['kill_required'] = not _root_exited_unreaped(process)
                # Keep the exited root unreaped until after the last group
                # signal. Its PID cannot be recycled into an unrelated group.
                _signal_owned_group(process, signal.SIGKILL, status)
                process.wait(timeout=2)
                status['returncode'] = process.returncode
        except BaseException as error:
            status.update(cleanup_incomplete=True, cleanup_error=f'{type(error).__name__}: {error}')
            # No later group signal is attempted after this failure. Reap only
            # an already-exited owned root; a live root retains its lease FDs.
            try:
                if process is not None and _root_exited_unreaped(process):
                    status['returncode'] = process.wait(timeout=2)
            except BaseException as reap_error:
                status['reap_error'] = f'{type(reap_error).__name__}: {reap_error}'
        finally:
            os.write(status_fd, json.dumps(status).encode())
            os.close(status_fd)
            os.close(lifeline_fd)
    return 2 if status.get('error') or status.get('cleanup_incomplete') or status['parent_lost'] else 0


class Lease:
    def __init__(self, description, descriptors, pressure=None):
        self.description = description
        # Console guardians and specialized observer roots retain admission
        # independently of the Python launcher.
        self.descriptors = tuple(descriptors)
        self.pressure = pressure
        self.execution_pressure = {'samples': 0, 'stopped': False}

    def _check_artifacts(self, budget):
        if budget is None:
            return
        self.execution_artifacts['samples'] += 1
        try:
            self.execution_artifacts['last_sample'] = budget.check()
        except ArtifactLimit as error:
            self.execution_artifacts.update(stopped=True, reason=str(error),
                                             last_sample=budget.last_sample)
            raise

    def run(self, command, *, timeout=1800, max_output_bytes=64 * 1024 * 1024,
            output=None, cwd=None, env=None, artifact_budget=None):
        """Supervise a console command and its inherited process group.

        Drain a combined output pipe into a capped temporary file. Only relay it
        after cleanup, so a slow output consumer cannot stall pressure/deadline
        checks. Commands creating new sessions escape this boundary; native/game
        adapters still need their specialized focus/container supervision.
        """
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError('execution timeout must be finite and positive')
        if type(max_output_bytes) is not int or max_output_bytes < 1:
            raise ValueError('output limit must be a positive integer')
        self.execution_pressure = {'samples': 0, 'stopped': False}
        self.execution = {'output_bytes': 0, 'output_limit_bytes': max_output_bytes,
                          'stop_reason': None, 'kill_required': False}
        self.execution_artifacts = {'samples': 0, 'stopped': False}
        try:
            self._check_artifacts(artifact_budget)
        except ArtifactLimit:
            self.execution['stop_reason'] = 'artifact_limit'
            raise
        start = time.monotonic()
        next_sample = start
        next_artifacts = start + 1
        with (tempfile.TemporaryFile() as captured, tempfile.TemporaryFile() as config_file,
              tempfile.TemporaryFile() as status_file, selectors.DefaultSelector() as selector):
            config_file.write(json.dumps({'command': [os.fsdecode(arg) for arg in command],
                                          'lease_descriptors': self.descriptors,
                                          'cwd': os.fsdecode(cwd) if cwd is not None else None,
                                          'env': None if env is None else {
                                              os.fsdecode(key): os.fsdecode(value)
                                              for key, value in env.items()}}).encode())
            config_file.seek(0)
            life_read, life_write = os.pipe()
            try:
                process = subprocess.Popen(
                    [sys.executable, '-I', str(Path(__file__).resolve()), '--console-guardian',
                     str(config_file.fileno()), str(life_read), str(status_file.fileno())],
                    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    start_new_session=True,
                    pass_fds=(*self.descriptors, config_file.fileno(), life_read, status_file.fileno()))
            except BaseException:
                os.close(life_write)
                raise
            finally:
                os.close(life_read)
            try:
                os.write(life_write, b'G')
                selector.register(process.stdout, selectors.EVENT_READ)
                while True:
                    now = time.monotonic()
                    if artifact_budget is not None and now >= next_artifacts:
                        try:
                            self._check_artifacts(artifact_budget)
                        except ArtifactLimit:
                            self.execution['stop_reason'] = 'artifact_limit'
                            raise
                        next_artifacts = time.monotonic() + 1
                    if self.pressure is not None and now >= next_sample:
                        ok, sample, reasons = self.pressure.check()
                        self.execution_pressure['samples'] += 1
                        self.execution_pressure['last_sample'] = sample
                        next_sample = time.monotonic() + 1
                        if not ok:
                            self.execution_pressure.update(stopped=True, reasons=reasons)
                            self.execution['stop_reason'] = 'pressure'
                            raise ResourcePressure('; '.join(reasons))
                    remaining = timeout - (time.monotonic() - start)
                    if remaining <= 0:
                        self.execution['stop_reason'] = 'timeout'
                        raise subprocess.TimeoutExpired(command, timeout)
                    if not selector.get_map() and process.poll() is not None:
                        break
                    for key, _ in selector.select(min(.1, remaining)):
                        data = os.read(key.fileobj.fileno(), 65536)
                        if not data:
                            selector.unregister(key.fileobj)
                            continue
                        previous = self.execution['output_bytes']
                        self.execution['output_bytes'] += len(data)
                        captured.write(data[:max(0, max_output_bytes - previous)])
                        if self.execution['output_bytes'] > max_output_bytes:
                            self.execution['stop_reason'] = 'output_limit'
                            raise OutputLimit(f'combined output exceeded {max_output_bytes} bytes')
            finally:
                # EOF is the same cleanup request on ordinary failure or SIGKILL
                # of this launcher; the independent guardian owns the root PID.
                os.close(life_write)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.execution['guardian_cleanup_missing'] = True
                    process.kill()
                    process.wait(timeout=2)
                process.stdout.close()
                status_file.seek(0)
                raw_status = status_file.read(65537)
                try:
                    status = json.loads(raw_status) if len(raw_status) <= 65536 else {}
                except (ValueError, UnicodeError):
                    status = {}
                self.execution.update(status)
                self.execution['elapsed_seconds'] = time.monotonic() - start
                self.execution['retained_output_bytes'] = captured.tell()
                captured.seek(0)
                shutil.copyfileobj(captured, output or sys.stdout.buffer, 65536)
            if not status or status.get('error') or status.get('parent_lost') or process.returncode:
                raise RuntimeError(f'console guardian failed: {status}')
            try:
                self._check_artifacts(artifact_budget)
            except ArtifactLimit:
                self.execution['stop_reason'] = 'artifact_limit'
                raise
            return subprocess.CompletedProcess(command, status['returncode'])

    def run_observer(self, command, *, timeout, artifact_budget=None, **kwargs):
        """Wait for the focus supervisor while checking host pressure once a second.

        Only for the owned focus observer: its SIGTERM handler/watchdog drains its
        private child group. This is not a process-tree supervisor for arbitrary
        commands. Do not use PIPE (streams go to run-local files). On failure give
        that supervisor eight seconds to clean up before killing a stalled root;
        missing cleanup evidence remains a failed/inconclusive run.
        """
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError('execution timeout must be finite and positive')
        if any(kwargs.get(key) == subprocess.PIPE for key in ('stdin', 'stdout', 'stderr')):
            raise ValueError('observer streams must not use PIPE')
        if 'pass_fds' in kwargs:
            raise ValueError('lease owns inherited resource descriptors')
        self.execution_pressure = {'samples': 0, 'stopped': False}
        self.execution_artifacts = {'samples': 0, 'stopped': False}
        self._check_artifacts(artifact_budget)
        start = time.monotonic()
        process = subprocess.Popen(command, pass_fds=self.descriptors, **kwargs)
        try:
            while True:
                if self.pressure is not None:
                    ok, sample, reasons = self.pressure.check()
                    self.execution_pressure['samples'] += 1
                    self.execution_pressure['last_sample'] = sample
                    if not ok:
                        self.execution_pressure.update(stopped=True, reasons=reasons)
                        raise ResourcePressure('; '.join(reasons))
                remaining = timeout - (time.monotonic() - start)
                if remaining <= 0:
                    raise subprocess.TimeoutExpired(command, timeout)
                try:
                    code = process.wait(timeout=min(1, remaining))
                    self._check_artifacts(artifact_budget)
                    return subprocess.CompletedProcess(command, code)
                except subprocess.TimeoutExpired:
                    self._check_artifacts(artifact_budget)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    self.execution_pressure['observer_kill_required'] = True
                    process.kill()
                    process.wait(timeout=2)


class ResourcePool:
    def __init__(self, directory=None, capacity=None, pressure=None):
        self.pressure = pressure
        supplied = Path(directory or os.environ.get('SLRR_RESOURCE_POOL', DEFAULT_POOL))
        self.directory = supplied.resolve()
        reference = (ROOT / 'ref').resolve()
        if self.directory == reference or reference in self.directory.parents:
            raise ValueError('resource pool must be outside ref/')
        self.capacity = dict(CAPACITY if capacity is None else capacity)
        if (not self.capacity or any(not isinstance(k, str) or not k.isidentifier() or
                type(v) is not int or v < 1 for k, v in self.capacity.items())):
            raise ValueError('capacities must be named positive integers')
        self.directory.mkdir(parents=True, exist_ok=True)
        with self._gate():
            config = self.directory / 'capacity.json'
            descriptor = _open(config)
            with os.fdopen(descriptor, 'r+') as stream:
                content = stream.read()
                if content:
                    if json.loads(content) != self.capacity:
                        raise ValueError('pool capacity differs; use the existing configuration or a new pool')
                else:
                    stream.write(json.dumps(self.capacity, sort_keys=True) + '\n')
                    stream.flush()
                    os.fsync(stream.fileno())

    @contextlib.contextmanager
    def _gate(self, timeout=5):
        descriptor = _open(self.directory / 'admission.lock')
        deadline = time.monotonic() + timeout
        try:
            while True:
                try:
                    fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError('resource pool admission mutex deadline exceeded')
                    time.sleep(min(.01, remaining))
            yield
        finally:
            os.close(descriptor)

    def _enqueue(self, timeout):
        # Creation and taking the liveness lock share the admission mutex: another
        # contender must never mistake a half-created waiter for a dead process.
        with self._gate(timeout):
            path = self.directory / f'wait-{time.monotonic_ns():020d}-{uuid.uuid4().hex}.lock'
            descriptor = _open(path)
            try:
                fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BaseException:
                os.close(descriptor)
                path.unlink()
                raise
        return path, descriptor

    def _first_waiter(self, own_path):
        # Called under the mutex. A kernel lock, not PID reuse or a timestamp,
        # establishes that a queued contender is still alive. Crash residue is
        # removed without waiting for a lease deadline or a human cleanup pass.
        for path in sorted(self.directory.glob('wait-*.lock')):
            if path == own_path:
                return path
            descriptor = _open(path)
            try:
                try:
                    fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    return path
                path.unlink()
            finally:
                os.close(descriptor)
        raise RuntimeError('resource admission waiter disappeared')

    def _try(self, request, timeout, waiter):
        held = []
        try:
            with self._gate(timeout):
                if self._first_waiter(waiter) != waiter:
                    return None
                for resource, wanted in sorted(request.items()):
                    acquired = 0
                    for slot in range(self.capacity[resource]):
                        if acquired == wanted:
                            break
                        descriptor = _open(self.directory / f'{resource}-{slot}.lock')
                        try:
                            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                        except BlockingIOError:
                            os.close(descriptor)
                            continue
                        held.append(descriptor)
                        acquired += 1
                    if acquired != wanted:
                        return None
                waiter.unlink()
            result, held = held, []
            return result
        finally:
            for descriptor in held:
                os.close(descriptor)

    @contextlib.contextmanager
    def acquire(self, request, timeout=120):
        request = dict(request)
        if (not request or any(k not in self.capacity or type(v) is not int or
                v < 0 or v > self.capacity[k] for k, v in request.items()) or
                not any(request.values())):
            raise ValueError('request must fit the pool and reserve at least one permit')
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError('admission timeout must be positive and finite')
        start = time.monotonic()
        sample = {}
        waiter, waiter_descriptor = self._enqueue(timeout)
        try:
            while True:
                if self.pressure is not None:
                    ok, sample, reasons = self.pressure.check()
                    if not ok:
                        remaining = timeout - (time.monotonic() - start)
                        if remaining <= 0:
                            raise ResourcePressure('; '.join(reasons))
                        time.sleep(min(.5, remaining))
                        continue
                held = self._try(request, max(0, timeout - (time.monotonic() - start)), waiter)
                if held is not None:
                    break
                remaining = timeout - (time.monotonic() - start)
                if remaining <= 0:
                    raise TimeoutError('resource admission deadline exceeded')
                time.sleep(min(.05, remaining))
        finally:
            # On success _try already dequeued us under the mutex. On timeout
            # unlink while still owning the liveness lock; later arrivals can
            # then proceed. A hard kill leaves unlocked residue for pruning.
            try:
                waiter.unlink(missing_ok=True)
            finally:
                os.close(waiter_descriptor)
        try:
            yield Lease({'capacity': self.capacity, 'request': request,
                         'pool': str(self.directory), 'wait_seconds': time.monotonic() - start,
                         **({'pressure_sample': sample} if self.pressure is not None else {})}, held, self.pressure)
        finally:
            for descriptor in held:
                os.close(descriptor)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pool', type=Path)
    parser.add_argument('--wait', type=float, default=120)
    parser.add_argument('--timeout', type=float, default=1800,
                        help='execution deadline in seconds, excluding admission (default: 1800)')
    parser.add_argument('--max-output-mib', type=int, default=64,
                        help='combined stdout/stderr capture limit (default: 64 MiB)')
    parser.add_argument('--artifact-dir', type=Path, action='append', default=[],
                        help='existing canonical private artifact tree to account (repeatable)')
    parser.add_argument('--max-artifact-mib', type=int, default=2048,
                        help='aggregate logical artifact bytes when --artifact-dir is used (default: 2048 MiB)')
    parser.add_argument('--artifact-links', choices=('reject', 'count'), default='reject',
                        help='never follow links; count charges link bytes only, isolation must be validated separately')
    parser.add_argument('--artifact-sockets', choices=('reject', 'count'), default='reject',
                        help='count charges Unix socket namespace metadata only; never connects or reads traffic')
    parser.add_argument('--min-free-disk-gib', type=float, default=10,
                        help='wait/refuse while free disk is lower (0 disables)')
    parser.add_argument('--min-available-memory-gib', type=float, default=1,
                        help='wait/refuse while reclaimable memory is lower (0 disables)')
    for name in CAPACITY:
        parser.add_argument('--' + name.replace('_', '-'), type=int, default=0)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('a command is required after --')
    try:
        pressure = Pressure(int(args.min_free_disk_gib * GIB), int(args.min_available_memory_gib * GIB))
        artifact_budget = ArtifactBudget(args.artifact_dir, args.max_artifact_mib * 1024 * 1024,
                                         link_policy=args.artifact_links,
                                         socket_policy=args.artifact_sockets) if args.artifact_dir else None
        with ResourcePool(args.pool, pressure=pressure).acquire({key: getattr(args, key) for key in CAPACITY}, args.wait) as lease:
            return lease.run(command, timeout=args.timeout,
                             max_output_bytes=args.max_output_mib * 1024 * 1024,
                             artifact_budget=artifact_budget).returncode
    except (OSError, ValueError, RuntimeError, TimeoutError, ResourcePressure, OutputLimit,
            subprocess.TimeoutExpired) as error:
        parser.exit(2, f'resource_pool: {error}\n')


if __name__ == '__main__':
    if len(sys.argv) == 5 and sys.argv[1] == '--console-guardian':
        raise SystemExit(_console_guardian(*(int(arg) for arg in sys.argv[2:])))
    raise SystemExit(main())
