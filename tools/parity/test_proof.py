#!/usr/bin/env python3
"""Mutation tests of proof evaluation, not fabricated Stock/Port game evidence."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import uuid
import struct

spec = importlib.util.spec_from_file_location('proof', Path(__file__).with_name('proof.py'))
proof = importlib.util.module_from_spec(spec)
spec.loader.exec_module(proof)


class ProofTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sl-proof-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.frames = [{'sequence': n, 'boundary': name,
                        'stimulus': {'dt_bits': '3ca3d70a', 'keys': [], 'rng_seed': 235},
                        'observations': {'state': state, 'speed': 0.0}}
                       for n, (name, state) in enumerate([
                           ('boot:0', 'boot'), ('menu:0', 'menu'),
                           ('dialog:0', 'dialog'), ('menu:1', 'menu')])]
        self.contract = {'schema': 1, 'id': 'unit-fixture-only',
                         'authority': 'synthetic comparator fixture; no gameplay claim',
                         'inputs': {key: proof.sha(key.encode()) for key in ('game', 'fixture', 'scenario')},
                         'stock_executable': proof.sha(b'stock'),
                         'terminal_boundary': 'menu:1',
                         'boundaries': [f['boundary'] for f in self.frames],
                         'observations': ['state', 'speed'],
                         'capabilities': ['focus_preserved', 'private_audio'],
                         'stimulus_sha256': proof.sha(proof.encoded([
                             {'boundary': f['boundary'], 'stimulus': f['stimulus']}
                             for f in self.frames]).encode())}
        self.paths = [self.make_run(side, n) for side in ('stock', 'port') for n in range(2)]

    def make_run(self, side, n):
        root = self.root / f'{side}-{n}'
        root.mkdir()
        tape = root / 'transitions.jsonl'
        tape.write_text(''.join(json.dumps(frame) + '\n' for frame in self.frames))
        descriptor = lambda p: {'path': p.name, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
        pid = 100 + n + (10 if side == 'port' else 0)
        owner = {'pid': pid, 'pgid': pid, 'ppid': 90, 'start_sec': 20, 'start_usec': 0,
                 'read_ok': True, 'exe': '/unit-fixture'}
        observer = dict(owner, pid=90, pgid=90, ppid=1, start_sec=10, exe='/observer')
        external = dict(owner, pid=200, pgid=200, ppid=1, start_sec=5, exe='/user-app')
        events = [{'kind': 'observer_ready', 'observer': observer, 'requested_executable': '/unit-fixture'},
                  {'kind': 'child_started', 'spawned_pid': pid, 'root': owner},
                  {'kind': 'foreground', 'chain': [external], 'end_reason': 'terminal'},
                  {'kind': 'observer_finished', 'child_exit': 0, 'timed_out': False,
                   'polls': 4, 'max_poll_gap_seconds': .02}]
        focus = root / 'focus.jsonl'
        focus.write_text(''.join(json.dumps(dict(e, schema=1, sequence=i, monotonic=i*.02)) + '\n'
                                 for i, e in enumerate(events)))
        pcm = root / 'mix.raw'
        pcm.write_bytes(struct.pack('<4f', .125, .125, .25, .25))
        audio = proof.audio_module.summarize(pcm, 'f32le', 2, 48000)
        audio.update(path='mix.raw', route='sdl-disk', stage='physical_final_mix',
                     complete=True, physical_output=False)
        audio_path = root / 'audio.json'
        audio_path.write_text(json.dumps(audio))
        manifest = {'schema': 1, 'run_id': str(uuid.uuid4()), 'contract': self.contract['id'],
                    'implementation': side, 'complete': True, 'dropped_records': 0,
                    'exit_code': 0, 'timed_out': False, 'source_preserved': True,
                    'execution_owner': owner,
                    'identity': {key: proof.sha((side if key == 'executable' else key).encode())
                                 for key in ('executable', 'game', 'fixture', 'scenario')},
                    'capabilities': {'focus_preserved': {'verified': True, 'artifact': descriptor(focus)},
                                     'private_audio': {'verified': True, 'artifact': descriptor(audio_path)}},
                    'coverage': self.contract['observations'], 'tape': descriptor(tape)}
        manifest['identity']['environment'] = {'platform': side, 'backend': 'unit-fixture'}
        path = root / 'run.json'
        path.write_text(json.dumps(manifest))
        return path

    def mutate_manifest(self, index, fn):
        path = self.paths[index]
        manifest = json.loads(path.read_text())
        fn(manifest)
        path.write_text(json.dumps(manifest))

    def mutate_tape(self, index, fn, rehash=True):
        path = self.paths[index].parent / 'transitions.jsonl'
        frames = [json.loads(line) for line in path.read_text().splitlines()]
        fn(frames)
        path.write_text(''.join(json.dumps(frame) + '\n' for frame in frames))
        if rehash:
            self.mutate_manifest(index, lambda m: m['tape'].update(sha256=proof.sha(path.read_bytes())))

    def result(self, mode='parity', paths=None):
        return proof.evaluate(self.contract, self.paths if paths is None else paths, mode)

    def test_pair_and_repeatability_are_distinct(self):
        self.assertEqual(self.result()['status'], 'matched')
        self.assertEqual(self.result('repeatability', self.paths[:2])['status'], 'repeatable')
        self.assertEqual(self.result(paths=self.paths[:2])['status'], 'inconclusive')

    def test_missing_and_corrupt_records_do_not_pass(self):
        self.mutate_tape(0, lambda f: f[1]['observations'].update(state='wrong'), rehash=False)
        self.assertEqual(self.result()['status'], 'inconclusive')

    def test_forged_reindexed_truncation_does_not_pass(self):
        def truncate(frames):
            del frames[1]
            for n, f in enumerate(frames):
                f['sequence'] = n
        for i in range(4):
            self.mutate_tape(i, truncate)
        self.assertEqual(self.result()['status'], 'inconclusive')

    def test_changed_input_on_both_sides_does_not_pass(self):
        for i in range(4):
            self.mutate_tape(i, lambda f: f[1]['stimulus'].update(keys=['Up']))
        self.assertEqual(self.result()['status'], 'inconclusive')

    def test_wrong_fixture_on_every_run_still_fails_contract(self):
        for i in range(4):
            self.mutate_manifest(i, lambda m: m['identity'].update(fixture='f' * 64))
        self.assertEqual(self.result()['status'], 'inconclusive')

    def test_labeling_another_executable_as_stock_does_not_pass(self):
        for i in (0, 1):
            self.mutate_manifest(i, lambda m: m['identity'].update(executable='f' * 64))
        self.assertEqual(self.result()['status'], 'inconclusive')

    def test_repeatability_before_cross_side_comparison(self):
        self.mutate_tape(3, lambda f: f[1]['observations'].update(state='wrong'))
        result = self.result()
        self.assertEqual(result['status'], 'inconclusive')
        self.assertEqual(result['reason'], 'not_repeatable')

    def test_reports_first_matching_boundary_divergence(self):
        for i in (2, 3):
            self.mutate_tape(i, lambda f: f[1]['observations'].update(state='wrong'))
        result = self.result()
        self.assertEqual(result['status'], 'diverged')
        self.assertEqual(result['difference']['boundary'], 'menu:0')
        self.assertEqual(result['difference']['field'], 'state')

    def test_missing_capabilities_fixture_and_reused_runs(self):
        original = self.paths[0].read_text()
        mutations = [lambda m: m['capabilities'].pop('private_audio'),
                     lambda m: m['identity'].update(fixture='f' * 64),
                     lambda m: m.update(dropped_records=1),
                     lambda m: m.update(source_preserved=False),
                     lambda m: m.update(timed_out=True)]
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                self.paths[0].write_text(original)
                self.mutate_manifest(0, mutation)
                self.assertEqual(self.result()['status'], 'inconclusive')
        self.paths[0].write_text(original)
        self.assertEqual(self.result(paths=[self.paths[0]] * 4)['status'], 'inconclusive')
        run_id = json.loads(original)['run_id']
        self.mutate_manifest(1, lambda m: m.update(run_id=run_id))
        self.assertEqual(self.result()['status'], 'inconclusive')

    def test_tolerance_requires_evidence_and_preserves_discrete_types(self):
        for i in (2, 3):
            self.mutate_tape(i, lambda f: f[2]['observations'].update(speed=0.001))
        self.assertEqual(self.result()['status'], 'diverged')
        self.contract['tolerances'] = {'speed': {'kind': 'numeric', 'absolute': 0.01, 'relative': 0}}
        self.assertEqual(self.result()['status'], 'inconclusive')
        self.contract['tolerances']['speed']['evidence'] = 'unit-test-only tolerance'
        self.assertEqual(self.result()['status'], 'matched')
        for i in (2, 3):
            self.mutate_tape(i, lambda f: f[2]['observations'].update(speed=False))
        self.assertEqual(self.result()['status'], 'diverged')

    def test_symlink_and_nonfinite_data_do_not_pass(self):
        target = self.paths[0].parent / 'transitions.jsonl'
        real = target.with_name('real.jsonl')
        target.rename(real)
        target.symlink_to(real)
        self.assertEqual(self.result()['status'], 'inconclusive')
        target.unlink()
        real.rename(target)
        self.mutate_tape(0, lambda f: f[0]['observations'].update(speed=float('nan')))
        self.assertEqual(self.result()['status'], 'inconclusive')

    def test_exact_values_preserve_signed_zero_and_json_types(self):
        self.assertNotEqual(proof.encoded(0.0), proof.encoded(-0.0))
        self.assertNotEqual(proof.encoded(0), proof.encoded(False))
        with self.assertRaises(proof.InvalidEvidence):
            proof.read_json('{"a": 1, "a": 2}')

    def test_claimed_capability_without_actual_observer_data_does_not_pass(self):
        path = self.paths[0].parent / 'focus.jsonl'
        path.write_text('{"verified":true}\n')
        self.mutate_manifest(0, lambda m: m['capabilities']['focus_preserved']['artifact'].update(sha256=proof.sha(path.read_bytes())))
        self.assertEqual(self.result()['status'], 'inconclusive')

    def test_silence_cannot_substitute_for_required_sound(self):
        self.contract['minimum_audio_nonzero_samples'] = 1
        self.assertEqual(self.result()['status'], 'matched')
        for index in range(4):
            root = self.paths[index].parent
            pcm = root / 'mix.raw'
            pcm.write_bytes(bytes(16))
            report = proof.audio_module.summarize(pcm, 'f32le', 2, 48000)
            report.update(path='mix.raw', route='sdl-disk', stage='physical_final_mix', complete=True, physical_output=False)
            data = root / 'audio.json'
            data.write_text(json.dumps(report))
            self.mutate_manifest(index, lambda m: m['capabilities']['private_audio']['artifact'].update(sha256=proof.sha(data.read_bytes())))
        self.assertEqual(self.result()['status'], 'inconclusive')

    def test_audio_corruption_and_wrong_focus_owner_cannot_pass(self):
        original = self.paths[0].read_text()
        self.mutate_manifest(0, lambda m: m['execution_owner'].update(pid=999))
        self.assertEqual(self.result()['status'], 'inconclusive')
        self.paths[0].write_text(original)
        (self.paths[0].parent / 'mix.raw').write_bytes(b'broken capture')
        self.assertEqual(self.result()['status'], 'inconclusive')


if __name__ == '__main__':
    unittest.main()
