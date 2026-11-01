import json
from pathlib import Path
import subprocess
import tempfile
import unittest


class NativeRecordTest(unittest.TestCase):
    def test_record_contains_private_save_and_process_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            game, save = root / 'game', root / 'save'
            game.mkdir()
            (game / 'system.rpk').write_bytes(b'game-data')
            (save / 'game').mkdir(parents=True)
            (save / 'game/options').write_bytes(b'initial')
            probe = root / 'probe'
            probe.write_text('#!/usr/bin/env python3\nimport os, sys\nfrom pathlib import Path\nassert os.environ["SLRR_TRACE_RANDOM_SEED"] == "1"\nassert os.environ["SLRR_TRACE_CAR_CREATION"] == "1"\nPath(sys.argv[sys.argv.index("--data-root") + 1], "save/game/options").write_bytes(b"changed")\nprint("startup probe: milestone menu process=1.250 loop=1.000 frame=12")\nprint("startup probe: milestone driving process=3.500 loop=3.250 frame=25")\n')
            probe.chmod(0o755)
            out = root / 'record'
            tool = Path(__file__).with_name('run_native.py')
            command = ['python3', '-I', str(tool), str(out), '--probe', str(probe), '--game', str(game), '--save', str(save)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((out / 'initial-save/save/game/options').read_bytes(), b'initial')
            self.assertEqual((save / 'game/options').read_bytes(), b'initial')
            self.assertEqual((out / 'data/save/game/options').read_bytes(), b'changed')
            self.assertEqual(json.loads((out / 'result.json').read_text())['exit_code'], 0)
            status = json.loads((out / 'result.json').read_text())
            self.assertTrue(status['source_save_unchanged'])
            self.assertEqual(status['timing']['menu_seconds'], 1.25)
            self.assertEqual(status['timing']['driving_seconds'], 3.5)
            self.assertGreater(status['timing']['preparation_seconds'], 0)
            self.assertGreaterEqual(status['timing']['total_seconds'], status['elapsed_seconds'] + status['timing']['preparation_seconds'])
            metadata = json.loads((out / 'metadata.json').read_text())
            self.assertEqual(metadata['game_data']['status'], 'complete')
            manifest = json.loads((out / 'game-data-manifest.json').read_text())
            self.assertTrue(manifest['complete'])
            self.assertEqual(manifest['files']['system.rpk']['size'], 9)
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 2)

    def test_rejects_invalid_arguments_before_creating_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            game, save = root / 'game', root / 'save'
            game.mkdir()
            save.mkdir()
            probe = root / 'probe'
            probe.write_text('unused')
            tool = Path(__file__).with_name('run_native.py')
            base = ['python3', '-I', str(tool), str(root / 'out'), '--probe', str(probe),
                    '--game', str(game), '--save', str(save)]
            missing = subprocess.run(base + ['--script', str(root / 'absent.txt')],
                                     capture_output=True)
            self.assertEqual(missing.returncode, 2)
            for value in ('0', 'nan', 'inf'):
                bad_timeout = subprocess.run(base + ['--timeout', value], capture_output=True)
                self.assertEqual(bad_timeout.returncode, 2)
            escaping = root / 'escaping.txt'
            escaping.write_text(f'0 wait menu 30\n1 shot {save / "options"}\n2 quit\n')
            escape = subprocess.run(base + ['--script', str(escaping)], capture_output=True)
            self.assertEqual(escape.returncode, 2)
            self.assertFalse((root / 'out').exists())

    def test_explicit_reference_root_rejects_symlink_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            reference, game, save = (root / name for name in ('reference', 'game', 'save'))
            for path in (reference, game, save):
                path.mkdir()
            alias = root / 'reference-alias'
            alias.symlink_to(reference, target_is_directory=True)
            probe = root / 'probe'
            probe.write_text('unused')
            tool = Path(__file__).with_name('run_native.py')
            result = subprocess.run(['python3', '-I', str(tool), str(alias / 'out'),
                                     '--probe', str(probe), '--game', str(game),
                                     '--save', str(save), '--reference-root', str(reference)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('outside reference root', result.stderr)
            self.assertFalse((reference / 'out').exists())

    def test_rejects_overlapping_paths_even_through_symlinks(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            game, save = root / 'game', root / 'save'
            game.mkdir()
            save.mkdir()
            alias = root / 'alias'
            alias.symlink_to(game, target_is_directory=True)
            probe = root / 'probe'
            probe.write_text('unused')
            tool = Path(__file__).with_name('run_native.py')
            def run(out):
                return subprocess.run(['python3', '-I', str(tool), str(out), '--probe', str(probe),
                                       '--game', str(game), '--save', str(save)], capture_output=True)
            self.assertEqual(run(alias / 'nested').returncode, 2)
            self.assertEqual(run(root).returncode, 2)
            self.assertFalse((game / 'nested').exists())


if __name__ == '__main__':
    unittest.main()
