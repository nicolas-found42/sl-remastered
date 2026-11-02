"""Loss/identity controls for the experimental Stock raw inventory (no Wine)."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('stock_input_tape', Path(__file__).with_name('stock_input_tape.py'))
tape = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tape)


class InventoryTest(unittest.TestCase):
    def setUp(self):
        self.setup = [{'event': 'setup', 'observer_bound': 1}]
        self.setup += [{'event': 'device_setup', 'device_id': i, 'kind': i, 'object': i*100,
                        'bound': 1, 'gap': 0, 'added_com_calls': 0, 'aliases_bound': 0} for i in (1, 2)]
        self.rows = [{'event': 'installation', 'file_sha256': '7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde'}]
        for i in (1, 2):
            size = 256 if i == 1 else 16
            self.rows.append(dict(event='input_return', scope='experimental_stock_consumption',
                seq=i, return_seq=i, entry_phase=2*i-1, return_phase=2*i, lane=0, lane_read=i,
                device_id=i, incarnation=1, device_kind=i, method=0, caller_raw=0x400100,
                object_raw=i*100, related_raw=0, result=0, gap=0, size=size, count_in=0, count_out=0,
                flags=0, cw=[0x37f,0x37f], mxcsr=[0x1f80,0x1f80], last_error=[0,0],
                pre_valid=1, post_valid=1, pre=bytes(size).hex(), post=bytes(size).hex()))
        self.rows += [dict(event='input_observer_status', scope='experimental_stock_consumption',
                           input_entered=2, input_returned=2),
                      dict(event='status', format_version=6, mode='record_only', entered=2,
                           returned=2, phases=4, active=0, pending=0, accepting=0, dropped=0, gaps=0)]

    def audit(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            for name, rows in [('tape', self.rows), ('setup', self.setup)]:
                (p / name).write_text(''.join(json.dumps(row)+'\n' for row in rows))
            return tape.inventory(p/'tape', p/'setup')

    def test_bounded_inventory(self):
        result = self.audit()
        self.assertEqual(result['mouse_transitions'], [0])
        self.assertFalse(result['process_replay_qualified'])
        self.assertFalse(result['caller_admission_qualified'])

    def test_omitted_input_rejected(self):
        del self.rows[1]
        with self.assertRaises(ValueError): self.audit()

    def test_duplicate_phase_rejected(self):
        self.rows[2]['return_phase'] = 2
        with self.assertRaises(ValueError): self.audit()

    def test_unbound_alias_gap_rejected(self):
        self.rows[2]['gap'] = 20
        with self.assertRaises(ValueError): self.audit()

    def test_stale_generation_rejected(self):
        self.rows[2]['incarnation'] = 2
        with self.assertRaises(ValueError): self.audit()

    def test_failed_drain_rejected(self):
        self.rows[-1]['active'] = 1
        with self.assertRaises(ValueError): self.audit()

    def test_hidden_setup_intervention_rejected(self):
        self.setup[1]['added_com_calls'] = 2
        with self.assertRaises(ValueError): self.audit()

    def test_truncated_postimage_rejected(self):
        self.rows[2]['post'] = ''
        with self.assertRaises(ValueError): self.audit()

    def test_wrong_stock_identity_rejected(self):
        self.rows[0]['file_sha256'] = '0'*64
        with self.assertRaises(ValueError): self.audit()

    def test_wrong_scope_rejected(self):
        self.rows[2]['scope'] = 'owned_console_only'
        with self.assertRaises(ValueError): self.audit()

    def test_lane_loss_rejected(self):
        self.rows[2]['lane_read'] = 3
        with self.assertRaises(ValueError): self.audit()

    def test_input_counter_lie_rejected(self):
        self.rows[-2]['input_entered'] = 1
        with self.assertRaises(ValueError): self.audit()

    def test_boolean_gap_is_not_a_protocol_integer(self):
        self.rows[2]['gap'] = False
        with self.assertRaises(ValueError): self.audit()


class BulkPayloadTest(unittest.TestCase):
    def row(self,n=16):
        return dict(method=2,size=20,device_kind=1,flags=0,count_in=n,count_out=2 if n>=2 else 1,buffer_bytes=20*n,pre_valid=1,post_valid=1,pre='7e'*(20*n),post='00'*(20*n))
    def test_supported_shapes(self):
        for n in (1,2,16):self.assertEqual(tape.validate_payload(self.row(n),7),20*n)
    def test_version6_does_not_admit_bulk(self):
        with self.assertRaises(ValueError):tape.validate_payload(self.row(16),6)
    def test_full_tail_required(self):
        for field in ('pre','post'):
            r=self.row();r[field]=r[field][:40]
            with self.assertRaises(ValueError):tape.validate_payload(r,7)
    def test_no_gap_payload_cannot_omit_snapshots(self):
        r=self.row();r.update(pre_valid=0,post_valid=0,pre='',post='')
        with self.assertRaises(ValueError):tape.validate_payload(r,7)

    def test_inventory_cannot_omit_state_snapshots(self):
        fixture=InventoryTest();fixture.setUp()
        fixture.rows[1].update(pre_valid=0,post_valid=0,pre='',post='')
        with self.assertRaises(ValueError):fixture.audit()

    def test_count_and_byte_bounds(self):
        for field,value in [('count_in',0),('count_in',17),('count_in',0xffffffff),('count_in',True),('count_out',17),('buffer_bytes',20),('buffer_bytes',True),('size',16),('flags',1),('device_kind',2),('pre_valid',True)]:
            with self.subTest(field=field,value=value):
                r=self.row();r[field]=value
                with self.assertRaises(ValueError):tape.validate_payload(r,7)


if __name__ == '__main__':
    unittest.main(verbosity=2)
