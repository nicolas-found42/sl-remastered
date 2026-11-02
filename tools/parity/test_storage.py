import hashlib
import contextlib
import fcntl
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import subprocess
import shutil
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('storage', Path(__file__).with_name('storage.py'))
s = importlib.util.module_from_spec(spec); spec.loader.exec_module(s)


@unittest.skipUnless(sys.platform == 'darwin', 'APFS implementation')
class StorageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve()
        self.files = self.root/'files'; self.files.mkdir()
        self.state = self.root/'state'

    def tearDown(self):
        self.temp.cleanup()

    def test_duplicate_paths_bytes_metadata_and_future_writes_survive(self):
        payload = b'evidence\x00' * 262144
        a, b = self.files/'a', self.files/'b'
        a.write_bytes(payload); b.write_bytes(payload)
        subprocess.run(['xattr', '-w', 'user.source-only', 'source', str(a)], check=True)
        b.chmod(0o640); os.utime(b, ns=(123000000000, 456000000000))
        subprocess.run(['xattr', '-w', 'user.parity', 'original-target', str(b)], check=True)
        original = b.stat()
        report = s.compact([self.files], self.state, apply=True)
        self.assertEqual(report['replaced'], 1)
        self.assertEqual(a.read_bytes(), b.read_bytes())
        self.assertEqual(b.stat().st_mtime_ns, original.st_mtime_ns)
        self.assertEqual(b.stat().st_mode, original.st_mode)
        self.assertEqual(subprocess.check_output(['xattr', '-p', 'user.parity', str(b)]).strip(), b'original-target')
        self.assertNotIn('user.source-only', subprocess.check_output(['xattr', str(b)], text=True))
        self.assertNotEqual(a.stat().st_ino, b.stat().st_ino)
        self.assertNotEqual(b.stat().st_ino, original.st_ino)
        with b.open('r+b') as stream: stream.write(b'CHANGED')
        self.assertEqual(a.read_bytes(), payload)
        self.assertNotEqual(a.read_bytes(), b.read_bytes())
        events = [json.loads(line) for p in self.state.glob('journal-*') for line in p.read_text().splitlines()]
        self.assertEqual([e['event'] for e in events], ['prepared', 'replaced'])

    def test_dry_run_and_unequal_bytes_do_not_replace(self):
        a, b = self.files/'a', self.files/'b'
        a.write_bytes(b'A'*1024**2); b.write_bytes(b'A'*1024**2)
        inode = b.stat().st_ino
        r = s.compact([self.files], self.state)
        self.assertEqual(r['duplicates'], 1); self.assertEqual(r['replaced'], 0)
        self.assertEqual(b.stat().st_ino, inode)
        b.write_bytes(b'B'*1024**2)
        r = s.compact([self.files], self.state, apply=True)
        self.assertEqual(r['replaced'], 0)

    def test_read_only_directory_is_preserved_and_reported(self):
        a, b = self.files/'a', self.files/'b'
        a.write_bytes(b'A'*1024**2); b.write_bytes(a.read_bytes())
        inode = b.stat().st_ino
        self.files.chmod(0o555)
        try:
            if os.access(self.files, os.W_OK): self.skipTest('privileged user bypasses permissions')
            r = s.compact([self.files], self.state, apply=True)
            self.assertTrue(r['complete']); self.assertEqual(r['skipped_metadata'], 1)
            self.assertEqual(b.stat().st_ino, inode)
            self.assertEqual(self.files.stat().st_mode & 0o777, 0o555)
        finally:
            self.files.chmod(0o755)

    def test_reject_hardlink_symlink_and_wrong_digest(self):
        a, b = self.files/'a', self.files/'b'
        a.write_bytes(b'A'*1024**2); b.write_bytes(b'B'*1024**2)
        with (self.root/'journal').open('w') as journal:
            with self.assertRaisesRegex(ValueError, 'content changed'):
                s.cow_replace(a, b, hashlib.sha256(a.read_bytes()).hexdigest(), journal)
        self.assertEqual(b.read_bytes(), b'B'*1024**2)
        c = self.files/'c'; c.symlink_to(a)
        with self.assertRaises(ValueError): s.checked(c)
        c.unlink(); os.link(a, c)
        with self.assertRaises(ValueError): s.checked(a)

    def test_stale_canonical_is_replaced_in_index_without_data_loss(self):
        a, b = self.files/'a', self.files/'b'
        a.write_bytes(b'A'*1024**2)
        s.compact([self.files], self.state)
        a.write_bytes(b'B'*1024**2); b.write_bytes(b'A'*1024**2)
        r = s.compact([self.files], self.state, apply=True)
        self.assertEqual(r['replaced'], 0)
        self.assertEqual(b.read_bytes(), b'A'*1024**2)

    def test_journal_failure_never_replaces_target(self):
        a, b = self.files/'a', self.files/'b'
        a.write_bytes(b'A'*1024**2); b.write_bytes(a.read_bytes())
        before = b.stat().st_ino
        journal = mock.Mock(); journal.write.side_effect = OSError('disk full')
        with self.assertRaisesRegex(OSError, 'disk full'):
            s.cow_replace(a, b, hashlib.sha256(a.read_bytes()).hexdigest(), journal)
        self.assertEqual(b.stat().st_ino, before)
        self.assertEqual(b.read_bytes(), a.read_bytes())
        self.assertEqual(list(self.files.glob('.storage-clone-*')), [])

    def test_clone_fsync_failure_leaves_old_target(self):
        a, b = self.files/'a', self.files/'b'
        a.write_bytes(b'A'*1024**2); b.write_bytes(a.read_bytes())
        before = b.stat().st_ino
        with (self.root/'journal').open('w') as journal, mock.patch.object(s.os, 'fsync', side_effect=OSError('sync failed')):
            with self.assertRaisesRegex(OSError, 'sync failed'):
                s.cow_replace(a, b, hashlib.sha256(a.read_bytes()).hexdigest(), journal)
        self.assertEqual(b.stat().st_ino, before)
        self.assertEqual((self.root/'journal').read_bytes(), b'')

    def test_after_replace_failure_reports_committed_identity(self):
        a, b = self.files/'a', self.files/'b'
        a.write_bytes(b'A'*1024**2); b.write_bytes(a.read_bytes())
        before = b.stat().st_ino
        real_sync = os.fsync
        calls = []
        def fail_parent(fd):
            calls.append(fd)
            if len(calls) == 3: raise OSError('parent sync failed')
            return real_sync(fd)
        with (self.root/'journal').open('w') as journal, mock.patch.object(s.os, 'fsync', side_effect=fail_parent):
            with self.assertRaises(s.CommittedCloneError) as error:
                s.cow_replace(a, b, hashlib.sha256(a.read_bytes()).hexdigest(), journal)
        self.assertEqual(error.exception.record['new_inode'], b.stat().st_ino)
        self.assertNotEqual(before, b.stat().st_ino)
        self.assertEqual(a.read_bytes(), b.read_bytes())
        self.assertEqual(json.loads((self.root/'journal').read_text())['event'], 'prepared')

    def test_second_journal_write_failure_reports_replacement(self):
        a, b = self.files/'a', self.files/'b'
        a.write_bytes(b'A'*1024**2); b.write_bytes(a.read_bytes())
        with (self.root/'journal').open('w') as stream:
            proxy = mock.Mock(wraps=stream)
            writes = []
            def write(value):
                writes.append(value)
                if len(writes) == 2: raise OSError('second write failed')
                return stream.write(value)
            proxy.write.side_effect = write
            with self.assertRaises(s.CommittedCloneError) as error:
                s.cow_replace(a, b, hashlib.sha256(a.read_bytes()).hexdigest(), proxy)
        self.assertEqual(error.exception.record['new_inode'], b.stat().st_ino)
        self.assertEqual(json.loads((self.root/'journal').read_text())['event'], 'prepared')

    def test_last_file_deadline_cannot_report_complete(self):
        (self.files/'a').write_bytes(b'A'*1024**2)
        ticks = iter([0,0,2,2])
        with mock.patch.object(s.time, 'monotonic', side_effect=lambda: next(ticks, 3)):
            with self.assertRaises(TimeoutError):
                s.compact([self.files], self.state, seconds=1)
        report = json.loads(next(self.state.glob('report-*')).read_text())
        self.assertFalse(report['complete'])

    def test_changed_original_attributes_and_unknown_additions_refuse(self):
        a, b = self.files/'a', self.files/'b'
        a.write_bytes(b'A'*1024**2); b.write_bytes(a.read_bytes())
        subprocess.run(['xattr', '-w', 'user.parity', 'original', str(b)], check=True)
        original_inode = b.stat().st_ino
        original_reader = s.xattrs
        for variant in ('changed', 'missing', 'unknown-extra', 'changed-provenance'):
            calls = []
            def altered(path):
                attrs = original_reader(path)
                if path == b and variant == 'changed-provenance':
                    attrs[b'com.apple.provenance'] = b'original-provenance'
                if path.name.startswith('.storage-clone-'):
                    calls.append(path)
                    if len(calls) >= 2:
                        if variant == 'changed': attrs[b'user.parity'] = b'changed'
                        elif variant == 'missing': attrs.pop(b'user.parity', None)
                        elif variant == 'unknown-extra': attrs[b'user.unknown'] = b'extra'
                        else: attrs[b'com.apple.provenance'] = b'changed-provenance'
                return attrs
            with self.subTest(variant=variant), (self.root/('journal-'+variant)).open('w') as journal:
                with mock.patch.object(s, 'xattrs', side_effect=altered):
                    with self.assertRaises(s.IneligibleClone):
                        s.cow_replace(a, b, hashlib.sha256(a.read_bytes()).hexdigest(), journal)
            self.assertEqual(b.stat().st_ino, original_inode)

    def test_ineligible_metadata_is_left_untouched_and_counted(self):
        a, b = self.files/'a', self.files/'b'
        a.write_bytes(b'A'*1024**2); b.write_bytes(a.read_bytes())
        inode = b.stat().st_ino
        with mock.patch.object(s, 'cow_replace', side_effect=s.IneligibleClone('original attributes differ')):
            report = s.compact([self.files], self.state, apply=True)
        self.assertTrue(report['complete'])
        self.assertEqual(report['replaced'], 0)
        self.assertEqual(report['skipped_metadata'], 1)
        self.assertEqual(b.stat().st_ino, inode)

    def test_protected_descendant_and_state_symlink_refuse(self):
        repo = self.root/'repo'; ref = repo/'ref'; ref.mkdir(parents=True)
        protected = ref/'a'; protected.write_bytes(b'A'*1024**2)
        normal = self.root/'b'; normal.write_bytes(protected.read_bytes())
        inode = protected.stat().st_ino
        with mock.patch.object(s, 'ROOT', repo):
            with self.assertRaisesRegex(ValueError, 'protected storage'):
                s.compact([self.files], ref/'state', apply=True)
            s.compact([self.root], self.state, apply=True)
        self.assertEqual(protected.stat().st_ino, inode)
        db = self.state/'objects.sqlite'; db.unlink(); db.symlink_to(protected)
        with self.assertRaisesRegex(ValueError, 'state file alias'):
            s.compact([self.files], self.state, apply=True)


