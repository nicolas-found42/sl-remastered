#!/usr/bin/env python3
"""Check changed files using repository formatting/lint settings; never rewrite."""
import argparse
import ast
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CPP = {'.c', '.cpp', '.cc', '.h', '.hpp', '.m', '.mm'}


def tool(name):
    found = shutil.which(name)
    if not found:
        candidate = Path('/opt/homebrew/opt/llvm/bin') / name
        if candidate.is_file():
            found = str(candidate)
    if not found:
        raise ValueError(f'{name} required; install LLVM and put its bin directory on PATH')
    return found


def check_file(path, content, *, tidy=True):
    if path.suffix == '.py':
        ast.parse(content, filename=str(path))
    elif path.suffix in CPP:
        formatted = subprocess.run([tool('clang-format'), '--style=file',
                                    '--assume-filename=' + str(path)], input=content,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        if formatted.returncode or formatted.stdout != content:
            raise ValueError(f'{path}: clang-format check failed')
        if tidy:
            if path.read_bytes() != content:
                raise ValueError(f'{path}: staged/working contents differ; lint requires matching contents')
            if not (ROOT / 'build/compile_commands.json').is_file():
                raise ValueError('configure CMake first to generate build/compile_commands.json')
            result = subprocess.run([sys.executable, '-I', str(ROOT / 'tools/parity/resource_pool.py'),
                                     '--cpu', '1', '--host-memory', '2', '--timeout', '120', '--',
                                     tool('clang-tidy'), str(path), '-p', str(ROOT / 'build'),
                                     '--warnings-as-errors=*'], timeout=150)
            if result.returncode:
                raise ValueError(f'{path}: clang-tidy failed')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--staged', action='store_true')
    group.add_argument('--base', help='Git revision to compare to HEAD')
    args = parser.parse_args(argv)
    os.chdir(ROOT)
    try:
        command = ['git', 'diff', '--name-only', '-z', '--diff-filter=ACMR']
        command += ['--cached'] if args.staged else [args.base, 'HEAD']
        command += ['--']
        names = subprocess.check_output(command).decode().split('\0')
        checked = 0
        for name in filter(None, names):
            path = ROOT / name
            if name.startswith('ref/'):
                raise ValueError('protected ref/ change staged')
            if path.suffix not in CPP | {'.py'}:
                continue
            content = subprocess.check_output(['git', 'show', ':' + name if args.staged else 'HEAD:' + name])
            check_file(path, content)
            checked += 1
        print(f'guardrails: {checked} changed source files checked')
        return 0
    except (OSError, ValueError, SyntaxError, subprocess.SubprocessError) as error:
        print(f'guardrails: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
