import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('trace', Path(__file__).with_name('trace_inspect.py'))
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


class TraceTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.path = Path(temporary.name) / 'trace.jsonl'
        self.rows = [{'phase': 'before', 'step': 40, 'words': [0, 0]},
                     {'phase': 'after', 'step': 40, 'words': [107592, 4]}]

    def run_trace(self):
        self.path.write_text(''.join(json.dumps(row) + '\n' for row in self.rows))
        return trace.compare(self.path, {'phase': 'before', 'step': 40},
                             {'phase': 'after', 'step': 40}, {'head': 0, 'count': 1})

    def test_named_phase_difference(self):
        report = self.run_trace()
        self.assertEqual(report['before']['values'], {'head': 0, 'count': 0})
        self.assertEqual(report['after']['values'], {'head': 107592, 'count': 4})

    def test_duplicate_phase_refuses(self):
        self.rows.append(self.rows[1])
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            self.run_trace()

    def test_file_replaced_during_read_refuses(self):
        original = json.loads
        changed = False
        def replace_after_read(raw):
            nonlocal changed
            value = original(raw)
            if not changed:
                changed = True
                replacement = self.path.with_suffix('.replacement')
                replacement.write_bytes(self.path.read_bytes())
                replacement.replace(self.path)
            return value
        with patch.object(trace.json, 'loads', side_effect=replace_after_read):
            with self.assertRaisesRegex(ValueError, 'changed during'):
                self.run_trace()

    def test_explicit_row_disambiguates_reused_contact_pointer(self):
        self.rows += self.rows
        self.path.write_text(''.join(json.dumps(row) + '\n' for row in self.rows))
        report = trace.compare(self.path, {'phase': 'before', '@line': 1},
                               {'phase': 'after', '@line': 2}, {'count': 1})
        self.assertEqual(report['after']['line'], 2)

    def test_missing_reversed_or_malformed_phase_refuses(self):
        for variant in ('missing', 'reverse', 'boolean', 'short'):
            self.setUp()
            if variant == 'missing': self.rows.pop()
            elif variant == 'reverse': self.rows.reverse()
            elif variant == 'boolean': self.rows[1]['words'][1] = True
            else: self.rows[1]['words'] = []
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                self.run_trace()


if __name__ == '__main__':
    unittest.main()