class RetentionTest(unittest.TestCase):
    def test_capture_and_compaction_locks_exclude_each_other(self):
        pool_spec = importlib.util.spec_from_file_location('locking_pool', Path(__file__).with_name('resource_pool.py'))
        pool = importlib.util.module_from_spec(pool_spec); pool_spec.loader.exec_module(pool)
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(); (root/'tools/parity').mkdir(parents=True)
            shutil.copy2(s.__file__, root/'tools/parity/storage.py')
            out = root/'local/out'; out.mkdir(parents=True)
            state = root/'local/storage-index'
            with mock.patch.object(pool, 'ROOT', root):
                budget = pool.ArtifactBudget([out], 1024)
                with self.assertRaises(BlockingIOError): s.compact([out], state)
                budget.close()
                self.assertTrue(s.compact([out], state)['complete'])
                with (state/'lock').open('r+') as lock:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    other = root/'local/other'; other.mkdir()
                    with self.assertRaises(pool.ArtifactLimit): pool.ArtifactBudget([other], 1024)
                self.assertEqual(s.reserve_capture([out], 1024, state)['reserved_bytes'], 1024)

    def test_stock_entrypoint_refuses_before_launch_when_retention_is_full(self):
        stock_spec = importlib.util.spec_from_file_location('quota_stock', Path(__file__).with_name('run_stock.py'))
        stock = importlib.util.module_from_spec(stock_spec); stock_spec.loader.exec_module(stock)
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(); repo = root/'repo'
            (repo/'tools/parity').mkdir(parents=True)
            shutil.copy2(s.__file__, repo/'tools/parity/storage.py')
            for part in ('local/old','game','disc'): (repo/part).mkdir(parents=True)
            binary = repo/'stock.exe'; binary.write_bytes(b'owned nonexecuted input')
            texture = repo/'texture'; texture.write_bytes(b'owned')
            args = ['run_stock.py', '--source', str(repo/'game'), '--stock', str(binary),
                    '--texture-table', str(texture), '--disc', str(repo/'disc'),
                    '--out', str(repo/'local/new'), '--focus-observer', sys.executable]
            admission = mock.Mock(); admission.acquire.return_value = contextlib.nullcontext(mock.Mock())
            image = mock.Mock(stdout=json.dumps([{'Id':'sha256:owned', 'Architecture':'arm64'}]).encode())
            with mock.patch.object(stock.pool, 'ROOT', repo), mock.patch.object(stock, 'ROOT', repo):
                stock.pool.ArtifactBudget([repo/'local/old'], 64*1024**3)
                with mock.patch.object(stock, 'STOCK_SHA', hashlib.sha256(binary.read_bytes()).hexdigest()), \
                     mock.patch.object(stock.sys, 'argv', args), \
                     mock.patch.object(stock.shutil, 'which', return_value='/bin/true'), \
                     mock.patch.object(stock.pool, 'ResourcePool', return_value=admission), \
                     mock.patch.object(stock.subprocess, 'run', return_value=image) as commands, \
                     mock.patch.object(stock.sys, 'stderr', io.StringIO()):
                    with self.assertRaises(SystemExit) as stopped: stock.main()
                self.assertEqual(stopped.exception.code, 2)
                self.assertEqual(commands.call_count, 1)
                self.assertEqual(commands.call_args.args[0][1:3], ['image','inspect'])
                self.assertFalse((repo/'local/new/config.json').exists())

    def test_renaming_then_recreating_output_keeps_old_charge(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(); out = root/'out'; out.mkdir(); state = root/'state'
            s.reserve_capture([out], 6, state, 10)
            out.rename(root/'archived'); out.mkdir()
            with self.assertRaisesRegex(ValueError, 'cumulative'):
                s.reserve_capture([out], 6, state, 10)
            self.assertEqual(s.reserve_capture([root/'archived'], 6, state, 10)['reserved_bytes'], 6)

    def test_real_artifact_budget_rejects_aggregate_before_execution(self):
        pool_spec = importlib.util.spec_from_file_location('tested_pool', Path(__file__).with_name('resource_pool.py'))
        pool = importlib.util.module_from_spec(pool_spec); pool_spec.loader.exec_module(pool)
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve()
            (root/'tools/parity').mkdir(parents=True)
            shutil.copy2(s.__file__, root/'tools/parity/storage.py')
            for x in ('a','b'): (root/'local'/x).mkdir(parents=True)
            with mock.patch.object(pool, 'ROOT', root):
                pool.ArtifactBudget([root/'local/a'], 40*1024**3)
                with self.assertRaisesRegex(pool.ArtifactLimit, 'cumulative capture'):
                    pool.ArtifactBudget([root/'local/b'], 40*1024**3)

    def test_retained_capture_outside_repository_cannot_bypass_allowance(self):
        pool_spec = importlib.util.spec_from_file_location('outside_pool', Path(__file__).with_name('resource_pool.py'))
        pool = importlib.util.module_from_spec(pool_spec); pool_spec.loader.exec_module(pool)
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(); repo = root/'repo'
            (repo/'tools/parity').mkdir(parents=True)
            shutil.copy2(s.__file__, repo/'tools/parity/storage.py')
            for x in ('a','b'): (root/x).mkdir()
            with mock.patch.object(pool, 'ROOT', repo):
                pool.ArtifactBudget([root/'a'], 40*1024**3, retained=True)
                with self.assertRaisesRegex(pool.ArtifactLimit, 'cumulative capture'):
                    pool.ArtifactBudget([root/'b'], 40*1024**3, retained=True)

    def test_independent_processes_cannot_both_overbook(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(); (root/'a').mkdir(); (root/'b').mkdir()
            code = ('import importlib.util,sys\nfrom pathlib import Path\n'
                    'sp=importlib.util.spec_from_file_location("s",sys.argv[1]);s=importlib.util.module_from_spec(sp);sp.loader.exec_module(s)\n'
                    'try: s.reserve_capture([Path(sys.argv[2])],6,Path(sys.argv[3]),10)\n'
                    'except ValueError: sys.exit(2)\n')
            children = [subprocess.Popen([sys.executable, '-I', '-c', code, str(Path(s.__file__)),
                         str(root/x), str(root/'state')], stdout=subprocess.PIPE, stderr=subprocess.PIPE) for x in ('a','b')]
            exits = []
            for child in children:
                _, error = child.communicate(timeout=10)
                self.assertEqual(error, b''); exits.append(child.returncode)
            self.assertEqual(sorted(exits), [0,2])

    def test_many_small_runs_are_capped_and_restart_does_not_reset(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(); state = root/'state'
            for i in range(3): (root/str(i)).mkdir()
            s.reserve_capture([root/'0'], 4, state, 10)
            s.reserve_capture([root/'1'], 4, state, 10)
            with self.assertRaisesRegex(ValueError, 'cumulative'):
                s.reserve_capture([root/'2'], 4, state, 10)
            # Fresh SQLite connection, same durable accounting; failed admission
            # does not commit a phantom reservation.
            self.assertEqual(s.reserve_capture([root/'2'], 2, state, 10)['reserved_bytes'], 10)
            self.assertEqual(s.reserve_capture([root/'0'], 1, state, 10)['reserved_bytes'], 10)
            with self.assertRaisesRegex(ValueError, 'policy changed'):
                s.reserve_capture([root/'0'], 1, state, 20)

    def test_nested_root_cannot_hide_reservation(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve(); out = root/'out'; out.mkdir(); (out/'child').mkdir()
            s.reserve_capture([out], 4, root/'state', 10)
            with self.assertRaisesRegex(ValueError, 'overlapping'):
                s.reserve_capture([out/'child'], 4, root/'state', 10)


if __name__ == '__main__': unittest.main()
