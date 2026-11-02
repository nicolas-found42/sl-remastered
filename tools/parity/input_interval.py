#!/usr/bin/env python3
"""Offline validator and Stock-consumption projection for a PROPOSED input tape.

Nothing records this format yet: no producer exists and no real Stock tape has been
validated. This module fixes the contract so a future observer and the Port projection
have one machine-checked target. It never launches or modifies anything and it does not
establish replay (`process_replay_qualified` is always false).

Stock rules encoded here come from the hashed PE (7d7746...edde) and the research notes:
immediate state `FUN_00556bc0`, buffered keys `FUN_00556e00` (docs/research/stock-input-
consumption.md) and the window-procedure cursor math at 004b8210..004b826c (re-read from
the instructions: integer width/height divisors, float32 constant 1.0 at 005f08f0,
`edi` = wParam, `[esp+0x20]` = lParam). Failure post-images follow Wine 11.16 only
(docs/research/input-observer-primary-sources.md) and are enforced only when the header
names that runtime.
"""
import argparse
from fractions import Fraction
import json
import struct
import sys
from pathlib import Path

STOCK_SHA256 = '7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde'
WINE_RUNTIME = 'wine-11.16'
SIZES = {'keyboard': 256, 'mouse': 16}
# Post-call VAs minus 0x400000, from the input-consumption research (FUN_00556bc0/00556e00).
STATE_SITES = {'keyboard': {'initial': 0x156da6, 'acquire': 0x156db2, 'retry': 0x156dc4},
               'mouse': {'initial': 0x156ce0, 'acquire': 0x156cec, 'retry': 0x156cfb}}
BUFFERED = {'acquire': 0x156e13, 'data': 0x156e3d}
CURSOR_MESSAGES = {0x200, 0x201, 0x202, 0x204, 0x205}
MAX_BYTES, MAX_RECORDS, MAX_LINE = 64 * 1024**2, 200000, 8192


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def uint(value, bits=32):
    require(type(value) is int and 0 <= value < (1 << bits), f'invalid uint{bits}: {value!r}')
    return value


def hexbytes(value, size):
    require(type(value) is str and len(value) == 2 * size, f'expected {size} hex bytes')
    try:
        return bytes.fromhex(value)
    except ValueError as error:
        raise ValueError('invalid hex bytes') from error


def signed32(value):
    return value - (1 << 32) if value & 0x80000000 else value


def round_to_precision(value, bits):
    """Round an exact Fraction to a `bits`-bit mantissa, ties to even (x87 RC=nearest)."""
    if value == 0:
        return Fraction(0)
    sign = -1 if value < 0 else 1
    magnitude = abs(value)
    exponent = 0
    while magnitude >= 2:
        magnitude /= 2
        exponent += 1
    while magnitude < 1:
        magnitude *= 2
        exponent -= 1
    scaled = magnitude * (1 << (bits - 1))
    floor = scaled.numerator // scaled.denominator
    remainder = scaled - floor
    if remainder > Fraction(1, 2) or (remainder == Fraction(1, 2) and floor % 2):
        floor += 1
    return sign * Fraction(floor) * Fraction(2) ** (exponent - bits + 1)


def x87_normalized_cursor_bits(packed_half, divisor, control_word):
    """float32 bits Stock stores for one cursor axis: fild; fidiv; fadd st,st; fsub 1.0f; fstp."""
    require(0 <= packed_half <= 0xffff and 0 < divisor <= 0x7fffffff, 'unsupported cursor operand')
    require(0 <= control_word <= 0xffff and (control_word >> 10) & 3 == 0,
            'only round-to-nearest x87 control words are supported')
    precision = {0: 24, 2: 53, 3: 64}.get((control_word >> 8) & 3)
    require(precision is not None, 'reserved x87 precision control')
    quotient = round_to_precision(Fraction(packed_half, divisor), precision)
    doubled = 2 * quotient
    result = round_to_precision(doubled - 1, precision)
    stored = round_to_precision(result, 24)  # fstp to float32
    return struct.unpack('<I', struct.pack('<f', float(stored)))[0]


def project_cursor(lparam, wparam, width, height, control_word, flags_before):
    """Everything the window procedure writes for a cursor message (004b8210..004b826c)."""
    x, y = lparam & 0xffff, (lparam >> 16) & 0xffff  # unsigned, not WM_MOUSEMOVE's signed contract
    return {'x_raw': x, 'y_raw': y,
            'norm_x_bits': x87_normalized_cursor_bits(x, width, control_word),
            'norm_y_bits': x87_normalized_cursor_bits(y, height, control_word),
            'flags_after': ((flags_before & 0xffff0000) | wparam) & 0xffffffff}


