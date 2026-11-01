#!/usr/bin/env python3
"""Copy and identify a save directory before a run, without changing it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

REF = Path(__file__).resolve().parents[2] / 'ref'
REF = REF.resolve()


def manifest(root):
    files = {}
    for directory, dirs, names in os.walk(root, followlinks=False):
        for name in sorted(dirs + names):
            path = Path(directory) / name
            if path.is_symlink():
                raise ValueError(f'symlinks are not supported: {path}')
            if path.is_file():
                files[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
            elif not path.is_dir():
                raise ValueError(f'unsupported file: {path}')
    return dict(sorted(files.items()))


def snapshot(source, output):
    source, output = Path(source).resolve(strict=True), Path(output).resolve()
    if not source.is_dir():
        raise ValueError('source must be a directory')
    if output == REF or REF in output.parents:
        raise ValueError('output must be outside ref/')
    if output == source or source in output.parents:
        raise ValueError('output must be outside the source directory')
    before = manifest(source)
    output.mkdir(parents=True, exist_ok=False)
    shutil.copytree(source, output / 'save', symlinks=False)
    copied = manifest(output / 'save')
    after = manifest(source)
    if before != copied or before != after:
        raise ValueError('source changed while copying; discard this incomplete snapshot')
    info = {'source': str(source), 'files': copied}
    (output / 'manifest.json').write_text(json.dumps(info, indent=2) + '\n')
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', help='save directory before launch')
    parser.add_argument('output', help='new snapshot directory outside ref/')
    args = parser.parse_args()
    try:
        info = snapshot(args.source, args.output)
        print(f"Captured {len(info['files'])} files in {args.output}; source {info['source']}")
    except (OSError, ValueError) as error:
        parser.exit(2, f'save_snapshot: {error}\n')


if __name__ == '__main__':
    main()
