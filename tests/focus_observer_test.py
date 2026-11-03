#!/usr/bin/env python3
"""Passive observer lifecycle and cleanup; never induces real focus theft."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(add_help=False)
parser.add_argument('--observer', type=Path, default=ROOT / 'build/parity_focus_observer')
options, arguments = parser.parse_known_args()
spec = importlib.util.spec_from_file_location('focus_evidence', ROOT / 'tools/parity/focus_evidence.py')
focus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(focus)


class FocusObserverTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='sl-focus-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def observe(self, code, limit=5):
        path = self.root / 'focus.jsonl'
        command = [str(options.observer.resolve()), str(path),
                   str(Path(sys.executable).resolve()), str(limit), '-I', '-c', code]
        result = subprocess.run(command, capture_output=True, text=True, timeout=12)
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        return result, rows

    def test_short_child_identity_is_recorded_before_execution(self):
        result, rows = self.observe('import sys; assert sys.stdin.read() == ""; print("captured")')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root / 'focus.jsonl.stdout').read_text().strip(), 'captured')
        self.assertEqual(focus.classify(rows)['status'], 'no_owned_activation_observed')
        started = next(row for row in rows if row['kind'] == 'child_started')
        self.assertTrue(started['root']['read_ok'])
        self.assertEqual(started['root']['pid'], started['root']['pgid'])
        self.assertEqual([row['sequence'] for row in rows], list(range(len(rows))))

    def test_nonzero_child_exit_is_preserved(self):
        result, rows = self.observe('raise SystemExit(7)')
        self.assertEqual(result.returncode, 7)
        self.assertEqual(focus.classify(rows)['child_exit'], 7)

    def test_script_interpreter_keeps_kernel_spawn_identity(self):
        script = self.root / 'script'
        script.write_text('#!/bin/sh\nprintf "script ran\\n"\n')
        script.chmod(0o700)
        path = self.root / 'script-focus.jsonl'
        result = subprocess.run([str(options.observer.resolve()), str(path), str(script), '5'],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        self.assertEqual(focus.classify(rows)['status'], 'no_owned_activation_observed')

    def test_timeout_stops_a_child_ignoring_termination(self):
        result, rows = self.observe('import signal,time; signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(30)', .15)
        self.assertEqual(result.returncode, 124, result.stderr)
        self.assertTrue(rows[-1]['timed_out'])
        self.assertEqual(focus.classify(rows)['status'], 'inconclusive')

    def test_orphaned_group_is_killed_and_fails(self):
        pid_file = self.root / 'orphan.pid'
        code = ('import os,signal,time,pathlib\n'
                f'p=pathlib.Path({str(pid_file)!r})\n'
                'if os.fork()==0:\n'
                ' signal.signal(signal.SIGTERM,signal.SIG_IGN)\n'
                ' p.write_text(str(os.getpid()))\n'
                ' time.sleep(30)\n'
                'else:\n'
                ' while not p.exists(): time.sleep(.005)\n'
                ' os._exit(0)\n')
        result, rows = self.observe(code)
        self.assertEqual(result.returncode, 125, result.stderr)
        self.assertTrue(rows[-1]['orphaned_group'])
        self.assertEqual(focus.classify(rows)['status'], 'inconclusive')
        child = pid_file.read_text()
        deadline = time.monotonic() + 1
        while True:
            state = subprocess.run(['ps', '-p', child, '-o', 'state='], capture_output=True, text=True).stdout.strip()
            if not state or state.startswith('Z'):
                break
            self.assertLess(time.monotonic(), deadline, 'orphan still running')
            time.sleep(.01)

    def test_existing_output_and_nonfinite_deadline_are_rejected(self):
        path = self.root / 'existing.jsonl'
        path.write_text('preserve me')
        for deadline in ('1', 'nan', 'inf', '-1', 'garbage'):
            result = subprocess.run([str(options.observer.resolve()), str(path), '/usr/bin/true', deadline],
                                    capture_output=True, text=True, timeout=5)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(path.read_text(), 'preserve me')

    def test_synthetic_focus_changes_and_coverage_gaps(self):
        focus.selftest()


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *arguments])
