import importlib.util
import copy
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


class BodySeriesTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.left = Path(temporary.name) / 'left.jsonl'
        self.right = Path(temporary.name) / 'right.jsonl'
        self.rows = []
        for phase, step in [('constructor', 0), ('step-initial', 0), ('step-final', 0),
                            ('step-initial', 1), ('step-final', 1)]:
            for index in range(2):
                current = 0 if phase == 'constructor' else (step + (phase == 'step-final')) % 2
                self.rows.append({'type': 'body', 'phase': phase, 'step': step,
                                  'index': index, 'current': current, 'flags': 5,
                                  'states': [0] * 106})
        self.write(self.left, self.rows)

    def write(self, path, rows):
        path.write_text(''.join(json.dumps(row) + '\n' for row in rows))

    def compare(self, rows=None):
        self.write(self.right, self.rows if rows is None else rows)
        return trace.compare_body_series(self.left, self.right, 2)

    def test_complete_series_and_first_changed_current_word(self):
        self.assertTrue(self.compare()['equal'])
        changed = copy.deepcopy(self.rows)
        changed[4]['states'][53] = 1
        report = self.compare(changed)
        self.assertFalse(report['equal'])
        self.assertEqual(report['first_difference']['key'], ('step-final', 0, 0))
        self.assertEqual(report['first_difference']['words'], [{'offset': 53, 'left': 0, 'right': 1}])
        self.assertNotEqual(report['sources'][0]['sha256'], report['sources'][1]['sha256'])

    def test_missing_duplicate_reordered_or_unknown_phases_refuse(self):
        for variant in ('missing', 'duplicate', 'reordered', 'unknown', 'missing-tail'):
            rows = copy.deepcopy(self.rows)
            if variant == 'missing': rows.pop(4)
            elif variant == 'missing-tail': rows.pop()
            elif variant == 'duplicate': rows.insert(4, rows[4])
            elif variant == 'reordered': rows[4], rows[5] = rows[5], rows[4]
            else: rows[4]['phase'] = 'unqualified'
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                self.compare(rows)

    def test_wrong_buffer_and_boolean_identifiers_refuse(self):
        for field, value in [('current', 0), ('current', True), ('step', True),
                             ('index', False), ('flags', True), ('flags', -1)]:
            rows = copy.deepcopy(self.rows)
            rows[4][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                self.compare(rows)

    def test_incomplete_or_invalid_words_refuse(self):
        for value in ([0] * 105, [0] * 107, [True] * 106, [-1] * 106, [2**32] * 106):
            rows = copy.deepcopy(self.rows)
            rows[4]['states'] = value
            with self.subTest(value=value[:2]), self.assertRaises(ValueError):
                self.compare(rows)

    def test_equal_words_do_not_hide_flag_difference(self):
        rows = copy.deepcopy(self.rows)
        rows[4]['flags'] = 9
        report = self.compare(rows)
        self.assertFalse(report['equal'])
        self.assertEqual(report['first_difference']['words'], [])
        self.assertEqual(report['first_difference']['right_flags'], 9)

    def test_invalid_or_excessive_series_dimensions_refuse(self):
        for steps, bodies in [(True, 2), (0, 2), (2, False), (2, 65), (10000, 64)]:
            with self.subTest(steps=steps, bodies=bodies), self.assertRaises(ValueError):
                trace.compare_body_series(self.left, self.left, steps, bodies)

    def test_replaced_input_and_malformed_row_refuse(self):
        self.write(self.right, self.rows)
        original = json.loads
        def replace_after_read(raw):
            result = original(raw)
            replacement = self.left.with_suffix('.replacement')
            replacement.write_bytes(self.left.read_bytes())
            replacement.replace(self.left)
            return result
        with patch.object(trace.json, 'loads', side_effect=replace_after_read):
            with self.assertRaisesRegex(ValueError, 'changed during'):
                trace.compare_body_series(self.left, self.right, 2)
        with self.right.open('a') as output:
            output.write('not a JSON record\n')
        with self.assertRaises(ValueError):
            trace.compare_body_series(self.left, self.right, 2)


if __name__ == '__main__':
    unittest.main()
