"""Mutate retained owned bytes; never fabricate Stock or successful writing proof."""
import argparse
import ast
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import sys
import unittest
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('owned_durable_test_subject', Path(__file__).with_name('check_owned_durable.py'))
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)
CAPTURE = OUTPUT = DATA = SENTINEL = None


def fingerprint(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob('*')) if p.is_file() and not p.is_symlink()}


class ActualOwnedRecordTest(unittest.TestCase):
    def copy(self, name):
        destination = OUTPUT / self._testMethodName
        shutil.copytree(CAPTURE / 'work/cases' / name, destination, symlinks=True)
        return destination

    def inspect(self, root, name):
        return checker.inspect_case(root, name, next(r['nonce'] for r in DATA['cases'] if r['case'] == name))

    def report(self, root, operation):
        path = root / 'result.json'
        value = checker.decode(path.read_bytes())
        operation(value)
        path.write_text(json.dumps(value) + '\n')

    def event(self, root, ticket, operation):
        path = root / 'events.jsonl'
        rows = [checker.decode(line) for line in path.read_bytes().splitlines()]
        operation(next(r for r in rows if r['ticket'] == ticket))
        path.write_text(''.join(json.dumps(r) + '\n' for r in rows))

    def late_payload(self, root, ticket, word, value):
        path = root / 'journal.bin'
        raw = bytearray(path.read_bytes())
        offset = next(i for i in range(0, len(raw), 1040) if struct.unpack_from('<I', raw, i)[0] == ticket)
        struct.pack_into('<I', raw, offset + 16 + 4 * word, value)
        path.write_bytes(raw)
        path = root / 'mapping-final.bin'
        raw = bytearray(path.read_bytes())
        struct.pack_into('<I', raw, 128 + ((ticket-1) % 64) * 1040 + 16 + 4 * word, value)
        path.write_bytes(raw)
        self.event(root, ticket, lambda row: row.update({('index', 'thread', 'stage', 'view_raw')[word]: value}))

    def test_six_positive_components_and_existing_refusal(self):
        for name in ('normal', 'late', 'pending', 'abrupt', 'missing', 'nonce'):
            with self.subTest(name=name):
                result = self.inspect(CAPTURE / 'work/cases' / name, name)
                self.assertIs(result['target_abi_qualified'], False)
                self.assertIs(result['process_replay_qualified'], False)
        checker.inspect_preexisting(CAPTURE / 'work/cases/preexisting', SENTINEL)
        self.assertIs(DATA['execution_finished'], False)

    def test_actual_writing_failure_is_still_rejected(self):
        with self.assertRaisesRegex(ValueError, 'collector exit'):
            self.inspect(CAPTURE / 'work/cases/writing', 'writing')

    def test_process_pid_is_bound_to_producer_bytes(self):
        root = self.copy('normal')
        self.report(root, lambda row: row.update(child_pid=row['child_pid'] ^ 4))
        with self.assertRaisesRegex(ValueError, 'attach/view'):
            self.inspect(root, 'normal')

    def test_raw_slot_generation_is_bound(self):
        root = self.copy('normal')
        path = root / 'mapping-final.bin'
        raw = bytearray(path.read_bytes())
        struct.pack_into('<I', raw, 128, struct.unpack_from('<I', raw, 128)[0] ^ 8)
        path.write_bytes(raw)
        with self.assertRaisesRegex(ValueError, 'slot generation'):
            self.inspect(root, 'normal')

    def test_historical_payload_lie_cannot_match_only_its_json(self):
        root = self.copy('normal')
        path = root / 'journal.bin'
        raw = bytearray(path.read_bytes())
        struct.pack_into('<I', raw, 1040 + 16, 1)
        path.write_bytes(raw)
        self.event(root, 2, lambda row: row.update(index=1))
        with self.assertRaisesRegex(ValueError, 'normal consumer order'):
            self.inspect(root, 'normal')

    def test_late_worker_index_is_checked_across_all_three_artifacts(self):
        root = self.copy('late')
        self.late_payload(root, 3, 0, 99)
        with self.assertRaisesRegex(ValueError, 'late worker marker'):
            self.inspect(root, 'late')

    def test_late_worker_stage_is_checked_across_all_three_artifacts(self):
        root = self.copy('late')
        self.late_payload(root, 3, 2, 99)
        with self.assertRaisesRegex(ValueError, 'late worker marker'):
            self.inspect(root, 'late')

    def test_exit_marker_stage_is_checked_across_all_three_artifacts(self):
        root = self.copy('late')
        self.late_payload(root, 2, 2, 99)
        with self.assertRaisesRegex(ValueError, 'late worker marker'):
            self.inspect(root, 'late')

    def test_missing_event_is_rejected(self):
        root = self.copy('normal')
        path = root / 'events.jsonl'
        rows = path.read_bytes().splitlines()
        path.write_bytes(b'\n'.join(rows[1:]) + b'\n')
        with self.assertRaisesRegex(ValueError, 'event count'):
            self.inspect(root, 'normal')

    def test_pending_cannot_be_zeroed_in_summary(self):
        root = self.copy('pending')
        self.report(root, lambda row: row['frozen'].update(reserved=0))
        with self.assertRaisesRegex(ValueError, 'reported/raw frozen'):
            self.inspect(root, 'pending')

    def test_failed_writing_case_cannot_be_relabelled_successful(self):
        root = self.copy('writing')
        (root / 'launcher-exit.txt').write_text('0\n')
        self.report(root, lambda row: row.update(exit_code=37, runtime_ok=True, owned_expectations_met=True))
        with self.assertRaisesRegex(ValueError, 'terminal states'):
            self.inspect(root, 'writing')

    def test_scope_cannot_promote_target_abi(self):
        root = self.copy('normal')
        self.report(root, lambda row: row.update(target_abi_qualified=True))
        with self.assertRaisesRegex(ValueError, 'scope or execution'):
            self.inspect(root, 'normal')

    def test_truncated_journal_is_rejected(self):
        root = self.copy('normal')
        path = root / 'journal.bin'
        path.write_bytes(path.read_bytes()[:-1])
        with self.assertRaisesRegex(ValueError, 'truncation'):
            self.inspect(root, 'normal')

    def test_existing_output_preservation_is_byte_bound(self):
        root = self.copy('preexisting')
        (root / 'journal.bin').write_bytes(SENTINEL + b'changed')
        with self.assertRaisesRegex(ValueError, 'preexisting output changed'):
            checker.inspect_preexisting(root, SENTINEL)


