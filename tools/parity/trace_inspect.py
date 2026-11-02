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
    parser.add_argument('--before', required=True, help='exact JSON selector')
    parser.add_argument('--after', required=True, help='exact JSON selector')
    parser.add_argument('--fields', required=True, help='JSON object mapping names to word offsets')
    args = parser.parse_args(argv)
    try:
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
