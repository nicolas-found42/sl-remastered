#!/usr/bin/env python3
import hashlib
import importlib.util
import os
from pathlib import Path
import tempfile
import contextlib
import io
from unittest import mock
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('replay', ROOT / 'tools/replay.py')
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


def make_snapshot(run, contents=b'original'):
    save = run / 'initial-save/game'
    save.mkdir(parents=True)
    (save / 'options').write_bytes(contents)
    digest = hashlib.sha256(contents).hexdigest()
    (run / 'initial-save-manifest.txt').write_text(
        f'format=1\nfiles=1\nbytes={len(contents)}\n{digest}  game/options\n')


class ReplayTest(unittest.TestCase):
    def test_selects_script_when_recorded_input_is_empty(self):
        with tempfile.TemporaryDirectory() as temp:
            run = Path(temp)
            (run / 'input.txt').write_text('  # unsupported input\n\n')
            (run / 'script.txt').write_text('# recorded script\n10 move 420 127\n13 down\n13.5 up\n')
            path, count, kind, warning = replay.select_input(run)
            self.assertEqual(path, (run / 'script.txt').resolve())
            self.assertEqual(count, 3)
            self.assertEqual(kind, 'recorded script')
            self.assertIsNone(warning)

    def test_mixed_inputs_warn_and_empty_actions_fail(self):
        with tempfile.TemporaryDirectory() as temp:
            run = Path(temp)
            (run / 'input.txt').write_text('1 keydown A\n')
            (run / 'script.txt').write_text('2 down\n')
            path, count, kind, warning = replay.select_input(run)
            self.assertEqual(path, (run / 'input.txt').resolve())
            self.assertIn('omits the script', warning)
            (run / 'input.txt').write_text('# comment\nnot a command\n')
            (run / 'script.txt').write_text('# comment\nnan down\n-1 up\n1 nonsense\n2 move\n3 move bad 1\n4 keydown\n5 fail unknown\n')
            with self.assertRaisesRegex(ValueError, 'no replayable'):
                replay.select_input(run)

    def test_recorded_save_is_verified_and_copied_to_new_root(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run = root / 'run'
            make_snapshot(run)
            original = root / 'current/save/game'
            original.mkdir(parents=True)
            (original / 'options').write_text('current')
            data, matched = replay.prepare_replay_save(run, {'initial_save_snapshot': 'captured', 'data_root': str(root / 'current')}, root, root / 'replays')
            self.assertTrue(matched)
            self.assertEqual((data / 'save/game/options').read_bytes(), b'original')
            (data / 'save/game/options').write_text('modified by game')
            self.assertEqual((original / 'options').read_text(), 'current')
            self.assertEqual((run / 'initial-save/game/options').read_bytes(), b'original')
            self.assertEqual(data.stat().st_mode & 0o077, 0)

    def test_tampering_and_symlinks_fail_without_fallback(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run = root / 'run'
            make_snapshot(run)
            (run / 'initial-save/game/options').write_text('tampered')
            with self.assertRaisesRegex(ValueError, 'does not match'):
                replay.prepare_replay_save(run, {'initial_save_snapshot': 'captured'}, root, root / 'replays')
            (run / 'initial-save/game/options').unlink()
            (run / 'initial-save/game/options').symlink_to(root / 'outside')
            with self.assertRaisesRegex(ValueError, 'symlink'):
                replay.prepare_replay_save(run, {'initial_save_snapshot': 'captured'}, root, root / 'replays')
            self.assertFalse((root / 'replays').exists())
            (run / 'initial-save-manifest.txt').unlink()
            with self.assertRaisesRegex(ValueError, 'manifest is missing'):
                replay.prepare_replay_save(run, {'initial_save_snapshot': 'captured'}, root, root / 'replays')

    def test_historical_save_is_a_copy_and_explicitly_unmatched(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run = root / 'run'; run.mkdir()
            save = root / 'data/save'; save.mkdir(parents=True)
            (save / 'file').write_text('current')
            data, matched = replay.prepare_replay_save(run, {'data_root': str(root / 'data')}, root, root / 'replays')
            self.assertFalse(matched)
            self.assertIn('initial_state=current-unmatched', (data / 'replay-source.txt').read_text())
            (data / 'save/file').write_text('changed')
            self.assertEqual((save / 'file').read_text(), 'current')
            with self.assertRaisesRegex(ValueError, 'no data root'):
                replay.prepare_replay_save(run, {}, root, root / 'replays')

    def test_ref_and_symlink_destination_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run = root / 'run'; make_snapshot(run)
            with self.assertRaisesRegex(ValueError, 'outside ref'):
                replay.prepare_replay_save(run, {'initial_save_snapshot': 'captured'}, root, replay.REF)
            link = root / 'link'; link.symlink_to(replay.REF, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'outside ref'):
                replay.prepare_replay_save(run, {'initial_save_snapshot': 'captured'}, root, link / 'nested')

    def test_main_runs_against_private_save_and_preserves_original(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run = root / 'original'; make_snapshot(run)
            (run / 'input.txt').write_text('1 quit\n')
            original_save = root / 'data/save'; original_save.mkdir(parents=True)
            (original_save / 'file').write_text('current')
            (run / 'meta.txt').write_text(
                f'cwd: {root}\nexecutable: /bin/true\nexe_sha256: unknown\n'
                f'data_root: {root / "data"}\ninitial_save_snapshot: captured\n'
                'arg[0]: /bin/true\narg[1]: --data-root\n'
                f'arg[2]: {root / "data"}\narg[3]: game\n')
            (run / 'result.txt').write_text('status=failed\nreason=sample\n')
            (run / 'run.log').write_text('[+00.100 f0] note random seed: elapsed_ms=42.1 seed=42\n')
            runs = root / 'runs'; runs.mkdir()
            def fake_run(command, **kwargs):
                self.assertEqual(kwargs['env']['SLRR_FORCE_SEED'], '42')
                target = Path(command[command.index('--data-root') + 1])
                self.assertEqual((target / 'save/game/options').read_bytes(), b'original')
                (target / 'save/game/options').write_text('game wrote here')
                fresh = runs / 'new-run'; fresh.mkdir()
                (fresh / 'result.txt').write_text('status=failed\nreason=sample\n')
                (fresh / 'meta.txt').write_text('pid: 12345\nprocess_start_unix: 100.000042\n')
                unrelated = runs / 'z-newer-run'; unrelated.mkdir()
                (unrelated / 'meta.txt').write_text('pid: 12346\nprocess_start_unix: 100.000042\n')
                return mock.Mock(pid=12345, wait=mock.Mock(return_value=1))
            output = io.StringIO()
            with mock.patch.object(replay, 'REPLAY_BASE', root / 'replay-data'), \
                 mock.patch.object(replay.diagnose, 'run_base', return_value=runs), \
                 mock.patch.object(replay.subprocess, 'Popen', side_effect=fake_run), \
                 mock.patch.object(replay.diagnose, 'read_process_identity', return_value=(100, 42, 1)), \
                 mock.patch.object(replay.sys, 'argv', ['replay.py', str(run)]), \
                 contextlib.redirect_stdout(output):
                self.assertEqual(replay.main(), 0)
            self.assertEqual((original_save / 'file').read_text(), 'current')
            self.assertEqual((run / 'initial-save/game/options').read_bytes(), b'original')
            self.assertIn('does not establish deterministic replay', output.getvalue())

    def test_uninterruptible_replay_does_not_wait_indefinitely(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); run = root / 'original'; make_snapshot(run)
            (run / 'input.txt').write_text('1 quit\n')
            (run / 'meta.txt').write_text(f'cwd: {root}\nexecutable: /bin/true\ninitial_save_snapshot: captured\narg[0]: /bin/true\n')
            (run / 'result.txt').write_text('status=failed\nseconds=100000000\n')
            runs = root / 'runs'; runs.mkdir()
            process = mock.Mock(pid=12345)
            process.wait.side_effect = [replay.subprocess.TimeoutExpired('probe', 600), replay.subprocess.TimeoutExpired('probe', 2)]
            with mock.patch.object(replay, 'REPLAY_BASE', root / 'replay-data'), \
                 mock.patch.object(replay.diagnose, 'run_base', return_value=runs), \
                 mock.patch.object(replay.subprocess, 'Popen', return_value=process), \
                 mock.patch.object(replay.diagnose, 'read_process_identity', return_value=(100, 42, 1)), \
                 mock.patch.object(replay.sys, 'argv', ['replay.py', str(run)]), \
                 contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(replay.main(), 1)
            self.assertEqual(process.wait.call_args_list, [mock.call(timeout=600.0), mock.call(timeout=2)])
            process.kill.assert_called_once()

    def test_process_matching_rejects_ambiguous_and_unverified_runs(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            for name, pid, start in [('a', 100, '8.000001'), ('b', 101, '8.000001')]:
                run = base / name; run.mkdir()
                (run / 'meta.txt').write_text(f'pid: {pid}\nprocess_start_unix: {start}\n')
            self.assertEqual(replay.find_replay_run(base, set(), 100, (8, 1, 1)), base / 'a')
            with self.assertRaisesRegex(ValueError, 'cannot verify'):
                replay.find_replay_run(base, set(), 100, None)
            with self.assertRaisesRegex(ValueError, 'found 0'):
                replay.find_replay_run(base, set(), 100, (9, 1, 1))
            (base / 'b/meta.txt').write_text('pid: 100\nprocess_start_unix: 8.000001\n')
            with self.assertRaisesRegex(ValueError, 'found 2'):
                replay.find_replay_run(base, set(), 100, (8, 1, 1))

    def test_command_replaces_every_data_root_and_preserves_other_arguments(self):
        meta = {f'arg[{i}]': v for i, v in enumerate(['probe', '--data-root', 'old', '--init', '--script', 'old-script', 'game', '--data-root', 'other'])}
        command = replay.rebuild_command(meta, Path('/recorded/input'), Path('/new/data'))
        self.assertEqual(command, ['probe', '--init', '--script', '/recorded/input', 'game', '--data-root', '/new/data'])
        self.assertEqual(replay.rebuild_command({'arg[0]': 'probe', 'arg[1]': 'game'}, Path('/i'), Path('/d')), ['probe', 'game', '--script', '/i', '--data-root', '/d'])
        with self.assertRaisesRegex(ValueError, 'missing argument'):
            replay.rebuild_command({'arg[0]': 'probe', 'arg[1]': '--data-root'}, Path('/i'), Path('/d'))


if __name__ == '__main__':
    unittest.main()