def project_buffered(hresult, count_out, data):
    """FUN_00556e00 acceptance: a key-down offset or None (event bytes are ignored otherwise)."""
    require(len(data) == 20, 'DIDEVICEOBJECTDATA is 20 bytes')
    offset, value = struct.unpack_from('<II', data)
    if signed32(hresult) < 0 or count_out != 1 or offset >= 256 or (value & 0x80) == 0:
        return None
    return {'key_down_offset': offset}  # timestamp/sequence/appdata are not read by Stock


def project_sample(kind, steps):
    """Bytes Stock keeps after one immediate-state poll of one device.

    `steps` = ordered dicts: initial state, optional acquire, optional retry state.
    Keyboard: a failed retry leaves the stale bytes it was given; mouse clears on any non-zero.
    """
    require(kind in SIZES and steps, 'unknown device or empty poll')
    zeros = bytes(SIZES[kind])
    initial = steps[0]
    if initial['hresult'] == 0:
        return initial['post']
    if len(steps) < 2 or steps[1]['kind'] != 'acquire' or steps[1]['hresult'] != 0:
        return zeros
    require(len(steps) == 3 and steps[2]['kind'] == 'state', 'successful acquire requires a retry state call')
    retry = steps[2]
    if kind == 'keyboard':
        return retry['post']
    return retry['post'] if retry['hresult'] == 0 else zeros


def read_rows(path):
    import os
    import stat
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    rows, used = [], 0
    with os.fdopen(fd, 'rb') as stream:
        require(stat.S_ISREG(os.fstat(stream.fileno()).st_mode), 'input tape must be a regular file')
        for line in iter(lambda: stream.readline(MAX_LINE + 1), b''):
            used += len(line)
            require(len(line) <= MAX_LINE and line.endswith(b'\n') and used <= MAX_BYTES and len(rows) < MAX_RECORDS,
                    'input tape size/line limit')
            rows.append(json.loads(line, object_pairs_hook=_unique,
                                   parse_constant=lambda v: require(False, 'nonfinite JSON number')))
    return rows


def _unique(pairs):
    out = {}
    for key, value in pairs:
        require(key not in out, f'duplicate JSON key: {key}')
        out[key] = value
    return out


COMMON = {'event', 'ticket', 'entry_phase', 'return_phase', 'caller_rva'}
FIELDS = {
    'di.state': COMMON | {'device', 'cb', 'hresult', 'pre', 'post'},
    'di.acquire': COMMON | {'device', 'hresult'},
    'di.data': COMMON | {'device', 'count_in', 'hresult', 'count_out', 'pre', 'post'},
    'win.callback': COMMON | {'message', 'wparam', 'lparam', 'width', 'height', 'x87_cw', 'flags_before',
                              'norm_x_bits', 'norm_y_bits', 'flags_after'},
}
HEADER = {'event', 'version', 'runtime', 'stock_sha256', 'scope'}
STATUS = {'event', 'counts', 'phases', 'dropped', 'gaps', 'pending', 'accepting'}


