"""Corruption controls for retained prefix deltas; no Stock execution claim."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('archive', Path(__file__).with_name('prefix_archive.py'))
archive = importlib.util.module_from_spec(spec)
spec.loader.exec_module(archive)


class PrefixArchiveTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.old = {'user.reg': self.identity(b'old'), 'unchanged.dll': self.identity(b'stable')}
        self.new = dict(self.old, **{'user.reg': self.identity(b'new')})
        for name, value in [('prefix-seed.tsv', self.old), ('prefix-before.tsv', self.old),
                            ('prefix-after.tsv', self.new), ('prefix-changed.tsv', {'user.reg': self.new['user.reg']}),
                            ('prefix-archive.tsv', {'user.reg': self.new['user.reg']})]:
            (self.root / name).write_bytes(archive.encode_manifest(value))
        (self.root / 'prefix-prepared.tsv').write_text('complete\t1\nseed_mount_readonly\t1\nseed_bytes_match\t1\nprivate_inode_identities\t1\nentry_count\t2\ntarget_fs_type\ttmpfs\ntarget_mount_executable\t1\n')
        (self.root / 'prefix-finished.tsv').write_text('complete\t1\nwriters_quiescent\t1\nchanged_count\t1\nchanged_regular_bytes\t3\n')
        (self.root / 'prefix-quiescence.txt').write_text('0 0\n')
        (self.root / 'prefix-inodes.tsv').write_text('unchanged.dll\t1:2\t2:3\nuser.reg\t1:3\t2:4\n')
        (self.root / 'prefix-changes').mkdir()
        (self.root / 'prefix-changes/user.reg').write_bytes(b'new')

    @staticmethod
    def identity(data):
        return {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}

    def audit(self):
        return archive.validate_capture(self.root, self.old)

    def test_valid_delta_preserves_full_after_manifest(self):
        _, finished, after = self.audit()
        self.assertTrue(finished['complete'])
        self.assertEqual(after, self.new)

    def test_noexec_prefix_refuses(self):
        path = self.root / 'prefix-prepared.tsv'
        path.write_text(path.read_text().replace('target_mount_executable\t1', 'target_mount_executable\t0'))
        with self.assertRaisesRegex(ValueError, 'prepare incomplete'):
            self.audit()

    def test_readonly_audit_preserves_all_retained_bytes(self):
        before = {str(p.relative_to(self.root)): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        _, finished, _ = archive.validate_capture(self.root, self.old, write_reports=False)
        self.assertTrue(finished['complete'])
        self.assertEqual({str(p.relative_to(self.root)): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}, before)

    def test_changed_bytes_cannot_pass_from_status_booleans(self):
        (self.root / 'prefix-changes/user.reg').write_bytes(b'bad')
        with self.assertRaisesRegex(ValueError, 'hash'):
            self.audit()

    def test_missing_changed_file_refuses(self):
        (self.root / 'prefix-changes/user.reg').unlink()
        with self.assertRaisesRegex(ValueError, 'regular set'):
            self.audit()

    def test_unreported_extra_file_refuses(self):
        (self.root / 'prefix-changes/unreported').write_bytes(b'new')
        with self.assertRaises(ValueError):
            self.audit()

    def test_alias_to_external_archive_refuses(self):
        path = self.root / 'prefix-changes/user.reg'
        path.rename(self.root / 'external')
        path.symlink_to(self.root / 'external')
        with self.assertRaisesRegex(ValueError, 'special/link'):
            self.audit()

    def test_missing_quiescence_refuses(self):
        (self.root / 'prefix-quiescence.txt').write_text('0 124\n')
        with self.assertRaisesRegex(ValueError, 'quiescent'):
            self.audit()

    def test_false_private_inode_claim_refuses(self):
        (self.root / 'prefix-inodes.tsv').write_text('unchanged.dll\t1:2\t1:2\nuser.reg\t1:3\t2:4\n')
        with self.assertRaisesRegex(ValueError, 'independent'):
            self.audit()

    def test_omitted_change_refuses(self):
        (self.root / 'prefix-changed.tsv').write_bytes(b'')
        with self.assertRaisesRegex(ValueError, 'changed metadata'):
            self.audit()


if __name__ == '__main__':
    unittest.main(verbosity=2)
