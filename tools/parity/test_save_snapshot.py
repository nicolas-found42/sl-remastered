import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('save_snapshot', Path(__file__).with_name('save_snapshot.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SnapshotTest(unittest.TestCase):
    def test_snapshot_preserves_contents_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'source'
            (source / 'game').mkdir(parents=True)
            (source / 'game/options').write_bytes(b'initial options')
            info = module.snapshot(source, root / 'snapshot')
            (source / 'game/options').write_bytes(b'changed')
            self.assertEqual((root / 'snapshot/save/game/options').read_bytes(), b'initial options')
            self.assertEqual(json.loads((root / 'snapshot/manifest.json').read_text()), info)
            with self.assertRaises(FileExistsError):
                module.snapshot(source, root / 'snapshot')

    def test_rejects_symlinks_and_nested_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'source'
            source.mkdir()
            with self.assertRaises(ValueError):
                module.snapshot(source, source / 'snapshot')
            (source / 'link').symlink_to(root)
            with self.assertRaises(ValueError):
                module.snapshot(source, root / 'snapshot')


if __name__ == '__main__':
    unittest.main()
