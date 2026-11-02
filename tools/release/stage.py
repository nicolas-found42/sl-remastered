#!/usr/bin/env python3
"""Stage explicitly selected game bytes in a developer-only macOS candidate.

The selected inventory binds bytes, not official provenance or reconciliation.
No Developer ID signing, notarization, DMG or release acceptance is performed.
Failed staging trees remain available for inspection and are never overwritten.
Run through the project's shared admission and artifact-budget wrapper.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


catalog = load('stage_catalog', ROOT / 'tools/parity/catalog.py')
bundle = load('stage_bundle', ROOT / 'tools/release/bundle.py')
data = catalog.data


def ordinary_path(value, directory=False):
    path = Path(value)
    if '..' in path.parts or path.is_symlink():
        raise ValueError('input path must not contain traversal or be a symlink')
    resolved = path.resolve(strict=True)
    mode = resolved.stat().st_mode
    if not (stat.S_ISDIR(mode) if directory else stat.S_ISREG(mode)):
        raise ValueError('ordinary input directory or file required: ' + str(resolved))
    return resolved


def read_document(path):
    before = path.stat()
    if before.st_size > 8 * 1024 * 1024:
        raise ValueError('inventory document exceeds 8 MiB')
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(descriptor, 'rb') as stream:
        if data.fingerprint(before) != data.fingerprint(os.fstat(stream.fileno())):
            raise ValueError('inventory changed before reading')
        raw = stream.read(before.st_size + 1)
        if (len(raw) != before.st_size or
                data.fingerprint(before) != data.fingerprint(os.fstat(stream.fileno()))):
            raise ValueError('inventory changed during reading')
    if data.fingerprint(before) != data.fingerprint(path.stat()):
        raise ValueError('inventory changed after reading')
    return catalog.parse_json(raw), hashlib.sha256(raw).hexdigest()


def file_identity(path):
    before = path.stat()
    if not stat.S_ISREG(before.st_mode) or before.st_size > data.MAX_FILE_BYTES:
        raise ValueError('cursor carrier must be a bounded ordinary file')
    digest = hashlib.sha256()
    count = 0
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(descriptor, 'rb') as stream:
        if data.fingerprint(before) != data.fingerprint(os.fstat(stream.fileno())):
            raise ValueError('cursor carrier changed before reading')
        while True:
            block = stream.read(min(1024 * 1024, before.st_size - count + 1))
            if not block:
                break
            count += len(block)
            if count > before.st_size:
                raise ValueError('cursor carrier grew during reading')
            digest.update(block)
        if (count != before.st_size or
                data.fingerprint(before) != data.fingerprint(os.fstat(stream.fileno()))):
            raise ValueError('cursor carrier changed during reading')
    if data.fingerprint(before) != data.fingerprint(path.stat()):
        raise ValueError('cursor carrier changed after reading')
    return {'size': count, 'sha256': digest.hexdigest()}


def inventory(root):
    value = data.collect(root, exclude_runtime=False)
    catalog.validate_inventory(value)
    return value


def overlap(first, second):
    return first == second or first in second.parents or second in first.parents


def require_same_files(expected, actual, label):
    if (expected['files'] != actual['files'] or
            expected['hashed_bytes'] != actual['hashed_bytes']):
        raise ValueError(label + ' file bytes differ from the selected inventory')


def stage_application(application, content, inventory_path, cursor, cursor_sha256, output):
    application = ordinary_path(application, directory=True)
    content = ordinary_path(content, directory=True)
    inventory_path = ordinary_path(inventory_path)
    cursor = ordinary_path(cursor)
    supplied_output = Path(output)
    if '..' in supplied_output.parts or supplied_output.is_symlink():
        raise ValueError('output path must not contain traversal or be a symlink')
    output = supplied_output.resolve()
    if application.suffix != '.app' or output.suffix != '.app':
        raise ValueError('application and output must be .app directories')
    if any(overlap(output, path) for path in
           (application, content, inventory_path, cursor, ROOT / 'ref', ROOT / '.git')):
        raise ValueError('output overlaps an input or protected reference/Git path')
    if output.exists():
        raise ValueError('output must be absent; retained candidates are never overwritten')
    if not output.parent.is_dir():
        raise ValueError('output parent must already exist')
    if not catalog.sha256(cursor_sha256):
        raise ValueError('expected cursor SHA-256 must be 64 lowercase hexadecimal digits')
    selected, inventory_sha256 = read_document(inventory_path)
    if not isinstance(selected, dict) or type(selected.get('format')) is not int or selected['format'] != 1:
        raise ValueError('expected complete diagnostic-data format-1 inventory')
    catalog.validate_inventory(selected)
    if selected.get('game_root') != str(content):
        raise ValueError('inventory does not name the explicitly selected content root')
    app_before = inventory(application)
    content_before = inventory(content)
    require_same_files(selected, content_before, 'selected content')
    cursor_before = file_identity(cursor)
    if cursor_before['sha256'] != cursor_sha256:
        raise ValueError('cursor resource carrier SHA-256 differs')
    for relative in ('Contents/Resources/Game', 'Contents/Resources/StockCursorResources.exe'):
        path = application / relative
        if path.exists() or path.is_symlink():
            raise ValueError('built application already contains reserved staged resources: ' + relative)
    # Keep any raced source symlink as a link, then refuse it in the complete
    # inventory, rather than following it into a separately owned source tree.
    shutil.copytree(application, output, symlinks=True)
    require_same_files(app_before, inventory(output), 'copied application')
    resources = output / 'Contents/Resources'
    resources.mkdir(exist_ok=True)
    staged_content = resources / 'Game'
    staged_cursor = resources / 'StockCursorResources.exe'
    shutil.copytree(content, staged_content, symlinks=True)
    shutil.copy2(cursor, staged_cursor, follow_symlinks=False)
    require_same_files(selected, inventory(staged_content), 'staged content')
    if staged_cursor.is_symlink() or file_identity(staged_cursor) != cursor_before:
        raise ValueError('staged cursor resource carrier differs')
    # All resources precede the dependency helper's final ad-hoc bundle seal.
    dependencies = bundle.bundle_libraries(output)
    require_same_files(app_before, inventory(application), 'source application')
    require_same_files(content_before, inventory(content), 'source content')
    selected_after, document_after = read_document(inventory_path)
    if document_after != inventory_sha256 or not catalog.same_json(selected, selected_after):
        raise ValueError('selected inventory changed during staging')
    if cursor.is_symlink() or file_identity(cursor) != cursor_before:
        raise ValueError('source cursor resource carrier changed during staging')
    require_same_files(selected, inventory(staged_content), 'sealed content')
    if file_identity(staged_cursor) != cursor_before:
        raise ValueError('sealed cursor resource carrier differs')
    bundle.command('codesign', '--verify', '--strict', output)
    final_inventory = inventory(output)
    return {'schema': 1, 'status': 'developer_candidate', 'release_accepted': False,
            'scope': 'explicitly selected bytes; no official provenance, reconciliation or release acceptance',
            'source_bytes_preserved': True, 'signing': 'ad-hoc',
            'application': app_before,
            'selected_content': {'path': str(content), 'inventory_path': str(inventory_path),
                                 'inventory_sha256': inventory_sha256,
                                 'files': len(selected['files']), 'bytes': selected['hashed_bytes']},
            'cursor': {'path': str(cursor), **cursor_before},
            'dependencies': dependencies, 'output': final_inventory}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('application', 'content', 'inventory', 'cursor', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cursor-sha256', required=True)
    args = parser.parse_args()
    try:
        report = stage_application(args.application, args.content, args.inventory,
                                   args.cursor, args.cursor_sha256, args.out)
        print(json.dumps(report, indent=2))
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(1, f'stage: {error}\n')


if __name__ == '__main__':
    main()
