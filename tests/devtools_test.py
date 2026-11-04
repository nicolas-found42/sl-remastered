"""The environment report must reject bad tools without installing or building."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DoctorTest(unittest.TestCase):
    def invoke(self, *args, env=None):
        return subprocess.run(
            [sys.executable, '-I', str(ROOT / 'tools/devtools.py'), '--json', *args],
            capture_output=True,
            text=True,
            env=env,
            timeout=45,
        )

    def test_wrong_ruff_version_is_reported_and_source_bytes_unchanged(self):
        paths = [ROOT / p for p in ('tools/devtools.json', 'tools/guardrails.py', 'ruff.toml')]
        before = [hashlib.sha256(p.read_bytes()).digest() for p in paths]
        with tempfile.TemporaryDirectory() as temporary:
            ruff = Path(temporary) / 'ruff'
            ruff.write_text('#!/bin/sh\nprintf "ruff 999.0.0\\n"\n')
            ruff.chmod(0o700)
            result = self.invoke(
                env={**os.environ, 'PATH': temporary + os.pathsep + os.environ['PATH']}
            )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        report = json.loads(result.stdout)
        self.assertFalse(report['checks']['ruff']['ok'])
        self.assertEqual(before, [hashlib.sha256(p.read_bytes()).digest() for p in paths])

    def test_missing_build_is_reported_without_creating_it(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary) / 'not-configured'
            result = self.invoke('--build-dir', str(build))
            self.assertIn(result.returncode, (0, 1), result.stdout + result.stderr)
            report = json.loads(result.stdout)
            self.assertFalse(report['compilation_database']['present'])
            self.assertFalse(build.exists())

    def test_malformed_existing_database_is_not_ready(self):
        with tempfile.TemporaryDirectory() as temporary:
            database = Path(temporary) / 'compile_commands.json'
            database.write_text('{"not": "a database"}')
            result = self.invoke('--build-dir', temporary)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn('error', json.loads(result.stdout)['compilation_database'])
            self.assertEqual(database.read_text(), '{"not": "a database"}')


if __name__ == '__main__':
    unittest.main()
