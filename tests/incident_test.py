"""Behavioral incident retrieval checks; optional real native lifecycle probes."""
import argparse
import json
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'tools/incident.py'
PROBE = None
STARTUP_PROBE = None


def query(*args, expected=0):
    done = subprocess.run([sys.executable, '-I', str(TOOL), *map(str, args)],
                          capture_output=True, text=True, timeout=15)
    if done.returncode != expected:
        raise AssertionError((done.returncode, done.stdout, done.stderr))
    return json.loads(done.stdout)


def event(identity=1, end=0, result=0, elapsed=0, **extra):
    data = dict(v=1, i=identity, p=0, s=1, k=1, e=end, h=1, r=result, n=0)
    data.update(extra)
    return f'[+{elapsed:.3f} f7] note op1 ' + json.dumps(data, separators=(',', ':')) + '\n'


class Incidents(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.run = self.root / 'run-1'
        self.run.mkdir()
        (self.run / 'meta.txt').write_text('start_utc: 2026-11-03T23:59:58Z\ngit_head: abc123\nargv: --token private-secret\n')
        (self.run / 'result.txt').write_text('status=failed\nseconds=10\nreason=private-secret\n')

    def test_time_overlap_and_operation_reconstruction_with_failure_tail(self):
        begin, end = event(elapsed=3), event(end=1, result=3, elapsed=4)
        (self.run / 'run.log').write_text(begin + end + '--- last events ---\n' + begin + end)
        result = query('--record', self.run, '--operation', 'op1:1', '--since', '2026-11-04T00:00:00Z')
        row = result['records'][0]
        self.assertEqual([r['event'] for r in row['events']], ['begin', 'end'])
        self.assertEqual(row['events'][-1]['outcome'], 'rejected')
        self.assertTrue(row['events'][0]['utc'].startswith('2026-11-04'))
        self.assertNotIn('private-secret', json.dumps(result))
        self.assertEqual(len(row['sources'][-1]['sha256']), 64)
        self.assertEqual(query('--record', self.run, '--since', '2026-11-05T00:00:00Z')['records'], [])

    def test_missing_result_and_unknown_times_stay_unknown(self):
        (self.run / 'meta.txt').unlink()
        (self.run / 'result.txt').unlink()
        row = query('--record', self.run, '--since', '2026-11-04T00:00:00Z')['records'][0]
        self.assertEqual(row['status'], 'unknown')
        self.assertEqual(row['time_match'], 'unknown')
        self.assertIn('liveness/outcome unknown', ' '.join(row['problems']))

    def test_torn_and_conflicting_operation_records(self):
        (self.run / 'run.log').write_text(event() + event(end=1, result=1) + event(end=1, result=4) +
                                         '[+1.000 f8] note op1 {"v":1,\n')
        row = query('--record', self.run, '--events')['records'][0]
        self.assertIn('conflicting duplicate', ' '.join(row['problems']))
        self.assertIn('malformed op1', ' '.join(row['problems']))

    def test_mismatched_scope_unknown_outcome_and_duplicate_identity_are_unknown(self):
        (self.run / 'run.log').write_text(event() + event(end=1, s=2))
        (self.run / 'telemetry.json').write_text('{"schema":1,"io_failures":false}')
        problems = query('--record', self.run, '--events')['records'][0]['problems']
        self.assertIn('identity differs', ' '.join(problems))
        self.assertIn('without a known outcome', ' '.join(problems))
        self.assertIn('malformed recorder health', ' '.join(problems))
        with (self.run / 'meta.txt').open('a') as output:
            output.write('start_utc: 2026-11-04T00:00:00Z\n')
        query('--record', self.run, expected=2)

    def test_missing_end_and_budget_loss_are_explicit(self):
        (self.run / 'run.log').write_text(event())
        (self.run / 'telemetry.json').write_text(json.dumps({'schema': 1, 'dropped_lines': 8, 'io_failures': 1}))
        row = query('--record', self.run, '--events')['records'][0]
        self.assertIn('missing begin/end', ' '.join(row['problems']))
        self.assertIn('budget dropped', ' '.join(row['problems']))
        self.assertIn('IO failures', ' '.join(row['problems']))

    def test_symlinks_fifo_and_entry_bounds(self):
        (self.root / 'alias').symlink_to(self.run, target_is_directory=True)
        result = query('--root', self.root)
        self.assertEqual(len(result['records']), 1)
        query('--record', self.root / 'alias', expected=2)
        query('--root', self.root, '--max-entries', 1, expected=2)
        import os
        (self.run / 'meta.txt').unlink()
        os.mkfifo(self.run / 'meta.txt')
        query('--record', self.run, expected=2)

    def test_byte_bounds_and_bad_json_are_not_clean_empty_history(self):
        query('--record', self.run, '--max-bytes', 1, expected=2)
        (self.run / 'result.json').write_text('{"status":"ok","status":"failed"}')
        query('--record', self.run, expected=2)
        (self.run / 'result.json').write_text('{"status":NaN}')
        query('--record', self.run, expected=2)
        (self.run / 'result.json').write_text('{"seconds":1e999}')
        query('--record', self.run, expected=2)
        query('--record', self.root / 'absent', expected=2)

    def test_exact_request_lookup_omits_unrelated_private_payload(self):
        identity = 'a' * 64
        document = self.root / 'review.json'
        document.write_text(json.dumps({'result': {'status': 'incomplete'}, 'environment': {'TOKEN': 'private-secret'},
            'reviews': [{'request_id': identity, 'expired': True, 'response_consumed': False,
                         'response_file_exists': True, 'response_mtime_ns': 123, 'reason': 'private-secret'}]}))
        row = query('--record', document, '--request-id', identity)['records'][0]
        self.assertEqual(row['requests'], [{'request_id': identity, 'expired': True, 'response_consumed': False,
                                          'response_file_exists': True, 'response_mtime_ns': 123}])
        self.assertNotIn('private-secret', json.dumps(row))

    def test_console_result_distinguishes_failed_and_unexecuted_stages(self):
        (self.run / 'result.json').write_text(json.dumps({'execution_passed': False,
            'required_stages': ['private-name', 'remaining'],
            'stages': {'private-name': {'completed': True, 'actual_exit': 17, 'expected_exit': 0}}}))
        row = query('--record', self.run)['records'][0]
        self.assertEqual(row['stages'], [
            {'operation': 'stage:0', 'state': 'completed', 'actual_exit': 17, 'expected_exit': 0},
            {'operation': 'stage:1', 'state': 'not_started'}])
        self.assertNotIn('private-name', json.dumps(row))

    def test_invalid_time_and_unscoped_operation_refuse(self):
        for args in [('--since', '2026-11-04'), ('--operation', 'op1:1'), ('--limit', '0')]:
            done = subprocess.run([sys.executable, '-I', str(TOOL), *args], capture_output=True, timeout=10)
            self.assertEqual(done.returncode, 2)

    def test_stage_journal_survives_without_final_report_and_refuses_wrong_attempt(self):
        (self.run / 'attempt.json').write_text(json.dumps({'attempt_id': 'a'*32, 'started_utc': '2026-11-04T00:00:00Z'}))
        (self.run / 'meta.txt').unlink()
        (self.run / 'result.txt').unlink()
        row = {'schema': 1, 'attempt_id': 'a'*32, 'event': 'begin', 'stage': 0,
               'utc': '2026-11-04T00:00:01Z', 'exit_code': None}
        (self.run / 'stages.jsonl').write_text(json.dumps(row)+'\n')
        result = query('--record', self.run, '--operation', 'stage:0')['records'][0]
        self.assertEqual(result['events'][0]['event'], 'begin')
        self.assertIn('incomplete/interrupted', ' '.join(result['problems']))
        row['attempt_id'] = 'b'*32
        (self.run / 'stages.jsonl').write_text(json.dumps(row)+'\n')
        result = query('--record', self.run, '--events')['records'][0]
        self.assertIn('binding mismatch', ' '.join(result['problems']))

    def test_actual_native_save_retry_log_budget_and_io_failure(self):
        if PROBE is None:
            self.skipTest('pass --probe for actual native lifecycle probes')
        for mode in ('normal', 'cap', 'io', 'stream', 'capture-fault'):
            root = self.root / mode
            root.mkdir()
            done = subprocess.run([str(PROBE), '--record', str(root), mode], capture_output=True, text=True, timeout=30)
            self.assertEqual(done.returncode, 0, done.stderr)
            directory = Path(done.stdout.splitlines()[0])
            row = query('--record', directory, '--events')['records'][0]
            self.assertEqual(row['status'], 'ok')
            if mode == 'normal':
                self.assertTrue(any(e['outcome'] == 'open_unavailable' for e in row['events']))
                self.assertTrue(any(e['kind'] == 'file_close' and e['bytes_written'] > 0 for e in row['events']))
            elif mode == 'cap':
                self.assertGreater(row['health']['dropped_lines'], 0)
            elif mode == 'io':
                self.assertGreater(row['health']['io_failures'], 0)
                import os
                diagnosis = subprocess.run([sys.executable, '-I', str(TOOL.with_name('diagnose.py')), str(directory)],
                    capture_output=True, text=True, timeout=15,
                    env=dict(os.environ, SLRR_CRASH_REPORT_DIR='/nonexistent'))
                self.assertEqual(diagnosis.returncode, 0, diagnosis.stderr)
                self.assertIn('EVIDENCE INCOMPLETE: recorder io_failures=', diagnosis.stdout)
            elif mode == 'stream':
                self.assertGreaterEqual(row['health']['dropped_context_lines'], 936)
            else:
                self.assertEqual(row['health']['capture_unavailable'], 1)
                self.assertGreater(row['health']['io_failures'], 0)
            self.assertNotIn('private-player-token', json.dumps(row))

    def test_logging_startup_failure_does_not_change_headless_application_result(self):
        if STARTUP_PROBE is None:
            self.skipTest('pass --startup-probe with private resources for application startup control')
        import os
        baseline = subprocess.run([str(STARTUP_PROBE), '--no-input', '--no-run-log'],
                                  capture_output=True, text=True, timeout=30)
        self.assertEqual(baseline.returncode, 0, baseline.stderr)
        blocked = self.root / 'not-a-directory'
        blocked.write_text('occupied')
        result = subprocess.run([str(STARTUP_PROBE), '--no-input'], capture_output=True,
                                text=True, timeout=30, env=dict(os.environ, SLRR_RUN_DIR=str(blocked)))
        self.assertEqual(result.returncode, baseline.returncode, result.stderr)
        self.assertIn('Config.init =', result.stdout)
        self.assertIn('run log:', result.stderr)

    def test_actual_killed_process_keeps_checkpoint_and_incomplete_operation(self):
        if PROBE is None:
            self.skipTest('pass --probe for actual SIGKILL survival')
        process = subprocess.Popen([str(PROBE), '--record', str(self.root), 'hold'],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        try:
            # poll() is not a deadline: communicate enforces termination below;
            # a readiness pipe read is bounded with select, including failed startup.
            import select
            import time
            import os
            output = b''
            deadline = time.monotonic() + 20
            while output.count(b'\n') < 2 and time.monotonic() < deadline:
                if select.select([process.stdout], [], [], 0.1)[0]:
                    chunk = os.read(process.stdout.fileno(), 4096)
                    self.assertTrue(chunk, 'native fixture exited before readiness')
                    output += chunk
            lines = output.decode().splitlines()
            self.assertEqual(len(lines), 2, 'readiness deadline exceeded')
            directory = Path(lines[0])
            process.send_signal(signal.SIGKILL)
            process.communicate(timeout=10)
            row = query('--record', directory, '--events')['records'][0]
            self.assertEqual(row['status'], 'unknown')
            self.assertIn('missing begin/end', ' '.join(row['problems']))
            self.assertEqual(row['health']['phase'], 'checkpoint')
            self.assertTrue((directory / 'flight-1.log').is_file())
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate(timeout=10)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', type=Path)
    parser.add_argument('--startup-probe', type=Path)
    args, rest = parser.parse_known_args()
    PROBE = args.probe.resolve() if args.probe else None
    STARTUP_PROBE = args.startup_probe.resolve() if args.startup_probe else None
    unittest.main(argv=[sys.argv[0], *rest])
