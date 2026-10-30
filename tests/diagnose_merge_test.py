#!/usr/bin/env python3
import importlib.util
import hashlib
import hashlib
import tempfile
import subprocess
import sys
import unittest
from unittest.mock import Mock, patch
from pathlib import Path

MODULE = Path(__file__).resolve().parents[1] / 'tools' / 'diagnose.py'
spec = importlib.util.spec_from_file_location('diagnose', MODULE)
diagnose = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnose)


class DiagnoseTest(unittest.TestCase):
    def test_camera_follow_reports_latest_valid_record(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'run.log').write_text(
                '[+01.000 f1] note camfollow1 {"v":1,"p":1,"c":2,"car":3,"body":4,"look":5,"move":6,"r":0,"reason":"sim"}\n'
                '[+02.000 f2] note camfollow1 {"v":1,"p":4294901761,"c":2,"car":3,"body":4,"look":5,"move":6,"r":1,"reason":"sim"}\n')
            output = diagnose.diagnose(run)
            self.assertIn('CAMERA FOLLOW', output)
            self.assertIn('p=0xffff0001 c=0x00000002 car=0x00000003 body=0x00000004 look=0x00000005 move=0x00000006 registered=yes reason=simulation unsupported', output)
            self.assertIn('1 earlier camera follow records omitted', output)

    def test_camera_follow_rejects_malformed_and_unknown(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            records = [
                '{"v":1,"p":1',
                '{"v":2,"p":1,"c":2,"car":3,"body":4,"look":5,"move":6,"r":1,"reason":"sim"}',
                '{"v":1,"p":1,"c":2,"car":3,"body":4,"look":5,"r":1,"reason":"sim"}',
                '{"v":1,"p":true,"c":2,"car":3,"body":4,"look":5,"move":6,"r":1,"reason":"sim"}',
                '{"v":1,"p":1,"c":2,"car":3,"body":4,"look":5,"move":4294967296,"r":1,"reason":"sim"}',
                '{"v":1,"p":1,"c":2,"car":3,"body":4,"look":5,"move":6,"r":true,"reason":"sim"}',
                '{"v":1,"p":1,"c":2,"car":3,"body":4,"look":5,"move":6,"r":0,"reason":"unknown"}',
            ]
            (run / 'run.log').write_text(''.join(
                f'[+01.{i:03d} f1] note camfollow1 {record}\n' for i, record in enumerate(records)))
            output = diagnose.diagnose(run)
            self.assertIn('7 malformed or unsupported camera follow records; state may be incomplete', output)
            self.assertNotIn('registered=yes reason=', output)
            self.assertNotIn('registered=no reason=', output)

    def test_old_run_has_no_camera_follow_section(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'run.log').write_text('[+01.000 f1] note ordinary event\n')
            self.assertNotIn('\nCAMERA FOLLOW\n', diagnose.diagnose(run))

    def test_camera_follow_maximum_record_fits_recorder(self):
        record = ('camfollow1 {"v":1,"p":4294967295,"c":4294967295,"car":4294967295,'
                  '"body":4294967295,"look":4294967295,"move":4294967295,"r":1,"reason":"sim"}')
        self.assertLessEqual(len(record.encode('utf-8')), 151)
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'run.log').write_text('[+01.000 f1] note ' + record + '\n')
            output = diagnose.diagnose(run)
            self.assertIn('p=0xffffffff c=0xffffffff car=0xffffffff body=0xffffffff '
                          'look=0xffffffff move=0xffffffff registered=yes reason=simulation unsupported', output)

    def test_type1_stages_report_return_and_interruption(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'run.log').write_text(
                '[+01.000 f1] note load1 {"v":1,"id":1,"resource":2359297,"event":"enter","stage":"payload"}\n'
                '[+01.010 f1] note load1 {"v":1,"id":1,"resource":2359297,"event":"enter","stage":"road_initialization"}\n'
                '[+01.020 f1] note load1 {"v":1,"id":1,"resource":2359297,"event":"end","stage":"road_initialization"}\n'
                '[+02.000 f2] note load1 {"v":1,"id":2,"resource":2359297,"event":"enter","stage":"payload"}\n'
                '[+02.010 f2] note load1 {"v":1,"id":2,"resource":2359297,"event":"enter","stage":"mesh"}\n'
                '[+02.020 f2] note load1 {"v":1\n')
            output = diagnose.diagnose(run)
            self.assertIn('resource=0x00240001 stages=payload -> road_initialization; returned or unwound; see cause', output)
            self.assertIn('stages=payload -> mesh; no end record; outcome unknown', output)
            self.assertIn('1 malformed or unsupported load records', output)

    def test_ground_snapshot_reports_last_valid_record(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'run.log').write_text(
                '[+01.000 f1] note ground1 {"v":1,"c":1,"r":1,"t":12,"ds":0,"y":0,"z":0}\n'
                '[+14.000 f2] note ground1 {"v":2,"c":1,"r":1,"t":717,"p":6,"wa":0,"ds":1,"y":0.0005,"z":0.00055}\n'
                '[+14.050 f2] note ground2 {"v":1,"px":1,"py":2,"pz":3,"ea":40,"eb":50}\n'
                '[+14.100 f2] note ground1 {"v":1,"c":\n')
            output = diagnose.diagnose(run)
            self.assertIn(
        'cities=1 roads=1 traffic=717 pedestrian_types=6 water_active=0 '
        'water_limits=0 fog_settings=0 '
        'density_set=1 density=0.0005 density_hi=0.00055', output)
            self.assertIn('water pos=(1,2,3) extents=(40,50)', output)
            self.assertIn('1 earlier ground snapshots omitted', output)
            self.assertIn('1 malformed ground records', output)

    def test_ground_last_native_failure_and_car_delta(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'run.log').write_text(
                '[+10.000 f100] note grlast {"v":1,"m":"addPedestrianType","ok":1}\n'
                '[+14.000 f200] note grlast {"v":1,"m":"setWater","ok":0}\n'
                '[+14.000 f200] note fail1 {"v":1,"cls":"java.game.Valocity"}\n'
                '[+14.000 f200] note card1 {"v":1,"dx":0,"dy":1.5,"dz":0}\n')
            output = diagnose.diagnose(run)
            self.assertIn('method=setWater ok=0', output)
            self.assertIn('innermost=java.game.Valocity', output)
            self.assertIn('dx=0 dy=1.5 dz=0', output)

    def test_world_and_assembly_events_are_visible(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'run.log').write_text(
                '[+01.000 f1] note world state: nodes=7 active_threads=2 car=unavailable\n'
                '[+02.000 f2] note part creation requested: resource=0x123 class=part config=x.cfg\n'
                '[+03.000 f3] note part install: part=0xffff0001 target=0x0 slot=1 outcome=rejected\n')
            output = diagnose.diagnose(run)
            self.assertIn('WORLD STATE', output)
            self.assertIn('nodes=7 active_threads=2', output)
            self.assertIn('ASSEMBLY EVENTS', output)
            self.assertIn('outcome=rejected', output)

    def test_structured_assembly_and_incomplete_records(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'run.log').write_text(
                '[+01.000 f1] note asm1 {"v":1,"k":"req","id":1,"part":4,"target":5,"slot":0}\n'
                '[+01.000 f1] note asm1 {"v":1,"k":"cand","id":1,"n":0,"candidate":5,"reasons":32}\n'
                '[+01.000 f1] note asm1 {"v":1,"k":"state","id":1,"parent0":0,"parent1":0,"pp0":1,"pp1":1,"tp0":1,"tp1":1,"omitted":0}\n'
                '[+01.000 f1] note asm1 {"v":1,"k":"result","id":1,"why":"no_slot","resolved":5,"ps":0,"ts":0}\n'
                '[+01.000 f1] note asm1 {"v":1,"k":"req","id":3,"part":6,"target":5,"slot":0}\n'
                '[+01.000 f1] note asm1 {"v":1,"k":"summary","id":3,"examined":1,"reasons":2}\n'
                '[+01.000 f1] note asm1 {"v":1,"k":"state","id":3,"parent0":0,"parent1":0,"pp0":1,"pp1":1,"tp0":1,"tp1":1,"omitted":0}\n'
                '[+01.000 f1] note asm1 {"v":1,"k":"result","id":3,"why":"no_slot","resolved":5,"ps":0,"ts":0}\n'
                '[+01.000 f1] note asm1 {"v":1,"k":"req","id":2,"part":4,"target":5,"slot":0}\n'
                '[+01.000 f1] note asm1 {"v":1\n')
            output = diagnose.diagnose(run)
            self.assertIn('no matching pair found', output)
            self.assertIn('examined=1 aggregate reasons=prototype unavailable', output)
            self.assertIn('attempt 2: incomplete records; cause unknown', output)
            self.assertIn('malformed, truncated, or unsupported', output)
            self.assertIn('Run did not record a final result', output)

    def test_failed_install_inventory_is_readable(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'run.log').write_text(
                '[+01.000 f1] note asm1 {"v":1,"k":"req","id":1,"part":4,"target":5,"slot":0}\n'
                '[+01.000 f1] note asm1 {"v":1,"k":"slot","id":1,"c":0,"node":5,"slot":12,"flags":16,"attached":7}\n'
                '[+01.000 f1] note asm1 {"v":1,"k":"inventory","id":1,"c":0,"node":5,"type":123,"slots":10,"shown":8,"links":2}\n'
                '[+01.000 f1] note asm1 {"v":1,"k":"state","id":1,"parent0":0,"parent1":0,"pp0":1,"pp1":1,"tp0":1,"tp1":1,"omitted":0}\n'
                '[+01.000 f1] note asm1 {"v":1,"k":"result","id":1,"why":"no_slot","resolved":5,"ps":0,"ts":0}\n')
            output = diagnose.diagnose(run)
            self.assertIn('type=123 slots=10 shown=8 attached_links=2', output)
            self.assertIn('id=12 flags=16 attached=7', output)

    def test_removal_records_are_readable_and_incomplete_flagged(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'run.log').write_text(
                '[+01.000 f1] note asmr {"v":1,"k":"req","id":1,"part":7,"slot":0,"found":0,"tgt":5,"tsl":24}\n'
                '[+01.000 f1] note asmr {"v":1,"k":"done","id":1,"det":1,"rb":2,"ra":1,"ub":3,"ua":2}\n'
                '[+01.000 f1] note asmr {"v":1,"k":"req","id":3,"part":11,"slot":0,"found":2,"tgt":5,"tsl":26}\n')
            output = diagnose.diagnose(run)
            self.assertIn('removal 1: part=7 requested_slot=0 found_slot=0 host_target=5 host_slot=24'
                          ' links_detached=1 registry 2->1 used_slots 3->2', output)
            self.assertIn('removal 3: incomplete records; registry state after the removal is unknown', output)

    def test_wheel_pose_declines_are_aggregated_per_reason(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'run.log').write_text(
                '[+01.000 f1] note wheelref {"v":1,"slotType":400,"index":99,"wheels":1,"reason":"no_wheel_record"}\n'
                '[+01.000 f1] note wheelref {"v":1,"slotType":400,"index":99,"wheels":1,"reason":"no_wheel_record"}\n'
                '[+01.000 f1] note wheelref {"v":1,"slotType":103,"index":2,"wheels":1,"reason":"no_wheel_record"}\n'
                '[+01.000 f1] note wheelref {"v":1,"slotType":101,"index":0,"wheels":0,"reason":"bad_reason"}\n')
            output = diagnose.diagnose(run)
            self.assertIn('wheel pose override declined 2x: slot_type=400 reason=no_wheel_record', output)
            self.assertIn('wheel pose override declined 1x: slot_type=103 reason=no_wheel_record', output)
            self.assertIn('malformed, truncated, or unsupported', output)

    def test_game_data_manifest_integrity_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            data = b'{"complete":true}\n'
            (run / 'game-data-manifest.json').write_bytes(data)
            digest = hashlib.sha256(data).hexdigest()
            (run / 'meta.txt').write_text('game_data_status: launcher-reported complete\n'
                                          f'game_data_manifest_sha256: {digest}\n')
            self.assertIn('game data manifest integrity: matches recorded hash', diagnose.diagnose(run))
            (run / 'game-data-manifest.json').write_bytes(b'changed')
            self.assertIn('game data manifest integrity: MISMATCH', diagnose.diagnose(run))

    def test_completed_checkpoints_are_merged_and_truncated_files_ignored(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'run.log').write_text('[+01.000 f1] input click\n')
            (run / 'flight-0.log').write_text(
                '# checkpoint=2 ring_written=500 retained=2 slots=2 overwrites_older_checkpoints=1\n'
                '[+01.000 f1] input click\n[+02.000 f2] nativecall failing-operation\n')
            (run / 'flight-1.log').write_text(
                '# checkpoint=3 ring_written=600 retained=2 slots=2 overwrites_older_checkpoints=1\n'
                '[+03.000 f3] nativecall partial\n')
            output = diagnose.diagnose(run, raw_mode=True)
            self.assertIn('failing-operation', output)
            self.assertNotIn('nativecall partial', output)
            self.assertIn('older checkpoints are overwritten', output)
            self.assertEqual(output.split('LAST EVENTS', 1)[1].split('COUNTERS', 1)[0].count('input click'), 1)

    def test_merge_preserves_occurrences_and_orders_events(self):
        move = (1., 1, 'input', 'move')
        click = (2., 2, 'input', 'down')
        routine = (2.5, 2, 'nativecall', 'Math.random')
        self.assertEqual(diagnose.merge_events([move, click, click], [click, routine]),
                         [move, click, click, routine])

    def test_diagnosis_recovers_persisted_events_and_traces(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'result.txt').write_text('status=failed\nexit_code=1\nseconds=4\nframes=3\nreason=failed\n')
            (run / 'run.log').write_text(
                '[+00.100 f0] err random seed: elapsed_ms=7.5 seed=7\n'
                '[+01.000 f1] input click\n'
                '[+02.000 f2] err car creation requested: resource=0x01 class=car config=car.cfg\n'
                '[+03.000 f3] frame final\n'
                '--- last events ---\n[+03.000 f3] frame final\n')
            output = diagnose.diagnose(run)
            key = output.split('KEY EVENTS', 1)[1].split('TIMELINE', 1)[0]
            self.assertIn('input click', key)
            self.assertEqual(key.count('frame final'), 1)
            self.assertIn('random seed: elapsed_ms=7.5 seed=7', output)
            self.assertIn('car creation requested: resource=0x01 class=car config=car.cfg', output)
            self.assertIn('equal timestamps and thread order are approximate', output)

    def test_note_and_legacy_trace_are_paired_without_dropping_repeated_calls(self):
        events = [(1.0, 4, 'note', 'random seed: elapsed_ms=7 seed=7'),
                  (1.001, 4, 'err', 'random seed: elapsed_ms=7 seed=7'),
                  (1.002, 4, 'note', 'random seed: elapsed_ms=7 seed=7'),
                  (1.003, 4, 'err', 'random seed: elapsed_ms=7 seed=7'),
                  (1.1, 5, 'note', 'car creation requested: resource=1 class=car config=x'),
                  (1.101, 5, 'err', 'car creation requested: resource=1 class=car config=x')]
        text = '\n'.join(diagnose.trace_lines(events))
        self.assertEqual(text.count('random seed: elapsed_ms=7 seed=7'), 2)
        self.assertEqual(text.count('car creation requested:'), 1)

    def test_full_car_trace_replaces_truncated_note(self):
        full = 'car creation requested: ' + 'x' * 160 + '.cfg'
        events = [(1.0, 4, 'note', full[:151]), (1.001, 4, 'err', full)]
        text = '\n'.join(diagnose.trace_lines(events))
        self.assertEqual(text.count('car creation requested:'), 1)
        self.assertIn(full, text)

    def test_process_identity_states_and_missing_metadata(self):
        meta = {'pid': '321', 'process_start_unix': '123.000456'}
        self.assertEqual(diagnose.missing_process_status(meta, lambda _: (123, 456, 2)), 'running')
        self.assertEqual(diagnose.missing_process_status(meta, lambda _: (124, 456, 2)), 'not running (PID reused)')
        self.assertEqual(diagnose.missing_process_status(meta, lambda _: (123, 456, 5)), 'not running (exited, awaiting collection)')
        self.assertEqual(diagnose.missing_process_status(meta, lambda _: 'absent'), 'not running')
        self.assertEqual(diagnose.missing_process_status(meta, lambda _: None), 'unknown')
        self.assertEqual(diagnose.missing_process_status({'pid': '321'}, lambda _: 'absent'), 'unknown')
        self.assertEqual(diagnose.missing_process_status({'pid': '-1', 'process_start_unix': '123.000456'}, lambda _: 'absent'), 'unknown')
        self.assertEqual(diagnose.missing_process_status({'pid': '321', 'process_start_unix': '123.12'}, lambda _: 'absent'), 'unknown')

    def test_process_inspection_denial_is_unknown(self):
        lib = Mock()
        lib.proc_pidinfo.return_value = 0
        with patch.object(diagnose.sys, 'platform', 'darwin'), \
             patch.object(diagnose.ctypes, 'CDLL', return_value=lib), \
             patch.object(diagnose.os, 'kill', side_effect=PermissionError()):
            self.assertIsNone(diagnose.read_process_identity(321))
            self.assertEqual(diagnose.missing_process_status(
                {'pid': '321', 'process_start_unix': '123.000456'}), 'unknown')

    @unittest.skipUnless(sys.platform == 'darwin', 'macOS process identity')
    def test_real_live_and_exited_process(self):
        child = subprocess.Popen(['sleep', '30'])
        try:
            identity = diagnose.read_process_identity(child.pid)
            self.assertIsInstance(identity, tuple)
            meta = {'pid': str(child.pid), 'process_start_unix': f'{identity[0]}.{identity[1]:06d}'}
            self.assertEqual(diagnose.missing_process_status(meta), 'running')
            with tempfile.TemporaryDirectory() as temp:
                run = Path(temp)
                (run / 'meta.txt').write_text(f"pid: {child.pid}\nprocess_start_unix: {meta['process_start_unix']}\n")
                self.assertIn('recorded process is currently running', diagnose.diagnose(run))
        finally:
            child.terminate()
            child.wait()
        self.assertEqual(diagnose.missing_process_status(meta), 'not running')

    def test_snapshot_and_source_metadata_are_visible(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'meta.txt').write_text('process_start_unix: 123.000456\nsource_identity: unavailable\ninitial_save_snapshot: captured\ninitial_save_limits: 10 bytes\n')
            (run / 'initial-save-manifest.txt').write_text('manifest\n')
            report = diagnose.diagnose(run)
            self.assertIn('process_start_unix: 123.000456', report)
            self.assertIn('source_identity: unavailable', report)
            self.assertIn('initial_save_snapshot: captured', report)
            self.assertIn('initial_save_limits: 10 bytes', report)
            self.assertIn(str(run / 'initial-save-manifest.txt'), report)

    def test_verified_run_manifest_and_corrupt_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            a, b = root / 'a', root / 'b'
            a.mkdir(); b.mkdir()
            first = ('src/a.cpp\t' + 'a' * 64 + '\n').encode()
            second = ('src/a.cpp\t' + 'b' * 64 + '\n').encode()
            first_hash, second_hash = hashlib.sha256(first).hexdigest(), hashlib.sha256(second).hexdigest()
            (a / 'meta.txt').write_text(f'src_tree_sha256: {first_hash}\n')
            (b / 'meta.txt').write_text(f'src_tree_sha256: {second_hash}\n')
            (a / 'source-manifest.txt').write_bytes(first)
            (b / 'source-manifest.txt').write_bytes(b'corrupt')
            store = root / 'local/src-manifests'
            store.mkdir(parents=True)
            (store / f'{second_hash}.txt').write_bytes(second)
            with patch.object(diagnose, 'REPO', root):
                report = diagnose.diff_code(a, b)
                self.assertIn('source manifest hash mismatch', report)
                self.assertIn('modified  src/a.cpp', report)
                self.assertIn('not necessarily the executable', report)
                (store / f'{second_hash}.txt').unlink()
                report = diagnose.diff_code(a, b)
                self.assertIn('no verified manifest', report)
                self.assertNotIn('modified  src/a.cpp', report)

    def test_crash_tail_and_absent_traces(self):
        with tempfile.TemporaryDirectory() as tmp:
            run = Path(tmp)
            (run / 'result.txt').write_text('status=terminated\nsignal=SIGTERM\n')
            (run / 'run.log').write_text('[+01.000 f1] input click\n--- last events ---\n[+01.500 f1] resource cached\n')
            (run / 'crash.txt').write_text('signal=SIGTERM\n--- last events ---\n[+02.000 f2] frame final\n')
            output = diagnose.diagnose(run)
            self.assertIn('input click', output)
            self.assertIn('frame final', output)
            self.assertIn('resource cached', output)
            self.assertIn('random seed: not recorded (tracing may have been disabled)', output)
            self.assertIn('car request: not recorded (tracing may have been disabled)', output)


if __name__ == '__main__':
    unittest.main()
