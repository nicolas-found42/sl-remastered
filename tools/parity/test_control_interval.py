"""Synthetic validator controls; never Stock execution or replay evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import uuid

spec = importlib.util.spec_from_file_location('interval', Path(__file__).with_name('control_interval.py'))
interval = importlib.util.module_from_spec(spec)
spec.loader.exec_module(interval)


def fixture(version=3):
    base = 0x400000
    initial = [dict(event='initial_global', name=name, image_base=base, rva=rva, size=size,
                    valid=1, words=[0] * (size // 4)) for name, (rva, size) in interval.INITIAL.items()]
    one = dict(event='api_return', gap_reason=0, seq=1, return_seq=2, thread=100, lane=0,
               lane_read=1, role='unresolved', api='QueryPerformanceFrequency',
               caller=base + 0x15169b, caller_rva=0x15169b, arg=base + 0x36a280,
               result=1, last_error_before=0, last_error_after=0, output_valid=1,
               output_u32=[10000000, 0], cw=[0x27f, 0x27f], mxcsr=[0x1f80, 0x1f80])
    two = dict(one, seq=2, return_seq=1, thread=200, lane=1, api='GetTickCount',
               caller=base + 0x155278, caller_rva=0x155278, arg=0, result=7,
               output_valid=0, output_u32=[0, 0])
    footer = dict(event='status', format_version=version, entered=2, returned=2, active=0,
                  pending=0, dropped=0, gaps=0, accepting=0, mode='record_only',
                  early_startup='main_iat_from_proxy_attach' if version == 4 else 'unobserved')
    if version >= 3:
        one.update(entry_phase=1, return_phase=4)
        two.update(entry_phase=2, return_phase=3)
        footer['phases'] = 4
    if version == 4:
        initial.insert(0, {'event': 'installation', 'boundary': 'dll_process_attach_main_iat',
                           'identity': 'late_full_file_sha256_verified',
                           'initial_observations': 'not_restorable_checkpoint',
                           'file_sha256': interval.STOCK_SHA256, 'scope': 'stock_main_image_api_interval'})
    return initial + [one, two, footer]


class IntervalTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / 'capture.jsonl'

    def write(self, rows):
        self.path.write_text(''.join(json.dumps(row) + '\n' for row in rows))
        return self.path

    def test_shared_phases_preserve_cross_lane_overlap(self):
        result = interval.compile_tape(self.write(fixture()))
        self.assertEqual([(p['phase'], p['call']) for p in result['phase_order']],
                         [('entry', 1), ('entry', 2), ('return', 2), ('return', 1)])
        self.assertFalse(result['process_replay_qualified'])
        self.assertIn('unresolved', result['lane_roles'])

    def native_fixture(self):
        rows = fixture(4)
        rows[0] = copy.deepcopy(interval.NATIVE_INSTALLATION)
        rows[-1].update(format_version=8, phases=5)
        rows[-3]['return_phase'] = 5
        fx = bytearray(512)
        fx[0:2] = (0x37f).to_bytes(2, 'little')
        fx[4] = 1  # TOP=0, physical ST0 nonempty
        fx[32:42] = bytes.fromhex('0000000000000080ff3f')  # extended 1.0
        record = dict(event='native_clock_return', native='java.lang.System.currentTime()F',
                      ordinal=1, phase=4, thread=200, lane=1, lane_read=2,
                      role='unresolved', caller=0x47aaaa, last_error=7, fxsave_hex=fx.hex())
        rows.insert(-1, record)
        rows.insert(-1, dict(event='native_clock_status', returns=1,
                            phase_source='shared_next_phase', scope='return_only_not_replay'))
        return rows

    def test_native_returns_keep_raw_shared_order_without_invented_entries(self):
        compiled = interval.compile_tape(self.write(self.native_fixture()))
        self.assertEqual([p['ordinal'] for p in compiled['phase_order']], [1, 2, 3, 4, 5])
        self.assertEqual(compiled['phase_order'][3], dict(ordinal=4, phase='native_return', native_return=1, lane=1))
        self.assertEqual(len(compiled['calls']), 2)
        self.assertEqual(compiled['native_returns'][0]['phase'], 4)
        self.assertFalse(compiled['process_replay_qualified'])
        self.assertNotEqual(compiled['format'], 'stock-api-interval-offline/2')

    def test_native_return_loss_reordering_empty_st0_and_identity_are_refused(self):
        for mutation in ('missing', 'ordinal', 'phase', 'lane-read', 'empty-st0',
                         'truncated-fx', 'caller', 'fake-entry', 'count', 'scope'):
            rows = self.native_fixture()
            record, status = rows[-3:-1]
            if mutation == 'missing': del rows[-3]
            elif mutation == 'ordinal': record['ordinal'] = 2
            elif mutation == 'phase': record['phase'] = 3
            elif mutation == 'lane-read': record['lane_read'] = 1
            elif mutation == 'empty-st0': record['fxsave_hex'] = '00' * 512
            elif mutation == 'truncated-fx': record['fxsave_hex'] = record['fxsave_hex'][:-2]
            elif mutation == 'caller': record['caller'] = 0
            elif mutation == 'fake-entry': record['entry_phase'] = 1
            elif mutation == 'count': status['returns'] = 2
            else: rows[0]['scope'] = 'stock_main_image_api_interval'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                interval.compile_tape(self.write(rows))

    def test_native_return_value_corruption_is_detected_at_raw_bound_byte(self):
        rows = self.native_fixture()
        original = interval.compile_tape(self.write(rows))
        raw = bytearray.fromhex(rows[-3]['fxsave_hex'])
        raw[32] ^= 1
        rows[-3]['fxsave_hex'] = raw.hex()
        corrupted = interval.compile_tape(self.write(rows))
        compared = interval.compare(original, corrupted)
        self.assertFalse(compared['offline_match'])
        self.assertEqual(compared['first_divergence']['path'], '$.native_returns[0].fxsave_hex')

    def test_legacy_counters_cannot_be_promoted_to_shared_phase_order(self):
        result = interval.compile_tape(self.write(fixture(2)))
        self.assertNotIn('phase_order', result)
        self.assertFalse(result['process_replay_qualified'])
        self.assertNotEqual(result['calls'][0]['entry'], result['calls'][0]['return'])

    def test_early_header_requires_exact_identity_boundary_and_single_installation(self):
        rows = fixture(4)
        compiled = interval.compile_tape(self.write(rows))
        self.assertEqual(compiled['installation']['file_sha256'], interval.STOCK_SHA256)
        self.assertFalse(compiled['process_replay_qualified'])
        for mutation in ('missing-header', 'duplicate-header', 'wrong-identity', 'owned-only', 'fake-checkpoint', 'relocated-base'):
            bad = copy.deepcopy(rows)
            if mutation == 'missing-header':
                del bad[0]
            elif mutation == 'duplicate-header':
                bad.insert(0, copy.deepcopy(bad[0]))
            elif mutation == 'wrong-identity':
                bad[0]['file_sha256'] = '0' * 64
            elif mutation == 'owned-only':
                bad[0]['scope'] = 'owned_console_only'
            elif mutation == 'fake-checkpoint':
                bad[0]['initial_observations'] = 'restorable_checkpoint'
            else:
                for row in bad:
                    if row['event'] == 'initial_global':
                        row['image_base'] += 0x10000
                    if row['event'] == 'api_return':
                        row['caller'] += 0x10000
                        if row['api'] in ('QueryPerformanceCounter', 'QueryPerformanceFrequency'):
                            row['arg'] += 0x10000
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                interval.compile_tape(self.write(bad))

    def test_valid_changed_output_reports_exact_first_difference_and_both_hashes(self):
        rows = fixture()
        original = interval.compile_tape(self.write(rows))
        rows[-3]['output_u32'][0] ^= 1
        changed = interval.compile_tape(self.write(rows))
        verdict = interval.compare(original, changed)
        self.assertFalse(verdict['offline_match'])
        self.assertFalse(verdict['process_replay_qualified'])
        self.assertEqual(verdict['first_divergence']['path'], '$.calls[0].reply.output_u32[0]')
        self.assertEqual(verdict['expected_sha256'], original['source_sha256'])
        self.assertEqual(verdict['observed_sha256'], changed['source_sha256'])
        self.assertNotEqual(verdict['expected_sha256'], verdict['observed_sha256'])

    def test_invalid_accounting_phase_and_output_mutations_are_rejected(self):
        for mutation in ('missing-call', 'duplicate-global', 'unknown-api', 'wrong-caller', 'wrong-read',
                         'invalid-output', 'drop', 'not-stopped', 'duplicate-phase', 'reversed-phase',
                         'missing-phase', 'boolean-phase', 'unknown-field', 'mixed-version', 'late-global'):
            rows = fixture()
            one, two, footer = rows[-3:]
            if mutation == 'missing-call':
                del rows[-3]
            elif mutation == 'duplicate-global':
                rows.insert(0, copy.deepcopy(rows[0]))
            elif mutation == 'unknown-api':
                one['api'] = 'unknown'
            elif mutation == 'wrong-caller':
                one['caller'] += 1
            elif mutation == 'wrong-read':
                one['lane_read'] = 2
            elif mutation == 'invalid-output':
                one['output_valid'] = 0
            elif mutation == 'drop':
                footer['dropped'] = 1
            elif mutation == 'not-stopped':
                footer['accepting'] = 1
            elif mutation == 'duplicate-phase':
                two['entry_phase'] = 1
            elif mutation == 'reversed-phase':
                one.update(entry_phase=4, return_phase=1)
            elif mutation == 'missing-phase':
                del one['entry_phase']
            elif mutation == 'boolean-phase':
                one['entry_phase'] = True
            elif mutation == 'unknown-field':
                one['ignored'] = 1
            elif mutation == 'mixed-version':
                rows.insert(8, dict(footer, format_version=2))
            else:
                rows.insert(8, dict(footer, entered=0, returned=0, phases=0))
                rows[0], rows[9] = rows[9], rows[0]
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                interval.compile_tape(self.write(rows))

    def test_bounds_duplicates_nonfinite_and_partial_lines_are_rejected(self):
        self.write(fixture())
        for limits in ({'max_bytes': 1}, {'max_records': 1}):
            with self.assertRaises(ValueError):
                interval.compile_tape(self.path, **limits)
        for raw in (b'{"event":"status","event":"status"}\n', b'{"value":NaN}\n',
                    b'{}', b'x' * (interval.MAX_LINE_BYTES + 1) + b'\n', b'[]\n'):
            self.path.write_bytes(raw)
            with self.subTest(raw=raw[:30]), self.assertRaises(ValueError):
                interval.compile_tape(self.path)

    def test_null_stack_output_cannot_be_normalized_as_a_valid_local(self):
        rows = fixture()
        rows[-3].update(api='QueryPerformanceCounter', caller_rva=0x15233e,
                        caller=0x400000 + 0x15233e, arg=0)
        with self.assertRaises(ValueError):
            interval.compile_tape(self.write(rows))

    def test_cli_refuses_to_overwrite_input_or_write_reference_folder(self):
        self.write(fixture())
        original = self.path.read_bytes()
        ref_out = interval.REFERENCE / ('interval-must-not-exist-' + uuid.uuid4().hex)
        for output in (self.path, ref_out):
            result = subprocess.run([sys.executable, '-I', interval.__file__, str(self.path),
                                     '--output', str(output)], capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 1)
        self.assertFalse(ref_out.exists())
        self.assertEqual(self.path.read_bytes(), original)


def rng_fixture():
    """Synthetic format-5 tape using the exact CRT tail arithmetic (not Stock evidence)."""
    base = 0x400000
    initial = [dict(event='initial_global', name=name, image_base=base, rva=rva, size=size,
                    valid=1, words=[0] * (size // 4)) for name, (rva, size) in interval.INITIAL.items()]
    install = copy.deepcopy(interval.RNG_INSTALLATION)
    srand, rand = 'CRT_srand_after_original_TLS_getter', 'CRT_rand_after_original_TLS_getter'
    rows, phase, state = [], 0, {}

    def event(seq, lane, read, api, rva, block, before, arg):
        nonlocal phase
        if api == srand:
            after, eax, flags = arg, block, [0x246, 0x246]
        else:
            after = (before * 0x343fd + 0x269ec3) & 0xffffffff
            eax = (after >> 16) & 0x7fff
            flags = [0x246, 0x202 | (int(bin(eax & 0xff).count('1') % 2 == 0) << 2) | ((eax == 0) << 6)]
        phase += 2
        state[block] = after
        return dict(event='rng_return', gap_reason=0, seq=seq, return_seq=seq, entry_phase=phase - 1,
                    return_phase=phase, thread=1000 + lane, lane=lane, lane_read=read, role='unresolved',
                    api=api, caller=base + rva, caller_rva=rva, arg=arg, result=eax,
                    last_error_before=0, last_error_after=0, output_valid=0, output_u32=[0, 0],
                    cw=[0x27f, 0x27f], mxcsr=[0x1f80, 0x1f80], rng_tls_raw=block, rng_before=before,
                    rng_after=after, eflags=flags)

    rows.append(event(1, 0, 1, srand, 0x7c7e0, 0x20000, 1, 1234))
    rows.append(event(2, 0, 2, rand, 0x36148, 0x20000, 1234, 0))
    rows.append(event(3, 0, 3, rand, 0x51339, 0x20000, state[0x20000], 0))
    rows.append(event(4, 1, 1, rand, 0x36148, 0x30000, 1, 0))
    footer = dict(event='status', format_version=5, entered=4, returned=4, active=0, pending=0,
                  dropped=0, gaps=0, accepting=0, mode='record_only',
                  early_startup='main_iat_from_proxy_attach', phases=8)
    return [install] + initial + rows + [footer]


def consistent(r, before):
    """Rewrite a rand row so every per-row relation still holds for a new `rng_before`."""
    after = (before * 0x343fd + 0x269ec3) & 0xffffffff
    eax = (after >> 16) & 0x7fff
    r.update(rng_before=before, rng_after=after, result=eax)
    r['eflags'] = [r['eflags'][0], 0x202 | (int(bin(eax & 0xff).count('1') % 2 == 0) << 2) | ((eax == 0) << 6)]


class RngIntervalTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'capture.jsonl'

    def compile(self, rows):
        self.path.write_text(''.join(json.dumps(row) + '\n' for row in rows))
        return interval.compile_tape(self.path)

    def test_valid_format_five_keeps_raw_rng_and_denies_replay(self):
        result = self.compile(rng_fixture())
        self.assertEqual(result['format'], 'stock-api-interval-offline/3')
        self.assertEqual(result['rng_calls'], 4)
        self.assertFalse(result['process_replay_qualified'])
        self.assertEqual(result['calls'][0]['request']['argument'], {'seed': 1234})
        self.assertEqual(result['calls'][1]['reply']['state_after'], (1234 * 0x343fd + 0x269ec3) & 0xffffffff)

    def test_each_rng_corruption_is_rejected_for_its_own_reason(self):
        def row(rows, seq):
            return next(r for r in rows if r.get('seq') == seq and r['event'] == 'rng_return')

        cases = {
            'flipped-result-bit': (lambda r: row(r, 2).update(result=row(r, 2)['result'] ^ 1), 'contradicts'),
            'flipped-state-after': (lambda r: row(r, 2).update(rng_after=row(r, 2)['rng_after'] ^ 1), 'contradicts'),
            'broken-state-chain': (lambda r: consistent(row(r, 3), (row(r, 3)['rng_before'] ^ 1) & 0xffffffff), 'unobserved'),
            'lasterror-perturbed': (lambda r: row(r, 2).update(last_error_after=5), 'perturbed'),
            'x87-perturbed': (lambda r: row(r, 2).update(cw=[0x27f, 0x37f]), 'perturbed'),
            'mxcsr-perturbed': (lambda r: row(r, 2).update(mxcsr=[0x1f80, 0x1f81]), 'perturbed'),
            'nonarith-flag-changed': (lambda r: row(r, 2).update(eflags=[0x246, row(r, 2)['eflags'][1] ^ 0x400]), 'non-arithmetic'),
            'wrong-flags': (lambda r: row(r, 2).update(eflags=[0x246, row(r, 2)['eflags'][1] ^ 0x40]), 'EFLAGS'),
            'seed-flags-changed': (lambda r: row(r, 1).update(eflags=[0x246, 0x247]), 'contradicts'),
            'seed-eax-not-block': (lambda r: row(r, 1).update(result=7), 'contradicts'),
            'unknown-caller': (lambda r: row(r, 2).update(caller_rva=0x36149, caller=0x400000 + 0x36149), 'callsite'),
            'seed-caller-on-draw': (lambda r: row(r, 2).update(caller_rva=0x7c7e0, caller=0x400000 + 0x7c7e0), 'callsite'),
            'block-changed-in-lane': (lambda r: row(r, 3).update(rng_tls_raw=0x40000), 'block'),
            'block-shared-across-lanes': (lambda r: row(r, 4).update(rng_tls_raw=0x20000), 'block'),
            'misaligned-block': (lambda r: row(r, 4).update(rng_tls_raw=0x30001), 'block'),
            'gap-record': (lambda r: row(r, 2).update(gap_reason=12), 'gap'),
            'rng-output-claimed': (lambda r: row(r, 2).update(output_valid=1, output_u32=[1, 0]), 'output'),
            'missing-rng-field': (lambda r: row(r, 2).pop('rng_before'), 'RNG fields'),
            'extra-field': (lambda r: row(r, 2).update(extra=1), 'RNG fields'),
            'tampered-installation': (lambda r: r[0].update(code_mutations=[{'rva': 0x1d7400, 'bytes': 8}]), 'installation'),
            'no-installation-rng-boundary': (lambda r: r[0].update(scope='stock_main_image_api_interval'), 'installation'),
            'draw-with-argument': (lambda r: row(r, 2).update(arg=5), 'contradicts'),
        }
        for name, (mutate, reason) in cases.items():
            rows = rng_fixture()
            mutate(rows)
            with self.subTest(mutation=name), self.assertRaises(ValueError) as caught:
                self.compile(rows)
            self.assertIn(reason, str(caught.exception), name)

    def test_rows_in_ring_slot_order_are_accepted_and_chain_is_checked_in_seq_order(self):
        rows = rng_fixture()
        idx = [i for i, r in enumerate(rows) if r.get('event') == 'rng_return']
        shuffled = copy.deepcopy(rows)
        shuffled[idx[1]], shuffled[idx[2]] = shuffled[idx[2]], shuffled[idx[1]]
        self.assertEqual(self.compile(shuffled)['rng_calls'], 4)  # valid tape, different file order
        # Values untouched; swap only the call identifiers of seq2/seq3. The file chain
        # (1234 -> A -> B) still looks continuous, but in true seq order it is broken.
        broken = copy.deepcopy(rows)
        a, b = broken[idx[1]], broken[idx[2]]
        for key in ('seq', 'return_seq', 'entry_phase', 'return_phase', 'lane_read'):
            a[key], b[key] = b[key], a[key]
        with self.assertRaises(ValueError) as caught:
            self.compile(broken)
        self.assertIn('unobserved', str(caught.exception))

    def test_lane_seq_order_must_agree_with_phase_order(self):
        rows = rng_fixture()
        two = next(r for r in rows if r.get('seq') == 2 and r['event'] == 'rng_return')
        three = next(r for r in rows if r.get('seq') == 3 and r['event'] == 'rng_return')
        two['entry_phase'], two['return_phase'], three['entry_phase'], three['return_phase'] = (
            three['entry_phase'], three['return_phase'], two['entry_phase'], two['return_phase'])
        with self.assertRaises(ValueError) as caught:
            self.compile(rows)
        self.assertIn('phase order', str(caught.exception))

    def test_format_five_with_no_rng_observation_is_refused(self):
        rows = [r for r in rng_fixture() if r.get('event') != 'rng_return']
        # Replace the removed rows with one clock call so the tape is otherwise complete.
        base = 0x400000
        rows.insert(-1, dict(event='api_return', gap_reason=0, seq=1, return_seq=1, entry_phase=1, return_phase=2,
                             thread=9, lane=0, lane_read=1, role='unresolved', api='GetTickCount',
                             caller=base + 0x155278, caller_rva=0x155278, arg=0, result=7, last_error_before=0,
                             last_error_after=0, output_valid=0, output_u32=[0, 0], cw=[0x27f, 0x27f],
                             mxcsr=[0x1f80, 0x1f80]))
        rows[-1].update(entered=1, returned=1, phases=2)
        with self.assertRaises(ValueError) as caught:
            self.compile(rows)
        self.assertIn('no RNG call', str(caught.exception))

    def test_rng_rows_are_refused_outside_format_five(self):
        rows = rng_fixture()
        for r in rows:
            if r['event'] == 'status':
                r['format_version'] = 4
        rows[0] = dict(interval.RNG_INSTALLATION, scope='stock_main_image_api_interval')
        del rows[0]['rng_boundary'], rows[0]['code_mutations']
        with self.assertRaises(ValueError):
            self.compile(rows)

    def test_format_five_without_installation_provenance_is_refused(self):
        rows = rng_fixture()[1:]
        with self.assertRaises(ValueError):
            self.compile(rows)

    def test_changed_rng_draw_reports_exact_first_divergence(self):
        a = self.compile(rng_fixture())
        rows = rng_fixture()
        for r in rows:
            if r.get('seq') == 4 and r['event'] == 'rng_return':
                r.update(rng_before=2, rng_after=(2 * 0x343fd + 0x269ec3) & 0xffffffff)
                r.update(result=(r['rng_after'] >> 16) & 0x7fff)
                r['eflags'][1] = 0x202 | (int(bin(r['result'] & 0xff).count('1') % 2 == 0) << 2) | ((r['result'] == 0) << 6)
        b = self.compile(rows)
        verdict = interval.compare(a, b)
        self.assertFalse(verdict['offline_match'])
        self.assertEqual(verdict['first_divergence']['path'], '$.calls[3].request.state_before')


if __name__ == '__main__':
    unittest.main()
