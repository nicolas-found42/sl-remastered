"""Strict raw component audit of owned mapping fixtures; never Stock/ABI proof."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import stat
import struct

MODES = {'normal', 'late', 'pending', 'writing', 'abrupt', 'missing', 'nonce'}


def require(value, reason):
    if not value:
        raise ValueError(reason)


def read(path, maximum=1024 * 1024):
    path = Path(path)
    require(not any(p.is_symlink() for p in (path, *path.parents)), 'symlink artifact')
    info = path.stat()
    require(stat.S_ISREG(info.st_mode) and info.st_size <= maximum, 'artifact size/type')
    return path.read_bytes()


def decode(raw):
    def pairs(values):
        out = {}
        for key, value in values:
            require(key not in out, 'duplicate JSON key')
            out[key] = value
        return out
    def constant(value):
        raise ValueError('nonfinite JSON: ' + value)
    return json.loads(raw, object_pairs_hook=pairs, parse_constant=constant)


def uint(value):
    require(type(value) is int and 0 <= value <= 0xffffffff, 'typed uint32 differs')
    return value


def inspect_preexisting(root, sentinel):
    root = Path(root)
    require(read(root / 'launcher-exit.txt', 16).strip() == b'60', 'preexisting refusal exit')
    require(read(root / 'journal.bin') == sentinel, 'preexisting output changed')
    require(all(not (root / name).exists() for name in ('events.jsonl', 'result.json', 'mapping-final.bin')),
            'preexisting refusal created target artifacts')
    return {'scope': 'owned existing-output refusal', 'expected_refusal': True,
            'sentinel_sha256': hashlib.sha256(sentinel).hexdigest(), 'process_replay_qualified': False}


def inspect_case(root, mode, nonce):
    root = Path(root)
    require(mode in MODES and re.fullmatch('[0-9a-fA-F]{32}', nonce), 'unsupported case/nonce')
    require(read(root / 'launcher-exit.txt', 16).strip() == b'0', 'collector exit differs')
    result = decode(read(root / 'result.json', 8192))
    keys = {'schema', 'scope', 'mode', 'nonce', 'child_nonce', 'mapping', 'child_mapping',
            'child_pid', 'wait_result', 'exit_code', 'forced', 'runtime_ok', 'semantic_ok',
            'records', 'collector_view_raw', 'producer_view_raw', 'frozen', 'owned_expectations_met',
            'target_abi_qualified', 'process_replay_qualified'}
    require(type(result) is dict and set(result) == keys, 'result schema differs')
    require(result['schema'] == 1 and type(result['schema']) is int and
            result['scope'] == 'owned PE32 mapping lifetime only' and result['mode'] == mode and
            result['nonce'] == nonce, 'result identity differs')
    for key in ('child_pid', 'wait_result', 'exit_code', 'records', 'collector_view_raw', 'producer_view_raw'):
        uint(result[key])
    require(result['child_pid'] > 0 and result['collector_view_raw'] > 0 and result['wait_result'] == 0,
            'actual process/view boundary differs')
    require(all(result[key] is True for key in ('runtime_ok', 'semantic_ok', 'owned_expectations_met')) and
            result['target_abi_qualified'] is False and result['process_replay_qualified'] is False,
            'qualification scope or execution differs')
    child_nonce = nonce
    if mode in ('missing', 'nonce'):
        child_nonce = ('1' if nonce[0] == '0' else '0') + nonce[1:]
    require(result['child_nonce'] == child_nonce and result['mapping'] == 'Local\\slrr-durable-' + nonce and
            result['child_mapping'] == 'Local\\slrr-durable-' + (child_nonce if mode == 'missing' else nonce),
            'mapping/nonce intervention differs')
    require(result['forced'] is (mode == 'abrupt'), 'termination intervention differs')
    expected_exit = 0xc0000142 if mode in ('missing', 'nonce') else (39 if mode == 'abrupt' else
                    (37 if mode in ('pending', 'writing') else 0))
    require(result['exit_code'] == expected_exit, 'actual child exit differs')
    mapping = read(root / 'mapping-final.bin')
    require(len(mapping) == 128 + 64 * 1040, 'fixed mapping size differs')
    header = struct.unpack_from('<32I', mapping)
    require(header[:6] == (0x44525231, 1, 128, 1040, 64, len(mapping)) and
            header[6:10] == tuple(int(nonce[i:i+8], 16) for i in range(0, 32, 8)) and
            not any(header[14:]), 'mapping ABI/nonce/reserved words differ')
    next_ticket, ack, flags, lock = header[10:14]
    require(1 <= ack <= next_ticket <= 0x20000000 and next_ticket - ack <= 64 and flags == lock == 0,
            'raw mapping accounting/fault differs')
    frozen = {'next_ticket': next_ticket, 'ack_ticket': ack, 'flags': flags, 'collector_lock': lock,
              'reserved': 0, 'writing': 0, 'committed': 0, 'reading': 0, 'incomplete': 0}
    slots = {}
    last = next_ticket - 1
    for index in range(64):
        offset = 128 + index * 1040
        tag, kind, length, reserved = struct.unpack_from('<4I', mapping, offset)
        payload = mapping[offset+16:offset+1040]
        expected_ticket = index + 1
        if expected_ticket <= last:
            expected_ticket += ((last - expected_ticket) // 64) * 64
            require(tag >> 3 == expected_ticket and reserved == 0, 'slot generation/metadata differs')
            state = tag & 7
            require(state <= 4, 'unknown publication state')
            if expected_ticket < ack:
                require(state == 0, 'acknowledged slot not free')
            elif state in (1, 2, 3, 4):
                frozen[{1: 'reserved', 2: 'writing', 3: 'committed', 4: 'reading'}[state]] += 1
            else:
                frozen['incomplete'] += 1
            slots[expected_ticket] = (state, kind, length, payload)
        else:
            require(mapping[offset:offset+1040] == bytes(1040), 'unreserved slot changed')
    require(type(result['frozen']) is dict and set(result['frozen']) == set(frozen), 'frozen schema differs')
    for key, value in frozen.items():
        require(type(result['frozen'][key]) is int and result['frozen'][key] == value, 'reported/raw frozen state differs')
    expected = {
        'normal': (131, 131, 0, 0, 0), 'late': (5, 5, 0, 0, 0),
        'pending': (4, 2, 1, 0, 1), 'writing': (4, 2, 0, 1, 1),
        'abrupt': (3, 2, 1, 0, 0), 'missing': (1, 1, 0, 0, 0), 'nonce': (1, 1, 0, 0, 0)}[mode]
    require((next_ticket, ack, frozen['reserved'], frozen['writing'], frozen['committed']) == expected and
            frozen['reading'] == frozen['incomplete'] == 0, 'expected terminal states differ')
    raw = read(root / 'journal.bin')
    require(len(raw) % 1040 == 0, 'journal record truncation')
    records = []
    for offset in range(0, len(raw), 1040):
        ticket, generation, kind, length = struct.unpack_from('<4I', raw, offset)
        payload = raw[offset+16:offset+1040]
        require(0 < ticket < next_ticket and generation == (ticket-1)//64 and length == 16 and
                payload[16:] == bytes(1008), 'record generation/length/tail differs')
        event = struct.unpack_from('<4I', payload)
        index, thread, stage, view = event
        require(thread > 0 and view > 0, 'producer thread/view absent')
        if ticket in slots:
            state, slot_kind, slot_length, slot_payload = slots[ticket]
            require(state in (0, 3, 4) and (kind, length, payload) == (slot_kind, slot_length, slot_payload),
                    'journal differs from retained shared slot')
        records.append({'ticket': ticket, 'generation': generation, 'kind': kind, 'length': length,
                        'index': index, 'thread': thread, 'stage': stage, 'view_raw': view})
    expected_tickets = (list(range(1, 131)) if mode == 'normal' else list(range(1, 5)) if mode == 'late' else
                        [1, 3] if mode in ('pending', 'writing') else [1] if mode == 'abrupt' else [])
    require([r['ticket'] for r in records] == expected_tickets and result['records'] == len(records),
            'journal omitted/duplicated/reordered actual record')
    rows = [decode(line) for line in read(root / 'events.jsonl').splitlines()]
    require(len(rows) == len(records), 'event count differs')
    for row, record in zip(rows, records):
        require(type(row) is dict and set(row) == {'event', 'frozen_tail', *record}, 'event schema differs')
        require(row['event'] == 'record' and row['frozen_tail'] is (record['ticket'] >= ack), 'archive phase differs')
        for key, value in record.items():
            require(type(row[key]) is int and row[key] == value, 'event differs from binary journal')
    if records:
        main_thread = records[0]['thread']
        require(records[0]['kind'] == 1 and records[0]['stage'] == 1 and records[0]['index'] == result['child_pid'] and
                result['producer_view_raw'] == records[0]['view_raw'] and
                all(r['view_raw'] == records[0]['view_raw'] for r in records), 'attach/view identity differs')
        if mode != 'abrupt':
            require(records[-1]['kind'] == 2 and records[-1]['stage'] == 1 and records[-1]['index'] == result['child_pid'] and
                    records[-1]['thread'] == main_thread, 'actual process detach differs')
        if mode == 'normal':
            require(all(r['kind'] == 10 and r['index'] == i and r['thread'] == main_thread and r['stage'] == 0
                        for i, r in enumerate(records[1:-1])), 'normal consumer order differs')
        if mode == 'late':
            require(records[1]['kind'] == 11 and records[1]['thread'] == main_thread and
                    records[1]['index'] == records[1]['stage'] == 0 and
                    records[2]['kind'] == 12 and records[2]['thread'] != main_thread and
                    records[2]['index'] == 1 and records[2]['stage'] == 0, 'late worker marker order differs')
    else:
        require(result['producer_view_raw'] == 0, 'refused target published identity')
    return {'scope': 'owned mapping lifetime only; no Stock or observer ABI qualification', 'mode': mode,
            'nonce': nonce, 'raw_records': len(records), 'frozen': frozen,
            'unfinished_reservations_retained': frozen['reserved'] + frozen['writing'],
            'artifact_sha256': {n: hashlib.sha256(read(root/n)).hexdigest() for n in
                               ('result.json', 'journal.bin', 'events.jsonl', 'mapping-final.bin')},
            'target_abi_qualified': False, 'process_replay_qualified': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('case', type=Path)
    parser.add_argument('--mode', choices=sorted(MODES), required=True)
    parser.add_argument('--nonce', required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    report = inspect_case(args.case.resolve(strict=True), args.mode, args.nonce)
    with args.report.open('x') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
