import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('index', Path(__file__).with_name('evidence_index.py'))
index = importlib.util.module_from_spec(spec)
spec.loader.exec_module(index)


class IndexTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.source = self.root / 'result.json'
        self.source.write_text('{"exit": 1}')
        self.manifest = {'schema': 1, 'deliverables': {'a': 'Whole pipeline', 'b': 'Other'},
                         'attempts': [{'id': 'first', 'deliverable': 'a', 'sequence': 1,
                                       'scope': 'One failed child', 'review': 'pending', 'outcome': 'failed',
                                       'evidence': {'result': {'path': 'result.json',
                                        'sha256': hashlib.sha256(self.source.read_bytes()).hexdigest()}},
                                       'assertions': [{'evidence': 'result', 'pointer': '/exit', 'equals': 1}]}]}

    def test_failed_current_unknown_deliverable_and_preserved_history(self):
        second = copy.deepcopy(self.manifest['attempts'][0])
        second.update(id='second', sequence=2)
        self.manifest['attempts'].append(second)
        report = index.generate(self.manifest, self.root)
        self.assertEqual(report['current'][0]['latest']['id'], 'second')
        self.assertEqual(len(report['history']), 2)
        self.assertIsNone(report['current'][1]['latest'])
        self.assertFalse(report['current'][0]['completion_proven'])
        self.assertIn('Execution: failed', index.markdown(report))

    def test_changed_or_missing_file_invalidates_current(self):
        for missing in (False, True):
            self.source.write_text('{"exit": 0}')
            if missing:
                self.source.unlink()
            report = index.generate(self.manifest, self.root)
            self.assertFalse(report['all_evidence_valid'])
            self.assertTrue(report['history'][0]['problems'])

    def test_wrong_observation_or_boolean_cannot_match_integer(self):
        for expected in (0, True):
            self.manifest['attempts'][0]['assertions'][0]['equals'] = expected
            self.assertFalse(index.generate(self.manifest, self.root)['all_evidence_valid'])

    def test_duplicate_or_reversed_sequence_refuses(self):
        self.manifest['attempts'].append(copy.deepcopy(self.manifest['attempts'][0]))
        with self.assertRaises(ValueError):
            index.generate(self.manifest, self.root)

    def test_nested_boolean_is_not_integer_evidence(self):
        self.source.write_text('{"nested": [{"count": 1}]}')
        row = self.manifest['attempts'][0]
        row['evidence']['result']['sha256'] = hashlib.sha256(self.source.read_bytes()).hexdigest()
        row['assertions'] = [{'evidence': 'result', 'pointer': '/nested', 'equals': [{'count': True}]}]
        self.assertFalse(index.generate(self.manifest, self.root)['all_evidence_valid'])
        row['assertions'][0]['equals'] = [{'count': 1}]
        self.assertTrue(index.generate(self.manifest, self.root)['all_evidence_valid'])


if __name__ == '__main__':
    unittest.main()
