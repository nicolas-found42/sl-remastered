"""Read-only comparison of exact named phases in retained JSONL traces.

Word offsets are explicit observations, not inferred structure definitions.
Missing or ambiguous selectors refuse; no runtime lookup can perturb cache LRU.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import os


def body_series(path, steps, bodies=2):
    """Read the constructor/initial/final fixture format with two 53-word states."""
    if type(steps) is not int or not 1 <= steps <= 10000:
        raise ValueError('bounded positive step count required')
    if type(bodies) is not int or not 1 <= bodies <= 64:
        raise ValueError('bounded positive body count required')
    if bodies * (1 + 2 * steps) > 100000:
        raise ValueError('body phase count exceeds trace row bound')
    expected = [('constructor', 0, index) for index in range(bodies)]
    expected += [(phase, step, index) for step in range(steps)
                 for phase in ('step-initial', 'step-final') for index in range(bodies)]
    rows = []
    digest = hashlib.sha256()
    count = 0
    with Path(path).open('rb') as source:
        initial = os.fstat(source.fileno())
        for line_number in range(1, 100001):
            raw = source.readline(1024 * 1024 + 1)
            if not raw:
                break
            count += len(raw)
            if len(raw) > 1024 * 1024 or count > 64 * 1024 * 1024:
                raise ValueError('trace size bound exceeded')
            digest.update(raw)
            row = json.loads(raw)
            if not isinstance(row, dict):
                raise ValueError('trace row must be an object')
            if row.get('type') != 'body':
                continue
            if any(type(row.get(name)) is not int for name in ('step', 'index', 'current', 'flags')):
                raise ValueError('body identifiers, flags and current must be integers')
            key = (row.get('phase'), row['step'], row['index'])
            if len(rows) >= len(expected) or key != expected[len(rows)]:
                raise ValueError('incomplete, duplicate or out-of-order body phases')
            current = 0 if key[0] == 'constructor' else (key[1] + (key[0] == 'step-final')) % 2
            if row['current'] != current:
                raise ValueError('body current buffer identity disagrees with phase')
            words = row.get('states')
            if not isinstance(words, list) or len(words) != 106 or any(
                    type(value) is not int or not 0 <= value <= 0xffffffff for value in words):
                raise ValueError('body phase needs exactly 106 unsigned 32-bit words')
            if not 0 <= row['flags'] <= 0xffffffff:
                raise ValueError('body flags must be unsigned 32-bit')
            rows.append({'line': line_number, 'key': key, 'current': current,
                         'flags': row['flags'], 'states': words})
        else:
            raise ValueError('trace row bound exceeded')
        fingerprint = lambda value: (value.st_dev, value.st_ino, value.st_size,
                                      value.st_mtime_ns, value.st_ctime_ns)
        if fingerprint(initial) != fingerprint(os.fstat(source.fileno())) or fingerprint(initial) != fingerprint(Path(path).stat()):
            raise ValueError('trace changed during observation')
    if len(rows) != len(expected):
        raise ValueError('incomplete body phase set')
    return {'path': str(path), 'sha256': digest.hexdigest(), 'body_records': len(rows)}, rows


def compare_body_series(left, right, steps, bodies=2):
    bindings = []
    series = []
    for path in (left, right):
        binding, rows = body_series(path, steps, bodies)
        bindings.append(binding)
        series.append(rows)
    differences = []
    for a, b in zip(*series):
        words = [{'offset': index, 'left': x, 'right': y}
                 for index, (x, y) in enumerate(zip(a['states'], b['states'])) if x != y]
        if words or a['flags'] != b['flags']:
            differences.append({'key': a['key'], 'left_line': a['line'], 'right_line': b['line'],
                                'current': a['current'], 'left_flags': a['flags'],
                                'right_flags': b['flags'], 'words': words})
    return {'sources': bindings, 'steps': steps, 'bodies': bodies,
            'equal': not differences, 'differing_body_records': len(differences),
            'first_difference': differences[0] if differences else None,
            'scope': 'Complete ordered constructor/initial/final body snapshots only; '
                     'two 53-word buffers starting at current=0. No intermediate contact causality or game parity.'}


def compare(path, before, after, fields):
    if any(not isinstance(value, dict) for value in (before, after, fields)):
        raise ValueError('selectors and fields must be objects')
    if not before or not after or not fields or before == after:
        raise ValueError('distinct nonempty selectors and fields required')
    if any(type(offset) is not int or offset < 0 for offset in fields.values()):
        raise ValueError('nonnegative integer word offsets required')
    matches = [[], []]
    digest = hashlib.sha256()
    count = 0
    with Path(path).open('rb') as source:
        initial = os.fstat(source.fileno())
        for line_number in range(1, 100001):
            raw = source.readline(1024 * 1024 + 1)
            if not raw:
                break
            count += len(raw)
            if len(raw) > 1024 * 1024 or count > 64 * 1024 * 1024:
                raise ValueError('trace size bound exceeded')
            digest.update(raw)
            row = json.loads(raw)
            if not isinstance(row, dict):
                raise ValueError('trace row must be an object')
            for index, selector in enumerate((before, after)):
                observed = dict(row, **{'@line': line_number})
                if all(key in observed and type(observed[key]) is type(value) and observed[key] == value
                       for key, value in selector.items()):
                    words = row.get('words')
                    if not isinstance(words, list) or any(
                            type(value) is not int or not 0 <= value <= 0xffffffff for value in words):
                        raise ValueError('selected row needs unsigned 32-bit words')
                    if max(fields.values()) >= len(words):
                        raise ValueError('field offset outside captured words')
                    matches[index].append({'line': line_number,
                                           'values': {name: words[offset] for name, offset in fields.items()}})
                    if len(matches[index]) > 1:
                        raise ValueError('ambiguous phase; include step, body index and correction')
        else:
            raise ValueError('trace row bound exceeded')
        current = os.fstat(source.fileno())
        named = Path(path).stat()
        fingerprint = lambda value: (value.st_dev, value.st_ino, value.st_size,
                                      value.st_mtime_ns, value.st_ctime_ns)
        if fingerprint(initial) != fingerprint(current) or fingerprint(initial) != fingerprint(named):
            raise ValueError('trace changed during observation')
    if any(len(found) != 1 for found in matches):
        raise ValueError('missing selected phase')
    left, right = matches[0][0], matches[1][0]
    if left['line'] >= right['line']:
        raise ValueError('before phase must precede after phase')
    return {'source': str(path), 'sha256': digest.hexdigest(), 'before_selector': before,
            'after_selector': after, 'word_offsets': fields, 'before': left, 'after': right,
            'changed': [name for name in fields if left['values'][name] != right['values'][name]],
            'scope': 'Captured selected words only; no inferred cache chain or contact lifecycle.'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--before', help='exact JSON selector')
    parser.add_argument('--after', help='exact JSON selector')
    parser.add_argument('--fields', help='JSON object mapping names to word offsets')
    parser.add_argument('--body-series', type=Path, help='compare complete fixture body phases with this trace')
    parser.add_argument('--steps', type=int, help='required complete steps in each body series')
    parser.add_argument('--bodies', type=int, default=2)
    args = parser.parse_args(argv)
    try:
        if args.body_series is not None:
            if any(value is not None for value in (args.before, args.after, args.fields)):
                raise ValueError('body series and selected-word modes cannot be combined')
            print(json.dumps(compare_body_series(args.trace, args.body_series, args.steps, args.bodies), indent=2))
            return 0
        if args.steps is not None or args.bodies != 2:
            raise ValueError('steps and bodies require body-series mode')
        if any(value is None for value in (args.before, args.after, args.fields)):
            raise ValueError('before, after and fields are required for selected-word mode')
        selectors = [json.loads(value) for value in (args.before, args.after, args.fields)]
        if any(not isinstance(value, dict) for value in selectors):
            raise ValueError('selectors and fields must be JSON objects')
        print(json.dumps(compare(args.trace, *selectors), indent=2))
        return 0
    except (OSError, ValueError, TypeError) as error:
        print(f'trace inspection refused: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
