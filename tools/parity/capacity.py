"""Read-only retention planning. Observations never grant execution admission."""
import argparse
import contextlib
import json
import os
from pathlib import Path
import sqlite3
import shutil
import stat
import time
import math

ROOT = Path(__file__).resolve().parents[2]


def usage(root, *, seconds=5, max_entries=100000):
    if not isinstance(seconds, (int, float)) or not math.isfinite(seconds) or seconds <= 0:
        raise ValueError('finite positive scan deadline required')
    if type(max_entries) is not int or max_entries < 1:
        raise ValueError('positive entry bound required')
    deadline = time.monotonic() + seconds
    count = 0
    total = 0
    before = root.stat()

    def scan(fd):
        nonlocal count, total
        with os.scandir(fd) as entries:
            for entry in entries:
                count += 1
                if count > max_entries or time.monotonic() >= deadline:
                    raise ValueError('usage scan bound exceeded')
                info = entry.stat(follow_symlinks=False)
                if stat.S_ISDIR(info.st_mode):
                    child = os.open(entry.name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
                    try:
                        opened = os.fstat(child)
                        if (opened.st_dev, opened.st_ino) != (info.st_dev, info.st_ino):
                            raise ValueError('directory replaced during scan')
                        scan(child)
                    finally:
                        os.close(child)
                elif stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode) or stat.S_ISSOCK(info.st_mode):
                    total += info.st_size
                else:
                    raise ValueError('unsupported special file')
        if time.monotonic() >= deadline:
            raise ValueError('usage scan deadline exceeded')

    fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        opened = os.fstat(fd)
        if (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino):
            raise ValueError('root replaced before scan')
        scan(fd)
        after = root.stat()
        if (before.st_dev, before.st_ino) != (after.st_dev, after.st_ino):
            raise ValueError('root replaced during scan')
    finally:
        os.close(fd)
    return {'logical_bytes': total, 'entries': count, 'complete': True}


def report(ledger, parent=None):
    ledger = Path(ledger).absolute()
    if ledger.resolve(strict=True) != ledger or not ledger.is_file():
        raise ValueError('canonical ledger file required')
    ledger_identity = (ledger.stat().st_dev, ledger.stat().st_ino)
    with contextlib.closing(sqlite3.connect(ledger.as_uri() + '?mode=ro', uri=True)) as db:
        db.execute('BEGIN')
        if [row[1] for row in db.execute('PRAGMA table_info(retention)')] != ['identity', 'root', 'bytes']:
            raise ValueError('unsupported ledger schema')
        policy = db.execute('SELECT id,allowance FROM policy').fetchall()
        if len(policy) != 1 or policy[0][0] != 1 or type(policy[0][1]) is not int or policy[0][1] <= 0:
            raise ValueError('invalid retention policy')
        allowance = policy[0][1]
        rows = db.execute('SELECT identity,root,bytes FROM retention ORDER BY root').fetchall()
    if (ledger.stat().st_dev, ledger.stat().st_ino) != ledger_identity:
        raise ValueError('ledger replaced during observation')
    if any(type(size) is not int or size <= 0 for _, _, size in rows):
        raise ValueError('invalid reservation')
    charged = sum(row[2] for row in rows)
    if charged > allowance:
        raise ValueError('ledger over allowance')
    result = {'schema': 1, 'readonly': True, 'admission_granted': False,
              'free_disk_bytes': shutil.disk_usage(ledger.parent).free,
              'required_free_disk_bytes': 10 * 1024**3,
              'allowance_bytes': allowance, 'charged_bytes': charged,
              'new_reservation_capacity_bytes': allowance - charged,
              'reservations': len(rows), 'parent': None,
              'limitations': 'Sampled logical usage, not an atomic snapshot or disk allocation. '
                             'Links and sockets count metadata only. Adapter admission, writer locks, '
                             'pressure checks and preservation checks remain required.'}
    if parent is not None:
        parent = Path(parent).absolute()
        if parent.resolve(strict=True) != parent or not parent.is_dir():
            raise ValueError('canonical parent directory required')
        info = parent.stat()
        identity = f'{info.st_dev}:{info.st_ino}'
        selected = [row for row in rows if row[0] == identity and row[1] == str(parent)]
        if len(selected) != 1:
            raise ValueError('parent identity/path is not registered')
        for key, path, _ in rows:
            other = Path(path)
            if key != identity and (other == parent or other in parent.parents or parent in other.parents):
                raise ValueError('overlapping reservations')
        observed = usage(parent)
        after = parent.stat()
        if f'{after.st_dev}:{after.st_ino}' != identity:
            raise ValueError('registered parent replaced during observation')
        maximum = selected[0][2]
        remaining = max(0, maximum - observed['logical_bytes'])
        result['parent'] = dict(path=str(parent), identity=identity, maximum_bytes=maximum,
                                remaining_bytes=remaining, over_budget=observed['logical_bytes'] > maximum,
                                observation=observed,
                                native_child_limit_bytes=min(1024**3, remaining),
                                stock_prepaid_supported=True,
                                stock_child_limit_bytes=min(1024**3, remaining),
                                stock_prepaid_scope='experimental ephemeral-prefix only; qualified seed, private tmpfs, preservation and adapter admission required; not D4 acceptance')
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ledger', type=Path, default=ROOT / 'local/storage-index/retention.sqlite')
    parser.add_argument('--parent', type=Path)
    args = parser.parse_args(argv)
    try:
        print(json.dumps(report(args.ledger, args.parent), indent=2))
        return 0
    except (OSError, ValueError, sqlite3.Error) as error:
        print(json.dumps({'complete': False, 'error': str(error), 'admission_granted': False}))
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
