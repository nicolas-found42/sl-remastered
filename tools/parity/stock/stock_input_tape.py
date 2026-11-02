"""Experimental raw Stock input consumption inventory; never replay validation.

Keeps all shared API/input/RNG phases. This inventory does not establish caller
allowlists, construction/configuration transparency or thread identity.
"""
import json
from pathlib import Path


def require(value, reason):
    if not value:
        raise ValueError(reason)


def uint(value):
    require(type(value) is int and 0 <= value <= 0xffffffff, 'invalid uint32')
    return value


def read_rows(path, partial=False):
    path = Path(path)
    require(path.stat().st_size <= 64 * 1024**2, 'oversized Stock input tape')
    raw = path.read_bytes()
    if partial and raw and raw[-1:] != b'\n':
        raw = raw[:raw.rfind(b'\n') + 1]
    rows = [json.loads(line) for line in raw.splitlines()]
    require(all(type(row) is dict for row in rows), 'nonobject record')
    return rows


def validate_payload(row,version):
    require(version in (6,7),'unsupported payload version')
    method=uint(row['method']);size=uint(row['size']);uint(row['device_kind']);uint(row['flags'])
    require(method<=5,'unsupported method')
    count=uint(row['count_in']);returned=uint(row['count_out'])
    for key in ('pre_valid','post_valid'):require(type(row[key]) is int and row[key] in (0,1),'invalid snapshot validity')
    if method==0:require(size==(256 if row['device_kind']==1 else 16) and row['device_kind'] in (1,2),'unsupported state shape')
    if method in (1,4,5):require(size==0 and not count and not returned and not row['pre_valid'] and not row['post_valid'],'invalid payload-free method')
    if method==3:require(size==16,'unsupported query GUID shape')
    if method in (0,2,3):require(row['pre_valid']==row['post_valid']==1,'missing required snapshots')
    if method==2:
        require(size==20 and row['device_kind']==1 and row['flags']==0,'unsupported buffered shape')
        require(1<=count<=(1 if version==6 else 16) and returned<=count,'invalid buffered count')
        expected=size*count
    else:expected=size
    if version==7:require(type(row.get('buffer_bytes')) is int and row['buffer_bytes']==expected,'buffer byte bound mismatch')
    pre,post=bytes.fromhex(row['pre']),bytes.fromhex(row['post'])
    require(len(pre)==(expected if row['pre_valid'] else 0),'preimage length mismatch')
    require(len(post)==((4 if method==3 else expected) if row['post_valid'] else 0),'postimage length mismatch')
    return expected


def consumption(rows):
    inputs = sorted((row for row in rows if row.get('event') == 'input_return'), key=lambda row: row['seq'])
    transitions = []
    successful = 0
    for row in inputs:
        if row.get('method') != 0 or row.get('device_kind') != 2:
            continue
        if row.get('gap') != 0 or row.get('result', 0xffffffff) & 0x80000000:
            continue
        require(row.get('size') == row.get('post_valid') * 16 == 16, 'unsupported mouse state')
        post = bytes.fromhex(row['post'])
        require(len(post) == 16, 'wrong mouse postimage')
        value = post[12] & 128
        if not transitions or transitions[-1] != value:
            transitions.append(value)
        successful += 1
    return {'mouse_transitions': transitions, 'successful_mouse_reads': successful,
            'input_calls': len(inputs)}


