#!/usr/bin/env python3
"""Exercise oracle isolation and timeouts without starting Wine."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
CONTROLLER = ROOT / 'tools/wine/oracle_controller.py'


class OracleControllerTest(unittest.TestCase):
    def run_case(self, record, timeout, program, *extra):
        return subprocess.run([sys.executable, '-I', str(CONTROLLER), '--record-dir', str(record),
                               '--timeout', str(timeout), *extra, '--', sys.executable, '-c', program],
                              capture_output=True, text=True, timeout=10)

    def test_output_exit_and_hashes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); fixture = root / 'fixture'; fixture.write_bytes(b'known data')
            result = self.run_case(root / 'record', 3, "import sys;print('out');print('err',file=sys.stderr);sys.exit(7)", '--hash-file', str(fixture))
            self.assertEqual(result.returncode, 7)
            self.assertEqual(result.stdout, 'out\n'); self.assertEqual(result.stderr, 'err\n')
            record = json.loads((root / 'record/status.json').read_text())
            self.assertEqual(record['status'], 'completed'); self.assertEqual(record['returncode'], 7)
            self.assertEqual(record['hashes_sha256'][str(fixture.resolve())], hashlib.sha256(b'known data').hexdigest())
            self.assertEqual(record['stdout_sha256'], hashlib.sha256(b'out\n').hexdigest())

    def test_timeout_is_bounded_and_does_not_kill_other_process(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            other = subprocess.Popen([sys.executable, '-c', 'import time;time.sleep(20)'])
            try:
                before = time.monotonic()
                result = self.run_case(root / 'record', 0.2, "import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);print('started',flush=True);time.sleep(20)")
                self.assertEqual(result.returncode, 124)
                self.assertLess(time.monotonic() - before, 5)
                self.assertEqual(result.stdout, 'started\n')
                self.assertIsNone(other.poll())
                record = json.loads((root / 'record/status.json').read_text())
                self.assertEqual(record['status'], 'timed_out')
                self.assertFalse(record['termination_pending'])
            finally:
                other.terminate(); other.wait(timeout=3)

    def test_refusal_to_reuse_or_write_ref(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); record = root / 'record'; record.mkdir()
            (record / 'keep').write_text('evidence')
            result = self.run_case(record, 1, 'pass')
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((record / 'keep').read_text(), 'evidence')
            result = self.run_case(ROOT / 'ref' / 'must-not-be-created', 1, 'pass')
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((ROOT / 'ref' / 'must-not-be-created').exists())


if __name__ == '__main__':
    unittest.main()
