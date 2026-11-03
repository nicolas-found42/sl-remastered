"""Parser fixtures only; these are not ordinary game-exit evidence."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('shutdown_evidence', Path(__file__).with_name('shutdown_evidence.py'))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class ShutdownEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        # macOS /var is a platform symlink; production outputs are canonical.
        self.root = Path(self.temp.name).resolve()
        self.directory = self.root / 'game/dumps'
        self.directory.mkdir(parents=True)
        self.report = dict(schema=1, boundary=checker.BOUNDARY, dll_detach_observed=False,
                           caller=0x5d796f, exit_code=0, guarded=True, stop_result=1,
                           active=0, gate=0x80000000, dropped=0, gaps=0, entered=1, returned=1)
        self.rows = [{'event': 'initial_global', 'image_base': 0x400000},
                     dict(event='status', entered=1, returned=1, active=0, pending=0,
                          dropped=0, gaps=0, accepting=0)]

    def inspect(self):
        (self.directory / 'stock-control-shutdown.json').write_text(json.dumps(self.report))
        raw = ''.join(json.dumps(r) + '\n' for r in self.rows).encode()
        (self.directory / 'stock-control-tape.jsonl').write_bytes(raw)
        return checker.inspect(self.root, {'source_sha256': hashlib.sha256(raw).hexdigest()})

    def test_control_and_declared_limit(self):
        self.assertTrue(self.inspect()['passed'])
        self.assertIn('DLL detach excluded', self.inspect()['limits'])

    def test_failure_counters_exit_and_typed_fields(self):
        changes = [('active', 1), ('gate', 0x80000001), ('dropped', 1), ('gaps', 1),
                   ('returned', 0), ('exit_code', 37), ('guarded', False),
                   ('stop_result', -1), ('entered', True), ('dll_detach_observed', True)]
        for key, value in changes:
            with self.subTest(key=key):
                old = self.report[key]
                self.report[key] = value
                with self.assertRaisesRegex(ValueError, 'schema|guard/drain/exit'):
                    self.inspect()
                self.report[key] = old

    def test_raw_status_mismatch_and_pending_reject(self):
        for key in ('returned', 'pending', 'accepting'):
            with self.subTest(key=key):
                old = self.rows[-1][key]
                self.rows[-1][key] = 7
                with self.assertRaisesRegex(ValueError, 'final status|pending/accepted'):
                    self.inspect()
                self.rows[-1][key] = old

    def test_source_hash_and_caller_binding_reject(self):
        self.inspect()
        with self.assertRaisesRegex(ValueError, 'compiled interval'):
            checker.inspect(self.root, {'source_sha256': '0' * 64})
        self.report['caller'] += 4
        with self.assertRaisesRegex(ValueError, 'caller'):
            self.inspect()


class StopReceiptTest(unittest.TestCase):
    def receipt(self, raw):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory).resolve() / 'd3d9proxy.log'
            log.write_bytes(raw)
            result = subprocess.run(
                [sys.executable, '-I', str(Path(checker.__file__).resolve()),
                 '--stop-receipt', str(log)],
                capture_output=True, timeout=5)
            return result, json.loads(result.stdout)

    def test_native_windows_receipt_and_lf_receipt(self):
        # Actual format-9 attempt v4 stopped successfully, then the shell
        # rejected this CRLF line as an explicit failure (container exit 65).
        for ending in (b'\r\n', b'\n'):
            with self.subTest(ending=ending):
                result, receipt = self.receipt(
                    b'[   55641 ms] input: tape_stop' + ending +
                    b'[   55652 ms] control_tape_stop result=2167' + ending)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(receipt, {
                    'passed': True, 'observations': 1, 'committed_api_rows': 2167})

    def test_incomplete_failed_duplicate_or_malformed_receipt_refuses(self):
        good = b'[ 12 ms] control_tape_stop result=2167\r\n'
        for raw in (b'', good.replace(b'=2167', b'=-1'), good + good,
                    good.replace(b'=2167', b'=65537'),
                    good.replace(b'=2167', b'=+1'), good.replace(b'\r\n', b' garbage\r\n'),
                    good + b'control_tape_stop result=1\r\n'):
            with self.subTest(raw=raw):
                result, receipt = self.receipt(raw)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(receipt['passed'])

    def test_oversized_missing_and_linked_logs_refuse_without_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            large = root / 'large'
            with large.open('wb') as stream:
                stream.truncate(4 * 1024**2 + 1)
            linked = root / 'linked'
            linked.symlink_to(large)
            for path in (large, linked, root / 'absent'):
                with self.subTest(path=path):
                    result = subprocess.run(
                        [sys.executable, '-I', str(Path(checker.__file__).resolve()),
                         '--stop-receipt', str(path)],
                        capture_output=True, timeout=5)
                    self.assertEqual(result.returncode, 2)
                    self.assertEqual(result.stdout, b'')


if __name__ == '__main__':
    unittest.main()
