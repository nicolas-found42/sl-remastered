"""Actual console failures must reach the experiment CLI exit status."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

RUNNER = Path(__file__).with_name('experiment_result.py')


class ExperimentTest(unittest.TestCase):
    def run_case(self, source, expected=0, timeout=60, options=(), assertion_source='pass'):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            manifest = root / 'manifest.json'
            source = source.replace('OUTPUT_DIRECTORY', repr(str(root / 'result')))
            assertion_source = assertion_source.replace('OUTPUT_DIRECTORY', repr(str(root / 'result')))
            manifest.write_text(json.dumps({'stages': [
                {'name': 'target', 'command': [sys.executable, '-I', '-c', source],
                 'expected_exit': expected},
                {'name': 'assertions', 'command': [sys.executable, '-I', '-c', assertion_source]}]}))
            result = subprocess.run([sys.executable, '-I', str(RUNNER), str(manifest),
                                     str(root / 'result'), '--timeout', str(timeout), *options], capture_output=True, text=True)
            report = json.loads((root / 'result/result.json').read_text())
            return result, report

    def test_success_is_not_parity(self):
        result, report = self.run_case('pass')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report['execution_passed'])
        self.assertFalse(report['parity_verified'])

    def test_console_evidence_directory_is_private(self):
        result, report = self.run_case('from pathlib import Path; import stat; '
            'assert stat.S_IMODE(Path(OUTPUT_DIRECTORY).stat().st_mode) == 0o700')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report['execution_passed'])

    def test_failed_target_stops_assertion_stage_and_fails_wrapper(self):
        result, report = self.run_case('raise SystemExit(17)')
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report['stages']['target']['actual_exit'], 17)
        self.assertEqual(report['missing_stages'], ['assertions'])

    def test_signaled_target_fails_wrapper(self):
        result, report = self.run_case('import os,signal; os.kill(os.getpid(), signal.SIGTERM)')
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report['stages']['target']['actual_exit'], -15)

    def test_declared_negative_control(self):
        result, report = self.run_case('raise SystemExit(71)', expected=71)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report['execution_passed'])

    def test_timeout_is_failure_and_retains_incomplete_stage(self):
        result, report = self.run_case('import time; time.sleep(20)', timeout=.1)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertFalse(report['execution_passed'])
        self.assertFalse(report['stages']['target']['completed'])
        self.assertTrue(report['supervision_failures'])

    def test_journal_failure_is_visible_without_changing_command_outcome(self):
        source = ('from pathlib import Path; '
                  'p=Path(OUTPUT_DIRECTORY)/"stages.jsonl"; p.unlink(); p.mkdir()')
        result, report = self.run_case(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report['execution_passed'])
        self.assertGreater(report['telemetry_failures'], 0)
        self.assertTrue(all(row['completed'] for row in report['stages'].values()))

    def test_output_limit_is_failure(self):
        result, report = self.run_case('import sys; sys.stdout.write("x" * 2000000)')
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertFalse(report['execution_passed'])

    def test_final_report_growth_cannot_leave_a_success_result(self):
        source = ('from pathlib import Path; '
                  'root=Path(OUTPUT_DIRECTORY); '
                  'used=sum(p.stat().st_size for p in root.iterdir() if p.is_file()); '
                  '(root / "payload").write_bytes(b"x" * (8*1024*1024-used-512))')
        result, report = self.run_case('pass', assertion_source=source)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertFalse(report['execution_passed'])
        self.assertTrue(any('final artifact' in error for error in report['supervision_failures']))
        self.assertTrue(all(row['completed'] for row in report['stages'].values()))

    def test_symlink_metadata_requires_explicit_count_policy(self):
        source = ('from pathlib import Path; '
                  '(Path(OUTPUT_DIRECTORY) / "latest").symlink_to("missing-run")')
        refused, refused_report = self.run_case(source)
        self.assertEqual(refused.returncode, 2, refused.stderr)
        self.assertFalse(refused_report['execution_passed'])
        accepted, accepted_report = self.run_case(source, options=('--artifact-links', 'count'))
        self.assertEqual(accepted.returncode, 0, accepted.stderr)
        self.assertTrue(accepted_report['execution_passed'])
        self.assertFalse(accepted_report['parity_verified'])

    def test_existing_output_is_never_modified(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps({'stages': [{'name': 'target',
                'command': [sys.executable, '-I', '-c', 'pass']}]}))
            output = root / 'existing'
            output.mkdir()
            old = output / 'old.txt'
            old.write_text('preserve me')
            before = old.stat()
            result = subprocess.run([sys.executable, '-I', str(RUNNER), str(manifest), str(output)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(list(output.iterdir()), [old])
            self.assertEqual(old.read_text(), 'preserve me')
            self.assertEqual(old.stat().st_mtime_ns, before.st_mtime_ns)


if __name__ == '__main__':
    unittest.main()
