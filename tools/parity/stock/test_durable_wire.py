"""Synthetic binary-schema tests only; NOT target/kernel/ABI evidence.

Version3 carries raw integer/flags, FX bytes and typed input snapshots. Generated records do not prove
those fields were observed from a target.
"""
from pathlib import Path
import importlib.util
import struct
import sys
import unittest
sys.dont_write_bytecode = True
P = Path(__file__).resolve().parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


w = load('wire_test_subject', P / 'durable_wire.py')
BINDING = w.OwnedMain(11, 22, 1, 1)
NONCE = (1, 2, 3, 4)


def event(record_kind, ticket, ref=None, *, data=b'', fx=True, **changes):
    values = dict.fromkeys(w.FIELDS, 0)
    values.update(magic=w.MAGIC, version=w.VERSION, kind=record_kind, header_bytes=144,
                  phase=ticket, call_entry=(ticket if record_kind == w.ENTRY else 1) if ref is None else ref,
                  api=2, process_id=11, thread_id=22, lane_id=1, lane_generation=1,
                  identity=1, caller_raw=0x12345678, arg0=16, cw=0x037f, mxcsr=0x1f80,
                  fx_bytes=512 if fx else 0, data_bytes=len(data),
                  data_role=(1 if record_kind == w.ENTRY or (record_kind == w.GAP and ref == 0) else 2) if data else 0,
                  gap=17 if record_kind == w.GAP else 0)
    values.update(changes)
    state = bytearray(512 if fx else 0)
    if fx:
        struct.pack_into('<H', state, 0, 0x037f)
        struct.pack_into('<I', state, 24, 0x1f80)
    values['total_bytes'] = 144 + len(state) + len(data)
    return record_kind, struct.pack('<36I', *(values[name] for name in w.FIELDS)) + bytes(state) + data


