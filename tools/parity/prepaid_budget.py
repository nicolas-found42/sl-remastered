"""Prepaid-root composite retention budget.

Readonly ledger lookup. Tests use a separate tiny scratch ledger. No reservation
or allowance mutation occurs here. Cooperative locks exclude cooperating capture
writers and maintenance only; this is sampled evidence, never a hard quota.
"""
import contextlib
import fcntl
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import sqlite3
import stat
import time
from urllib.parse import quote

DEFAULT_CHILD_MAX = 1024**3
_xattr_reader = None


class Refused(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise Refused(message)


def canonical(path):
    path = Path(path).absolute()
    require(path.resolve(strict=True) == path, 'canonical existing path required')
    return path


def identity(info):
    return info.st_dev, info.st_ino


def metadata(info):
    # Reading may change atime. Never restore timestamps (would change ctime).
    return dict(dev=info.st_dev, ino=info.st_ino, mode=info.st_mode,
                uid=info.st_uid, gid=info.st_gid, size=info.st_size,
                mtime_ns=info.st_mtime_ns, ctime_ns=info.st_ctime_ns,
                nlink=info.st_nlink, flags=getattr(info, 'st_flags', 0))


def attributes(path):
    global _xattr_reader
    if not hasattr(os, 'listxattr'):
        # This Python build omits xattr wrappers. Reuse the existing readonly
        # Darwin no-follow inventory, never omit attributes silently.
        if _xattr_reader is None:
            source = Path(__file__).with_name('storage.py')
            spec = importlib.util.spec_from_file_location('prepaid_xattr_storage', source)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            _xattr_reader = module.xattrs
        return {name.hex(): hashlib.sha256(value).hexdigest()
                for name, value in _xattr_reader(path).items()}
    return {name: hashlib.sha256(os.getxattr(path, name, follow_symlinks=False)).hexdigest()
            for name in sorted(os.listxattr(path, follow_symlinks=False))}


class PrepaidCapture:
    """Composite budget requiring LeaseBudgetAdapter at the Lease seam.

    Writer lock lives outside the evidence root. Parent is held open throughout.
    Child receives a separate lower cap, without any nested retention row.
    Root-directory metadata transition is recorded immediately around mkdir;
    all subsequent checks require it stable except observational read atime.
    """
    def __init__(self, parent, child, state, parent_max, *, child_max=DEFAULT_CHILD_MAX,
                 allowance=64*1024**3, max_entries=100000, scan_seconds=5):
        self.descriptors = []
        self.samples = []
        self.lock_identities = []
        self.last_sample = {}
        self.old = None
        try:
            self.parent, self.state = canonical(parent), canonical(state)
            require(self.parent.is_dir() and self.state.is_dir(), 'directory required')
            require(self.state != self.parent and self.parent not in self.state.parents,
                    'state must be outside evidence parent')
            self.child = Path(child).absolute()
            require(self.child.parent == self.parent and self.child.name not in ('', '.', '..'),
                    'unique direct child required')
            require(not self.child.exists() and not self.child.is_symlink(), 'output already exists')
            require(not any(part in ('.git', 'ref') for part in self.parent.parts), 'protected parent')
            require(type(parent_max) is int and parent_max > 0 and type(child_max) is int
                    and 0 < child_max <= min(DEFAULT_CHILD_MAX, parent_max), 'separate child cap')
            require(type(allowance) is int and allowance > 0 and type(max_entries) is int
                    and max_entries > 0 and isinstance(scan_seconds, (int, float))
                    and math.isfinite(scan_seconds) and scan_seconds > 0, 'scan/policy bounds')
            self.parent_max, self.child_max, self.allowance = parent_max, child_max, allowance
            self.max_entries, self.scan_seconds = max_entries, scan_seconds
            self.state_fd = os.open(self.state, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
            self.descriptors.append(self.state_fd)
            self.state_identity = identity(os.fstat(self.state_fd))
            self.maintenance = self._lock(self.state/'lock', fcntl.LOCK_SH, create=False)
            self.parent_fd = os.open(self.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
            self.descriptors.append(self.parent_fd)
            self.parent_identity = identity(os.fstat(self.parent_fd))
            self.writer = self._lock(self.state/('prepaid-%s-%s.lock' % self.parent_identity),
                                     fcntl.LOCK_EX, create=True)
            self.ledger_path = canonical(self.state/'retention.sqlite')
            self.ledger_fd = os.open(self.ledger_path, os.O_RDONLY | os.O_NOFOLLOW)
            self.descriptors.append(self.ledger_fd)
            self.ledger_identity = identity(os.fstat(self.ledger_fd))
            self.ledger = self.lookup()
            first = self.scan(hashes=True)
            require(first['logical_bytes'] <= self.parent_max, 'parent budget exceeded before copy')
            self.old = first['records']
            self.root_before_child = self.old['.']
            self.child.mkdir(exist_ok=False)
            self.child_identity = identity(self.child.stat())
            self.root_after_child = self._record(self.parent, self.parent.stat(), hashes=False)
            changed = {key for key in self.root_before_child['metadata']
                       if self.root_before_child['metadata'][key] != self.root_after_child['metadata'][key]}
            require(changed <= {'size', 'mtime_ns', 'ctime_ns', 'nlink'}
                    and self.root_before_child['xattrs'] == self.root_after_child['xattrs'],
                    'unexpected parent metadata transition')
            self.old['.'] = self.root_after_child
            self.check('prelaunch')
        except BaseException as error:
            error.prepaid_diagnostics = dict(parent=str(getattr(self, 'parent', parent)),
                child=str(getattr(self, 'child', child)),
                child_identity=getattr(self, 'child_identity', None),
                samples=self.samples, admission_complete=False, reason=str(error))
            self.close()
            raise

    def _lock(self, path, mode, *, create):
        fd = os.open(path.name, os.O_RDWR | os.O_NOFOLLOW | (os.O_CREAT if create else 0),
                     0o600, dir_fd=self.state_fd)
        try:
            info = os.fstat(fd)
            require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, 'lock identity/type')
            fcntl.flock(fd, mode | fcntl.LOCK_NB)
            require(identity(path.lstat()) == identity(info), 'lock replaced')
        except BaseException:
            os.close(fd)
            raise
        self.descriptors.append(fd)
        self.lock_identities.append((path, fd, identity(info)))
        return fd

    def lookup(self):
        require(canonical(self.parent) == self.parent and
                identity(self.parent.stat()) == self.parent_identity, 'parent replaced')
        require(canonical(self.state) == self.state and identity(self.state.stat()) == self.state_identity,
                'state alias/replacement')
        for path, descriptor, bound in self.lock_identities:
            require(identity(path.lstat()) == identity(os.fstat(descriptor)) == bound,
                    'lock replaced')
        dbpath = canonical(self.state/'retention.sqlite')
        self._ledger_bound(dbpath)
        with contextlib.closing(sqlite3.connect('file:'+quote(str(dbpath))+'?mode=ro', uri=True)) as db:
            db.execute('BEGIN')
            require([x[1] for x in db.execute('PRAGMA table_info(retention)')] ==
                    ['identity', 'root', 'bytes'], 'unsupported ledger schema')
            policy = db.execute('SELECT allowance FROM policy WHERE id=1').fetchall()
            require(policy == [(self.allowance,)], 'allowance mismatch')
            rows = db.execute('SELECT identity,root,bytes FROM retention').fetchall()
        self._ledger_bound(dbpath)
        require(all(type(size) is int and size > 0 for _, _, size in rows)
                and sum(size for _, _, size in rows) <= self.allowance, 'invalid aggregate ledger')
        key = '%s:%s' % self.parent_identity
        require([row for row in rows if row[0] == key] ==
                [(key, str(self.parent), self.parent_max)], 'unknown/renamed/recreated/max mismatch')
        for rowkey, name, size in rows:
            other = Path(name)
            require(rowkey == key or not (other == self.parent or other in self.parent.parents
                                          or self.parent in other.parents), 'overlapping ledger root')
        return dict(identity=key, root=str(self.parent), max_bytes=self.parent_max,
                    allowance=self.allowance, ledger=str(dbpath), readonly=True)

    def _ledger_bound(self, path):
        info = path.lstat()
        require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, 'ledger type')
        require(path == self.ledger_path and identity(info) ==
                identity(os.fstat(self.ledger_fd)) == self.ledger_identity, 'ledger replaced')

    def _record(self, path, info, *, hashes, deadline=None):
        record = dict(metadata=metadata(info), xattrs=attributes(path), atime_ns=info.st_atime_ns)
        if stat.S_ISLNK(info.st_mode):
            record['link_target'] = os.readlink(path)
        if hashes and stat.S_ISREG(info.st_mode):
            fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
            try:
                require(metadata(os.fstat(fd)) == metadata(info), 'file changed before hash')
                h = hashlib.sha256()
                while True:
                    require(deadline is None or time.monotonic() < deadline, 'hash deadline bound')
                    chunk = os.read(fd, 1024*1024)
                    if not chunk:
                        break
                    h.update(chunk)
                require(metadata(os.fstat(fd)) == metadata(info), 'file changed while hash')
                require(metadata(path.lstat()) == metadata(info), 'file replaced while hash')
                record['sha256'] = h.hexdigest()
                record['atime_after_hash_ns'] = path.lstat().st_atime_ns
            finally:
                os.close(fd)
        return record

    def scan(self, *, hashes=False, old_hashes_only=False):
        self.lookup()
        started = time.monotonic()
        records = {}
        logical = child_bytes = 0
        child_name = self.child.name
        child_prefix = child_name+'/'
        def walk(fd, path, key):
            nonlocal logical, child_bytes
            require(len(records) < self.max_entries and time.monotonic()-started < self.scan_seconds,
                    'scan entry/deadline bound')
            info = os.fstat(fd)
            require(identity(path.lstat()) == identity(info), 'directory replaced')
            records[key] = self._record(path, info, hashes=False)
            with os.scandir(fd) as entries:
                for entry in entries:
                    require(len(records) < self.max_entries and time.monotonic()-started < self.scan_seconds,
                            'scan entry/deadline bound')
                    name = entry.name if key == '.' else key+'/'+entry.name
                    full = path/entry.name
                    info = entry.stat(follow_symlinks=False)
                    if stat.S_ISDIR(info.st_mode):
                        nested = os.open(entry.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
                        try:
                            require(identity(os.fstat(nested)) == identity(info), 'directory changed opening')
                            walk(nested, full, name)
                        finally:
                            os.close(nested)
                    else:
                        require(stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode), 'special artifact refused')
                        require(not stat.S_ISREG(info.st_mode) or info.st_nlink == 1, 'hardlinked artifact refused')
                        logical += info.st_size
                        # The admitted child is one direct component of the
                        # parent. Match that exact component in the relative
                        # key, without rebuilding all Path ancestors per file.
                        if name == child_name or name.startswith(child_prefix):
                            child_bytes += info.st_size
                        # Final preservation compares only pre-existing bytes.
                        # Child hashes were discarded by check(), yet reading
                        # large new PCM/traces consumed the same bounded scan.
                        # Keep every old-file hash; child metadata/accounting
                        # and all type/link/cap guards still cover every entry.
                        hash_file = hashes and (not old_hashes_only or name in self.old)
                        records[name] = self._record(full, info, hashes=hash_file,
                                                     deadline=started+self.scan_seconds)
                        require(logical <= self.parent_max, 'parent budget exceeded')
                        require(child_bytes <= self.child_max, 'child budget exceeded')
            require(metadata(path.lstat()) == metadata(os.fstat(fd)), 'directory changed scanning')
        walk(self.parent_fd, self.parent, '.')
        require(time.monotonic()-started < self.scan_seconds, 'final scan deadline bound')
        return dict(records=records, logical_bytes=logical, child_bytes=child_bytes,
                    elapsed=time.monotonic()-started)

    def check(self, stage='periodic'):
        sample = dict(stage=stage, complete=False, parent=str(self.parent), child=str(self.child),
                      parent_identity=list(self.parent_identity), child_identity=list(self.child_identity),
                      parent_max=self.parent_max, child_max=self.child_max)
        self.last_sample = sample
        self.samples.append(sample)
        current = self.scan(hashes=(stage == 'final'), old_hashes_only=(stage == 'final'))
        require(identity(self.child.stat()) == self.child_identity and not self.child.is_symlink(), 'child replaced')
        records = current.pop('records')
        child_prefix = self.child.name
        old_records = {k: v for k, v in records.items()
                       if k != child_prefix and not k.startswith(child_prefix+'/')}
        require(set(old_records) == set(self.old), 'old tree membership changed/outside-child write')
        atime = {}
        for name, before in self.old.items():
            after = old_records[name]
            require(before['metadata'] == after['metadata'] and before['xattrs'] == after['xattrs']
                    and before.get('link_target') == after.get('link_target'), 'old metadata changed: '+name)
            if stage == 'final' and 'sha256' in before:
                require(before['sha256'] == after.get('sha256'), 'old bytes changed: '+name)
            if before['atime_ns'] != after['atime_ns']:
                atime[name] = [before['atime_ns'], after['atime_ns']]
        sample.update(current, complete=True, old_atime_deltas=atime)
        if stage == 'final':
            sample['hash_scope'] = 'all_preexisting_regular_files; new_child_fully_accounted_without_unused_hashes'
        return sample

    def report(self):
        return dict(scope='prepaid composite budget', ledger=self.ledger,
                    root_before_child=self.root_before_child, root_after_child=self.root_after_child,
                    old_manifest=self.old, samples=self.samples,
                    limitation='sampled/cooperative; atime can change on reads; historical inode reuse unknown')

    def close(self):
        for fd in reversed(getattr(self, 'descriptors', [])):
            os.close(fd)
        self.descriptors = []

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        try:
            self.check('final')
        finally:
            self.close()


class LeaseBudgetAdapter:
    """Translate prototype failures into the exact Lease exception contract.

    Caller injects resource_pool.ArtifactLimit; no duplicate module import.
    Constructor admission must be wrapped with admit(), not just later checks.
    """
    def __init__(self, budget, artifact_limit):
        self.budget, self.artifact_limit = budget, artifact_limit

    @classmethod
    def admit(cls, artifact_limit, *args, **kwargs):
        try:
            return cls(PrepaidCapture(*args, **kwargs), artifact_limit)
        except (ValueError, OSError, sqlite3.Error) as error:
            translated = artifact_limit('prepaid admission: '+str(error))
            translated.prepaid_diagnostics = getattr(error, 'prepaid_diagnostics', {})
            raise translated from error

    @property
    def last_sample(self):
        return self.budget.last_sample

    def check(self, stage='periodic'):
        try:
            return self.budget.check(stage)
        except (ValueError, OSError, sqlite3.Error) as error:
            raise self.artifact_limit('prepaid observation: '+str(error)) from error

    def close(self):
        self.budget.close()
