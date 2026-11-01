#!/usr/bin/env python3
"""Run one oracle command with bounded waiting and a private evidence record."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def stop_group(process, sig):
    try:
        os.killpg(process.pid, sig)
    except ProcessLookupError:
        pass


def targeted_docker_cleanup(cidfile):
    if not cidfile:
        return 'not applicable'
    try:
        cid = Path(cidfile).read_text().strip()
    except OSError:
        return 'container id unavailable'
    if not re.fullmatch(r'[0-9a-f]{64}', cid):
        return 'invalid container id; cleanup not attempted'
    try:
        cleanup = subprocess.Popen(['docker', 'rm', '-f', cid], stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, start_new_session=True)
    except OSError as e:
        return f'cleanup could not start: {e}'
    end = time.monotonic() + 2
    while cleanup.poll() is None and time.monotonic() < end:
        time.sleep(0.05)
    if cleanup.poll() is None:
        stop_group(cleanup, signal.SIGKILL)
        return 'targeted cleanup did not finish in 2 seconds'
    return f'targeted cleanup exited {cleanup.returncode}'


def forward(dest):
    # File-backed streams cannot keep the caller's pipes open if a child is
    # unkillable. Bound each read to the size observed at the end of the wait.
    result = {}
    for name, target in (('stdout.bin', sys.stdout.buffer), ('stderr.bin', sys.stderr.buffer)):
        with (dest / name).open('rb') as source:
            remaining = os.fstat(source.fileno()).st_size
            result[name + '_bytes'] = remaining
            digest = hashlib.sha256()
            while remaining:
                chunk = source.read(min(1 << 20, remaining))
                if not chunk:
                    break
                remaining -= len(chunk)
                digest.update(chunk)
                target.write(chunk)
            result[name + '_sha256'] = digest.hexdigest()
        target.flush()
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--record-dir', required=True)
    p.add_argument('--timeout', type=float, default=120)
    p.add_argument('--hash-file', action='append', default=[])
    p.add_argument('--docker-cidfile')
    p.add_argument('--fixture-root')
    p.add_argument('command', nargs=argparse.REMAINDER)
    a = p.parse_args()
    cmd = a.command[1:] if a.command[:1] == ['--'] else a.command
    if not cmd or not (0 < a.timeout < 86400):
        p.error('a command and timeout in (0, 86400) are required')
    # The worktree's ref/ may itself be a symlink to the shared reference
    # tree, and the record directory's leaf does not exist yet, so resolve the
    # nearest existing ancestor and compare that.
    dest = Path(a.record_dir).resolve()
    ref = (Path(__file__).resolve().parents[2] / 'ref').resolve()
    probe = dest
    while not probe.exists() and probe != probe.parent:
        probe = probe.parent
    if dest == ref or ref in dest.parents or ref in probe.resolve().parents \
            or probe.resolve() == ref:
        p.error('record directory must be outside ref/')
    dest.mkdir(parents=True, exist_ok=False)
    hashes = {}
    for name in a.hash_file:
        try:
            hashes[str(Path(name).resolve())] = sha256(name)
        except OSError as e:
            print(f'oracle controller: cannot hash {name}: {e}', file=sys.stderr)
            return 2
    for arg in cmd:
        if arg.startswith('Z:\\'):
            if arg.lower().startswith('z:\\out\\') and a.fixture_root:
                candidate = Path(a.fixture_root) / Path(arg[7:].replace('\\', '/'))
            else:
                candidate = Path('/' + arg[3:].replace('\\', '/'))
            if candidate.is_file():
                hashes[str(candidate.resolve())] = sha256(candidate)
    info = {'command': cmd, 'hashes_sha256': hashes, 'timeout_seconds': a.timeout,
            'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'status': 'starting'}
    def save():
        (dest / 'status.json').write_text(json.dumps(info, indent=2) + '\n')
    save()
    start = time.monotonic()
    try:
        with (dest / 'stdout.bin').open('xb') as stdout, (dest / 'stderr.bin').open('xb') as stderr:
            process = subprocess.Popen(cmd, stdout=stdout, stderr=stderr, start_new_session=True)
    except OSError as e:
        info.update(status='launch_failed', error=str(e)); save()
        print(f'oracle controller: {e}', file=sys.stderr); return 127
    info.update(status='running', pid=process.pid); save()
    deadline = start + a.timeout
    while process.poll() is None and time.monotonic() < deadline:
        time.sleep(0.05)
    if process.poll() is None:
        info['status'] = 'timed_out'; save()
        stop_group(process, signal.SIGTERM)
        deadline = time.monotonic() + 1
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        if process.poll() is None:
            stop_group(process, signal.SIGKILL)
        deadline = time.monotonic() + 1
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        info.update(termination_pending=process.poll() is None,
                    docker_cleanup=targeted_docker_cleanup(a.docker_cidfile),
                    elapsed_seconds=time.monotonic()-start)
        streams = forward(dest)
        info.update(stdout_sha256=streams['stdout.bin_sha256'], stderr_sha256=streams['stderr.bin_sha256'],
                    stdout_bytes=streams['stdout.bin_bytes'], stderr_bytes=streams['stderr.bin_bytes'])
        save()
        print(f'oracle controller: timed out after {a.timeout:g}s; evidence {dest}', file=sys.stderr)
        return 124
    code = process.returncode
    streams = forward(dest)
    info.update(stdout_sha256=streams['stdout.bin_sha256'], stderr_sha256=streams['stderr.bin_sha256'],
                stdout_bytes=streams['stdout.bin_bytes'], stderr_bytes=streams['stderr.bin_bytes'])
    info.update(status='completed', returncode=code, elapsed_seconds=time.monotonic()-start); save()
    return code if code >= 0 else 128 - code


if __name__ == '__main__':
    sys.exit(main())
