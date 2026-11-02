"""Capacity observations never reserve or reclaim retained allowance."""
import hashlib
import contextlib
import importlib.util
from pathlib import Path
import sqlite3
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('capacity', Path(__file__).with_name('capacity.py'))
capacity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capacity)


class CapacityTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.parent = self.root / 'capture'
        self.parent.mkdir()
        (self.parent / 'payload').write_bytes(b'abc')
        self.ledger = self.root / 'retention.sqlite'
        info = self.parent.stat()
        with contextlib.closing(sqlite3.connect(self.ledger)) as db:
            db.execute('CREATE TABLE retention(identity TEXT PRIMARY KEY,root TEXT,bytes INTEGER)')
            db.execute('CREATE TABLE policy(id INTEGER PRIMARY KEY,allowance INTEGER)')
            db.execute('INSERT INTO policy VALUES(1,100)')
            db.execute('INSERT INTO retention VALUES(?,?,?)',
                       (f'{info.st_dev}:{info.st_ino}', str(self.parent), 20))
            db.commit()

    def test_real_bytes_and_ledger_unchanged(self):
        before = hashlib.sha256(self.ledger.read_bytes()).digest()
        result = capacity.report(self.ledger, self.parent)
        self.assertEqual(result['new_reservation_capacity_bytes'], 80)
        self.assertEqual(result['parent']['remaining_bytes'], 17)
        self.assertFalse(result['admission_granted'])
        self.assertTrue(result['parent']['stock_prepaid_supported'])
        self.assertEqual(before, hashlib.sha256(self.ledger.read_bytes()).digest())

    def test_deleted_payload_does_not_release_ledger_charge(self):
        (self.parent / 'payload').unlink()
        result = capacity.report(self.ledger, self.parent)
        self.assertEqual(result['charged_bytes'], 20)
        self.assertEqual(result['parent']['remaining_bytes'], 20)

    def test_identity_replacement_refuses(self):
        self.parent.rename(self.root / 'old')
        self.parent.mkdir()
        with self.assertRaises(ValueError):
            capacity.report(self.ledger, self.parent)

    def test_aliased_parent_refuses(self):
        alias = self.root / 'alias'
        alias.symlink_to(self.parent)
        with self.assertRaises(ValueError):
            capacity.report(self.ledger, alias)

    def test_incomplete_scan_refuses(self):
        with self.assertRaises(ValueError):
            capacity.usage(self.parent, max_entries=0)

    def test_over_budget_is_not_free_capacity(self):
        (self.parent / 'payload').write_bytes(b'x' * 21)
        result = capacity.report(self.ledger, self.parent)
        self.assertTrue(result['parent']['over_budget'])
        self.assertEqual(result['parent']['native_child_limit_bytes'], 0)


if __name__ == '__main__':
    unittest.main()
