"""Controls for the proposed input tape validator and Stock-consumption projection (synthetic only)."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('inp', Path(__file__).with_name('input_interval.py'))
inp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inp)

KB, MS = inp.STATE_SITES['keyboard'], inp.STATE_SITES['mouse']


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def oracle_bits(x, w, cw):
    """Independent x87 model: 24-bit precision == float32 ops; 53-bit == double ops."""
    if (cw >> 8) & 3 == 0:
        return bits(f32(f32(2 * f32(x / w)) - 1.0))
    return bits(f32(2 * (x / w) - 1.0))


def hx(data):
    return bytes(data).hex()


def state(device, site, hresult, pre, post):
    return dict(event='di.state', caller_rva=site, device=device, cb=inp.SIZES[device], hresult=hresult,
                pre=hx(pre), post=hx(post))


def acquire(device, site, hresult):
    return dict(event='di.acquire', caller_rva=site, device=device, hresult=hresult)


def tape(events, runtime=inp.WINE_RUNTIME):
    rows, counts = [], {}
    for i, event in enumerate(events):
        rows.append(dict(event, ticket=i + 1, entry_phase=2 * i + 1, return_phase=2 * i + 2))
        counts[event['event']] = counts.get(event['event'], 0) + 1
    header = dict(event='input_header', version=1, runtime=runtime, stock_sha256=inp.STOCK_SHA256,
                  scope='proposed_input_tape')
    status = dict(event='input_status', counts=counts, phases=2 * len(events), dropped=0, gaps=0, pending=0,
                  accepting=0)
    return [header] + rows + [status]


def callback(message, lparam, wparam=0, width=800, height=600, cw=0x27f, before=0):
    row = dict(event='win.callback', caller_rva=0xb8210, message=message, wparam=wparam, lparam=lparam, width=width,
               height=height, x87_cw=cw, flags_before=before)
    if message in inp.CURSOR_MESSAGES:
        p = inp.project_cursor(lparam, wparam, width, height, cw, before)
        row.update(norm_x_bits=p['norm_x_bits'], norm_y_bits=p['norm_y_bits'], flags_after=p['flags_after'])
    else:
        row.update(norm_x_bits=0, norm_y_bits=0, flags_after=before)
    return row


def valid_events():
    key_pre = bytes(256)
    key_post = bytes(range(256))
    mouse_pre = bytes(16)
    mouse_post = struct.pack('<iii4B', -5, 7, 0, 1, 0, 0, 0)
    return [
        state('keyboard', KB['initial'], 0, key_pre, key_post),
        state('mouse', MS['initial'], 0, mouse_pre, mouse_post),
        callback(0x200, (479 << 16) | 299, 0),
        callback(0x201, (479 << 16) | 299, 1, before=0),
        callback(0x0f, 0, 0),
    ]


class ProjectionTest(unittest.TestCase):
    def test_cursor_bits_match_an_independent_x87_model_for_both_precisions(self):
        checked = 0
        for cw in (0x27f, 0x3f):
            for w in (640, 800, 1024, 1920):
                for x in list(range(0, 65536, 97)) + [0, 1, 65535, 400]:
                    got = inp.x87_normalized_cursor_bits(x, w, cw)
                    self.assertEqual(got, oracle_bits(x, w, cw), (cw, w, x))
                    checked += 1
        self.assertGreater(checked, 5000)

    def test_precision_actually_changes_some_results(self):
        diffs = sum(inp.x87_normalized_cursor_bits(x, 800, 0x27f) != inp.x87_normalized_cursor_bits(x, 800, 0x3f)
                    for x in range(0, 800))
        self.assertGreater(diffs, 0)

    def test_packed_coordinates_are_unsigned_and_wparam_is_ored_into_flags(self):
        out = inp.project_cursor(0xffff_ffff, 0x0000_0005, 800, 600, 0x27f, 0xabcd_0000)
        self.assertEqual((out['x_raw'], out['y_raw']), (65535, 65535))
        self.assertGreater(struct.unpack('<f', struct.pack('<I', out['norm_x_bits']))[0], 1)
        self.assertEqual(out['flags_after'], 0xabcd_0005)
        self.assertEqual(inp.project_cursor(0, 0xffff_ffff, 800, 600, 0x27f, 0x1234_5678)['flags_after'], 0x1234_ffff | 0xffff_0000 & 0xffff_ffff)

    def test_unsupported_operands_and_control_words_are_refused(self):
        for args in ((1, 0, 0x27f), (1, 800, 0x67f), (1, 800, 0x17f), (70000, 800, 0x27f)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                inp.x87_normalized_cursor_bits(*args)

    def test_buffered_acceptance_rules(self):
        down = struct.pack('<IIIII', 0x1c, 0x80, 1, 2, 3)
        self.assertEqual(inp.project_buffered(0, 1, down), {'key_down_offset': 0x1c})
        self.assertEqual(inp.project_buffered(1, 1, down), {'key_down_offset': 0x1c})  # S_FALSE-like >= 0 accepted
        for hr, count, data in ((0x80070005, 1, down), (0, 0, down), (0, 2, down),
                                (0, 1, struct.pack('<IIIII', 256, 0x80, 0, 0, 0)),
                                (0, 1, struct.pack('<IIIII', 5, 0x7f, 0, 0, 0))):
            self.assertIsNone(inp.project_buffered(hr, count, data), (hr, count))
        self.assertEqual(inp.project_buffered(0, 1, struct.pack('<IIIII', 255, 0x180, 9, 9, 9)), {'key_down_offset': 255})

    def test_keyboard_failed_retry_keeps_stale_bytes_but_mouse_clears(self):
        stale = bytes([7]) * 256
        kb = [dict(kind='state', hresult=0x8007001c, post=stale), dict(kind='acquire', hresult=0),
              dict(kind='state', hresult=0x8007001c, post=stale)]
        self.assertEqual(inp.project_sample('keyboard', kb), stale)
        ms = [dict(kind='state', hresult=0x8007001c, post=bytes([3]) * 16), dict(kind='acquire', hresult=0),
              dict(kind='state', hresult=0x8007001c, post=bytes([3]) * 16)]
        self.assertEqual(inp.project_sample('mouse', ms), bytes(16))
        self.assertEqual(inp.project_sample('keyboard', kb[:1] + [dict(kind='acquire', hresult=5)]), bytes(256))
        with self.assertRaises(ValueError):
            inp.project_sample('keyboard', kb[:2])


class TapeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)

    def compile(self, rows):
        return inp.compile_rows(rows)

    def test_valid_tape_projects_samples_cursor_and_denies_replay(self):
        result = self.compile(tape(valid_events()))
        self.assertFalse(result['process_replay_qualified'])
        self.assertEqual([s['device'] for s in result['immediate_samples']], ['keyboard', 'mouse'])
        self.assertEqual(result['immediate_samples'][0]['consumed'], hx(range(256)))
        self.assertEqual(len(result['cursor']), 2)
        self.assertEqual(result['cursor'][1]['flags_after'], 1)

    def test_control_flow_paths_for_failed_polls(self):
        stale = bytes([9]) * 256
        events = [state('keyboard', KB['initial'], 0x8007001c, stale, stale), acquire('keyboard', KB['acquire'], 0),
                  state('keyboard', KB['retry'], 0x8007001c, stale, stale),
                  state('mouse', MS['initial'], 0x8007001c, bytes([4]) * 16, bytes([4]) * 16),
                  acquire('mouse', MS['acquire'], 5)]
        out = self.compile(tape(events))['immediate_samples']
        self.assertEqual([(s['path'], s['consumed']) for s in out],
                         [('retry', hx(stale)), ('acquire-failed', hx(bytes(16)))])

    def test_each_corruption_is_rejected_for_its_own_reason(self):
        def row(r, i):
            return r[i]
        cases = {
            'cursor-bits': (lambda r: row(r, 3).update(norm_x_bits=row(r, 3)['norm_x_bits'] ^ 1), 'contradict'),
            'cursor-y-bits': (lambda r: row(r, 3).update(norm_y_bits=row(r, 3)['norm_y_bits'] ^ 0x100), 'contradict'),
            'flags': (lambda r: row(r, 4).update(flags_after=row(r, 4)['flags_after'] ^ 1), 'contradict'),
            'noncursor-with-results': (lambda r: row(r, 5).update(norm_x_bits=1), 'non-cursor'),
            'wrong-stock': (lambda r: r[0].update(stock_sha256='0' * 64), 'Stock identity'),
            'wrong-scope': (lambda r: r[0].update(scope='x'), 'scope'),
            'unknown-site': (lambda r: row(r, 1).update(caller_rva=0x1234), 'unknown site'),
            'wrong-size': (lambda r: row(r, 1).update(cb=255), 'unknown site/size'),
            'short-hex': (lambda r: row(r, 1).update(post='00'), 'hex'),
            'bad-hex': (lambda r: row(r, 2).update(post='zz' * 16), 'hex'),
            'extra-field': (lambda r: row(r, 1).update(extra=1), 'unknown event/fields'),
            'duplicate-ticket': (lambda r: row(r, 2).update(ticket=1), 'duplicate ticket'),
            'phase-gap': (lambda r: row(r, 5).update(return_phase=99), 'phase'),
            'reversed-phase': (lambda r: row(r, 1).update(entry_phase=2, return_phase=1), 'reversed'),
            'status-counts': (lambda r: r[-1]['counts'].update({'di.state': 9}), 'status disagrees'),
            'loss': (lambda r: r[-1].update(dropped=1), 'loss'),
            'bool-hresult': (lambda r: row(r, 1).update(hresult=True), 'uint32'),
            'negative-hresult': (lambda r: row(r, 1).update(hresult=-1), 'uint32'),
        }
        for name, (mutate, reason) in cases.items():
            rows = tape(valid_events() + [callback(0x0f, 0)])
            mutate(rows)
            with self.subTest(mutation=name), self.assertRaises(ValueError) as caught:
                self.compile(rows)
            self.assertIn(reason, str(caught.exception), name)

    def test_failure_that_changes_the_buffer_is_refused_only_on_the_wine_runtime(self):
        pre, post = bytes(256), bytes([1]) + bytes(255)
        events = [state('keyboard', KB['initial'], 0x8007001c, pre, post), acquire('keyboard', KB['acquire'], 5)]
        with self.assertRaises(ValueError) as caught:
            self.compile(tape(events))
        self.assertIn('Wine', str(caught.exception))
        other = self.compile(tape(events, runtime='windows-unqualified'))
        self.assertEqual(other['immediate_samples'][0]['path'], 'acquire-failed')

    def test_changed_bytes_are_derived_not_trusted(self):
        pre, post = bytes(256), bytes([0, 5]) + bytes(254)
        rows = tape([state('keyboard', KB['initial'], 0, pre, post)])
        from_tape = [e for e in rows[1:-1]]
        typed_pre = bytes.fromhex(from_tape[0]['pre'])
        typed_post = bytes.fromhex(from_tape[0]['post'])
        self.assertEqual([i for i in range(256) if typed_pre[i] != typed_post[i]], [1])

    def test_sequence_violations_are_rejected(self):
        stale = bytes(256)
        cases = {
            'retry-without-acquire': [state('keyboard', KB['initial'], 0x8007001c, stale, stale),
                                      state('keyboard', KB['retry'], 0, stale, stale)],
            'acquire-without-initial': [acquire('keyboard', KB['acquire'], 0)],
            'unfinished-poll': [state('keyboard', KB['initial'], 0x8007001c, stale, stale)],
            'retry-after-failed-acquire': [state('keyboard', KB['initial'], 0x8007001c, stale, stale),
                                           acquire('keyboard', KB['acquire'], 5),
                                           state('keyboard', KB['retry'], 0, stale, stale)],
            'new-poll-before-finish': [state('keyboard', KB['initial'], 0x8007001c, stale, stale),
                                       state('keyboard', KB['initial'], 0, stale, stale)],
        }
        for name, events in cases.items():
            with self.subTest(sequence=name), self.assertRaises(ValueError):
                self.compile(tape(events))

    def test_buffered_call_shape_and_projection(self):
        down = struct.pack('<IIIII', 0x1c, 0x80, 1, 2, 3)
        data = dict(event='di.data', caller_rva=inp.BUFFERED['data'], device='keyboard', count_in=1, hresult=0,
                    count_out=1, pre=hx(bytes(20)), post=hx(down))
        out = self.compile(tape([acquire('keyboard', inp.BUFFERED['acquire'], 1), data]))
        self.assertEqual(out['buffered'][0]['consumed'], {'key_down_offset': 0x1c})
        for mutation in ({'count_in': 2}, {'device': 'mouse'}, {'caller_rva': 5}):
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                self.compile(tape([dict(data, **mutation)]))

    def test_cli_reports_inconclusive_for_bad_files_and_never_claims_replay(self):
        good = Path(self.temp.name) / 'good.jsonl'
        good.write_text(''.join(json.dumps(r) + '\n' for r in tape(valid_events())))
        run = lambda path: subprocess.run([sys.executable, '-I', inp.__file__, str(path)], capture_output=True,
                                          text=True, timeout=30)
        result = run(good)
        self.assertEqual(result.returncode, 0)
        self.assertFalse(json.loads(result.stdout)['process_replay_qualified'])
        bad = Path(self.temp.name) / 'bad.jsonl'
        bad.write_text('{"event":"input_header","event":"x"}\n')
        broken = run(bad)
        self.assertEqual(broken.returncode, 1)
        self.assertEqual(json.loads(broken.stdout)['status'], 'inconclusive')
        self.assertEqual(run(Path(self.temp.name) / 'missing').returncode, 1)


if __name__ == '__main__':
    unittest.main()
