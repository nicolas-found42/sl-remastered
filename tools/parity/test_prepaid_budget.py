"""Tiny scratch-ledger adversarial controls; no real allowance charges here."""
import contextlib
import fcntl
import hashlib
import importlib.util
import json
import multiprocessing
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest
import time
from unittest import mock
import importlib.util
prepaid_spec = importlib.util.spec_from_file_location('prepaid_budget', Path(__file__).with_name('prepaid_budget.py'))
prepaid = importlib.util.module_from_spec(prepaid_spec)
prepaid_spec.loader.exec_module(prepaid)

PrepaidCapture, Refused = prepaid.PrepaidCapture, prepaid.Refused

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('scratch_storage', ROOT/'tools/parity/storage.py')
storage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(storage)


def writer_probe(parent, state, queue):
    try:
        with PrepaidCapture(parent, Path(parent)/'other', state, 64, child_max=16, allowance=128):
            queue.put('admitted')
    except BlockingIOError:
        queue.put('blocked')


class Tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='slrr-prepaid-prototype-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.parent, self.state = self.root/'parent', self.root/'state'
        self.parent.mkdir()
        (self.parent/'old').write_bytes(b'prior evidence')
        storage.reserve_capture([self.parent], 64, self.state, limit=128)
        (self.state/'lock').touch()
        self.db_before = (self.state/'retention.sqlite').read_bytes()

    def budget(self, **changes):
        args = dict(parent=self.parent, child=self.parent/'fresh', state=self.state,
                    parent_max=64, child_max=16, allowance=128)
        args.update(changes)
        return PrepaidCapture(**args)

    def assertLedgerUnchanged(self):
        self.assertEqual(self.db_before, (self.state/'retention.sqlite').read_bytes())

    def test_positive_separate_caps_readonly_ledger_and_atime_evidence(self):
        old = (self.parent/'old').stat()
        with self.budget() as budget:
            (budget.child/'capture').write_bytes(b'new capture')
            sample = budget.check()
            self.assertEqual((sample['logical_bytes'], sample['child_bytes']), (25, 11))
            self.assertEqual((sample['parent_max'], sample['child_max']), (64, 16))
        report = budget.report()
        self.assertTrue(report['samples'][-1]['complete'])
        self.assertEqual(old.st_ino, (self.parent/'old').stat().st_ino)
        self.assertEqual(old.st_mtime_ns, (self.parent/'old').stat().st_mtime_ns)
        self.assertLedgerUnchanged()

    def test_default_child_cap_is_one_gib(self):
        import inspect
        self.assertEqual(inspect.signature(PrepaidCapture).parameters['child_max'].default, 1024**3)

    def test_unknown_parent_refused_without_new_reservation(self):
        unknown = self.root/'unknown'; unknown.mkdir()
        with self.assertRaisesRegex(Refused, 'unknown'):
            self.budget(parent=unknown, child=unknown/'fresh')
        self.assertLedgerUnchanged()

    def test_recreated_parent_different_inode_refused(self):
        held = os.open(self.parent, os.O_RDONLY)
        try:
            self.parent.rename(self.root/'old-parent')
            self.parent.mkdir()
            with self.assertRaisesRegex(Refused, 'unknown'):
                self.budget()
        finally:
            os.close(held)
        self.assertLedgerUnchanged()

    def test_renamed_registered_inode_refused(self):
        moved = self.root/'moved'; self.parent.rename(moved)
        with self.assertRaisesRegex(Refused, 'renamed'):
            self.budget(parent=moved, child=moved/'fresh')
        self.assertLedgerUnchanged()

    def test_symlink_parent_refused(self):
        alias = self.root/'alias'; alias.symlink_to(self.parent, target_is_directory=True)
        with self.assertRaisesRegex(Refused, 'canonical'):
            self.budget(parent=alias, child=alias/'fresh')

    def test_max_and_allowance_mismatch_refused(self):
        for changed in (dict(parent_max=65), dict(allowance=129)):
            with self.subTest(changed=changed), self.assertRaises(Refused):
                self.budget(**changed)
        self.assertLedgerUnchanged()

    def test_child_must_be_unique_direct_descendant(self):
        for child in (self.parent/'old', self.root/'escape', self.parent/'nested'/'fresh'):
            with self.subTest(child=child), self.assertRaises(Refused):
                self.budget(child=child)

    def test_overlapping_ledger_entry_refused(self):
        with contextlib.closing(sqlite3.connect(self.state/'retention.sqlite')) as db:
            db.execute('INSERT INTO retention VALUES (?,?,?)', ('hostile', str(self.parent/'nested'), 1))
            db.commit()
        with self.assertRaisesRegex(Refused, 'overlapping'):
            self.budget()

    def test_concurrent_writer_refused_in_another_process(self):
        with self.budget():
            ctx = multiprocessing.get_context('spawn'); queue = ctx.Queue()
            process = ctx.Process(target=writer_probe, args=(str(self.parent), str(self.state), queue))
            process.start(); process.join(10)
            if process.is_alive(): process.terminate(); process.join(3)
            self.assertEqual(process.exitcode, 0)
            self.assertEqual(queue.get(timeout=3), 'blocked')

    def test_maintenance_excludes_capture_and_capture_excludes_maintenance(self):
        fd = os.open(self.state/'lock', os.O_RDWR)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaises(BlockingIOError): self.budget()
            fcntl.flock(fd, fcntl.LOCK_UN)
            with self.budget():
                with self.assertRaises(BlockingIOError): fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally: os.close(fd)

    def test_parent_and_child_overflow_are_distinct(self):
        b = self.budget()
        try:
            (b.child/'large').write_bytes(bytes(17))
            with self.assertRaisesRegex(Refused, 'child budget'): b.check()
        finally: b.close()
        other = self.parent/'second'
        b = self.budget(child=other)
        try:
            (b.child/'within-child').write_bytes(bytes(16))
            (self.parent/'outside-child').write_bytes(bytes(40))
            with self.assertRaisesRegex(Refused, 'parent budget'): b.check()
        finally: b.close()

    def test_final_old_preservation_does_not_spend_deadline_reading_new_artifact_bytes(self):
        budget = self.budget(scan_seconds=.25)
        self.addCleanup(budget.close)
        capture = budget.child/'capture'
        capture.write_bytes(b'new capture')
        capture_identity = prepaid.identity(capture.stat())
        original_read = os.read

        def slow_new_artifact(fd, count):
            if prepaid.identity(os.fstat(fd)) == capture_identity:
                time.sleep(.30)
            return original_read(fd, count)

        with mock.patch.object(prepaid.os, 'read', slow_new_artifact):
            sample = budget.check('final')
        self.assertTrue(sample['complete'])
        self.assertEqual((sample['logical_bytes'], sample['child_bytes']), (25, 11))
        self.assertLedgerUnchanged()

    def test_final_checks_old_bytes_even_when_metadata_witness_is_unchanged(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        old = self.parent/'old'
        bound = budget.old['old']['metadata']
        old.write_bytes(b'other evidence')
        original_metadata = prepaid.metadata

        def metadata_witness(info):
            if prepaid.identity(info) == (bound['dev'], bound['ino']):
                return dict(bound)
            return original_metadata(info)

        # Counterfactual metadata witness isolates the content assertion: this
        # must refuse on the actual changed SHA, not merely an mtime/ctime delta.
        with mock.patch.object(prepaid, 'metadata', metadata_witness):
            with self.assertRaisesRegex(Refused, 'old bytes changed'):
                budget.check('final')

    def test_final_still_enforces_the_new_child_cap(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        (budget.child/'large').write_bytes(bytes(17))
        with self.assertRaisesRegex(Refused, 'child budget'):
            budget.check('final')

    def test_child_accounting_matches_only_its_exact_direct_component(self):
        (self.parent/'freshness').write_bytes(bytes(20))
        nested = self.parent/'elsewhere'/'fresh'
        nested.mkdir(parents=True)
        (nested/'outside').write_bytes(bytes(7))
        budget = self.budget()
        self.addCleanup(budget.close)
        (budget.child/'capture').write_bytes(bytes(11))
        sample = budget.check('final')
        self.assertEqual((sample['logical_bytes'], sample['child_bytes']), (52, 11))
        (self.parent/'fresh-other').write_bytes(b'x')
        with self.assertRaisesRegex(Refused, 'membership'):
            budget.check('final')

    def test_attribute_values_are_fresh_after_repeated_observations(self):
        path = self.parent/'old'
        key = 'com.example.prepaid-fresh' if sys.platform=='darwin' else 'user.prepaid-fresh'
        def change(value):
            if hasattr(os, 'setxattr'):
                os.setxattr(path, key, value.encode())
            else:
                subprocess.run(['/usr/bin/xattr','-w',key,value,str(path)],check=True)
        change('first')
        before = prepaid.attributes(path)
        change('second')
        after = prepaid.attributes(path)
        lookup = key if hasattr(os, 'listxattr') else key.encode().hex()
        self.assertEqual(before[lookup], hashlib.sha256(b'first').hexdigest())
        self.assertEqual(after[lookup], hashlib.sha256(b'second').hexdigest())

    @unittest.skipUnless(sys.platform=='darwin' and not hasattr(os, 'listxattr'),
                         'specific to the Darwin ctypes xattr fallback')
    def test_periodic_guard_does_not_rebind_libc_for_every_observation(self):
        import ctypes
        budget = self.budget(scan_seconds=.25)
        self.addCleanup(budget.close)
        original = ctypes.CDLL
        def slow_bind(*args, **kwargs):
            time.sleep(.30)
            return original(*args, **kwargs)
        with mock.patch.object(ctypes, 'CDLL', slow_bind):
            sample = budget.check()
        self.assertTrue(sample['complete'])
        self.assertLedgerUnchanged()

    def test_old_tree_overflow_refuses_before_child_creation(self):
        (self.parent/'too-large').write_bytes(bytes(65))
        with self.assertRaisesRegex(Refused, 'parent budget'): self.budget()
        self.assertFalse((self.parent/'fresh').exists())

    def test_old_bytes_metadata_and_membership_mutation_refuse(self):
        for mutation in ('bytes', 'mode', 'xattr', 'new-sibling', 'link-target'):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temp:
                parent = Path(temp).resolve()/'parent'; parent.mkdir()
                state = Path(temp).resolve()/'state'
                (parent/'old').write_bytes(b'original')
                (parent/'link').symlink_to('old')
                storage.reserve_capture([parent], 64, state, limit=128); (state/'lock').touch()
                b = self.budget(parent=parent, child=parent/'fresh', state=state)
                try:
                    if mutation == 'bytes': (parent/'old').write_bytes(b'changed!')
                    elif mutation == 'mode': (parent/'old').chmod(0o400)
                    elif mutation == 'xattr':
                        if hasattr(os, 'setxattr'): os.setxattr(parent/'old', 'user.prototype' if sys.platform!='darwin' else 'com.example.prototype', b'changed')
                        else: subprocess.run(['/usr/bin/xattr', '-w', 'com.example.prototype', 'changed', str(parent/'old')], check=True)
                    elif mutation == 'new-sibling': (parent/'outside').write_bytes(b'new')
                    else: (parent/'link').unlink(); (parent/'link').symlink_to('other')
                    with self.assertRaises(Refused): b.check('final')
                finally: b.close()

    def test_root_and_child_replacement_during_capture_refuse(self):
        b = self.budget()
        try:
            b.child.rename(self.parent/'moved-child'); b.child.mkdir()
            with self.assertRaisesRegex(Refused, 'child replaced'): b.check()
        finally: b.close()

    def test_parent_replacement_during_capture_refuses(self):
        b = self.budget()
        try:
            self.parent.rename(self.root/'old-parent'); self.parent.mkdir()
            with self.assertRaisesRegex(Refused, 'parent replaced'): b.check()
        finally: b.close()

    def test_lock_and_state_replacement_refuse(self):
        for target in ('lock', 'state'):
            with self.subTest(target=target):
                b = self.budget(child=self.parent/('fresh-'+target))
                try:
                    if target == 'lock':
                        (self.state/'lock').rename(self.state/'old-lock'); (self.state/'lock').touch()
                    else:
                        self.state.rename(self.root/'old-state'); self.state.mkdir()
                    with self.assertRaises(Refused): b.check()
                finally: b.close()
                if target == 'lock':
                    (self.state/'lock').unlink(); (self.state/'old-lock').rename(self.state/'lock')

    def test_state_cannot_create_lock_files_inside_old_evidence(self):
        nested = self.parent/'state'; nested.mkdir()
        with self.assertRaisesRegex(Refused, 'outside evidence'):
            self.budget(state=nested)

    def test_nonfinite_scan_deadlines_refuse(self):
        for seconds in [float('inf'), float('nan'), 0, -1]:
            with self.subTest(seconds=seconds), self.assertRaises(Refused):
                self.budget(scan_seconds=seconds)

    def test_delayed_last_empty_directory_attributes_refuse(self):
        budget = self.budget(scan_seconds=.02)
        self.addCleanup(budget.close)
        original = prepaid.attributes
        def delayed(path):
            result = original(path)
            if path == budget.child:
                time.sleep(.04)
            return result
        with mock.patch.object(prepaid, 'attributes', delayed):
            with self.assertRaisesRegex(Refused, 'deadline'):
                budget.check()

    def test_equivalent_ledger_inode_replacement_refuses(self):
        budget = self.budget(); self.addCleanup(budget.close)
        db = self.state/'retention.sqlite'
        replacement = self.state/'replacement.sqlite'
        replacement.write_bytes(db.read_bytes())
        replacement.replace(db)
        with self.assertRaisesRegex(Refused, 'ledger replaced'):
            budget.check()

    def test_ledger_replacement_during_read_refuses(self):
        budget = self.budget(); self.addCleanup(budget.close)
        original = prepaid.sqlite3.connect
        dbpath = self.state/'retention.sqlite'
        class ReplacingConnection:
            def __init__(self, db): self.db = db
            def execute(self, sql):
                cursor = self.db.execute(sql)
                if sql == 'SELECT identity,root,bytes FROM retention':
                    replacement = self_path/'replacement.sqlite'
                    replacement.write_bytes(dbpath.read_bytes())
                    replacement.replace(dbpath)
                return cursor
            def close(self): self.db.close()
        self_path = self.state
        with mock.patch.object(prepaid.sqlite3, 'connect',
                lambda *args, **kwargs: ReplacingConnection(original(*args, **kwargs))):
            with self.assertRaisesRegex(Refused, 'ledger replaced'):
                budget.check()

    def test_same_inode_ledger_transaction_allowed(self):
        budget = self.budget(); self.addCleanup(budget.close)
        dbpath = self.state/'retention.sqlite'; before = dbpath.stat().st_ino
        with sqlite3.connect(dbpath) as db:
            db.execute('INSERT INTO retention(identity,root,bytes) VALUES(?,?,?)',
                ('other:fixture', str(self.root/'other-root'), 1))
        self.assertEqual(before, dbpath.stat().st_ino)
        budget.check()

    def test_adapter_translates_admission_and_periodic_failure(self):
        class ArtifactLimit(RuntimeError): pass
        with self.assertRaises(ArtifactLimit):
            prepaid.LeaseBudgetAdapter.admit(ArtifactLimit, self.parent, self.parent/'fresh',
                self.state, 63, child_max=16, allowance=128)
        budget = self.budget(); self.addCleanup(budget.close)
        adapter = prepaid.LeaseBudgetAdapter(budget, ArtifactLimit)
        (budget.child/'too-large').write_bytes(b'x'*17)
        with self.assertRaises(ArtifactLimit): adapter.check()
        self.assertFalse(adapter.last_sample['complete'])


class NativeLifecycleTests(unittest.TestCase):
    def setUp(self):
        # These three cases exercise the in-process visible fake-Lease seam.
        # Do not inherit the real-capture gate's background observer selection.
        environment = mock.patch.dict(os.environ, {'SLRR_PROBE_BACKGROUND': '0'})
        environment.start()
        self.addCleanup(environment.stop)
        self.temp = tempfile.TemporaryDirectory(prefix='slrr-prepaid-lifecycle-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.parent, self.state = self.root/'parent', self.root/'state'
        self.parent.mkdir(); (self.parent/'old').write_bytes(b'old evidence')
        storage.reserve_capture([self.parent], 1024**2, self.state)
        (self.state/'lock').touch()
        spec = importlib.util.spec_from_file_location('native_prepaid_lifecycle', ROOT/'tools/parity/run_native.py')
        self.native = importlib.util.module_from_spec(spec); spec.loader.exec_module(self.native)
        self.native.STORAGE_STATE = self.state
        self.game, self.save = self.root/'game', self.root/'save'
        self.game.mkdir(); self.save.mkdir(); (self.game/'system.rpk').write_bytes(b'toy')
        self.probe = self.root/'probe'; self.probe.write_bytes(b'unused toy probe')
        self.script = self.root/'script'; self.script.write_text('0 quit\n')
        self.out = self.parent/'new'
        self.argv = ['run_native', str(self.out), '--probe', str(self.probe), '--game', str(self.game),
            '--save', str(self.save), '--script', str(self.script), '--visible',
            '--artifact-budget-root', str(self.parent), '--artifact-parent-max-mib', '1',
            '--max-artifact-mib', '1']
        self.lease = mock.Mock(description={'fixture': 'no runtime launched'})
        def fake_run(*args, **kwargs):
            kwargs['output'].write('startup probe: milestone menu process=1.0 loop=1.0 frame=1\n')
            return subprocess.CompletedProcess([], 0)
        self.lease.run.side_effect = fake_run

    def test_fake_lease_finalizes_all_outputs_and_old_bytes(self):
        with mock.patch.object(sys, 'argv', self.argv):
            self.assertEqual(self.native.run(self.lease), 0)
        result = json.loads((self.out/'result.json').read_text())
        self.assertEqual(result['artifact_finalization'], 'verified')
        self.assertTrue((self.out/'prepaid-audit.json').is_file())
        self.assertEqual((self.parent/'old').read_bytes(), b'old evidence')
        with sqlite3.connect(self.state/'retention.sqlite') as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM retention').fetchone()[0], 1)

    def test_late_final_scan_failure_rewrites_provisional_pass(self):
        original = self.native.prepaid_module.LeaseBudgetAdapter.check
        finals = []
        def failing(adapter, stage='periodic'):
            if stage == 'final':
                finals.append(stage)
                if len(finals) == 2:
                    raise self.native.pool_module.ArtifactLimit('second final scan fixture failure')
            return original(adapter, stage)
        with mock.patch.object(sys, 'argv', self.argv), mock.patch.object(
                self.native.prepaid_module.LeaseBudgetAdapter, 'check', failing):
            with self.assertRaises(self.native.pool_module.ArtifactLimit):
                self.native.run(self.lease)
        result = json.loads((self.out/'result.json').read_text())
        self.assertEqual(result['artifact_finalization'], 'failed')
        self.assertIn('artifact_error', result)
        self.assertTrue((self.out/'prepaid-failure.json').is_file())

    def test_partial_constructor_retains_owned_child_failure(self):
        def fail(*args, **kwargs):
            raise self.native.prepaid_module.Refused('prelaunch fixture refusal')
        with mock.patch.object(sys, 'argv', self.argv), mock.patch.object(
                self.native.prepaid_module.PrepaidCapture, 'check', fail):
            self.assertEqual(self.native.run(self.lease), 1)
        self.assertFalse(self.lease.run.called)
        result = json.loads((self.out/'result.json').read_text())
        self.assertEqual(result['artifact_finalization'], 'failed')
        self.assertTrue((self.out/'prepaid-admission-error.json').is_file())


if __name__ == '__main__':
    unittest.main(verbosity=2)
