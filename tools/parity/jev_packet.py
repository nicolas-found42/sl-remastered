"""Build bounded Jev requests from explicitly bound, hashed local evidence.

This checks provenance and declared coverage, not whether excerpts support prose.
Jev still judges support. This command never submits data or changes thresholds.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

MAX_SOURCE = 8 * 1024 * 1024
MAX_PACKET = 32000


def text(value, limit, label):
    if not isinstance(value, str) or not value.strip() or len(value) > limit:
        raise ValueError(f'{label}: nonempty text within {limit} characters required')
    return value


def excerpt(reference, root):
    if set(reference) != {'path', 'sha256', 'start_line', 'end_line', 'quote'}:
        raise ValueError('evidence requires path, sha256, exact line range and quote')
    path = Path(reference['path'])
    path = path if path.is_absolute() else root / path
    path = path.absolute()
    if path.resolve(strict=True) != path or not path.is_file():
        raise ValueError('evidence must be a canonical regular file')
    if not re.fullmatch('[0-9a-f]{64}', reference['sha256']):
        raise ValueError('invalid sha256')
    with path.open('rb') as source:
        raw = source.read(MAX_SOURCE + 1)
    if len(raw) > MAX_SOURCE:
        raise ValueError('source exceeds bounded reader; split source explicitly')
    if hashlib.sha256(raw).hexdigest() != reference['sha256']:
        raise ValueError('evidence hash changed')
    lines = raw.decode('utf-8').splitlines(keepends=True)
    start, end = reference['start_line'], reference['end_line']
    if type(start) is not int or type(end) is not int or not 1 <= start <= end <= len(lines):
        raise ValueError('invalid excerpt range')
    value = ''.join(lines[start - 1:end])
    if value != reference['quote'] or not value.strip():
        raise ValueError('excerpt does not match declared quote')
    return value


def build(manifest, root):
    if (set(manifest) - {'schema', 'claims', 'evidence', 'request', 'diff'}
            or type(manifest.get('schema')) is not int or manifest.get('schema') != 1):
        raise ValueError('unsupported packet schema or fields')
    claims, sources = manifest['claims'], manifest['evidence']
    if not isinstance(claims, list) or not 1 <= len(claims) <= 16:
        raise ValueError('one to sixteen claims required')
    if not isinstance(sources, dict) or not 1 <= len(sources) <= 16:
        raise ValueError('one to sixteen evidence items required')
    evidence = {}
    for key, reference in sources.items():
        if not re.fullmatch('[A-Za-z0-9_-]+', key):
            raise ValueError('invalid evidence id')
        evidence[key] = excerpt(reference, root)
    used = set()
    claim_text = []
    bindings = []
    for claim in claims:
        if set(claim) != {'text', 'requires'}:
            raise ValueError('claim needs text and required evidence ids')
        value = text(claim['text'], 2000, 'claim')
        required = claim['requires']
        if not isinstance(required, list) or not required or any(not isinstance(key, str) for key in required):
            raise ValueError('claim must declare evidence')
        if len(set(required)) != len(required) or any(key not in evidence for key in required):
            raise ValueError('missing or duplicated evidence binding')
        used.update(required)
        claim_text.append(value)
        bindings.append({'claim': value, 'evidence_ids': required})
    if used != set(evidence):
        raise ValueError('unbound evidence; remove unrelated material')
    payload = {'claims': claim_text, 'evidence': [{'id': key, 'text': value} for key, value in evidence.items()]}
    gate = 'request' in manifest or 'diff' in manifest
    if gate:
        payload['request'] = text(manifest['request'], 50000, 'request')
        payload['diff'] = text(excerpt(manifest['diff'], root), 50000, 'diff')
    if len(json.dumps(payload, ensure_ascii=False)) > MAX_PACKET:
        raise ValueError('packet exceeds conservative 32000-character bound; split claims explicitly')
    return {'tool': 'jev_gate' if gate else 'jev_verify', 'arguments': payload,
            'bindings': bindings, 'source_bindings': sources,
            'support_verified': False, 'submitted': False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    args = parser.parse_args(argv)
    try:
        with args.manifest.open('rb') as source:
            raw = source.read(MAX_SOURCE + 1)
        if len(raw) > MAX_SOURCE:
            raise ValueError('manifest too large')
        print(json.dumps(build(json.loads(raw), args.root.resolve()), indent=2))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f'jev packet refused: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
