"""Generate current attempt navigation without converting reports into proof.

The manifest orders attempts explicitly. Hashes and JSON assertions are checked
on every generation; execution and review status remain separate from completion.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys


def read_bound(binding, root):
    path = Path(binding['path'])
    path = path if path.is_absolute() else root / path
    if path.resolve(strict=True) != path.absolute() or not path.is_file():
        raise ValueError('canonical regular evidence file required')
    with path.open('rb') as source:
        raw = source.read(64 * 1024 * 1024 + 1)
    if len(raw) > 64 * 1024 * 1024:
        raise ValueError('evidence exceeds bounded reader')
    if hashlib.sha256(raw).hexdigest() != binding['sha256']:
        raise ValueError('source hash changed')
    return raw


def lookup(document, pointer):
    if pointer == '':
        return document
    if not isinstance(pointer, str) or not pointer.startswith('/'):
        raise ValueError('JSON pointer required')
    for part in pointer[1:].split('/'):
        part = part.replace('~1', '/').replace('~0', '~')
        document = document[int(part)] if isinstance(document, list) else document[part]
    return document


def generate(manifest, root):
    if type(manifest.get('schema')) is not int or manifest.get('schema') != 1:
        raise ValueError('unsupported index schema')
    deliverables = manifest['deliverables']
    if not isinstance(deliverables, dict) or not deliverables:
        raise ValueError('deliverables required')
    history = []
    ids = set()
    previous = {}
    for attempt in manifest['attempts']:
        identity, deliverable, sequence = attempt['id'], attempt['deliverable'], attempt['sequence']
        if not isinstance(identity, str) or not identity or identity in ids or deliverable not in deliverables:
            raise ValueError('duplicate attempt or unknown deliverable')
        if type(sequence) is not int or sequence <= previous.get(deliverable, -1):
            raise ValueError('attempt sequences must increase within each deliverable')
        if attempt['review'] not in ('pending', 'accepted', 'rejected', 'escalated'):
            raise ValueError('explicit review state required')
        if attempt['outcome'] not in ('passed', 'failed', 'incomplete'):
            raise ValueError('explicit execution outcome required')
        if not isinstance(attempt['scope'], str) or not attempt['scope'].strip():
            raise ValueError('bounded scope required')
        bindings = attempt['evidence']
        assertions = attempt['assertions']
        if not isinstance(bindings, dict) or not bindings or not assertions:
            raise ValueError('bound evidence and assertions required')
        ids.add(identity)
        previous[deliverable] = sequence
        row = {key: attempt[key] for key in ('id', 'deliverable', 'sequence', 'scope', 'review', 'outcome')}
        row.update(evidence=bindings, problems=[], evidence_valid=False, completion_proven=False)
        documents = {}
        for key, binding in bindings.items():
            try:
                documents[key] = read_bound(binding, root)
            except (OSError, ValueError, KeyError) as error:
                row['problems'].append(f'{key}: {error}')
        for assertion in assertions:
            try:
                value = lookup(json.loads(documents[assertion['evidence']]), assertion['pointer'])
                expected = assertion['equals']
                # Python equates True with 1, including nested lists/dicts.
                # Canonical JSON preserves those types at every depth.
                if json.dumps(value, sort_keys=True, allow_nan=False) != json.dumps(expected, sort_keys=True, allow_nan=False):
                    raise ValueError('assertion differs from retained observation')
            except (ValueError, KeyError, IndexError, TypeError) as error:
                row['problems'].append(f'assertion: {error}')
        row['evidence_valid'] = not row['problems']
        history.append(row)
    current = []
    for identity, description in deliverables.items():
        candidates = [row for row in history if row['deliverable'] == identity]
        current.append({'id': identity, 'description': description,
                        'latest': candidates[-1] if candidates else None,
                        'completion_proven': False})
    return {'schema': 1, 'current': current, 'history': history,
            'all_evidence_valid': all(row['evidence_valid'] for row in history),
            'scope': 'Navigation and declared-assertion checks; no automatic deliverable completion.'}


def markdown(report):
    lines = ['# Current evidence', '', report['scope'], '']
    for item in report['current']:
        row = item['latest']
        lines += [f"## {item['id']}: {item['description']}", '']
        if row is None:
            lines += ['No recorded attempt. Completion unproven.', '']
            continue
        lines += [f"Latest: {row['id']} (sequence {row['sequence']}).",
                  f"Execution: {row['outcome']}; review: {row['review']}; evidence valid: {row['evidence_valid']}.",
                  '', row['scope'], '', 'Completion remains unproven.', '']
        lines += [f"- {key}: `{binding['path']}` (SHA-256 `{binding['sha256']}`)"
                  for key, binding in row['evidence'].items()]
        lines += [f'- Problem: {problem}' for problem in row['problems']]
        lines.append('')
    lines += ['## Preserved attempt history', '']
    lines += [f"- {row['deliverable']} / {row['sequence']} / {row['id']}: {row['outcome']}, review {row['review']}, evidence valid {row['evidence_valid']}"
              for row in report['history']]
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    parser.add_argument('--markdown', action='store_true')
    args = parser.parse_args(argv)
    try:
        if args.manifest.stat().st_size > 4 * 1024 * 1024:
            raise ValueError('index manifest too large')
        report = generate(json.loads(args.manifest.read_text()), args.root.resolve())
        print(markdown(report) if args.markdown else json.dumps(report, indent=2))
        return 0 if report['all_evidence_valid'] else 1
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f'evidence index refused: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
