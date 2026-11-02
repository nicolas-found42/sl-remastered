import copy
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('packet', Path(__file__).with_name('jev_packet.py'))
packet = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packet)


class PacketTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.source = self.root / 'test.log'
        self.source.write_text('build: 113 edges\n126/127 tests pass\n')
        self.manifest = {'schema': 1, 'claims': [{'text': 'Built 113 edges.', 'requires': ['build']}],
                         'evidence': {'build': {'path': 'test.log',
                          'sha256': hashlib.sha256(self.source.read_bytes()).hexdigest(),
                          'start_line': 1, 'end_line': 1, 'quote': 'build: 113 edges\n'}}}

    def test_exact_source_reaches_payload_without_promotion(self):
        result = packet.build(self.manifest, self.root)
        self.assertEqual(result['arguments']['evidence'][0]['text'], 'build: 113 edges\n')
        self.assertFalse(result['support_verified'])
        self.assertFalse(result['submitted'])

    def test_missing_required_build_log_refuses(self):
        self.manifest['claims'][0]['requires'].append('previous-failure')
        with self.assertRaisesRegex(ValueError, 'missing'):
            packet.build(self.manifest, self.root)

    def test_changed_source_refuses(self):
        self.source.write_text('build: 999 edges\n126/127 tests pass\n')
        with self.assertRaisesRegex(ValueError, 'hash changed'):
            packet.build(self.manifest, self.root)

    def test_wrong_quote_or_range_refuses(self):
        for change in ({'quote': 'invented'}, {'start_line': 2}, {'end_line': 99}):
            manifest = copy.deepcopy(self.manifest)
            manifest['evidence']['build'].update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                packet.build(manifest, self.root)

    def test_oversized_packet_refuses_without_truncation(self):
        self.source.write_text('x' * 32001)
        self.manifest['evidence']['build'].update(
            sha256=hashlib.sha256(self.source.read_bytes()).hexdigest(), quote=self.source.read_text())
        with self.assertRaisesRegex(ValueError, 'split claims'):
            packet.build(self.manifest, self.root)


if __name__ == '__main__':
    unittest.main()
