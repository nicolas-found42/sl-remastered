"""Execute only the container's shell preflight; no Wine/display/audio process."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).with_name('container.sh')


class ContainerPreflightTest(unittest.TestCase):
    def run_preflight(self, root):
        # Stop before the first runtime process. Replace the two absolute
        # filesystem roots only, leaving the Windows guest path untouched.
        parts = SOURCE.read_text().split('pulseaudio -n ', 1)
        self.assertEqual(len(parts), 2, 'missing runtime boundary; refuse to execute script')
        text = parts[0]
        text = text.replace('/work', str(root/'work')).replace('/tmp/pulse', str(root/'pulse'))
        text += '\nprintf "DUMP_DIR=%s\\n" "${SLRR_DUMP_DIR-unset}"\n'
        env = dict(os.environ)
        env.pop('SLRR_DUMP_DIR', None)
        return subprocess.run(['/bin/bash', '-c', text], env=env, capture_output=True, text=True, timeout=5)

    def test_private_directory_and_guest_path_exist_before_any_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = self.run_preflight(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((root/'work/game/dumps').is_dir())
            self.assertIn('DUMP_DIR=Z:\\work\\game\\dumps', result.stdout)

    def test_prior_observations_are_refused_and_preserved(self):
        for name in ('stock-input-setup.jsonl', 'stock-control-tape.jsonl', 'stock-control-shutdown.json'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                dumps = root/'work/game/dumps'
                dumps.mkdir(parents=True)
                path = dumps/name
                path.write_bytes(b'prior evidence\n')
                result = self.run_preflight(root)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(path.read_bytes(), b'prior evidence\n')

    def test_broken_artifact_symlink_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dumps = root/'work/game/dumps'
            dumps.mkdir(parents=True)
            path = dumps/'stock-control-tape.jsonl'
            path.symlink_to('absent')
            result = self.run_preflight(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(path.is_symlink())


if __name__ == '__main__':
    unittest.main(verbosity=2)
