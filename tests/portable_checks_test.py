"""Real CTest failure controls for the public asset-free lane; no game runs."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / 'tools/portable_checks.py'
spec = importlib.util.spec_from_file_location('portable_checks', HELPER)
portable = importlib.util.module_from_spec(spec)
spec.loader.exec_module(portable)


class PortableChecksTest(unittest.TestCase):
    def test_actual_ctest_pass_missing_selection_and_failed_expectation(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            # CTest consumes this format directly: no compiler, reference data or
            # renderer is needed to exercise the wrapper's actual exit boundary.
            (build / 'CTestTestfile.cmake').write_text(
                f'add_test(passing "{sys.executable}" "-I" "-c" "assert 2 + 2 == 4")\n'
                'set_tests_properties(passing PROPERTIES LABELS "passing")\n'
                f'add_test(failing "{sys.executable}" "-I" "-c" "assert 2 + 2 == 5")\n'
                'set_tests_properties(failing PROPERTIES LABELS "failing")\n'
            )
            for label, succeeds in [('passing', True), ('absent', False), ('failing', False)]:
                with self.subTest(label=label):
                    done = subprocess.run(
                        [sys.executable, '-I', str(HELPER), '--no-build',
                         '--build-dir', str(build), '--test-label', f'^{label}$'],
                        cwd=ROOT, capture_output=True, text=True, timeout=180,
                    )
                    output = done.stdout + done.stderr
                    self.assertEqual(done.returncode == 0, succeeds, output)
                    self.assertIn('portable: test', output)
                    if not succeeds:
                        self.assertIn('failed stage=test', output)

    def test_failure_stops_before_later_stage(self):
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / 'must-not-run'
            first = [sys.executable, '-I', '-c', 'raise SystemExit(7)']
            later = [sys.executable, '-I', '-c',
                     'from pathlib import Path; import sys; Path(sys.argv[1]).touch()', str(marker)]
            self.assertEqual(portable.execute([('configure', first), ('build', later)]), 7)
            self.assertFalse(marker.exists())


if __name__ == '__main__':
    unittest.main()
