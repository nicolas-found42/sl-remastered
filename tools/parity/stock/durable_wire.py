"""Owned binary wire structure; no Stock producer/ABI qualification.

The caller must independently bind CreateProcess main-thread identity, nonce,
actual process handle exit and stopped writers. This schema cannot attest those
facts. It never turns an unfinished slot or open call into a fabricated reply.
"""
from dataclasses import dataclass
import importlib.util
from pathlib import Path
import struct
import sys

_path = Path(__file__).with_name('durable_archive.py')
_spec = importlib.util.spec_from_file_location('experimental_wire_transport', _path)
archive = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = archive
_spec.loader.exec_module(archive)

MAGIC, VERSION, HEADER = 0x44574331, 3, 144
DATA_MAX, INPUT_HEADER = 368, 48
INPUT_FIELDS = ('schema device_id incarnation device_kind element_size count_in count_value flags '
                'snapshot_bytes related_raw retired_interfaces validity').split()
ENTRY, REPLY, GAP = 0x1001, 0x1002, 0x1003
FIELDS = ('magic version kind header_bytes total_bytes phase call_entry api process_id thread_id '
          'lane_id lane_generation identity caller_raw arg0 arg1 arg2 arg3 arg4 eax last_error cw mxcsr '
          'fx_bytes data_bytes data_role gap edi esi ebp esp ebx edx ecx eax_raw eflags').split()


class WireError(ValueError):
    pass


def require(value, reason):
    if not value:
        raise WireError(reason)


def uint(value):
    require(type(value) is int and 0 <= value <= 0xffffffff, 'typed uint32')
    return value


def input_shape(event, data):
    """Method shape only; device/lifetime provenance is external evidence."""
    if event['kind'] == GAP or event['api'] < 7:
        return None
    require(len(data) >= INPUT_HEADER and event['arg0'] != 0, 'input metadata/object missing')
    meta = dict(zip(INPUT_FIELDS, struct.unpack_from('<12I', data)))
    snapshot = data[INPUT_HEADER:]
    require(meta['schema'] == 7 and meta['device_id'] > 0 and meta['incarnation'] > 0 and
            meta['device_kind'] in (1, 2), 'input schema/identity')
    require(meta['snapshot_bytes'] == len(snapshot) <= 320, 'input full bounded snapshot')
    entry, api = event['kind'] == ENTRY, event['api']
    width, validity = 0, 0
    if api == 7:
        width, validity = (256 if meta['device_kind'] == 1 else 16), 1
        require(meta['element_size'] == event['arg1'] == width and event['arg2'] > 0 and
                meta['count_in'] == meta['count_value'] == meta['flags'] == 0, 'input state shape')
    elif api == 9:
        width, validity = 20 * meta['count_in'], 3
        require(meta['device_kind'] == 1 and meta['element_size'] == event['arg1'] == 20 and
                1 <= meta['count_in'] <= 16 and meta['flags'] == event['arg4'] == 0 and
                event['arg2'] > 0 and event['arg3'] > 0, 'input buffered shape')
        require(meta['count_value'] == meta['count_in'] if entry else
                meta['count_value'] <= meta['count_in'], 'input count boundary')
    elif api == 10:
        width, validity = (16 if entry else 4), 3
        require(meta['element_size'] == 16 and meta['flags'] == 0 and
                event['arg1'] > 0 and event['arg2'] > 0, 'input query shape')
        require(not entry or meta['count_value'] == meta['count_in'], 'input query preimage')
    else:
        require(meta['element_size'] == meta['count_in'] == meta['count_value'] == meta['flags'] == 0,
                'input scalar shape')
    require(len(snapshot) == width and meta['validity'] == validity, 'input missing required snapshot/count')
    if api == 10 and not entry:
        require(struct.unpack('<I', snapshot)[0] == meta['count_value'], 'input query raw pointer')
        if event['eax'] < 0x80000000:
            require(meta['count_value'] > 0 and meta['related_raw'] == meta['count_value'], 'input query alias')
        else:
            require(meta['related_raw'] == 0, 'failed query alias')
    else:
        require(meta['related_raw'] == 0, 'unexpected input alias')
    if api == 12 and not entry and event['eax'] == 0:
        require(meta['retired_interfaces'] > 0, 'zero release missing retirement')
    else:
        require(meta['retired_interfaces'] == 0, 'unexpected input retirement')
    return {**meta, 'snapshot': snapshot}


@dataclass(frozen=True)
class OwnedMain:
    process_id: int
    thread_id: int
    lane_id: int
    generation: int
    origin: str = 'owned_CreateProcess_main_thread'

    def validate(self):
        require(self.origin == 'owned_CreateProcess_main_thread', 'unsupported identity origin')
        for value in (self.process_id, self.thread_id, self.lane_id, self.generation):
            uint(value)
        require(self.process_id > 0 and self.thread_id > 0 and self.generation > 0, 'missing owned identity')


