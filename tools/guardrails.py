#!/usr/bin/env python3
"""Check selected source bytes without rewriting; deep C/C++ lint is explicit."""

import argparse
import ast
import difflib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CPP = {'.c', '.cpp', '.cc', '.h', '.hpp', '.m', '.mm'}
HEADERS = {'.h', '.hpp'}
CONFIGS = ('.clang-format', '.clang-tidy', 'ruff.toml', 'tools/devtools.json')


def run(command, **kwargs):
    return subprocess.run(command, check=True, timeout=30, **kwargs)


def manifest():
    try:
        value = json.loads((ROOT / 'tools/devtools.json').read_text())
        for name in ('llvm_major', 'ruff', 'shellcheck'):
            if name not in value:
                raise ValueError(f'tools/devtools.json: missing {name}')
        return value
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f'tools/devtools.json required: {error}') from error


def tool(name):
    """Use one Homebrew LLVM installation before considering PATH."""
    llvm = name in ('clang-format', 'clang-tidy')
    found = None
    if llvm:
        prefixes = []
        if os.environ.get('SLRR_LLVM_PREFIX'):
            requested = Path(os.environ['SLRR_LLVM_PREFIX']) / 'bin' / name
            if not requested.is_file():
                raise ValueError(f'{name}: SLRR_LLVM_PREFIX does not contain {requested}')
            prefixes.append(str(requested.parent))
        prefixes += ['/opt/homebrew/opt/llvm/bin', '/usr/local/opt/llvm/bin']
        for prefix in prefixes:
            candidate = Path(prefix) / name
            if candidate.is_file():
                found = str(candidate)
                break
    found = found or shutil.which(name)
    if not found:
        raise ValueError(f'{name} required; see tools/parity/agent-tools.md')
    result = run([found, '--version'], capture_output=True, text=True)
    versions = manifest()
    if llvm:
        match = re.search(r'(?:LLVM|clang-format) version\s+(\d+)', result.stdout)
        expected = str(versions['llvm_major'])
        actual = match.group(1) if match else None
    else:
        match = re.search(r'(?:ruff |version: )([0-9][\w.+-]*)', result.stdout)
        expected = str(versions[name])
        actual = match.group(1) if match else None
    if actual != expected:
        raise ValueError(
            f'{name}: expected version {expected}, found {actual or result.stdout.strip()} at {found}'
        )
    return found


def blob(name, revision):
    return subprocess.check_output(
        ['git', '--literal-pathspecs', 'show', f'{revision}:{name}'], cwd=ROOT
    )


def tracked_name(path):
    absolute = Path(os.path.abspath(ROOT / path))
    try:
        return absolute.relative_to(ROOT).as_posix()
    except ValueError as error:
        raise ValueError(f'path is outside repository: {path}') from error


def canonical_source(name):
    path = ROOT / name
    if path.resolve() != path:
        raise ValueError(f'{name}: canonical path alias or symlink is not an owned source path')
    return path


def format_source(name):
    path = canonical_source(name)
    identity = path.stat()
    if not stat.S_ISREG(identity.st_mode) or identity.st_nlink != 1:
        raise ValueError(f'{name}: formatting requires a single-link regular file')
    return path


def protected(name):
    if name == 'ref' or name.startswith('ref/'):
        raise ValueError(f'protected ref/ change: {name}')


def selected_changes(args):
    if args.paths is not None:
        names = list(dict.fromkeys(tracked_name(path) for path in args.paths))
        for name in names:
            protected(name)
        reader = format_source if args.format else canonical_source
        return [(name, reader(name).read_bytes(), None) for name in names]
    command = ['git', '--literal-pathspecs', 'diff', '--name-status', '-z', '--no-renames']
    command += ['--cached'] if args.staged else [args.base, 'HEAD']
    command += ['--']
    fields = subprocess.check_output(command, cwd=ROOT).decode('utf-8').split('\0')
    changes = []
    for index in range(0, len(fields) - 1, 2):
        status, name = fields[index : index + 2]
        protected(name)
        if status == 'D':
            continue
        content = blob(name, '' if args.staged else 'HEAD')
        original_revision = 'HEAD' if args.staged else args.base
        try:
            original = blob(name, original_revision) if status != 'A' else None
        except subprocess.CalledProcessError:
            original = None
        changes.append((name, content, original))
    return changes