def fixture(rows):
    slots = 8
    mapping = bytearray(128 + slots * 1040)
    struct.pack_into('<14I', mapping, 0, 0x44525231, 1, 128, 1040, slots,
                     len(mapping), *NONCE, len(rows)+1, len(rows)+1, 0, 0)
    journal = bytearray()
    for ticket, (kind, payload) in enumerate(rows, 1):
        offset = 128 + ((ticket-1) % slots) * 1040
        struct.pack_into('<4I', mapping, offset, ticket << 3, kind, len(payload), 0)
        mapping[offset+16:offset+16+len(payload)] = payload
        journal += struct.pack('<4I', ticket, (ticket-1)//slots, kind, len(payload)) + payload + bytes(1024-len(payload))
    terminal = {'process_id': 11, 'wait_result': 0, 'exit_code': 0, 'after_phase': len(rows)}
    return bytes(mapping), bytes(journal), terminal


def inspect(rows, *, module=w, terminal_changes=None):
    mapping, journal, terminal = fixture(rows)
    terminal.update(terminal_changes or {})
    return module.inspect(mapping, journal, NONCE, BINDING, terminal)


class WireTest(unittest.TestCase):
    def test_closed_pair_is_only_protocol_consistency(self):
        result = inspect([event(w.ENTRY, 1, data=b'pre', fx=True), event(w.REPLY, 2, 1, data=b'post', fx=True)])
        self.assertTrue(result['normal_pairs_complete'])
        self.assertFalse(result['target_abi_qualified'])
        self.assertFalse(result['process_replay_qualified'])

    def test_synchronous_nesting_is_lifo(self):
        result = inspect([event(w.ENTRY, 1), event(w.ENTRY, 2), event(w.REPLY, 3, 2), event(w.REPLY, 4, 1)])
        self.assertTrue(result['normal_pairs_complete'])
        self.assertFalse(any(result['active_stacks'].values()))

    def test_crossed_replies_are_rejected(self):
        with self.assertRaisesRegex(w.WireError, 'crossed synchronous reply'):
            inspect([event(w.ENTRY, 1), event(w.ENTRY, 2), event(w.REPLY, 3, 1), event(w.REPLY, 4, 2)])

    def test_full_register_and_fx_contract(self):
        for changes in ({'eax': 1}, {'eax_raw': 1}):
            _, payload = event(w.ENTRY, 1, **changes)
            with self.assertRaisesRegex(w.WireError, 'raw EAX'):
                w.decode(w.ENTRY, 1, payload, BINDING)
        _, payload = event(w.ENTRY, 1, fx=False)
        with self.assertRaisesRegex(w.WireError, 'full FX'):
            w.decode(w.ENTRY, 1, payload, BINDING)
        _, payload = event(w.ENTRY, 1, eax=7, eax_raw=7, edi=11, eflags=0x202)
        result = w.decode(w.ENTRY, 1, payload, BINDING)
        self.assertEqual((result['eax_raw'], result['edi'], result['eflags']), (7,11,0x202))

    def test_orphan_and_duplicate_replies_reject(self):
        unknown = event(w.GAP, 1, 0, identity=0, lane_generation=0)
        for rows in ([unknown, event(w.REPLY, 2, 1)],
                     [event(w.ENTRY, 1), event(w.REPLY, 2, 1), event(w.REPLY, 3, 1)]):
            with self.subTest(rows=len(rows)), self.assertRaisesRegex(w.WireError, 'orphan/duplicate'):
                inspect(rows)

    def test_unanswered_entry_is_not_a_zero_reply(self):
        result = inspect([event(w.ENTRY, 1)])
        self.assertEqual(result['open_entries'], (1,))
        self.assertEqual(result['replies'], {})
        self.assertFalse(result['normal_pairs_complete'])

    def test_referenced_gap_does_not_pop_or_close(self):
        result = inspect([event(w.ENTRY, 1), event(w.GAP, 2, 1)])
        self.assertEqual(result['open_entries'], (1,))
        self.assertEqual(tuple(result['active_stacks'].values()), ((1,),))
        self.assertEqual(len(result['interrupted_lanes']), 1)
        self.assertFalse(result['normal_pairs_complete'])

    def test_referenced_gap_ends_lane_qualification(self):
        for following in (event(w.REPLY, 3, 1), event(w.ENTRY, 3)):
            with self.subTest(kind=following[0]), self.assertRaisesRegex(w.WireError, 'lane interrupted'):
                inspect([event(w.ENTRY, 1), event(w.GAP, 2, 1), following])

    def test_gap_must_reference_active_top_call(self):
        for rows in ([event(w.ENTRY, 1), event(w.ENTRY, 2), event(w.GAP, 3, 1)],
                     [event(w.ENTRY, 1), event(w.REPLY, 2, 1), event(w.GAP, 3, 1)]):
            with self.subTest(rows=len(rows)), self.assertRaisesRegex(w.WireError, 'active top call'):
                inspect(rows)

    def test_reply_context_cannot_change(self):
        with self.assertRaisesRegex(w.WireError, 'reply changed call context'):
            inspect([event(w.ENTRY, 1), event(w.REPLY, 2, 1, arg4=17)])

    def test_raw_fx_control_words_are_bound(self):
        _, payload = event(w.ENTRY, 1, fx=True, mxcsr=0x3f80)
        with self.assertRaisesRegex(w.WireError, 'raw FX/header'):
            w.decode(w.ENTRY, 1, payload, BINDING)

    def test_schema_phase_identity_lengths_and_role_are_strict(self):
        cases = [({'version': 2}, b''), ({'phase': 9}, b''), ({'header_bytes': 100}, b''),
                 ({'fx_bytes': 4}, b''), ({'data_bytes': 369}, b''), ({'gap': 1}, b''),
                 ({'call_entry': 0}, b''), ({'identity': 0}, b''), ({'lane_generation': 0}, b''),
                 ({'thread_id': 23}, b''), ({'data_role': 2}, b'pre')]
        for changes, data in cases:
            with self.subTest(changes=changes):
                _, payload = event(w.ENTRY, 1, data=data, **changes)
                with self.assertRaises(w.WireError):
                    w.decode(w.ENTRY, 1, payload, BINDING)
        _, payload = event(w.ENTRY, 1)
        with self.assertRaises(w.WireError):
            w.decode(w.ENTRY, 1, payload + b'x', BINDING)

    def test_unknown_tid_cannot_gain_incarnation_or_reference(self):
        for changes in ({'identity': 0, 'lane_generation': 1}, {'identity': 0, 'lane_generation': 0, 'call_entry': 1}):
            _, payload = event(w.GAP, 2, 0, **changes)
            with self.assertRaisesRegex(w.WireError, 'guessed incarnation/reference'):
                w.decode(w.GAP, 2, payload, BINDING)
        result = inspect([event(w.GAP, 1, 0, identity=0, lane_generation=0)])
        self.assertFalse(result['normal_pairs_complete'])
        self.assertEqual(result['entries'], {})

    def test_terminal_requires_matching_process_and_last_reserved_phase(self):
        for changed in ({'process_id': 12}, {'wait_result': 258}, {'after_phase': 1}, {'wait_result': False}):
            with self.subTest(changed=changed), self.assertRaises(w.WireError):
                inspect([event(w.ENTRY, 1), event(w.REPLY, 2, 1)], terminal_changes=changed)

    def test_raw_tid_origin_and_zero_generation_refuse(self):
        _, payload = event(w.ENTRY, 1)
        for binding in (w.OwnedMain(11, 22, 1, 0), w.OwnedMain(11, 22, 1, 1, 'raw_TID_guess')):
            with self.assertRaises(w.WireError):
                w.decode(w.ENTRY, 1, payload, binding)

    def test_legacy_owned_data_is_not_accepted_as_wire(self):
        with self.assertRaisesRegex(w.WireError, 'unsupported wire record kind'):
            w.decode(10, 1, bytes(16), BINDING)

    def test_bulk_preserves_full_capacity_not_returned_count(self):
        before = bytes(i % 256 for i in range(320))
        after = b'x' * 40 + before[40:]
        def bulk(kind, count, snapshot, **meta_changes):
            m = dict(zip(w.INPUT_FIELDS, (7, 8, 2, 1, 20, 16, count, 0, len(snapshot), 0, 0, 3)))
            m.update(meta_changes)
            data = struct.pack('<12I', *(m[k] for k in w.INPUT_FIELDS)) + snapshot
            return event(kind, 1 if kind == w.ENTRY else 2, data=data, api=9,
                         arg0=0x1000, arg1=20, arg2=0x2000, arg3=0x3000, arg4=0)
        a, b = bulk(w.ENTRY, 16, before), bulk(w.REPLY, 2, after)
        result = inspect([a, b])
        self.assertTrue(result['normal_pairs_complete'])
        self.assertEqual(result['replies'][1]['input']['snapshot'][40:], before[40:])
        self.assertEqual(len(b[1]), 1024)
        for row in (bulk(w.REPLY, 17, after), bulk(w.REPLY, 2, after[:40]),
                    bulk(w.REPLY, 2, after, validity=0), bulk(w.REPLY, 2, after, incarnation=3),
                    bulk(w.ENTRY, 2, before), bulk(w.REPLY, 2, after, count_in=17)):
            with self.subTest(kind=row[0], length=len(row[1])), self.assertRaises(w.WireError):
                inspect([a, row] if row[0] == w.REPLY else [row, b])

    def test_input_method_shapes_and_query_pointer_counts(self):
        def row(kind, api, fields, snapshot=b'', **arguments):
            data = struct.pack('<12I', 7, 8, 2, *fields) + snapshot
            return event(kind, 1 if kind == w.ENTRY else 2, data=data, api=api,
                         arg0=0x1000, **arguments)
        # Exact state and scalar method boundaries. Release zero requires its
        # observed retirement count; it cannot silently retain a live identity.
        for api, size in ((7, 256), (8, 0), (11, 0), (12, 0)):
            a = row(w.ENTRY, api, (1, size, 0, 0, 0, size, 0, 0, int(bool(size))),
                    bytes(size), arg1=size, arg2=0x2000)
            b = row(w.REPLY, api, (1, size, 0, 0, 0, size, 0, int(api == 12), int(bool(size))),
                    bytes(size), arg1=size, arg2=0x2000)
            self.assertTrue(inspect([a, b])['normal_pairs_complete'])
        a = row(w.ENTRY, 10, (1, 16, 0xabab, 0xabab, 0, 16, 0, 0, 3),
                bytes(range(16)), arg1=0x2000, arg2=0x3000)
        b = row(w.REPLY, 10, (1, 16, 0xabab, 0x9999, 0, 4, 0x9999, 0, 3),
                struct.pack('<I', 0x9999), arg1=0x2000, arg2=0x3000)
        self.assertTrue(inspect([a, b])['normal_pairs_complete'])
        mutated = bytearray(b[1]); mutated[-1] ^= 1
        with self.assertRaisesRegex(w.WireError, 'raw pointer'):
            inspect([a, (w.REPLY, bytes(mutated))])


if __name__ == '__main__':
    unittest.main()
