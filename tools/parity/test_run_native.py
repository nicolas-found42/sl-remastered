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
            probe.write_text('#!/usr/bin/env python3\nimport os, sys\nfrom pathlib import Path\nassert os.environ["SLRR_TRACE_RANDOM_SEED"] == "1"\nassert os.environ["SLRR_TRACE_CAR_CREATION"] == "1"\nPath(sys.argv[sys.argv.index("--data-root") + 1], "save/game/options").write_bytes(b"changed")\nprint("fake trace", file=sys.stderr)\n')
            probe.chmod(0o755)
            out = root / 'record'
            tool = Path(__file__).with_name('run_native.py')
            command = ['python3', '-I', str(tool), str(out), '--probe', str(probe), '--game', str(game), '--save', str(save)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((out / 'initial-save/save/game/options').read_bytes(), b'initial')
            self.assertEqual((save / 'game/options').read_bytes(), b'initial')
            self.assertEqual((out / 'data/save/game/options').read_bytes(), b'changed')
            self.assertIn('fake trace', (out / 'stderr.log').read_text())
            self.assertEqual(json.loads((out / 'result.json').read_text())['exit_code'], 0)
            metadata = json.loads((out / 'metadata.json').read_text())
            self.assertEqual(metadata['game_data']['status'], 'complete')
            manifest = json.loads((out / 'game-data-manifest.json').read_text())
            self.assertTrue(manifest['complete'])
            self.assertEqual(manifest['files']['system.rpk']['size'], 9)
            self.assertEqual(subprocess.run(command, capture_output=True).returncode, 2)

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
