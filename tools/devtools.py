#!/usr/bin/env python3
"""Read-only development environment diagnosis; installation is an explicit lane."""

import argparse
import hashlib
import importlib.metadata
import importlib.util
import json
import platform
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def command_output(command):
    result = subprocess.run(command, capture_output=True, text=True, timeout=15, check=True)
    return (result.stdout or result.stderr).strip()


def doctor(build_dir):
    spec = importlib.util.spec_from_file_location(
        'development_guardrails', ROOT / 'tools/guardrails.py'
    )
    guard = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(guard)
    versions = json.loads((ROOT / 'tools/devtools.json').read_text())
    checks = {}
    for name in (
        'clang-format',
        'clang-tidy',
        'ruff',
        'shellcheck',
        'cmake',
        'ninja',
        'clang++',
        'clangd',
        'git',
        'actionlint',
    ):
        try:
            if name in ('clang-format', 'clang-tidy', 'ruff', 'shellcheck'):
                path = guard.tool(name)
            else:
                path = shutil.which(name)
                if not path:
                    raise ValueError('not installed or not on PATH')
            output = command_output([path, '--version'])
            if name == 'cmake':
                match = re.search(r'cmake version (\d+)\.(\d+)', output)
                if not match or tuple(map(int, match.groups())) < tuple(versions['cmake_minimum']):
                    raise ValueError('CMake is older than the project minimum')
            checks[name] = {'ok': True, 'path': path, 'version': output.splitlines()[0]}
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            checks[name] = {'ok': False, 'error': str(error)}
    minimum = tuple(versions['python_minimum'])
    checks['python'] = {
        'ok': sys.version_info[:2] >= minimum,
        'path': sys.executable,
        'version': platform.python_version(),
        'virtual_environment': sys.prefix != sys.base_prefix,
    }
    # Isolated Python still uses the selected interpreter's site-packages.
    # Check the dependency in that interpreter, rather than a different pip.
    requirements = (ROOT / 'requirements-dev.txt').read_text().splitlines()
    pillow_pin = next(line.split('==')[1] for line in requirements if line.startswith('Pillow=='))
    try:
        pillow = importlib.metadata.version('Pillow')
        checks['Pillow'] = {'ok': pillow == pillow_pin, 'version': pillow, 'required': pillow_pin}
    except importlib.metadata.PackageNotFoundError:
        checks['Pillow'] = {
            'ok': False,
            'required': pillow_pin,
            'error': 'not installed in this Python',
        }
    checks['host'] = {
        'ok': sys.platform == 'darwin' and platform.machine() == 'arm64',
        'system': platform.platform(),
        'architecture': platform.machine(),
        'scope': 'native game build host; Python-only lint can run elsewhere',
    }
    if sys.platform == 'darwin':
        for name, command in (
            ('sdk', ['xcrun', '--sdk', 'macosx', '--show-sdk-version']),
            ('developer_directory', ['xcode-select', '-p']),
        ):
            try:
                checks[name] = {'ok': True, 'value': command_output(command)}
            except (OSError, subprocess.SubprocessError) as error:
                checks[name] = {'ok': False, 'error': str(error)}

    build = Path(build_dir)
    if not build.is_absolute():
        build = ROOT / build
    database = build / 'compile_commands.json'
    compilation = {
        'path': str(database),
        'present': database.is_file(),
        'required_for': 'deep C++ lint',
    }
    if database.is_file():
        try:
            entries = json.loads(database.read_text())
            if not isinstance(entries, list) or any(not isinstance(row, dict) for row in entries):
                raise ValueError('expected a list of compilation commands')
            configured = set()
            for row in entries:
                if not isinstance(row.get('file'), str) or not isinstance(
                    row.get('directory'), str
                ):
                    raise ValueError('compilation entries need file and directory strings')
                file = Path(row['file'])
                if not file.is_absolute():
                    file = Path(row['directory']) / file
                configured.add(file.resolve())
            tracked = command_output(['git', '-C', str(ROOT), 'ls-files', '-z']).split('\0')
            sources = {
                ROOT / name for name in tracked if Path(name).suffix in ('.cpp', '.c', '.m', '.mm')
            }
            compilation.update(
                entries=len(entries),
                exact_source_files=len(configured),
                unconfigured_tracked_sources=sorted(
                    str(p.relative_to(ROOT)) for p in sources - configured
                ),
                scope='coverage inventory only; headers need actual compiler-proven includers',
            )
        except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
            compilation['error'] = str(error)
    tracked_status = command_output(['git', '-C', str(ROOT), 'status', '--porcelain=v1', '-uno'])
    report = {
        'schema': 1,
        'ok': all(row['ok'] for row in checks.values()) and 'error' not in compilation,
        'scope': 'read-only native developer environment; not game, parity, or release acceptance',
        'checks': checks,
        'compilation_database': compilation,
        'source': {
            'commit': command_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD']),
            'tracked_changes_present': bool(tracked_status),
            'tool_policy_sha256': hashlib.sha256(
                (ROOT / 'tools/devtools.json').read_bytes()
            ).hexdigest(),
        },
        'next_steps': 'Use dev.py setup for explicit installation, configure for a build database, and lint for selected source checks.',
    }
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', default='build')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args(argv)
    try:
        report = doctor(args.build_dir)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'doctor: {error}', file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        for name, row in report['checks'].items():
            detail = row.get('version', row.get('value', row.get('error', row.get('system', ''))))
            print(f'{"ok" if row["ok"] else "FAIL"}: {name}: {detail}')
        compilation = report['compilation_database']
        print(
            f'Compilation database: {compilation["path"]} ({"present" if compilation["present"] else "not configured"})'
        )
        if 'error' in compilation:
            print(f'FAIL: {compilation["error"]}')
        print(report['scope'])
        print(report['next_steps'])
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
