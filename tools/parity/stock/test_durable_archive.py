import struct
import unittest
import importlib.util
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location('durable_archive_test_subject', Path(__file__).with_name('durable_archive.py'))
d = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = d
spec.loader.exec_module(d)

NONCE = (1, 2, 3, 4)


def fixture(next_ticket=4, ack=2, states=(0, 1, 3), slots=4, flags=0, lock=0):
    mapping = bytearray(d.HEADER_BYTES + slots * d.SLOT_BYTES)
    struct.pack_into('<14I', mapping, 0, 0x44525231, 1, 128, 1040, slots,
                     len(mapping), *NONCE, next_ticket, ack, flags, lock)
    journal = bytearray()
    for ticket in range(1, next_ticket):
        state = states[ticket-1]
        payload = struct.pack('<I', ticket * 7)
        offset = 128 + ((ticket-1) % slots) * 1040
        if state != -1:
            struct.pack_into('<4I', mapping, offset, ticket << 3 | state, 10, len(payload), 0)
            mapping[offset+16:offset+20] = payload
        if ticket < ack or state in (3, 4):
            journal += struct.pack('<4I', ticket, (ticket-1)//slots, 10, len(payload)) + payload + bytes(1020)
    return bytes(mapping), bytes(journal)


class DurableArchiveTest(unittest.TestCase):
    def test_pending_preserves_later_committed_record(self):
        result = d.inspect(*fixture(), NONCE)
        self.assertEqual([r.ticket for r in result.records], [1, 3])
        self.assertEqual(result.unfinished, ((2, 1),))
        self.assertFalse(result.transport_complete)

    def test_death_before_slot_claim_is_explicit(self):
        result = d.inspect(*fixture(states=(0, -1, 3)), NONCE)
        self.assertEqual(result.unfinished, ((2, -1),))
        self.assertFalse(result.transport_complete)

    def test_death_before_reused_slot_claim_checks_retained_old_record(self):
        mapping, journal = fixture(next_ticket=6, ack=5, states=(0, 0, 0, 0, -1))
        result = d.inspect(mapping, journal, NONCE)
        self.assertEqual(result.unfinished, ((5, -1),))
        bad = bytearray(journal)
        struct.pack_into('<I', bad, 16, 999)
        with self.assertRaises(d.ArchiveError):
            d.inspect(mapping, bytes(bad), NONCE)

    def test_unclaimed_initial_slot_must_still_be_empty(self):
        mapping, journal = fixture(states=(0, -1, 3))
        bad = bytearray(mapping)
        struct.pack_into('<I', bad, 128+1040+16, 999)
        with self.assertRaises(d.ArchiveError):
            d.inspect(bytes(bad), journal, NONCE)

    def test_writing_bytes_are_never_fabricated_as_reply(self):
        result = d.inspect(*fixture(states=(0, 2, 3)), NONCE)
        self.assertEqual(result.unfinished, ((2, 2),))
        self.assertEqual(len(result.records), 2)

    def test_reading_tail_survives_abandoned_collector_lock(self):
        result = d.inspect(*fixture(states=(0, 4, 3), lock=1), NONCE)
        self.assertEqual([r.ticket for r in result.records], [1, 2, 3])
        self.assertFalse(result.transport_complete)

    def test_ack_death_after_free_before_counter_preserves_archived_record(self):
        mapping, journal = fixture(states=(0, 0, 3), lock=1)
        ticket2 = struct.pack('<4I', 2, 0, 10, 4) + struct.pack('<I', 14) + bytes(1020)
        journal = journal[:1040] + ticket2 + journal[1040:]
        result = d.inspect(mapping, journal, NONCE)
        self.assertEqual([r.ticket for r in result.records], [1, 2, 3])
        self.assertEqual(result.unfinished, ((2, 0),))
        self.assertFalse(result.transport_complete)

    def test_clean_transport_is_not_process_proof(self):
        result = d.inspect(*fixture(ack=4, states=(0, 0, 0)), NONCE)
        self.assertTrue(result.transport_complete)
        self.assertFalse(hasattr(result, 'process_replay_qualified'))

    def test_sticky_faults_never_become_complete(self):
        for flag in (1, 2, 4, 8, 16, 31):
            with self.subTest(flag=flag):
                result = d.inspect(*fixture(ack=4, states=(0, 0, 0), flags=flag), NONCE)
                self.assertFalse(result.transport_complete)

    def test_wrap_reuses_slot_without_exposing_stale_tail(self):
        mapping, journal = fixture(next_ticket=7, ack=7, states=(0,)*6)
        mutable = bytearray(mapping)
        mutable[128+20:128+24] = b'old!'
        result = d.inspect(bytes(mutable), journal, NONCE)
        self.assertEqual(len(result.records), 6)
        self.assertTrue(result.transport_complete)
        self.assertEqual(result.records[4].payload, struct.pack('<I', 35))

    def test_corruptions_reject(self):
        mapping, journal = fixture()
        controls = []
        for label, offset, value in [('nonce',24,9),('tag',128+1040,99),('size',20,42),
                                     ('version',4,2),('unreserved',128+3*1040,1),
                                     ('unknown_flags',48,32),('unbounded_ack',44,0xffffffff)]:
            altered = bytearray(mapping)
            struct.pack_into('<I', altered, offset, value)
            controls.append((label, bytes(altered), journal))
        controls.extend([('missing',mapping,journal[:1040]),('duplicate',mapping,journal+journal[-1040:]),
                         ('reverse',mapping,journal[1040:]+journal[:1040]),('truncated',mapping,journal[:-1])])
        for label, offset, value in [('generation',4,8),('payload',16,999),('tail',20,1),('length',12,1025)]:
            altered = bytearray(journal)
            struct.pack_into('<I', altered, offset, value)
            controls.append((label,mapping,bytes(altered)))
        for label, raw, archive in controls:
            with self.subTest(label=label), self.assertRaises(d.ArchiveError):
                d.inspect(raw, archive, NONCE)

    def test_nonce_and_raw_types_are_strict(self):
        mapping, journal = fixture()
        for nonce in ([1,2,3,4], (True,2,3,4), (1,2,3,-1), (1,2,3,5)):
            with self.assertRaises(d.ArchiveError):
                d.inspect(mapping,journal,nonce)
        with self.assertRaises(d.ArchiveError):
            d.inspect(bytearray(mapping),journal,NONCE)


if __name__ == '__main__':
    unittest.main()