def compile_rows(rows, source_digest=None):
    require(rows and all(type(r) is dict for r in rows), 'input tape must contain JSON objects')
    header, status = rows[0], rows[-1]
    require(set(header) == HEADER and header['event'] == 'input_header' and header['version'] == 1,
            'missing/unsupported input header')
    require(header['stock_sha256'] == STOCK_SHA256, 'wrong Stock identity')
    require(header['scope'] == 'proposed_input_tape', 'unsupported scope')
    wine = header['runtime'] == WINE_RUNTIME
    require(type(header['runtime']) is str and header['runtime'], 'runtime must be named')
    require(set(status) == STATUS and status['event'] == 'input_status', 'missing input status')
    events = rows[1:-1]
    require(events, 'input tape has no observations')
    counts, tickets, phases = {}, set(), []
    for row in events:
        tag = row.get('event')
        require(tag in FIELDS and set(row) == FIELDS[tag], f'unknown event/fields: {tag!r}')
        for key in ('ticket', 'entry_phase', 'return_phase', 'caller_rva'):
            uint(row[key])
        require(row['ticket'] not in tickets, 'duplicate ticket')
        tickets.add(row['ticket'])
        require(0 < row['entry_phase'] < row['return_phase'], 'reversed/zero phase')
        phases += [row['entry_phase'], row['return_phase']]
        counts[tag] = counts.get(tag, 0) + 1
    require(sorted(phases) == list(range(1, len(phases) + 1)), 'phase gap or duplicate')
    require(status['counts'] == counts and status['phases'] == len(phases), 'status disagrees with records')
    require(status['dropped'] == status['gaps'] == status['pending'] == status['accepting'] == 0,
            'recording reported loss or was not stopped')
    events.sort(key=lambda r: r['entry_phase'])
    typed = []
    for row in events:
        tag, site = row['event'], row['caller_rva']
        if tag in ('di.state', 'di.acquire', 'di.data'):
            require(row['device'] in SIZES, 'unknown device')
            uint(row['hresult'])
        if tag == 'di.state':
            kind, cb = row['device'], row['cb']
            roles = {v: k for k, v in STATE_SITES[kind].items() if k != 'acquire'}
            require(cb == SIZES[kind] and site in roles, 'state call from unknown site/size')
            pre, post = hexbytes(row['pre'], cb), hexbytes(row['post'], cb)
            if wine and row['hresult'] != 0:
                # Wine 11.16 never touches the buffer on failure; a changed buffer is not that runtime.
                require(pre == post, 'failure changed the buffer on a Wine runtime')
            typed.append({'kind': 'state', 'device': kind, 'role': roles[site], 'hresult': row['hresult'],
                          'pre': pre, 'post': post, 'changed_bytes': [i for i in range(cb) if pre[i] != post[i]],
                          'row': row})
        elif tag == 'di.acquire':
            sites = {STATE_SITES[row['device']]['acquire']} | ({BUFFERED['acquire']} if row['device'] == 'keyboard' else set())
            require(site in sites, 'acquire from unknown site')
            typed.append({'kind': 'acquire', 'device': row['device'], 'hresult': row['hresult'],
                          'buffered': site == BUFFERED['acquire'], 'row': row})
        elif tag == 'di.data':
            require(row['device'] == 'keyboard' and site == BUFFERED['data'] and row['count_in'] == 1,
                    'buffered data call shape')
            uint(row['count_out'])
            pre, post = hexbytes(row['pre'], 20), hexbytes(row['post'], 20)
            typed.append({'kind': 'data', 'hresult': row['hresult'], 'count_out': row['count_out'],
                          'post': post, 'pre': pre, 'row': row})
        else:
            for key in ('message', 'wparam', 'lparam', 'width', 'height', 'flags_before', 'norm_x_bits',
                        'norm_y_bits', 'flags_after'):
                uint(row[key])
            uint(row['x87_cw'], 16)
            typed.append({'kind': 'callback', 'row': row})
    samples = _group_samples(typed)
    cursor = []
    for item in (t for t in typed if t['kind'] == 'callback'):
        row = item['row']
        if row['message'] not in CURSOR_MESSAGES:
            require(row['norm_x_bits'] == row['norm_y_bits'] == 0 and row['flags_after'] == row['flags_before'],
                    'non-cursor message carries cursor results')
            continue
        projected = project_cursor(row['lparam'], row['wparam'], row['width'], row['height'],
                                   row['x87_cw'], row['flags_before'])
        require((projected['norm_x_bits'], projected['norm_y_bits'], projected['flags_after']) ==
                (row['norm_x_bits'], row['norm_y_bits'], row['flags_after']),
                'recorded cursor results contradict Stock arithmetic')
        cursor.append({'ticket': row['ticket'], **projected})
    buffered = [dict(ticket=t['row']['ticket'], consumed=project_buffered(t['hresult'], t['count_out'], t['post']))
                for t in typed if t['kind'] == 'data']
    return {'format': 'stock-input-interval-offline/1', 'source_sha256': source_digest, 'runtime': header['runtime'],
            'process_replay_qualified': False,
            'scope': 'validator and projection only; no recorder produces this format yet',
            'immediate_samples': samples, 'buffered': buffered, 'cursor': cursor, 'counts': counts}


def _group_samples(typed):
    """Split per-device state/acquire events into Stock polls and enforce its control flow."""
    samples, open_poll = [], {}
    for item in typed:
        if item['kind'] == 'state':
            kind, role = item['device'], item['role']
            current = open_poll.get(kind)
            if role == 'initial':
                require(current is None, 'new poll started before the previous one finished')
                if item['hresult'] == 0:
                    samples.append({'device': kind, 'path': 'ok', 'consumed': project_sample(kind, [item]).hex()})
                else:
                    open_poll[kind] = [item]
            else:
                require(current is not None and len(current) == 2 and current[1]['hresult'] == 0,
                        'retry state without a successful acquire')
                current.append(item)
                samples.append({'device': kind, 'path': 'retry', 'consumed': project_sample(kind, current).hex()})
                del open_poll[kind]
        elif item['kind'] == 'acquire' and not item['buffered']:
            kind = item['device']
            current = open_poll.get(kind)
            require(current is not None and len(current) == 1, 'acquire without a failed initial state')
            current.append(item)
            if item['hresult'] != 0:
                samples.append({'device': kind, 'path': 'acquire-failed',
                                'consumed': project_sample(kind, current).hex()})
                del open_poll[kind]
    require(not open_poll, 'poll left unfinished')
    return samples


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('tape', type=Path)
    args = parser.parse_args()
    try:
        result = compile_rows(read_rows(args.tape))
        print(json.dumps({'status': 'validated_input_tape', 'counts': result['counts'],
                          'process_replay_qualified': False}))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps({'status': 'inconclusive', 'reason': str(error), 'process_replay_qualified': False}))
        return 1


if __name__ == '__main__':
    sys.exit(main())
