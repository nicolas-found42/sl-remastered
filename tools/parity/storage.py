"""Lossless APFS compaction of stopped capture trees.

Every path and byte remains available. Distinct copy-on-write inodes preserve
write isolation; this is never hard-link deduplication or evidence eviction.
Inode/ctime changes are journaled. Run only after all writers have stopped.
"""
import argparse
import ctypes
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import stat
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_STATE = ROOT / 'local/storage-index'
CAPTURE_ALLOWANCE = 64 * 1024**3


class CommittedCloneError(OSError):
    def __init__(self, record, cause):
        super().__init__('replacement committed; final durability/journal step failed: ' + str(cause))
        self.record = record


class IneligibleClone(ValueError):
    """Leave this file unchanged; its original metadata cannot be cloned exactly."""


def protected(path):
    return path == ROOT/'ref' or ROOT/'ref' in path.parents or '.git' in path.parts


def prepare_state(state):
    if protected(state) or state == ROOT or state in ROOT.parents:
        raise ValueError('protected storage state refused')
    if state.resolve(strict=False) != state:
        raise ValueError('storage state alias refused before creation')
    state.mkdir(parents=True, exist_ok=True)
    if state.resolve(strict=True) != state:
        raise ValueError('storage state alias refused')
    names = ['lock'] + [base+suffix for base in ('objects.sqlite', 'retention.sqlite')
                       for suffix in ('', '-wal', '-shm', '-journal')]
    for name in names:
        path = state/name
        if path.is_symlink() or (path.exists() and not stat.S_ISREG(path.lstat().st_mode)):
            raise ValueError('storage state file alias/type refused')


def xattrs(path):
    libc = ctypes.CDLL(None, use_errno=True)
    listing = libc.listxattr
    listing.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
    listing.restype = ctypes.c_ssize_t
    getter = libc.getxattr
    getter.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p,
                       ctypes.c_size_t, ctypes.c_uint32, ctypes.c_int]
    getter.restype = ctypes.c_ssize_t
    name = os.fsencode(path)
    size = listing(name, None, 0, 1)
    if not 0 <= size <= 1024*1024:
        raise ValueError('xattr name inventory unavailable/oversized')
    buf = ctypes.create_string_buffer(size)
    if listing(name, buf, size, 1) != size:
        raise ValueError('xattr inventory changed')
    result = {}
    for key in buf.raw.split(b'\0'):
        if not key: continue
        length = getter(name, key, None, 0, 0, 1)
        if not 0 <= length <= 1024*1024:
            raise ValueError('xattr value unavailable/oversized')
        value = ctypes.create_string_buffer(length)
        if getter(name, key, value, length, 0, 1) != length:
            raise ValueError('xattr changed')
        result[key] = value.raw
    return result


