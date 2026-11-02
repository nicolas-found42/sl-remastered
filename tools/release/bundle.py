#!/usr/bin/env python3
"""Relocate a staged ARM64 application's dylib closure for local qualification.

Changes only the supplied staging bundle. Ad-hoc signatures enable local loader
checks; they are not Developer ID signing, notarization or release acceptance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess


def command(*argv):
    result = subprocess.run(list(map(str, argv)), capture_output=True,
                            text=True, timeout=30)
    if result.returncode:
        raise ValueError(f'{argv[0]} exited {result.returncode}: {result.stderr.strip()}')
    return result.stdout


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def system_library(name):
    return os.path.normpath(name).startswith(('/usr/lib/', '/System/Library/'))


def load_commands(path):
    identity_lines = command('otool', '-arch', 'arm64', '-D', path).splitlines()[1:]
    identity = identity_lines[0].strip() if identity_lines else None
    libraries = []
    for line in command('otool', '-arch', 'arm64', '-L', path).splitlines()[1:]:
        match = re.fullmatch(r'\s+(.+) \(compatibility version [^,]+, current version [^)]+\)', line)
        if not match:
            raise ValueError('unrecognized library load command: ' + line)
        if match[1] != identity:
            libraries.append(match[1])
    rpaths = re.findall(r'cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.+) \(offset \d+\)',
                       command('otool', '-arch', 'arm64', '-l', path))
    return libraries, rpaths


def expand(name, loader, executable):
    for prefix, root in (('@loader_path/', loader.parent), ('@executable_path/', executable.parent)):
        if name == prefix[:-1]:
            return root
        if name.startswith(prefix):
            return root / name[len(prefix):]
    if name.startswith('/'):
        return Path(name)
    raise ValueError('unsupported relative load path: ' + name)


def bundle_libraries(application):
    app = Path(application).resolve(strict=True)
    info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
    name = info['CFBundleExecutable']
    if not isinstance(name, str) or not name or Path(name).name != name:
        raise ValueError('invalid bundle executable name')
    executable = app / 'Contents/MacOS' / name
    if not executable.is_file() or executable.resolve() != executable:
        raise ValueError('ordinary bundle executable required')
    frameworks = app / 'Contents/Frameworks'
    if frameworks.exists() and (frameworks.is_symlink() or any(frameworks.iterdir())):
        raise ValueError('dependency staging directory must be absent or empty')
    # Resolve the entire source graph before modifying a binary. Shared images
    # reached with incompatible runpaths refuse instead of choosing one silently.
    graph = {}
    visited = set()
    pending = [(executable, ())]
    while pending:
        source, inherited = pending.pop()
        libraries, raw_rpaths = load_commands(source)
        # Preserve dyld search precedence while making repeated cyclic paths
        # converge. Every distinct inherited context still resolves its edges.
        rpaths = tuple(dict.fromkeys(tuple(expand(p, source, executable).resolve()
                                          for p in raw_rpaths) + inherited))
        context = (source, rpaths)
        if context in visited:
            continue
        if len(visited) >= 4096:
            raise ValueError('dependency context limit exceeded')
        visited.add(context)
        edges = {}
        for library in libraries:
            if system_library(library):
                continue
            if library.startswith('@rpath/'):
                candidates = [p / library[len('@rpath/'):] for p in rpaths]
                target = next((p for p in candidates if p.is_file()), None)
                if target is None:
                    raise ValueError('unresolved library: ' + library)
            else:
                target = expand(library, source, executable)
            target = target.resolve(strict=True)
            if not target.is_file():
                raise ValueError('library is not an ordinary file: ' + str(target))
            edges[library] = target
        if source in graph:
            if graph[source]['edges'] != edges:
                raise ValueError('incompatible inherited runpaths: ' + str(source))
        else:
            architectures = command('lipo', '-archs', source).split()
            if 'arm64' not in architectures:
                raise ValueError('dependency lacks ARM64: ' + str(source))
            if source == executable and architectures != ['arm64']:
                raise ValueError('application executable must be thin ARM64')
            graph[source] = dict(edges=edges, rpaths=raw_rpaths, sha256=digest(source),
                                 architectures=architectures)
        pending.extend((target, rpaths) for target in edges.values())
    frameworks.mkdir(exist_ok=True)
    destinations = {executable: executable}
    for source, row in graph.items():
        if source == executable:
            continue
        # Digest suffix avoids silently merging unrelated libraries with the same
        # install name. Paths stay deterministic for identical dependency bytes.
        target = frameworks / (source.stem + '-' + row['sha256'][:16] + '.dylib')
        if target.exists():
            raise ValueError('duplicate dependency destination: ' + target.name)
        if row['architectures'] == ['arm64']:
            shutil.copy2(source, target)
        else:
            command('lipo', source, '-thin', 'arm64', '-output', target)
        target.chmod(0o755)
        destinations[source] = target
    for source, row in graph.items():
        target = destinations[source]
        changes = []
        for original, dependency in row['edges'].items():
            relative = os.path.relpath(destinations[dependency], target.parent)
            changes += ['-change', original, '@loader_path/' + relative]
        for rpath in dict.fromkeys(row['rpaths']):
            changes += ['-delete_rpath', rpath]
        if source != executable:
            changes += ['-id', '@rpath/' + target.name]
        if changes:
            command('install_name_tool', *changes, target)
    # Sign nested code first, then seal the complete bundle after all edits.
    for source, target in destinations.items():
        if source != executable:
            command('codesign', '--force', '--sign', '-', target)
    command('codesign', '--force', '--sign', '-', app)
    for source, target in destinations.items():
        libraries, rpaths = load_commands(target)
        if rpaths:
            raise ValueError('unremoved dependency search path')
        for library in libraries:
            if not system_library(library):
                resolved = expand(library, target, executable).resolve(strict=True)
                if resolved not in destinations.values():
                    raise ValueError('dependency escaped staged bundle: ' + library)
        if source != executable and digest(source) != graph[source]['sha256']:
            raise ValueError('source dependency changed during staging')
        command('codesign', '--verify', '--strict', target)
    return {'schema': 1, 'scope': 'local dependency relocation; no release acceptance',
            'signing': 'ad-hoc', 'executable': str(executable.relative_to(app)),
            'executable_sha256': digest(executable),
            'libraries': [{'source': str(source), 'source_sha256': graph[source]['sha256'],
                           'bundled_path': str(target.relative_to(app)), 'sha256': digest(target)}
                          for source, target in destinations.items() if source != executable]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('application', type=Path, help='private staging .app; modified in place')
    args = parser.parse_args()
    try:
        print(json.dumps(bundle_libraries(args.application), indent=2))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, f'bundle: {error}\n')


if __name__ == '__main__':
    main()