def inventory(tape, setup):
    rows = read_rows(tape)
    setups = read_rows(setup)
    require(all(row.get('event') in {'setup', 'create_owner', 'device_setup', 'create_device'} for row in setups),
            'unknown factory/setup record')
    require(setups and setups[0].get('event') == 'setup' and setups[0].get('observer_bound') == 1,
            'observer owner binding missing')
    require(all(uint(row.get('gap', 0)) == 0 for row in setups), 'factory/setup gap')
    devices = [row for row in setups if row.get('event') == 'device_setup']
    require(len(devices) == 2 and {row['kind'] for row in devices} == {1, 2}, 'expected keyboard/mouse inventory missing')
    require(len({row['device_id'] for row in devices}) == 2 and all(
        uint(row.get('added_com_calls')) == 0 and uint(row.get('aliases_bound')) == 0 and uint(row.get('bound')) == 1
        for row in devices), 'unexpected setup intervention')
    identities = {(uint(row['device_id']), 1): uint(row['kind']) for row in devices}
    accepted = {'installation', 'initial_global', 'api_return', 'rng_return', 'input_return',
                'input_observer_status', 'status', 'complete'}
    require(all(row.get('event') in accepted for row in rows), 'unknown tape record')
    installations = [row for row in rows if row['event'] == 'installation']
    require(len(installations) == 1 and rows[0] is installations[0] and
            installations[0].get('file_sha256') == '7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde',
            'missing exact Stock installation identity')
    events = sorted((row for row in rows if row['event'] in {'api_return', 'rng_return', 'input_return'}),
                    key=lambda row: uint(row['seq']))
    require(events, 'no observations')
    n = len(events)
    require([uint(row['seq']) for row in events] == list(range(1, n + 1)), 'shared entry loss/duplication')
    require(sorted(uint(row['return_seq']) for row in events) == list(range(1, n + 1)), 'shared return loss/duplication')
    phases = []
    for row in events:
        entry, returned = uint(row['entry_phase']), uint(row['return_phase'])
        require(0 < entry < returned, 'invalid call phases')
        phases += [entry, returned]
        require(uint(row.get('gap', row.get('gap_reason', -1))) == 0, 'observation gap')
    require(sorted(phases) == list(range(1, 2*n + 1)), 'shared phase loss/duplication')
    lanes = {}
    for row in events:
        lanes.setdefault(uint(row['lane']), []).append(uint(row['lane_read']))
    require(all(reads == list(range(1, len(reads)+1)) for reads in lanes.values()), 'lane read loss/duplication')
    statuses = [row for row in rows if row['event'] == 'status']
    require(statuses and rows[-1] is statuses[-1], 'missing final status')
    final = statuses[-1]
    require(final.get('format_version') in (6,7) and final.get('mode') == 'record_only', 'wrong input format')
    require(all(type(final.get(k)) is int and final[k] == 0 for k in
                ('active', 'pending', 'dropped', 'accepting', 'gaps')), 'not drained or loss reported')
    require((final.get('entered'), final.get('returned'), final.get('phases')) == (n, n, 2*n), 'final shared accounting')
    inputs = [row for row in events if row['event'] == 'input_return']
    require(inputs, 'no actual input consumption')
    input_status = [row for row in rows if row['event'] == 'input_observer_status']
    require(input_status and input_status[-1].get('scope') == 'experimental_stock_consumption' and
            input_status[-1].get('input_entered') == input_status[-1].get('input_returned') == len(inputs),
            'input status accounting/scope')
    for row in inputs:
        identity = (uint(row['device_id']), uint(row['incarnation']))
        require(identity in identities and identities[identity] == uint(row['device_kind']), 'unbound device identity')
        method = uint(row['method'])
        require(method <= 5, 'unknown input method')
        require(row.get('scope') == 'experimental_stock_consumption', 'wrong input scope')
        for key in ('caller_raw', 'object_raw', 'related_raw', 'result', 'size', 'count_in', 'count_out', 'flags'):
            uint(row[key])
        for key in ('cw', 'mxcsr', 'last_error'):
            require(type(row[key]) is list and len(row[key]) == 2, 'invalid machine state pair')
            for word in row[key]: uint(word)
        for key in ('pre_valid', 'post_valid'):
            require(type(row[key]) is int and row[key] in (0, 1), 'invalid snapshot validity')
        validate_payload(row,final['format_version'])
    observed = consumption(rows)
    observed.update(scope='experimental_actual_Stock_input_consumption_inventory',
                    shared_calls=n, shared_phases=2*n, devices=devices,
                    process_replay_qualified=False, caller_admission_qualified=False,
                    construction_transparency_qualified=False,
                    limits=['raw inventory only; API/RNG semantics not compiled by this checker',
                            'configuration interception and thread identity unqualified',
                            'natural unsupported aliases invalidate the inventory'])
    return observed