def main():
    global CAPTURE, OUTPUT, DATA, SENTINEL
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    CAPTURE, OUTPUT = args.capture.resolve(strict=True), args.out.resolve()
    if CAPTURE == OUTPUT or CAPTURE in OUTPUT.parents or OUTPUT in CAPTURE.parents:
        parser.error('mutation output must not overlap capture')
    OUTPUT.mkdir(parents=True, exist_ok=False)
    DATA = checker.decode((CAPTURE / 'result.json').read_bytes())
    tree = ast.parse((CAPTURE / 'payload/runner.py').read_text())
    SENTINEL = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign) and
                    any(isinstance(a, ast.Name) and a.id == 'sentinel' for a in n.targets))
    assert hashlib.sha256(SENTINEL).hexdigest() == DATA['preexisting_sha256']
    before = fingerprint(CAPTURE / 'work/cases')
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ActualOwnedRecordTest))
    unchanged = fingerprint(CAPTURE / 'work/cases') == before
    report = {'scope': 'actual component-record mutations; original writing failure remains rejected',
              'tests': result.testsRun, 'failures': len(result.failures), 'errors': len(result.errors),
              'capture_preserved': unchanged, 'passed': result.wasSuccessful() and unchanged,
              'checker_sha256': hashlib.sha256(Path(checker.__file__).read_bytes()).hexdigest()}
    with (OUTPUT / 'result.json').open('x') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
