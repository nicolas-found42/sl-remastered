#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'tools/native_evidence_audit.py'


class NativeEvidenceAuditTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'docs').mkdir()
        (self.root / 'local').mkdir()
        self.artifact = self.root / 'local/stock.txt'
        self.artifact.write_text('FUNCTION FUN_0047c0b0 0047c0b0\n')
        self.registry = self.root / 'docs/natives.tsv'
        self.registry.write_text('class\tmethod\tdescriptor\thandler\tstatus\njava.lang.System\tbuildNumber\t()I\t0047c0b0\tmatched\n')
        self.manifest = {
            'format': 1,
            'artifacts': {'saved': {'path': 'local/stock.txt', 'sha256': hashlib.sha256(self.artifact.read_bytes()).hexdigest(), 'origin': 'unverified'}},
            'natives': [{'class': 'java.lang.System', 'method': 'buildNumber', 'descriptor': '()I', 'handler': '0047c0b0',
                         'references': [{'artifact': 'saved', 'anchor': 'FUNCTION FUN_0047c0b0 0047c0b0'}]}],
        }
        self.write_manifest()

    def write_manifest(self):
        (self.root / 'docs/native_evidence_artifacts.json').write_text(json.dumps(self.manifest))

    def run_audit(self):
        return subprocess.run(['python3', '-I', str(SCRIPT), '--root', str(self.root)], capture_output=True, text=True)

    def test_intact_artifact_and_unverified_origin_are_separate(self):
        result = self.run_audit()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('origin: unverified', result.stdout)
        self.assertIn('Origin remains unverifiable', result.stdout)
        self.assertIn('not behavioral coverage', result.stdout)

    def test_missing_and_tampered_artifacts_fail(self):
        self.artifact.write_text('tampered')
        result = self.run_audit()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('CORRUPT', result.stderr)
        self.artifact.unlink()
        result = self.run_audit()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('MISSING', result.stderr)

    def test_wrong_anchor_fails_even_when_hash_matches(self):
        self.artifact.write_text('different content')
        self.manifest['artifacts']['saved']['sha256'] = hashlib.sha256(self.artifact.read_bytes()).hexdigest()
        self.write_manifest()
        result = self.run_audit()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('missing anchor', result.stderr)

    def test_coverage_and_path_validation(self):
        self.manifest['natives'][0]['handler'] = '0047c0b1'
        self.write_manifest()
        self.assertNotEqual(self.run_audit().returncode, 0)
        self.manifest['natives'][0]['handler'] = '0047c0b0'
        self.manifest['artifacts']['saved']['path'] = '../elsewhere'
        self.write_manifest()
        self.assertNotEqual(self.run_audit().returncode, 0)
        self.manifest['artifacts']['saved']['path'] = 'local/link'
        (self.root / 'local/link').symlink_to(self.artifact)
        self.write_manifest()
        self.assertNotEqual(self.run_audit().returncode, 0)


if __name__ == '__main__':
    unittest.main()