def reserve_capture(roots, max_bytes, state=DEFAULT_STATE, limit=CAPTURE_ALLOWANCE):
    """Durable aggregate retention admission, serialized across processes.

    This conservative ledger charges each distinct output root its declared
    maximum, including failed runs. Restarting Python does not reset it. Existing
    historical captures are preserved; the allowance bounds NEW captures. No
    automatic eviction or ledger reset: archive/review retention deliberately.
    Root reuse may increase, never decrease its reservation.
    """
    if type(max_bytes) is not int or max_bytes < 1 or type(limit) is not int or limit < 1:
        raise ValueError('positive storage reservation/limit required')
    roots = [Path(p).absolute() for p in roots]
    state = Path(state).absolute()
    prepare_state(state)
    with contextlib.closing(sqlite3.connect(state/'retention.sqlite', timeout=5)) as db:
        db.execute('CREATE TABLE IF NOT EXISTS retention (identity TEXT PRIMARY KEY, root TEXT NOT NULL, bytes INTEGER NOT NULL)')
        if [row[1] for row in db.execute('PRAGMA table_info(retention)')] != ['identity', 'root', 'bytes']:
            raise ValueError('old retention schema requires charge-preserving migration; refusing reset')
        db.execute('CREATE TABLE IF NOT EXISTS policy (id INTEGER PRIMARY KEY, allowance INTEGER NOT NULL)')
        db.commit(); db.execute('BEGIN IMMEDIATE')
        policy = db.execute('SELECT allowance FROM policy WHERE id=1').fetchone()
        if policy and policy[0] != limit:
            raise ValueError('retention policy changed; explicit migration required')
        db.execute('INSERT OR IGNORE INTO policy VALUES (1,?)', (limit,))
        current = {key: (path, charged) for key, path, charged in db.execute('SELECT identity,root,bytes FROM retention')}
        for root in roots:
            if root.resolve(strict=True) != root or not root.is_dir():
                raise ValueError('canonical capture root required')
            if protected(root):
                raise ValueError('protected capture root refused')
            info = root.stat()
            incarnation = f'{info.st_dev}:{info.st_ino}'
            for old, charged in current.values():
                if root != Path(old) and (root in Path(old).parents or Path(old) in root.parents):
                    raise ValueError('overlapping retained capture roots refused')
            current[incarnation] = (str(root), max(current.get(incarnation, ('', 0))[1], max_bytes))
        used = sum(charged for _, charged in current.values())
        if used > limit:
            raise ValueError(f'cumulative capture allowance exhausted: {used} > {limit}; '
                             'compact/archive retained captures and review retention before more runs')
        for incarnation, (root, charged) in current.items():
            db.execute('INSERT OR REPLACE INTO retention VALUES (?,?,?)', (incarnation, root, charged))
        db.commit()
    return {'reserved_bytes': used, 'allowance_bytes': limit, 'roots': list(map(str, roots)),
            'scope': 'conservative maximum for new retained capture roots; historical evidence untouched'}


def identity(s):
    return (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)


def checked(path):
    if protected(path):
        raise ValueError('protected path refused')
    if path.resolve(strict=True) != path:
        raise ValueError('symlink/alias path refused: ' + str(path))
    s = path.lstat()
    if not stat.S_ISREG(s.st_mode) or s.st_nlink != 1:
        raise ValueError('requires single-link regular file: ' + str(path))
    return s


def hashed(path, expected):
    h = hashlib.sha256()
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(fd, 'rb') as stream:
        if identity(os.fstat(stream.fileno())) != identity(expected):
            raise ValueError('file changed before hashing')
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
        if identity(os.fstat(stream.fileno())) != identity(expected):
            raise ValueError('file changed while hashing')
    if identity(checked(path)) != identity(expected):
        raise ValueError('file replaced while hashing')
    return h.hexdigest()


