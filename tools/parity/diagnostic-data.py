#!/usr/bin/env python3
"""Capture a bounded prelaunch game-data manifest, not a file-access trace."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat

REF = Path(__file__).resolve().parents[2] / 'ref'
MAX_FILES = 50000
MAX_DIRS = 20000
MAX_FILE_BYTES = 2 * 1024**3
MAX_TOTAL_BYTES = 8 * 1024**3
# Saves and runtime products are documented independently of game data.
EXCLUDED_ROOT = {
    'save': 'writable save data', 'dumps': 'capture output',
    'diag.log': 'runtime log', 'res_ban.log': 'runtime log',
    'd3d9.dll': 'capture proxy', 'dinput8.dll': 'capture proxy',
    'mcicda.dll': 'capture proxy',
    'streetlegal_redline.exe': 'executable identified separately',
    'streetlegal_redline.bak.exe': 'executable identified separately',
}


def fingerprint(st):
    return (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns)


def collect(root, max_files=MAX_FILES, max_dirs=MAX_DIRS,
            max_file=MAX_FILE_BYTES, max_total=MAX_TOTAL_BYTES, exclude_runtime=True):
    root = Path(root).resolve(strict=True)
    if not root.is_dir():
        raise ValueError('game root must be a directory')
    items, exclusions, errors, directories = {}, [], [], {}
    used, seen = 0, 0
    def walk_error(error):
        errors.append({'path': str(error.filename), 'reason': str(error)})
    for directory, dirs, names in os.walk(root, topdown=True, followlinks=False, onerror=walk_error):
        directory = Path(directory)
        relative_dir = directory.relative_to(root)
        if len(directories) >= max_dirs:
            errors.append({'path': relative_dir.as_posix(), 'reason': 'directory count limit; subtree not scanned'})
            dirs.clear(); continue
        try:
            directories[directory] = fingerprint(directory.stat())
        except OSError as error:
            errors.append({'path': relative_dir.as_posix(), 'reason': str(error)})
            dirs.clear(); continue
        dirs.sort(); names.sort()
        for name in list(dirs):
            path = directory / name; rel = path.relative_to(root).as_posix()
            reason = EXCLUDED_ROOT.get(name.lower()) if exclude_runtime and directory == root else None
            if reason:
                exclusions.append({'path': rel + '/', 'reason': reason}); dirs.remove(name)
            elif path.is_symlink():
                errors.append({'path': rel, 'reason': 'directory symlink unsupported'}); dirs.remove(name)
        for name in names:
            path = directory / name; rel = path.relative_to(root).as_posix()
            reason = EXCLUDED_ROOT.get(name.lower()) if exclude_runtime and directory == root else None
            if reason:
                exclusions.append({'path': rel, 'reason': reason}); continue
            seen += 1
            if seen > max_files:
                errors.append({'path': rel, 'reason': 'file count limit; remaining files not scanned'})
                dirs.clear(); break
            try:
                if path.is_symlink(): raise ValueError('symlink unsupported')
                before = path.stat()
                if not stat.S_ISREG(before.st_mode): raise ValueError('not a regular file')
                if before.st_size > max_file or used + before.st_size > max_total:
                    raise ValueError('byte limit')
                digest = hashlib.sha256(); count = 0
                fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
                with os.fdopen(fd, 'rb') as f:
                    if fingerprint(before) != fingerprint(os.fstat(f.fileno())):
                        raise ValueError('file changed before reading')
                    while True:
                        chunk = f.read(min(1 << 20, before.st_size - count + 1))
                        if not chunk: break
                        count += len(chunk)
                        if count > before.st_size: raise ValueError('file grew during reading')
                        digest.update(chunk)
                    after_fd = os.fstat(f.fileno())
                if count != before.st_size or fingerprint(before) != fingerprint(after_fd) or fingerprint(before) != fingerprint(path.stat()):
                    raise ValueError('file changed during reading')
                used += count
                items[rel] = {'size': count, 'sha256': digest.hexdigest()}
            except (OSError, ValueError) as error:
                errors.append({'path': rel, 'reason': str(error)})
    for directory, before in directories.items():
        try:
            if fingerprint(directory.stat()) != before:
                errors.append({'path': directory.relative_to(root).as_posix(), 'reason': 'directory changed during capture'})
        except OSError as error:
            errors.append({'path': directory.relative_to(root).as_posix(), 'reason': str(error)})
    return {'format': 1, 'scope': ('prelaunch game-data tree' if exclude_runtime else 'complete file tree') + '; not a file-access trace or atomic snapshot',
            'game_root': str(root), 'complete': not errors, 'hashed_bytes': used,
            'limits': {'files': max_files, 'directories': max_dirs, 'file_bytes': max_file, 'total_bytes': max_total},
            'files': dict(sorted(items.items())), 'exclusions': exclusions, 'errors': errors}


def write_manifest(root, output):
    root = Path(root).resolve(strict=True); output = Path(output).resolve(); ref = REF.resolve()
    if output == ref or ref in output.parents or output == root or root in output.parents:
        raise ValueError('output must be outside ref/ and the game data root')
    result = collect(root)
    with output.open('x') as file:
        json.dump(result, file, indent=2); file.write('\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game_root'); parser.add_argument('output'); args = parser.parse_args()
    try:
        result = write_manifest(args.game_root, args.output)
    except (OSError, ValueError) as error:
        parser.exit(2, f'diagnostic data: {error}\n')
    print(f"hashed {len(result['files'])} files; complete={result['complete']}; {args.output}")


if __name__ == '__main__': main()