def edited_lines(original, content):
    """Return one-based new-file ranges; deletion-only edits retain their boundary."""
    before = original.decode('utf-8').splitlines(keepends=True)
    after = content.decode('utf-8').splitlines(keepends=True)
    ranges = []
    for tag, _, _, start, end in difflib.SequenceMatcher(
        None, before, after, autojunk=False
    ).get_opcodes():
        if tag != 'equal' and after:
            ranges.append((min(start + 1, len(after)), max(start + 1, end)))
    return ranges


def ensure_configuration(revision):
    for name in CONFIGS:
        path = ROOT / name
        if not path.is_file():
            raise ValueError(f'{name} required')
        if revision is not None and path.read_bytes() != blob(name, revision):
            raise ValueError(
                f'{name}: selected/working configuration differs; stage or restore it first'
            )


def check_file(path, content, *, original=None, tidy=False):
    if path.suffix == '.py':
        ast.parse(content, filename=str(path))
        run(
            [
                tool('ruff'),
                'check',
                '--no-cache',
                '--config',
                str(ROOT / 'ruff.toml'),
                '--stdin-filename',
                str(path),
                '-',
            ],
            input=content,
        )
    elif path.suffix in CPP:
        ranges = edited_lines(original, content) if original is not None else None
        if ranges == []:
            return
        command = [
            tool('clang-format'),
            '--style=file:' + str(ROOT / '.clang-format'),
            '--assume-filename=' + str(path),
        ]
        command += [f'--lines={start}:{end}' for start, end in ranges or []]
        formatted = run(command, input=content, capture_output=True)
        if formatted.stdout != content:
            raise ValueError(f'{path}: clang-format check failed; format the edited lines')
    elif path.suffix == '.sh' or path.name == 'pre-commit':
        # Detect the dialect from selected bytes, never a conflicting working file.
        first = content.splitlines()[0] if content.splitlines() else b''
        dialect = (
            'sh'
            if path.name == 'pre-commit'
            or (
                first.startswith(b'#!')
                and b'bash' not in first
                and re.search(rb'(?:/|\s)sh(?:\s|$)', first)
            )
            else 'bash'
        )
        run([tool('shellcheck'), '--severity=warning', '--shell=' + dialect, '-'], input=content)
    if tidy and path.suffix in CPP:
        raise ValueError('deep lint requires compilation-database preflight through the CLI')


def compilation_entries(build_dir):
    database = build_dir / 'compile_commands.json'
    try:
        entries = json.loads(database.read_text())
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f'configure CMake first: {database}: {error}') from error
    found = {}
    for entry in entries:
        directory = Path(entry['directory']).resolve()
        source = (directory / entry['file']).resolve()
        arguments = entry.get('arguments') or shlex.split(entry['command'])
        normalized = {'directory': str(directory), 'file': str(source), 'arguments': arguments}
        variants = found.setdefault(source, [])
        if not any(
            old['arguments'] == arguments and old_directory == directory
            for old_directory, old in variants
        ):
            variants.append((directory, normalized))
    return found