def cow_replace(source, target, digest, journal):
    """Fail before replacement if clone support, content or metadata is uncertain."""
    if sys.platform != 'darwin':
        raise OSError('APFS clone support required; no full-copy fallback')
    src, dst = checked(source), checked(target)
    if src.st_dev != dst.st_dev or src.st_ino == dst.st_ino:
        raise ValueError('distinct same-volume files required')
    if hashed(source, src) != digest or hashed(target, dst) != digest:
        raise ValueError('duplicate content changed')
    # Extended attributes (including ACL data exposed by the filesystem) belong
    # to the target. Refuse immutable flags rather than clearing protections.
    if getattr(dst, 'st_flags', 0) or getattr(src, 'st_flags', 0):
        raise IneligibleClone('flagged source/target refused')
    if not os.access(target.parent, os.W_OK | os.X_OK):
        raise IneligibleClone('target directory is read-only; permissions preserved')
    attrs = xattrs(target)
    temp = target.with_name('.storage-clone-' + uuid.uuid4().hex)
    fd = os.open(source, os.O_RDONLY | os.O_NOFOLLOW)
    parent = os.open(target.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        if identity(os.fstat(fd)) != identity(src):
            raise ValueError('source changed before clone')
        libc = ctypes.CDLL(None, use_errno=True)
        clone = libc.fclonefileat
        clone.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        clone.restype = ctypes.c_int
        if clone(fd, parent, os.fsencode(temp.name), 0):
            code = ctypes.get_errno()
            raise OSError(code, os.strerror(code), str(temp))
        metadata = libc.copyfile
        metadata.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_uint32]
        metadata.restype = ctypes.c_int
        # SDK copyfile.h: ACL=1, STAT=2, XATTR=4, never DATA=8.
        if metadata(os.fsencode(target), os.fsencode(temp), None, 7):
            code = ctypes.get_errno()
            raise OSError(code, os.strerror(code), str(temp))
        os.chown(temp, dst.st_uid, dst.st_gid)
        os.chmod(temp, stat.S_IMODE(dst.st_mode))
        # copyfile may retain/inject com.apple.provenance even when the target
        # has none. Remove only clone-only attributes, never touch the old file.
        remove_attr = libc.removexattr
        remove_attr.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
        remove_attr.restype = ctypes.c_int
        for key in xattrs(temp).keys() - attrs.keys():
            if remove_attr(os.fsencode(temp), key, 1):
                code = ctypes.get_errno()
                raise OSError(code, os.strerror(code), str(temp))
        os.utime(temp, ns=(dst.st_atime_ns, dst.st_mtime_ns))
        fresh = checked(temp)
        if fresh.st_ino in (src.st_ino, dst.st_ino) or hashed(temp, fresh) != digest:
            raise ValueError('clone failed content/private-inode check')
        os.utime(temp, ns=(dst.st_atime_ns, dst.st_mtime_ns))
        restored = checked(temp)
        restored_attrs = xattrs(temp)
        added_attrs = restored_attrs.keys() - attrs.keys()
        attributes_preserved = (all(restored_attrs.get(k) == v for k, v in attrs.items()) and
                                added_attrs <= {b'com.apple.provenance'})
        if ((restored.st_mode, restored.st_uid, restored.st_gid, restored.st_mtime_ns, restored.st_atime_ns) !=
                (dst.st_mode, dst.st_uid, dst.st_gid, dst.st_mtime_ns, dst.st_atime_ns) or not attributes_preserved):
            raise IneligibleClone('clone target metadata mismatch: ' + str(target) + ' ' + repr({
                'expected': (dst.st_mode, dst.st_uid, dst.st_gid, dst.st_mtime_ns, dst.st_atime_ns),
                'actual': (restored.st_mode, restored.st_uid, restored.st_gid, restored.st_mtime_ns, restored.st_atime_ns),
                'expected_xattr_keys': list(attrs), 'actual_xattr_keys': list(xattrs(temp))}))
        if identity(checked(target)) != identity(dst) or identity(checked(source)) != identity(src):
            raise ValueError('capture changed before atomic replacement')
        temp_fd = os.open(temp, os.O_RDONLY | os.O_NOFOLLOW)
        try:
            os.fsync(temp_fd)
        finally:
            os.close(temp_fd)
        record = {'source': str(source), 'target': str(target), 'sha256': digest,
                  'bytes': dst.st_size, 'old_inode': dst.st_ino, 'new_inode': fresh.st_ino,
                  'old_identity': identity(dst), 'new_identity': identity(checked(temp)),
                  'os_added_xattrs': {k.hex(): restored_attrs[k].hex() for k in added_attrs},
                  'mode': stat.S_IMODE(dst.st_mode), 'mtime_ns': dst.st_mtime_ns}
        journal.write(json.dumps(dict(record, event='prepared')) + '\n')
        journal.flush(); os.fsync(journal.fileno())
        os.replace(temp, target)
        try:
            os.fsync(parent)
            journal.write(json.dumps(dict(record, event='replaced')) + '\n')
            journal.flush(); os.fsync(journal.fileno())
        except (OSError, ValueError) as error:
            raise CommittedCloneError(record, error) from error
        return record
    finally:
        os.close(fd); os.close(parent)
        if temp.exists():
            temp.unlink()


