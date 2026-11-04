#!/usr/bin/env python3
"""Build and run the audited asset-free C++ tests through shared admission."""

import argparse
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
POOL = ROOT / 'tools/parity/resource_pool.py'


def pooled(command, *, writer=False, timeout=180):
    # Hosted Apple Silicon has 3 CPUs / 7 GB RAM; memory permits are 512 MiB.
    argv = [sys.executable, '-I', str(POOL), '--cpu', '2', '--host-memory', '6',
            '--wait', '120', '--timeout', str(timeout), '--max-output-mib', '8']
    if writer:
        argv += ['--build-writer', '1']
    return argv + ['--', *map(str, command)]


def plan(build_dir, test_label, no_build=False):
    if not test_label or test_label.startswith('-'):
        raise ValueError('a non-empty test label expression is required')
    stages = []
    if not no_build:
        stages += [
            ('configure', pooled(['cmake', '--preset', 'ninja-release', '-B', build_dir],
                                 writer=True, timeout=180)),
            ('build', pooled(['cmake', '--build', build_dir, '--target', 'portable-tests',
                             '--parallel', '2'], writer=True, timeout=600)),
        ]
    stages.append(('test', pooled(['ctest', '--test-dir', build_dir, '--parallel', '1',
                                  '-L', test_label, '--no-tests=error', '--output-on-failure'])))
    return stages


def execute(stages):
    for index, (name, command) in enumerate(stages):
        print(f'portable: {name}', flush=True)
        try:
            code = subprocess.run(command, cwd=ROOT, check=False).returncode
        except OSError as error:
            print(f'portable: {name} could not start: {error}', file=sys.stderr)
            code = 2
        if code:
            pending = ', '.join(stage for stage, _ in stages[index + 1:]) or 'none'
            print(f'portable: failed stage={name} exit={code}; unexecuted={pending}', file=sys.stderr)
            return code if code > 0 else 128 - code
    print('portable: selected CTest checks passed; full integration gate not executed')
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    parser.add_argument('--no-build', action='store_true', help='test an already configured and built tree')
    parser.add_argument('--test-label', default='^portable$', help='CTest label expression (empty selection fails)')
    parser.add_argument('--dry-run', action='store_true', help='print commands without executing them')
    args = parser.parse_args(argv)
    try:
        stages = plan(args.build_dir, args.test_label, args.no_build)
    except ValueError as error:
        parser.error(str(error))
    if args.dry_run:
        print(json.dumps([{'stage': name, 'command': command} for name, command in stages]))
        return 0
    return execute(stages)


if __name__ == '__main__':
    raise SystemExit(main())
