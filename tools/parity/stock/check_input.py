#!/usr/bin/env python3
"""Independent raw-artifact audit of qualify_input.py's owned console fixture.

This recognizes only that fixed 4x16-call fixture, not arbitrary Stock tapes.
Passing it never qualifies COM/Wine lifetimes, input replay or paired Parity.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import struct


SNAPSHOT_BYTES, CASES = 560, 64
RETURN_FLAGS = (0x202, 0x203, 0x246, 0x297)


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def payload(case, size):
    initial = b'\x7e' * size
    if 5 <= case <= 7 or case in (2, 4, 8):
        return initial
    if 9 <= case <= 11:
        return struct.pack('<5I', 255, 0x81, 0xaabbccdd, 0x12345678, 0xabcdef01)
    count = 17 if case == 3 else size
    return bytes(i ^ (case + 0x30) for i in range(count)) + initial[count:]


def hresult(case):
    if case in (2, 3, 4, 11):
        return 0x8007001e
    if case == 7:
        return 0x80070005
    return 1 if case in (6, 10) else 0


def read_rows(path):
    require(path.stat().st_size <= 4 * 1024**2, 'oversized fixture tape')
    return [json.loads(line) for line in path.read_text().splitlines()]


def validate_rows(rows, refuse=False):
    status = rows[-1]
    require(status['event'] == 'status' and status['format_version'] in (6,7), 'fixture status/version')
    events = sorted((row for row in rows if row['event'] in ('api_return', 'input_return')), key=lambda row: row['seq'])
    require(len(events) == 72 and [row['seq'] for row in events] == list(range(1, 73)), 'fixture call inventory')
    require([row['lane_read'] for row in events] == list(range(1, 73)) and {row['lane'] for row in events} == {0}, 'shared lane inventory')
    inputs = [row for row in events if row['event'] == 'input_return']
    if status['format_version']==7:
        for row in inputs:
            require(type(row.get('buffer_bytes')) is int and 0<=row['buffer_bytes']<=320,'invalid buffer byte bound')
            if not row['gap']:
                expected=row['size']*(row['count_in'] if row['method']==2 else 1)
                require(row['buffer_bytes']==expected,'buffer byte count mismatch')
    gaps = [row for row in inputs if row['gap']]
    require(len(gaps) == (8 if refuse else 0), 'unexpected input gaps')
    completed = [row for row in events if row['return_phase']]
    phases = [phase for row in events for phase in (row['entry_phase'], row['return_phase']) if phase]
    require(sorted(phases) == list(range(1, len(phases) + 1)), 'phase loss or duplication')
    require(sorted(row['return_seq'] for row in completed) == list(range(1, len(completed) + 1)), 'return inventory')
    require(all(row['entry_phase'] < row['return_phase'] for row in completed), 'reversed phases')
    require((status['entered'], status['returned'], status['phases'], status['gaps']) ==
            (72, len(completed), len(phases), len(gaps)), 'status accounting')
    require(all(status[key] == 0 for key in ('active', 'pending', 'dropped', 'accepting')), 'recording not drained/stopped')
    input_status = [row for row in rows if row['event'] == 'input_observer_status']
    require(len(input_status) == 1, 'missing/duplicate input status')
    require((input_status[0]['input_entered'], input_status[0]['input_returned'],
             input_status[0]['device_registrations'], input_status[0]['vtables']) == (68, 68-len(gaps), 4, 2), 'input inventory')
    require(input_status[0]['scope'] == 'owned_console_only', 'wrong fixture scope')
    cursor = 0
    for fx in range(4):
        for case in range(16):
            row = events[cursor]
            cursor += 1
            method = 1 if 5 <= case <= 7 else (2 if 8 <= case <= 11 else 0)
            device = 2 if case == 1 else (3 if case == 14 else 1)
            size = 0 if method == 1 else (20 if method == 2 else (16 if case == 1 else 256))
            gap = 11 if refuse and case == 14 else (10 if refuse and case == 15 else 0)
            require(row['event'] == 'input_return' and row['scope'] == 'owned_console_only' and row['method'] == method, 'method order/scope')
            require(row['gap'] == gap, 'wrong refusal reason')
            if gap:
                require(row['return_phase'] == row['return_seq'] == row['post_valid'] == row['pre_valid'] == 0, 'refused call pretends completion')
                continue
            require((row['device_id'], row['incarnation'], row['device_kind']) ==
                    (device, 2 if device == 1 else 1, 2 if device == 2 else 1), 'device/lifetime binding')
            require(row['size'] == size and row['flags'] == 0 and row['result'] == hresult(case), 'request or HRESULT')
            require(row['last_error'] == [0x55aa55aa, 0xdead0000 + case], 'LastError snapshot')
            require(row['cw'] == [0x37f + (fx << 10)] * 2 and row['mxcsr'] == [0x1f80 + (fx << 13)] * 2, 'FX control snapshot')
            require(row['pre_valid'] == row['post_valid'] == (method != 1), 'snapshot validity')
            require(row['pre'] == (b'\x7e' * size).hex() and row['post'] == payload(case, size).hex(), 'response bytes/failure postimage')
            if method == 2:
                require(row['count_in'] == 1 and row['count_out'] == (0 if case in (8, 11) else 1), 'buffered count postimage')
            if case in (12, 13):
                nested = events[cursor]
                cursor += 1
                require(row['entry_phase'] < nested['entry_phase'] < nested['return_phase'] < row['return_phase'], 'nested call phases')
                if case == 12:
                    require(nested['event'] == 'api_return' and nested['api'] == 'GetTickCount' and
                            nested['result'] == 0xfedcba98 and nested['gap_reason'] == 0, 'nested clock response')
                else:
                    require(nested['event'] == 'input_return' and nested['device_id'] == 2 and
                            nested['incarnation'] == 1 and nested['size'] == 16 and nested['gap'] == 0 and
                            nested['post'] == payload(13, 16).hex(), 'nested device response')
    require(cursor == len(events), 'unconsumed fixture records')
    return {'calls': len(events), 'inputs': len(inputs), 'clock_calls': 4, 'phases': len(phases), 'gaps': len(gaps)}


def validate_lifecycle_rows(rows, refuse=False):
    events = sorted((row for row in rows if row['event'] == 'input_return'), key=lambda row: row['seq'])
    count = 15 if refuse else 48
    require(len(events) == count and [row['seq'] for row in events] == list(range(1, count+1)), 'COM call inventory')
    status = rows[-1]
    completed = [row for row in events if row['return_phase']]
    phases = [phase for row in events for phase in (row['entry_phase'], row['return_phase']) if phase]
    require(sorted(phases) == list(range(1, len(phases)+1)), 'COM phase loss/duplicate')
    require(sorted(row['return_seq'] for row in completed) == list(range(1, len(completed)+1)), 'COM return inventory')
    require(all(row['entry_phase'] < row['return_phase'] for row in completed), 'COM reversed phases')
    require((status['entered'], status['returned'], status['phases'], status['gaps']) ==
            (count, count-1 if refuse else count, 29 if refuse else 96, 2 if refuse else 0), 'COM status accounting')
    require(all(status[key] == 0 for key in ('active', 'pending', 'dropped', 'accepting')), 'COM recording not drained')
    methods = (3, 4, 5, 0, 3, 3, 5, 5, 5, 0, 3, 0)
    base_address = events[0]['object_raw']
    alias_address = events[0]['related_raw']
    require(alias_address == base_address+16 and base_address != 0, 'COM alias pointer')
    for f in range(1 if refuse else 4):
        for case, method in enumerate(methods):
            row = events[f*12+case]
            require((row['method'], row['device_id'], row['incarnation'], row['device_kind'], row['gap']) ==
                    (method, 10, 2*f+(2 if case >= 9 else 1), 1, 0), 'COM identity/method/incarnation')
            require(row['object_raw'] == (alias_address if case in (1, 3, 6, 8, 11) else base_address), 'COM receiver alias')
            require(row['cw'] == [0x37f+(f<<10)]*2 and row['mxcsr'] == [0x1f80+(f<<13)]*2, 'COM FX controls')
            if method == 3:
                expected = 0x12345678 if case == 4 else (base_address if case == 5 else alias_address)
                require(row['pre'] == '44332211665588770102030405060708' and row['pre_valid'] == row['post_valid'] == 1,
                        'COM GUID request/snapshot')
                require(row['count_in'] == 0xaabbccdd and row['count_out'] == expected and
                        row['post'] == struct.pack('<I', expected).hex(), 'COM output pointer/failure postimage')
                require(row['result'] == (0x80004002 if case == 4 else 0), 'COM QI HRESULT')
                require(row['related_raw'] == (0 if case == 4 else expected), 'COM QI binding')
            if method in (4, 5):
                require(row['pre_valid'] == row['post_valid'] == 0, 'COM ref methods invent buffers')
                expected = {1:3, 2:2, 6:2, 7:1, 8:0}[case]
                require(row['result'] == expected, 'COM raw refcount return')
            require(row['retired_interfaces'] == (2 if case == 8 else 0), 'COM zero Release retirement')
    if refuse:
        unknown, reused, released = events[-3:]
        require(unknown['gap'] == 10 and unknown['object_raw'] == alias_address and unknown['return_phase'] == 0,
                'COM explicit retirement/unknown alias refused')
        require(reused['gap'] == 0 and reused['device_id'] == 10 and reused['incarnation'] == 3 and
                reused['object_raw'] == base_address, 'COM later incarnation accepted')
        require(released['gap'] == 0 and released['result'] == 0 and released['retired_interfaces'] == 1,
                'COM reused-object final retirement')
    return {'calls': count, 'phases': len(phases), 'gaps': status['gaps']}


def audit(directory):
    checks = {}

    def check(name, action):
        try:
            detail = action()
            checks[name] = {'ok': True, **(detail if isinstance(detail, dict) else {})}
        except (OSError, ValueError, KeyError, TypeError, AssertionError) as error:
            checks[name] = {'ok': False, 'reason': str(error)}

    def binaries(case):
        root = directory / case
        data = {name: (root / name).read_bytes() for name in ('baseline.bin', 'hooked.bin', 'unhooked-repeat.bin')}
        require(all(len(value) == SNAPSHOT_BYTES * CASES for value in data.values()), 'snapshot size')
        require(data['baseline.bin'] == data['unhooked-repeat.bin'], 'owned original is not repeatable')
        for index in range(CASES):
            require(struct.unpack_from('<I', data['baseline.bin'], index * SNAPSHOT_BYTES + 32)[0] == RETURN_FLAGS[index//16], 'owned target return flags')
            require(struct.unpack_from('<I', data['baseline.bin'], index * SNAPSHOT_BYTES + 28)[0] == hresult(index%16), 'owned target EAX return')
            require(struct.unpack_from('<I', data['baseline.bin'], index * SNAPSHOT_BYTES + 36)[0] == 0xdead0000 + index%16, 'owned target LastError return')
        if case == 'negative':
            require(all(data['baseline.bin'][i*560+48:(i+1)*560] != data['hooked.bin'][i*560+48:(i+1)*560] for i in range(CASES)), 'FX fault not detected in every case')
        else:
            require(data['baseline.bin'] == data['hooked.bin'], 'raw registers/flags/LastError/FX differ')
            require((root / 'entry-baseline.bin').read_bytes() == (root / 'entry-hooked.bin').read_bytes(), 'original target entry flags changed')
            require((root / 'entry-baseline.bin').read_bytes() == (root / 'entry-repeat.bin').read_bytes(), 'owned original entry flags not repeatable')
            entries = struct.unpack('<64I', (root / 'entry-baseline.bin').read_bytes())
            require(all(value == RETURN_FLAGS[i//16] for i, value in enumerate(entries) if i%16 != 13), 'explicit original entry flags')
        for index in range(CASES):
            case_index = index % 16
            method = 1 if 5 <= case_index <= 7 else (2 if 8 <= case_index <= 11 else 0)
            size = 0 if method == 1 else (20 if method == 2 else (16 if case_index == 1 else 256))
            if case == 'refuse' and case_index == 14:
                size = 15
            expected = b'\xa5' * 16 + payload(case_index, size) + b'\x7e' * (256-size) + b'\xa5' * 16
            for name in ('post-baseline.bin', 'post-hooked.bin'):
                raw = (root / name).read_bytes()
                require(len(raw) == CASES*288 and raw[index*288:(index+1)*288] == expected, 'buffer bytes/canary changed')
            for name in ('count-baseline.bin', 'count-hooked.bin'):
                raw = (root / name).read_bytes()
                require(len(raw) == CASES*4 and struct.unpack_from('<I', raw, index*4)[0] == (0 if case_index in (8, 11) else 1), 'count output changed')
        return {'compared': CASES, 'snapshot_bytes_each': SNAPSHOT_BYTES}

    result = json.loads((directory / 'result.json').read_text())
    def execution():
        require(result['execution_finished'] and result['container_removed'] and result['seed_unchanged'] and result['producer_unchanged'], 'execution/cleanup/source failure')
    check('bounded_execution_and_preservation', execution)
    def provenance():
        hashes = result['producer_sha256']
        expected_count = 10 if (directory / 'source/wine_input_lifecycle_test.c').exists() else 9
        require(len(hashes) == expected_count and len({Path(name).name for name in hashes}) == expected_count, 'incomplete producer inventory')
        for name, expected in hashes.items():
            original = (directory / 'source' / Path(name).name).read_bytes()
            require(hashlib.sha256(original).hexdigest() == expected, 'frozen producer identity mismatch')
            faulty = (directory / 'fault-source' / Path(name).name).read_bytes()
            expected_fault = original.replace(b'fxrstor (%esp)', b'nop') if Path(name).name in ('control_tape.c', 'input_observer.inc') else original
            require(faulty == expected_fault, 'negative-control source mutation mismatch')
    check('frozen_producer_and_exact_fault_sources', provenance)
    for case in ('accept', 'refuse', 'negative'):
        check(case + '_raw_abi_and_outputs', lambda case=case: binaries(case))
    for case in ('accept', 'refuse'):
        check(case + '_record_semantics', lambda case=case: validate_rows(read_rows(directory / case / 'input-tape.jsonl'), case=='refuse'))

    def phase_source():
        rows = read_rows(directory / 'accept/input-tape.jsonl')
        address = [row['phase_source_raw'] for row in rows if row['event'] == 'input_observer_status'][0]
        helper = (directory / 'accept/helper.log').read_text()
        require(int(re.search(r'phase_source=(\d+)', helper)[1]) == address and address != 0, 'function-table phase identity mismatch')
    check('shared_phase_source_identity', phase_source)

    def binding(case):
        rows = read_rows(directory / case / 'binding-tape.jsonl')
        status = rows[-1]
        require((status['entered'], status['returned'], status['phases']) == (3, 3, 6), 'cross-DLL observation inventory')
        require(all(status[key] == 0 for key in ('gaps', 'dropped', 'pending', 'active', 'accepting')), 'cross-DLL observation failure')
        events = [row for row in rows if row['event'] in ('api_return', 'input_return')]
        phases = [phase for row in events for phase in (row['entry_phase'], row['return_phase'])]
        require(sorted(phases) == list(range(1, 7)), 'cross-DLL phase loss/duplicate counter')
        state = [row for row in events if row['event'] == 'input_return']
        require(len(state) == 1 and state[0]['entry_phase'] == 3 and state[0]['return_phase'] == 6 and
                state[0]['post'] == '91'*256, 'cross-DLL nested state/clock response')
        text = (directory / case / 'helper.log').read_text()
        unavailable = 1 if case == 'client-first' else 0
        require(f'order={case} unavailable_refused={unavailable} not_ready_refused=1 phases=6 same_owner_table=1' in text,
                'cross-DLL availability/refusal verdict')
        return {'phases': 6, 'calls': 3}
    for case in ('owner-first', 'client-first'):
        check('binding_' + case, lambda case=case: binding(case))

    if (directory / 'source/wine_input_lifecycle_test.c').exists():
        for case in ('lifecycle', 'lifecycle-refuse'):
            check(case + '_record_semantics', lambda case=case: validate_lifecycle_rows(
                read_rows(directory / case / 'lifecycle-tape.jsonl'), case == 'lifecycle-refuse'))
        def lifecycle_abi():
            root = directory / 'lifecycle'
            baseline = (root / 'lifecycle-baseline.bin').read_bytes()
            require(len(baseline) == 48*SNAPSHOT_BYTES and baseline == (root / 'lifecycle-hooked.bin').read_bytes(),
                    'COM raw ABI/FLAGS/FX mismatch')
            require((root / 'lifecycle-out-baseline.bin').read_bytes() == (root / 'lifecycle-out-hooked.bin').read_bytes(),
                    'COM output pointer transparency')
            fault = directory / 'lifecycle-negative'
            before = (fault / 'lifecycle-baseline.bin').read_bytes()
            after = (fault / 'lifecycle-hooked.bin').read_bytes()
            require(len(before) == len(after) == len(baseline) and all(
                before[i*560+48:(i+1)*560] != after[i*560+48:(i+1)*560] for i in range(48)), 'COM FX fault not detected')
            return {'compared': 48, 'snapshot_bytes_each': SNAPSHOT_BYTES, 'FX_faults_detected': 48}
        check('lifecycle_raw_ABI_and_FX_control', lifecycle_abi)
        def retirement_fault():
            require(result['helper_exits']['lifecycle-no-retirement'] == '33', 'COM retirement omission not detected at reuse')
            for path in (directory / 'source').iterdir():
                original = path.read_bytes()
                expected = original.replace(b'd->live=0;++r->retired_interfaces;', b'++r->retired_interfaces;') if path.name == 'input_observer.inc' else original
                require((directory / 'retirement-fault-source' / path.name).read_bytes() == expected, 'COM retirement source mutation')
        check('lifecycle_retirement_omission_control', retirement_fault)
        lifecycle_original = read_rows(directory / 'lifecycle/lifecycle-tape.jsonl')
        for mutation in ('alias', 'incarnation', 'zero_release', 'retirement', 'QI_failure_pointer'):
            rows = copy.deepcopy(lifecycle_original)
            events = sorted((row for row in rows if row['event'] == 'input_return'), key=lambda row: row['seq'])
            if mutation == 'alias':
                events[1]['object_raw'] = events[0]['object_raw']
            elif mutation == 'incarnation':
                events[9]['incarnation'] = 1
            elif mutation == 'zero_release':
                events[8]['result'] = 1
            elif mutation == 'retirement':
                events[8]['retired_interfaces'] = 1
            else:
                events[4]['count_out'] = 0
            def rejects_lifecycle(rows=rows):
                try:
                    validate_lifecycle_rows(rows)
                except ValueError:
                    return
                raise ValueError('corrupted COM recording accepted')
            check('lifecycle_mutation_' + mutation, rejects_lifecycle)

    original = read_rows(directory / 'accept/input-tape.jsonl')
    for mutation in ('drop', 'duplicate_phase', 'failure_postimage', 'acquire_branch', 'buffered_count', 'device_identity'):
        rows = copy.deepcopy(original)
        inputs = [row for row in rows if row['event'] == 'input_return']
        if mutation == 'drop':
            rows.remove(inputs[0])
        elif mutation == 'duplicate_phase':
            inputs[1]['entry_phase'] = inputs[0]['entry_phase']
        elif mutation == 'failure_postimage':
            next(row for row in inputs if row['result'] == 0x8007001e)['post'] = '00' * 256
        elif mutation == 'acquire_branch':
            next(row for row in inputs if row['method'] == 1 and row['result'] == 1)['result'] = 0
        elif mutation == 'buffered_count':
            next(row for row in inputs if row['method'] == 2)['count_out'] = 2
        else:
            inputs[0]['device_id'] = 3
        def rejects(rows=rows):
            try:
                validate_rows(rows)
            except ValueError:
                return
            raise ValueError('corrupted real recording accepted')
        check('mutation_' + mutation, rejects)
    return {'scope': 'owned console controlled ABI only; real input and replay unqualified',
            'process_replay_qualified': False, 'passed': all(item['ok'] for item in checks.values()), 'checks': checks}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    result = audit(args.directory)
    out = args.directory / 'checks.json'
    with out.open('x') as stream:
        stream.write(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
