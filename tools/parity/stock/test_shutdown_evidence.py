"""Parser fixtures only; these are not ordinary game-exit evidence."""
import hashlib
import importlib.util
import json
from pathlib import Path
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


if __name__ == '__main__':
    unittest.main()
