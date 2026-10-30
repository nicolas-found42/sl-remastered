#!/usr/bin/env python3
"""Check saved matched-native evidence integrity, not its origin or completeness."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
FIELDS = ('class', 'method', 'descriptor', 'handler')
ORIGINS = {'unverified', 'self-reported', 'project-source'}


def identity(row):
    if not isinstance(row, dict) or any(not isinstance(row.get(k), str) or not row[k] for k in FIELDS):
        raise ValueError('native identity is incomplete')
    if not re.fullmatch(r'[0-9a-f]{8}', row['handler']):
        raise ValueError('invalid native handler address')
    return tuple(row[k] for k in FIELDS)


def safe_path(root, name):
    if not isinstance(name, str) or not name:
        raise ValueError('artifact path is missing')
    path = Path(name)
    if path.is_absolute() or any(part in ('.', '..') for part in path.parts) or path.parts[0] == 'ref':
        raise ValueError(f'unsafe artifact path: {name}')
    current = root
    for part in path.parts:
        current = current / part
        if current.is_symlink():
            raise ValueError(f'artifact path contains a symlink: {name}')
    return current


def audit(root, manifest_path, registry_path):
    data = json.loads(manifest_path.read_text())
    if not isinstance(data, dict) or data.get('format') != 1:
        raise ValueError('unsupported evidence manifest')
    artifacts, natives = data.get('artifacts'), data.get('natives')
    if not isinstance(artifacts, dict) or not isinstance(natives, list):
        raise ValueError('invalid evidence manifest')
    with registry_path.open(newline='', encoding='utf-8') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        if not set(FIELDS + ('status',)).issubset(reader.fieldnames or []):
            raise ValueError('registry columns are missing')
        matched = {identity(row) for row in reader if row['status'] == 'matched'}
    rows = {}
    for row in natives:
        key = identity(row)
        if key in rows:
            raise ValueError(f'duplicate native evidence: {key}')
        refs = row.get('references')
        if not isinstance(refs, list) or not refs:
            raise ValueError(f'missing artifact references: {key}')
        if not any(isinstance(ref, dict) and isinstance(ref.get('anchor'), str) and key[3] in ref['anchor'] for ref in refs):
            raise ValueError(f'no handler-address anchor for {key}')
        rows[key] = refs
    if not matched or matched != rows.keys():
        raise ValueError(f'matched natives and evidence entries differ: missing {sorted(matched - rows.keys())}, extra {sorted(rows.keys() - matched)}')
    contents, errors = {}, []
    used = set()
    for key, refs in rows.items():
        for ref in refs:
            if (not isinstance(ref, dict) or not isinstance(ref.get('artifact'), str) or
                    not isinstance(ref.get('anchor'), str) or not ref['anchor']):
                raise ValueError(f'invalid evidence reference for {key}')
            used.add(ref['artifact'])
    if used != artifacts.keys():
        raise ValueError('artifact references and definitions differ')
    for name, artifact in artifacts.items():
        if not isinstance(artifact, dict) or artifact.get('origin') not in ORIGINS:
            raise ValueError(f'invalid origin label for {name}')
        expected = artifact.get('sha256', '')
        if not isinstance(expected, str) or not re.fullmatch(r'[0-9a-f]{64}', expected):
            raise ValueError(f'invalid SHA-256 for {name}')
        path = safe_path(root, artifact.get('path'))
        try:
            content = path.read_bytes()
        except OSError:
            errors.append(f'MISSING {name}: {path}')
            continue
        if hashlib.sha256(content).hexdigest() != expected:
            errors.append(f'CORRUPT {name}: SHA-256 mismatch for {path}')
            continue
        required = artifact.get('required_text', [])
        if not isinstance(required, list):
            raise ValueError(f'invalid required text for {name}')
        for text in required:
            if not isinstance(text, str) or not text or text.encode() not in content:
                errors.append(f'CORRUPT {name}: required text missing in {path}')
        contents[name] = content
        print(f'OK {name}: {path} (origin: {artifact["origin"]})')
    for key, refs in rows.items():
        for ref in refs:
            if not isinstance(ref, dict) or ref.get('artifact') not in artifacts or not isinstance(ref.get('anchor'), str) or not ref['anchor']:
                raise ValueError(f'invalid evidence reference for {key}')
            content = contents.get(ref['artifact'])
            if content is not None and ref['anchor'].encode() not in content:
                errors.append(f'CORRUPT {ref["artifact"]}: missing anchor for {key}')
    for error in errors:
        print(error, file=sys.stderr)
    print(f'native evidence: {len(rows)} matched entries; {len(errors)} artifact errors')
    print('Origin remains unverifiable: unverified and self-reported labels do not prove the executable or analysis used.')
    print('This checks artifact integrity and anchors only, not behavioral coverage, stock semantics, or test execution.')
    return 1 if errors else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--registry', type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        return audit(root, args.manifest or root / 'docs/native_evidence_artifacts.json', args.registry or root / 'docs/natives.tsv')
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f'native evidence: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