def compact(roots, state=DEFAULT_STATE, *, apply=False, min_bytes=1024*1024,
            max_files=2000000, seconds=1800, exclude=()):
    roots = [Path(p).absolute() for p in roots]
    if not 0 < seconds <= 86400 or min_bytes < 1 or max_files < 1:
        raise ValueError('positive bounded compaction limits required')
    state = Path(state).absolute()
    excluded = {Path(p).absolute() for p in exclude}
    for path in roots:
        if path.resolve(strict=True) != path or not path.is_dir():
            raise ValueError('canonical directory root required')
        if path == ROOT or protected(path) or path == state:
            raise ValueError('protected root refused')
    prepare_state(state)
    start = time.monotonic()
    report = {'apply': apply, 'files': 0, 'candidate_bytes': 0, 'duplicates': 0,
              'replaced': 0, 'duplicate_bytes': 0, 'free_before': shutil.disk_usage(state).free,
              'skipped_metadata': 0, 'skipped_metadata_bytes': 0,
              'complete': False, 'limits': 'free-space delta is system-wide; logical duplicate bytes are not physical savings'}
    report['excluded_roots'] = sorted(map(str, excluded))
    lock = os.open(state/'lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with contextlib.closing(sqlite3.connect(state/'objects.sqlite')) as db, (state/('journal-'+uuid.uuid4().hex+'.jsonl')).open('x') as journal:
            db.execute('CREATE TABLE IF NOT EXISTS objects (hash TEXT PRIMARY KEY, path TEXT NOT NULL)')
            try:
                for root in roots:
                    if root in excluded: continue
                    for base, dirs, files in os.walk(root, followlinks=False):
                        dirs[:] = sorted(n for n in dirs if not (Path(base)/n).is_symlink()
                                         and (Path(base)/n) != state and (Path(base)/n) not in excluded
                                         and not protected(Path(base)/n))
                        for name in sorted(files):
                            report['files'] += 1
                            if report['files'] > max_files or time.monotonic()-start > seconds:
                                raise TimeoutError('compaction scan limit reached; retained journal is partial')
                            path = Path(base)/name
                            if protected(path): continue
                            s = path.lstat()
                            if not stat.S_ISREG(s.st_mode) or s.st_nlink != 1 or s.st_size < min_bytes:
                                continue
                            report['candidate_bytes'] += s.st_size
                            key = hashed(path, s)
                            row = db.execute('SELECT path FROM objects WHERE hash=?', (key,)).fetchone()
                            prior = Path(row[0]) if row else None
                            if prior == path:
                                continue
                            if prior:
                                try:
                                    prior_stat = checked(prior)
                                    if hashed(prior, prior_stat) != key:
                                        prior = None
                                except (OSError, ValueError):
                                    prior = None
                            if prior:
                                report['duplicates'] += 1; report['duplicate_bytes'] += s.st_size
                                if apply:
                                    try:
                                        cow_replace(prior, path, key, journal)
                                    except CommittedCloneError as error:
                                        report['replaced'] += 1
                                        report['committed_journal_incomplete'] = error.record
                                        raise
                                    except IneligibleClone as error:
                                        report['skipped_metadata'] += 1
                                        report['skipped_metadata_bytes'] += s.st_size
                                        journal.write(json.dumps({'event': 'skipped-metadata', 'target': str(path),
                                                                 'sha256': key, 'reason': str(error)}) + '\n')
                                        journal.flush(); os.fsync(journal.fileno())
                                    else:
                                        report['replaced'] += 1
                            else:
                                db.execute('INSERT OR REPLACE INTO objects VALUES (?,?)', (key, str(path)))
                            if report['files'] % 100 == 0:
                                db.commit()
                            if time.monotonic()-start > seconds:
                                raise TimeoutError('compaction deadline exceeded after file operation')
                if time.monotonic()-start > seconds:
                    raise TimeoutError('compaction deadline exceeded after final scan')
                report['complete'] = True
            finally:
                db.commit()
                report['free_after'] = shutil.disk_usage(state).free
                report['seconds'] = time.monotonic()-start
                with (state/('report-'+uuid.uuid4().hex+'.json')).open('x') as stream:
                    json.dump(report, stream, indent=2)
    finally:
        os.close(lock)
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('roots', type=Path, nargs='+')
    p.add_argument('--state', type=Path, default=DEFAULT_STATE)
    p.add_argument('--apply-stopped', action='store_true', help='operator has verified that all capture writers stopped')
    p.add_argument('--seconds', type=float, default=1800)
    p.add_argument('--min-mib', type=int, default=1)
    p.add_argument('--exclude-root', type=Path, action='append', default=[])
    a = p.parse_args()
    print(json.dumps(compact(a.roots, a.state, apply=a.apply_stopped,
                             min_bytes=a.min_mib*1024**2, seconds=a.seconds,
                             exclude=a.exclude_root), indent=2))


if __name__ == '__main__':
    main()
