"""Validate the declared application recorder boundary; never whole-process replay."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import stat
import sys


BOUNDARY = 'main_image_before_ExitProcess_after_CRT_callbacks'


def control_stop_evidence(text):
    """Require one whole successful acknowledgement, with native CRLF or LF."""
    values = re.findall(r'^\[[ \t]*[0-9]+ ms\] control_tape_stop result=(-?[0-9]+)\r?$', text, re.MULTILINE)
    count = int(values[0]) if len(values) == 1 else None
    return {'passed': len(values) == text.count('control_tape_stop result=') == 1 and
                     not values[0].startswith('-') and 0 <= count <= 65536,
            'observations': len(values), 'committed_api_rows': count}


def read(path, maximum):
    path = Path(path)
    if any(p.is_symlink() for p in (path, *path.parents)):
        raise ValueError('symlink in recorder shutdown artifact')
    info = path.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_size > maximum:
        raise ValueError('invalid/oversized recorder shutdown artifact')
    with path.open('rb') as stream:
        raw = stream.read(maximum + 1)
    if len(raw) > maximum:
        raise ValueError('oversized recorder shutdown artifact')
    return raw


def decode(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate shutdown JSON key')
            result[key] = value
        return result
    def constant(value):
        raise ValueError('nonfinite shutdown JSON: ' + value)
    return json.loads(raw, object_pairs_hook=pairs, parse_constant=constant)


def inspect(out, interval):
    directory = Path(out) / 'game/dumps'
    report_raw = read(directory / 'stock-control-shutdown.json', 4096)
    report = decode(report_raw)
    fields = {'schema', 'boundary', 'dll_detach_observed', 'caller', 'exit_code',
              'guarded', 'stop_result', 'active', 'gate', 'dropped', 'gaps', 'entered', 'returned'}
    numeric = fields - {'boundary', 'dll_detach_observed', 'guarded'}
    if set(report) != fields or any(type(report[k]) is not int for k in numeric):
        raise ValueError('invalid typed recorder shutdown schema')
    if (report['schema'] != 1 or report['boundary'] != BOUNDARY or
            report['dll_detach_observed'] is not False or report['guarded'] is not True or
            report['exit_code'] != 0 or report['stop_result'] < 0 or
            report['active'] != 0 or report['gate'] != 0x80000000 or
            report['dropped'] != 0 or report['gaps'] != 0 or
            not 0 < report['entered'] == report['returned'] <= 0x7fffffff or
            report['stop_result'] > report['entered']):
        raise ValueError('recorder shutdown guard/drain/exit failed')
    raw = read(directory / 'stock-control-tape.jsonl', 256 * 1024**2)
    digest = hashlib.sha256(raw).hexdigest()
    if digest != interval['source_sha256']:
        raise ValueError('shutdown tape differs from compiled interval')
    rows = [decode(line) for line in raw.splitlines() if line.strip()]
    if not rows or rows[-1].get('event') != 'status':
        raise ValueError('missing final recorder status')
    final = rows[-1]
    for key in ('entered', 'returned', 'active', 'dropped', 'gaps'):
        if type(final.get(key)) is not int or final[key] != report[key]:
            raise ValueError('shutdown report differs from actual final status: ' + key)
    if any(type(final.get(key)) is not int or final[key] != 0 for key in ('pending', 'accepting')):
        raise ValueError('shutdown has pending/accepted observations')
    bases = {r['image_base'] for r in rows if r.get('event') == 'initial_global'}
    if len(bases) != 1 or report['caller'] != next(iter(bases)) + 0x1d796f:
        raise ValueError('shutdown caller differs from observed main image')
    return {'passed': True, 'boundary': BOUNDARY, 'report': report,
            'report_sha256': hashlib.sha256(report_raw).hexdigest(), 'tape_sha256': digest,
            'limits': ['new roots after atomic admission close excluded',
                       'DLL detach excluded', 'thread incarnations unqualified',
                       'no whole-process or replay qualification']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stop-receipt', type=Path, required=True)
    args = parser.parse_args()
    try:
        receipt = control_stop_evidence(read(args.stop_receipt, 4 * 1024**2).decode('utf-8'))
    except (OSError, ValueError) as error:
        print('recorder stop receipt: ' + str(error), file=sys.stderr)
        raise SystemExit(2)
    print(json.dumps(receipt))
    raise SystemExit(0 if receipt['passed'] else 1)