def dependencies(directory, entry):
    arguments = entry.get('arguments') or shlex.split(entry['command'])
    command = [arguments[0]]
    skip = False
    for argument in arguments[1:]:
        if skip:
            skip = False
            continue
        if argument in ('-o', '-MF', '-MT', '-MQ', '-MJ', '--serialize-diagnostics'):
            skip = True
        elif argument.startswith(
            ('-o', '-MF', '-MT', '-MQ', '-MJ', '--output=', '--serialize-diagnostics=')
        ):
            continue
        elif argument not in ('-c', '-MD', '-MMD', '-MP'):
            command.append(argument)
    command += ['-M', '-MT', 'guardrails_dependencies']
    # The real compiler and exact TU flags resolve dependencies; no guessed include paths.
    output = run(
        [
            sys.executable,
            '-I',
            str(ROOT / 'tools/parity/resource_pool.py'),
            '--wait',
            '5',
            '--cpu',
            '1',
            '--host-memory',
            '2',
            '--timeout',
            '20',
            '--',
            *command,
        ],
        cwd=directory,
        capture_output=True,
        text=True,
    ).stdout
    prefix = 'guardrails_dependencies:'
    if not output.startswith(prefix):
        raise ValueError('compiler did not produce the expected dependency target')
    names = shlex.split(output[len(prefix) :].replace('\\\n', ' '))
    return {(directory / name).resolve() for name in names}


