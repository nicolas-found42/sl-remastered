#!/usr/bin/env python3
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).with_name('diagnostic-data.py')
spec = importlib.util.spec_from_file_location('diagnostic_data', MODULE)
subject = importlib.util.module_from_spec(spec)
spec.loader.exec_module(subject)


class DiagnosticDataTest(unittest.TestCase):
    def test_complete_tree_and_explicit_exclusions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'game'; root.mkdir()
            (root / 'maps').mkdir(); (root / 'maps/city.rpk').write_bytes(b'abc')
            (root / 'save').mkdir(); (root / 'save/options').write_bytes(b'private')
            (root / 'diag.log').write_text('runtime')
            result = subject.collect(root)
            self.assertTrue(result['complete'])
            self.assertEqual(result['files']['maps/city.rpk']['sha256'], hashlib.sha256(b'abc').hexdigest())
            self.assertEqual({x['path'] for x in result['exclusions']}, {'save/', 'diag.log'})
            self.assertIn('not a file-access trace', result['scope'])

    def test_limits_and_symlinks_mark_incomplete(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'game'; root.mkdir()
            (root / 'ok').write_bytes(b'abc'); (root / 'large').write_bytes(b'abcdef')
            (root / 'link').symlink_to(root / 'ok')
            result = subject.collect(root, max_file=4)
            self.assertFalse(result['complete'])
            errors = {item['path']: item['reason'] for item in result['errors']}
            self.assertIn('byte limit', errors['large'])
            self.assertIn('symlink', errors['link'])
            self.assertEqual(result['files']['ok']['size'], 3)

    def test_count_and_total_limits_are_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'game'; root.mkdir()
            (root / 'a').write_bytes(b'ab'); (root / 'b').write_bytes(b'cd')
            (root / 'sub').mkdir(); (root / 'sub/c').write_bytes(b'e')
            result = subject.collect(root, max_files=1)
            self.assertFalse(result['complete'])
            self.assertTrue(any('file count limit' in e['reason'] for e in result['errors']))
            result = subject.collect(root, max_dirs=1)
            self.assertFalse(result['complete'])
            self.assertTrue(any('directory count limit' in e['reason'] for e in result['errors']))
            result = subject.collect(root, max_total=2)
            self.assertFalse(result['complete'])
            self.assertTrue(any('byte limit' in e['reason'] for e in result['errors']))

    def test_output_is_new_and_outside_game(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'game'; root.mkdir(); output = Path(tmp) / 'data.json'
            result = subject.write_manifest(root, output)
            self.assertEqual(json.loads(output.read_text()), result)
            with self.assertRaises(FileExistsError): subject.write_manifest(root, output)
            with self.assertRaises(ValueError): subject.write_manifest(root, root / 'result.json')
            with self.assertRaises(ValueError): subject.write_manifest(root, subject.REF / 'result.json')


if __name__ == '__main__': unittest.main()