def decode(kind, ticket, payload, binding):
    binding.validate()
    uint(kind)
    require(type(ticket) is int and 0 < ticket <= 0x1fffffff, 'phase range')
    require(kind in (ENTRY, REPLY, GAP), 'unsupported wire record kind')
    require(type(payload) is bytes and HEADER <= len(payload) <= 1024, 'immutable wire size')
    values = dict(zip(FIELDS, struct.unpack_from('<36I', payload)))
    require((values['magic'], values['version'], values['kind'], values['header_bytes'], values['total_bytes']) ==
            (MAGIC, VERSION, kind, HEADER, len(payload)), 'wire ABI/version/size')
    require(values['phase'] == ticket and values['api'] <= 12 and values['cw'] <= 0xffff, 'wire phase/API/control')
    fx_bytes, data_bytes = values['fx_bytes'], values['data_bytes']
    require(fx_bytes in (0, 512) and data_bytes <= DATA_MAX and HEADER + fx_bytes + data_bytes == len(payload),
            'wire bounded spans')
    fx, data = payload[HEADER:HEADER+fx_bytes], payload[HEADER+fx_bytes:]
    if fx:
        require(struct.unpack_from('<H', fx)[0] == values['cw'] and
                struct.unpack_from('<I', fx, 24)[0] == values['mxcsr'], 'raw FX/header control mismatch')
    require(values['eax_raw'] == values['eax'], 'raw EAX/header mismatch')
    require(kind == GAP or fx_bytes == 512, 'normal boundary requires full FX snapshot')
    ref = values['call_entry']
    if kind == ENTRY:
        require(ref == ticket and values['gap'] == 0, 'entry identity/gap')
    elif kind == REPLY:
        require(0 < ref < ticket and values['gap'] == 0, 'reply reference/gap')
    else:
        require((ref == 0 or ref < ticket) and values['gap'] > 0, 'explicit gap reference/reason')
    if kind != GAP or values['identity'] != 0:
        require(values['identity'] == 1 and
                (values['process_id'], values['thread_id'], values['lane_id'], values['lane_generation']) ==
                (binding.process_id, binding.thread_id, binding.lane_id, binding.generation),
                'unknown/unbound owned incarnation')
    else:
        require(values['lane_generation'] == 0 and ref == 0, 'unqualified TID cannot acquire a guessed incarnation/reference')
    if data_bytes == 0:
        require(values['data_role'] == 0, 'empty data role')
    elif kind == ENTRY or (kind == GAP and ref == 0):
        require(values['data_role'] == 1, 'entry preimage role')
    else:
        require(values['data_role'] == 2, 'return postimage role')
    return {**values, 'fx': fx, 'data': data, 'input': input_shape(values, data)}


def inspect(mapping, journal, nonce, binding, terminal):
    """Validate structure and pairs; provided terminal must come from raw proof."""
    binding.validate()
    require(type(terminal) is dict and set(terminal) == {'process_id', 'wait_result', 'exit_code', 'after_phase'},
            'terminal schema')
    for value in terminal.values():
        uint(value)
    require(terminal['process_id'] == binding.process_id and terminal['wait_result'] == 0, 'terminal process binding')
    transport = archive.inspect(mapping, journal, nonce)
    require(terminal['after_phase'] == transport.next_ticket - 1, 'terminal phase boundary')
    entries, replies, gaps, stacks, interrupted = {}, {}, [], {}, set()
    context = ('api', 'process_id', 'thread_id', 'lane_id', 'lane_generation', 'identity',
               'caller_raw', 'arg0', 'arg1', 'arg2', 'arg3', 'arg4')
    for record in transport.records:
        event = decode(record.kind, record.ticket, record.payload, binding)
        ref = event['call_entry']
        lane = tuple(event[key] for key in ('process_id', 'thread_id', 'lane_id', 'lane_generation'))
        if record.kind == ENTRY:
            require(ref not in entries, 'duplicate entry')
            require(lane not in interrupted, 'lane interrupted by referenced GAP')
            entries[ref] = event
            stacks.setdefault(lane, []).append(ref)
        elif record.kind == REPLY:
            require(ref in entries and ref not in replies, 'orphan/duplicate reply')
            require(all(event[key] == entries[ref][key] for key in context), 'reply changed call context')
            if event['input'] is not None:
                require(all(event['input'][key] == entries[ref]['input'][key] for key in
                            ('schema', 'device_id', 'incarnation', 'device_kind', 'element_size',
                             'count_in', 'flags')), 'reply changed input context')
            require(lane not in interrupted, 'lane interrupted by referenced GAP')
            require(stacks.get(lane) and stacks[lane][-1] == ref, 'crossed synchronous reply')
            stacks[lane].pop()
            replies[ref] = event
        else:
            require(ref == 0 or ref in entries, 'gap references absent entry')
            if ref:
                require(all(event[key] == entries[ref][key] for key in context), 'gap changed call context')
                require(lane not in interrupted and ref not in replies and
                        stacks.get(lane) and stacks[lane][-1] == ref, 'gap does not reference active top call')
                # No result or unwind is invented. Ordering after this missing
                # response is outside this version's single-lane qualification.
                interrupted.add(lane)
            gaps.append(event)
    open_entries = tuple(sorted(set(entries) - set(replies)))
    return {'scope': 'experimental owned binary wire structure only', 'transport': transport,
            'entries': entries, 'replies': replies, 'gaps': gaps, 'open_entries': open_entries,
            'active_stacks': {key: tuple(value) for key, value in stacks.items()},
            'interrupted_lanes': tuple(sorted(interrupted)),
            'normal_pairs_complete': bool(entries) and transport.transport_complete and not gaps and
                                     not open_entries and not any(stacks.values()) and terminal['exit_code'] == 0,
            'target_abi_qualified': False, 'process_replay_qualified': False}
