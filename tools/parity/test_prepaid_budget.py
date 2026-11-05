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
import threading
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

    @contextlib.contextmanager
    def changing_directory(self, target, mutate, *, repeatedly=False):
        original = Path.lstat
        calls = mutations = 0

        def raced(path, *args, **kwargs):
            nonlocal calls, mutations
            info = original(path, *args, **kwargs)
            if path == target:
                calls += 1
                # Reproduce a real namespace mutation between the scanner's
                # ending path and open-descriptor metadata observations.
                if calls % 2 == 0 and (repeatedly or not mutations):
                    mutations += 1
                    mutate(mutations)
            return info

        with mock.patch.object(Path, 'lstat', raced):
            yield

    def test_scanner_waits_for_bounded_preparation_batch(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        entered, scanning, release = threading.Event(), threading.Event(), threading.Event()
        errors, samples = [], []
        original = prepaid.acquire_preparation
        original_scan = budget._scan
        def guarded_scan(**kwargs):
            if not release.is_set():
                raise RuntimeError('scanner entered an active copy batch')
            return original_scan(**kwargs)
        def observe(fd, deadline):
            if threading.current_thread().name == 'scanner':
                scanning.set()
            return original(fd, deadline)
        def copy_batch():
            try:
                with prepaid.PreparationWriter(budget.preparation_binding()) as writer:
                    with writer.batch():
                        (budget.child/'first').write_bytes(b'first')
                        entered.set()
                        if not release.wait(2):
                            raise RuntimeError('test barrier timeout')
                        (budget.child/'second').write_bytes(b'second')
            except BaseException as error:
                errors.append(error)
        def scan():
            try:
                samples.append(budget.check())
            except BaseException as error:
                errors.append(error)
        with mock.patch.object(prepaid, 'acquire_preparation', observe), \
                mock.patch.object(budget, '_scan', guarded_scan):
            writer = threading.Thread(target=copy_batch)
            scanner = threading.Thread(target=scan, name='scanner')
            writer.start()
            self.assertTrue(entered.wait(2))
            scanner.start()
            try:
                self.assertTrue(scanning.wait(2))
                self.assertEqual(samples, [])
            finally:
                release.set()
                writer.join(2)
                scanner.join(2)
        self.assertFalse(writer.is_alive() or scanner.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(len(samples), 1)
        self.assertEqual(samples[0]['child_bytes'], 11)
        self.assertEqual(len(samples[0]['scan_attempts']), 1)
        self.assertLedgerUnchanged()

    @unittest.skipUnless(sys.platform == 'darwin', 'APFS clone adapter requires Darwin cp -c')
    def test_real_clone_cooperates_with_periodic_observer(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        spec = importlib.util.spec_from_file_location('stock_copy_test', Path(__file__).with_name('run_stock.py'))
        stock = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(stock)
        source = self.root/'source'
        source.mkdir()
        for i in range(7):
            (source/str(i)).write_bytes(b'x')
        attempting, samples, errors = threading.Event(), [], []
        original_acquire, original_run = prepaid.acquire_preparation, stock.subprocess.run
        def observe(fd, deadline):
            if threading.current_thread().name == 'copy-observer':
                attempting.set()
            return original_acquire(fd, deadline)
        def scan():
            try:
                samples.append(budget.check())
            except BaseException as error:
                errors.append(error)
        observer = threading.Thread(target=scan, name='copy-observer')
        def cp(*args, **kwargs):
            observer.start()
            if not attempting.wait(2):
                raise RuntimeError('observer did not attempt bounded acquisition')
            return original_run(*args, **kwargs)
        with mock.patch.object(prepaid, 'acquire_preparation', observe), \
                mock.patch.object(stock.subprocess, 'run', cp), (self.root/'copy.log').open('wb') as log:
            try:
                stock.clone_preparation(source, budget.child/'game', stock.manifest(source),
                                        budget.preparation_binding(), log)
            finally:
                if observer.ident is not None:
                    observer.join(2)
        self.assertFalse(observer.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(len(samples), 1)
        self.assertEqual(samples[0]['child_bytes'], 7)
        self.assertEqual(len(samples[0]['scan_attempts']), 1)
        self.assertEqual(stock.manifest(budget.child/'game'), stock.manifest(source))
        self.assertLedgerUnchanged()

    def test_preparation_caller_deadline_refuses_before_mutation(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        with prepaid.PreparationWriter(budget.preparation_binding()) as writer:
            clock = [10.0]
            acquired = []
            def late_acquisition(fd, deadline):
                acquired.append(deadline)
                clock[0] = 10.5
            with mock.patch.object(prepaid.time, 'monotonic', lambda: clock[0]), \
                    mock.patch.object(prepaid, 'acquire_preparation', late_acquisition):
                with self.assertRaisesRegex(Refused, 'deadline bound'):
                    with writer.batch(deadline=10.5):
                        (budget.child/'late').write_bytes(b'forbidden')
            self.assertEqual(acquired, [10.5])
            self.assertFalse((budget.child/'late').exists())
            with mock.patch.object(prepaid.time, 'monotonic', lambda: 20.0):
                with self.assertRaisesRegex(Refused, 'deadline bound'):
                    with writer.batch(deadline=19.0):
                        (budget.child/'expired').mkdir()
            self.assertFalse((budget.child/'expired').exists())
        self.assertTrue(budget.check()['complete'])

    def test_preparation_replaced_lock_or_child_refuses_before_mutation(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        binding = budget.preparation_binding()
        with prepaid.PreparationWriter(binding) as writer:
            lock = Path(binding['path'])
            saved = lock.with_name(lock.name+'.saved')
            lock.rename(saved)
            lock.touch()
            try:
                with self.assertRaisesRegex(Refused, 'preparation lock replaced'):
                    with writer.batch():
                        (budget.child/'unbound').touch()
            finally:
                lock.unlink()
                saved.rename(lock)
            displaced = self.parent/'displaced'
            budget.child.rename(displaced)
            budget.child.mkdir()
            try:
                with self.assertRaisesRegex(Refused, 'preparation child replaced'):
                    with writer.batch():
                        (budget.child/'unbound').touch()
                self.assertFalse((budget.child/'unbound').exists())
            finally:
                budget.child.rmdir()
                displaced.rename(budget.child)
        # Replacing the child also changes the parent namespace; the old-tree
        # guard must continue to refuse that transition.
        with self.assertRaisesRegex(Refused, 'old metadata changed'):
            budget.check()

    def test_preparation_wait_uses_existing_scan_deadline(self):
        budget = self.budget(scan_seconds=.05)
        self.addCleanup(budget.close)
        clock = [0.0]
        def acquired_after_deadline(fd, deadline):
            self.assertEqual(deadline, .05)
            clock[0] = .051
        with mock.patch.object(prepaid.time, 'monotonic', lambda: clock[0]), \
                mock.patch.object(prepaid, 'acquire_preparation', acquired_after_deadline):
            with self.assertRaisesRegex(Refused, 'deadline bound'):
                budget.check()
        self.assertFalse(budget.last_sample['complete'])
        self.assertEqual(budget.scan_seconds, .05)

    def test_preparation_writer_failure_releases_lock(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        with prepaid.PreparationWriter(budget.preparation_binding()) as writer:
            with self.assertRaisesRegex(RuntimeError, 'copy failed'):
                with writer.batch():
                    (budget.child/'copied').write_bytes(b'copy')
                    raise RuntimeError('copy failed')
            self.assertTrue(budget.check()['complete'])
        self.assertLedgerUnchanged()

    def test_preparation_writer_does_not_hide_old_bytes_or_overflow(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        with prepaid.PreparationWriter(budget.preparation_binding()) as writer:
            with writer.batch():
                (budget.child/'copied').write_bytes(bytes(17))
            with self.assertRaisesRegex(Refused, 'child budget'):
                budget.check()
            with writer.batch():
                (budget.child/'copied').unlink()
                (self.parent/'old').write_bytes(b'other evidence')
            with self.assertRaisesRegex(Refused, 'old metadata changed|old bytes changed'):
                budget.check('final')

    def test_one_new_child_directory_race_restarts_complete_scan(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        with self.changing_directory(budget.child,
                lambda _: (budget.child/'capture').write_bytes(b'new capture')):
            sample = budget.check('final')
        self.assertTrue(sample['complete'])
        self.assertEqual((sample['logical_bytes'], sample['child_bytes']), (25, 11))
        self.assertEqual(len(sample['scan_attempts']), 2)
        self.assertFalse(sample['scan_attempts'][0]['complete'])
        self.assertEqual(sample['scan_attempts'][0]['relative_path'], 'fresh')
        self.assertIn('directory changed scanning', sample['scan_attempts'][0]['reason'])
        self.assertTrue(sample['scan_attempts'][1]['complete'])
        self.assertLedgerUnchanged()

    def test_nested_new_child_directory_race_is_fully_recounted(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        nested = budget.child/'nested'; nested.mkdir()
        with self.changing_directory(nested, lambda _: (nested/'capture').write_bytes(b'x')):
            sample = budget.check('final')
        self.assertEqual((sample['logical_bytes'], sample['child_bytes']), (15, 1))
        self.assertEqual(sample['scan_attempts'][0]['relative_path'], 'fresh/nested')
        self.assertTrue(sample['complete'])

    def test_child_permission_change_is_not_a_namespace_retry(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        with self.changing_directory(budget.child,
                lambda _: budget.child.chmod(budget.child.stat().st_mode ^ 0o010)):
            with self.assertRaisesRegex(Refused, 'directory changed scanning'):
                budget.check()
        self.assertEqual(len(budget.last_sample['scan_attempts']), 1)
        self.assertFalse(budget.last_sample['complete'])

    def test_new_child_retry_cannot_reset_entry_limit(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        budget.max_entries = 5
        with self.changing_directory(budget.child,
                lambda _: (budget.child/'capture').write_bytes(b'x')):
            with self.assertRaisesRegex(Refused, 'scan entry/deadline bound'):
                budget.check()
        self.assertEqual(len(budget.last_sample['scan_attempts']), 2)
        self.assertFalse(budget.last_sample['complete'])

    def test_new_child_retry_does_not_hide_old_evidence_mutation(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        def mutate(_):
            (budget.child/'capture').write_bytes(b'x')
            (self.parent/'old').write_bytes(b'changed bytes!')
        with self.changing_directory(budget.child, mutate):
            with self.assertRaisesRegex(Refused, 'old (metadata|bytes) changed'):
                budget.check('final')
        self.assertFalse(budget.last_sample['complete'])

    def test_preexisting_directory_race_does_not_retry(self):
        older = self.parent/'retained'; older.mkdir()
        budget = self.budget()
        self.addCleanup(budget.close)
        with self.changing_directory(older, lambda _: (older/'new').write_bytes(b'x')):
            with self.assertRaisesRegex(Refused, 'directory changed scanning'):
                budget.check()
        self.assertEqual(len(budget.last_sample['scan_attempts']), 1)
        self.assertFalse(budget.last_sample['complete'])

    def test_child_directory_retry_cannot_hide_overflow_or_special_file(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        with self.changing_directory(budget.child,
                lambda _: (budget.child/'large').write_bytes(bytes(17))):
            with self.assertRaisesRegex(Refused, 'child budget exceeded'):
                budget.check()
        (budget.child/'large').unlink()
        with self.changing_directory(budget.child,
                lambda _: os.mkfifo(budget.child/'pipe')):
            with self.assertRaisesRegex(Refused, 'special artifact refused'):
                budget.check()
        self.assertFalse(budget.last_sample['complete'])
        self.assertLedgerUnchanged()

    def test_continuous_child_churn_has_finite_attempts(self):
        budget = self.budget()
        self.addCleanup(budget.close)
        with self.changing_directory(budget.child,
                lambda n: (budget.child/str(n)).write_bytes(b'x'), repeatedly=True):
            with self.assertRaisesRegex(Refused, 'child directory scan retry bound'):
                budget.check()
        self.assertEqual(len(budget.last_sample['scan_attempts']), 3)
        self.assertTrue(all(not a['complete'] for a in budget.last_sample['scan_attempts']))

    def test_child_retries_share_the_original_scan_deadline(self):
        budget = self.budget(scan_seconds=.05)
        self.addCleanup(budget.close)
        clock = [100.0]
        def mutate(n):
            (budget.child/str(n)).write_bytes(b'x')
            clock[0] += .03
        with mock.patch.object(prepaid.time, 'monotonic', lambda: clock[0]), \
                self.changing_directory(budget.child, mutate, repeatedly=True):
            with self.assertRaisesRegex(Refused, 'deadline bound'):
                budget.check()
        self.assertEqual(len(budget.last_sample['scan_attempts']), 2)
        self.assertFalse(budget.last_sample['complete'])
        self.assertAlmostEqual(budget.last_sample['scan_attempts'][-1]['elapsed'], .06)
        self.assertEqual(budget.scan_seconds, .05)

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