def ensure_selected_file(path, revision, build_dir):
    try:
        name = path.relative_to(ROOT).as_posix()
    except ValueError:
        return
    tracked = (
        subprocess.run(
            ['git', '--literal-pathspecs', 'ls-files', '--error-unmatch', '--', name],
            cwd=ROOT,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode
        == 0
    )
    if revision is not None and not tracked and not path.is_relative_to(build_dir):
        raise ValueError(f'{name}: untracked repository dependency must be staged before deep lint')
    if tracked and revision is not None and path.read_bytes() != blob(name, revision):
        raise ValueError(f'{name}: deep lint requires matching selected/working dependency bytes')


def deep_check(changes, args, revision):
    native = [(name, content) for name, content, _ in changes if Path(name).suffix in CPP]
    if not native:
        return
    build_dir = (ROOT / args.build_dir).resolve()
    entries = compilation_entries(build_dir)
    explicit = (
        list(entries)
        if '@all' in args.translation_unit
        else [(ROOT / path).resolve() for path in args.translation_unit]
    )
    dependency_cache = {}
    for path in explicit:
        if path not in entries:
            raise ValueError(
                f'{path}: explicit translation unit absent from {build_dir}/compile_commands.json'
            )
    jobs = {}
    for name, content in native:
        source = (ROOT / name).resolve()
        if source.read_bytes() != content:
            raise ValueError(f'{name}: deep lint requires matching selected/working source bytes')
        candidates = explicit if source.suffix in HEADERS else [source]
        if not candidates:
            raise ValueError(
                f'{name}: header deep lint requires --translation-unit with a real includer'
            )
        covered = False
        for candidate in candidates:
            if candidate not in entries:
                raise ValueError(
                    f'{name}: no exact compilation database entry; foreign/experimental sources need their own real database'
                )
            for variant, (directory, entry) in enumerate(entries[candidate]):
                key = (candidate, variant)
                if key not in dependency_cache:
                    dependency_cache[key] = dependencies(directory, entry)
                deps = dependency_cache[key]
                if source != candidate and source not in deps:
                    continue
                covered = True
                for dependency in deps | {candidate}:
                    ensure_selected_file(dependency, revision, build_dir)
                jobs[key] = entry
        if not covered:
            raise ValueError(f'{name}: no supplied translation unit actually includes this header')
    for config in [
        'CMakeLists.txt',
        'CMakePresets.json',
        *[str(path.relative_to(ROOT)) for path in (ROOT / 'cmake').rglob('*.cmake')],
    ]:
        if (ROOT / config).is_file():
            ensure_selected_file(ROOT / config, revision, build_dir)
    tidy = tool('clang-tidy')
    failures = []
    # A one-entry database makes each real command variant unambiguous to clang-tidy.
    with tempfile.TemporaryDirectory(prefix='sl-guardrails-compdb-') as temporary:
        variant_database = Path(temporary)
        for (source, variant), entry in jobs.items():
            (variant_database / 'compile_commands.json').write_text(json.dumps([entry]))
            print(
                f'guardrails: clang-tidy {source} variant {variant + 1}/{len(entries[source])}',
                flush=True,
            )
            result = subprocess.run(
                [
                    sys.executable,
                    '-I',
                    str(ROOT / 'tools/parity/resource_pool.py'),
                    '--wait',
                    '5',
                    '--cpu',
                    '1',
                    '--host-memory',
                    '2',
                    '--timeout',
                    '120',
                    '--',
                    tidy,
                    str(source),
                    '-p',
                    str(variant_database),
                    '--config-file=' + str(ROOT / '.clang-tidy'),
                    '--header-filter=.*',
                    '--warnings-as-errors=*',
                ],
                timeout=150,
            )
            if result.returncode:
                failures.append(f'{source} variant {variant + 1}')
    if failures:
        raise ValueError('clang-tidy failed: ' + ', '.join(failures))


def format_paths(changes):
    for name, content, _ in changes:
        path = format_source(name)
        indexed = (
            subprocess.run(
                ['git', '--literal-pathspecs', 'ls-files', '--error-unmatch', '--', name],
                cwd=ROOT,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            ).returncode
            == 0
        )
        staged = (
            subprocess.run(
                ['git', '--literal-pathspecs', 'diff', '--cached', '--quiet', '--', name], cwd=ROOT
            ).returncode
            != 0
        )
        if indexed and staged and blob(name, '') != content:
            raise ValueError(
                f'{name}: --format refuses differing index/working bytes; format before staging or explicitly restore the index'
            )
        if path.suffix not in CPP | {'.py'}:
            raise ValueError(f'{name}: --format supports C/C++/ObjC and Python only')
    # Resolve every formatter successfully before the first write.
    outputs = []
    for name, content, _ in changes:
        path = ROOT / name
        if path.suffix == '.py':
            command = [
                tool('ruff'),
                'format',
                '--config',
                str(ROOT / 'ruff.toml'),
                '--stdin-filename',
                str(path),
                '-',
            ]
        else:
            command = [
                tool('clang-format'),
                '--style=file:' + str(ROOT / '.clang-format'),
                '--assume-filename=' + str(path),
            ]
        result = run(command, input=content, capture_output=True)
        outputs.append((name, path, content, result.stdout))
    for name, path, content, formatted in outputs:
        if format_source(name).read_bytes() != content:
            raise ValueError(f'{name}: file changed during formatting or is a symlink')
    for name, path, _, formatted in outputs:
        format_source(name).write_bytes(formatted)
        print(f'formatted working file: {name}; index unchanged')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--staged', action='store_true')
    group.add_argument('--base', help='Git revision to compare to HEAD')
    group.add_argument('--paths', nargs='+', help='explicit working-tree paths')
    parser.add_argument(
        '--tidy',
        action='store_true',
        help='deep lint with exact compilation-database/dependency preflight',
    )
    parser.add_argument('--build-dir', default='build')
    parser.add_argument(
        '--translation-unit',
        action='append',
        default=[],
        help='exact real includer for changed headers; @all scans database TUs',
    )
    parser.add_argument(
        '--format',
        action='store_true',
        help='opt-in working-file formatting with --paths; never stages',
    )
    args = parser.parse_args(argv)
    os.chdir(ROOT)
    try:
        if args.format and (args.paths is None or args.tidy):
            raise ValueError('--format requires --paths and cannot be combined with --tidy')
        changes = selected_changes(args)
        relevant = [
            (name, content, original)
            for name, content, original in changes
            if Path(name).suffix in CPP | {'.py', '.sh'} or name == '.githooks/pre-commit'
        ]
        revision = '' if args.staged else 'HEAD' if args.base else None
        if relevant:
            ensure_configuration(revision)
        if args.format:
            format_paths(relevant)
        else:
            for name, content, original in relevant:
                check_file(ROOT / name, content, original=original)
            if args.tidy:
                deep_check(relevant, args, revision)
        mode = 'deep lint' if args.tidy else 'fast checks (clang-tidy not run)'
        print(f'guardrails: {len(relevant)} selected source files; {mode}')
        return 0
    except (OSError, ValueError, KeyError, SyntaxError, subprocess.SubprocessError) as error:
        print(f'guardrails: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
