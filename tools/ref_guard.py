#!/usr/bin/env python3
"""Hash every file under ref/ and compare it with a committed baseline.

ref/ holds the original game files and third-party reference repos. Nothing in
this project may change them. This script only reads ref/ and writes only to
tools/ref.manifest.

    python3 -I tools/ref_guard.py generate   # one time, refuses to overwrite
    python3 -I tools/ref_guard.py verify     # exit 0 only if ref/ is unchanged
"""

import hashlib
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REF = ROOT / "ref"
MANIFEST = ROOT / "tools" / "ref.manifest"
SKIP_NAMES = {".DS_Store"}
CHUNK = 1 << 20
SHOW = 20


def digest(path: Path) -> tuple[str, int]:
    if path.is_symlink():
        return "symlink:" + os.readlink(path), 0
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(CHUNK):
            h.update(chunk)
    return h.hexdigest(), path.stat().st_size


def scan() -> dict[str, tuple[str, int]]:
    rows: dict[str, tuple[str, int]] = {}
    for dirpath, dirnames, filenames in os.walk(REF):
        dirnames.sort()
        names = list(filenames) + [d for d in dirnames if (Path(dirpath) / d).is_symlink()]
        for name in sorted(names):
            if name in SKIP_NAMES:
                continue
            path = Path(dirpath) / name
            rel = path.relative_to(REF).as_posix()
            if "\t" in rel or "\n" in rel:
                sys.exit(f"ref_guard: unsupported path {rel!r}")
            rows[rel] = digest(path)
    return rows


def read_manifest() -> dict[str, tuple[str, int]]:
    rows = {}
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        sha, size, rel = line.split("\t", 2)
        rows[rel] = (sha, int(size))
    return rows


def generate(force: bool) -> int:
    if MANIFEST.exists() and not force:
        print(f"ref_guard: {MANIFEST.relative_to(ROOT)} exists, pass --force to replace it")
        return 2
    rows = scan()
    with open(MANIFEST, "w", encoding="utf-8") as f:
        for rel in sorted(rows):
            sha, size = rows[rel]
            f.write(f"{sha}\t{size}\t{rel}\n")
    print(f"ref_guard: wrote {len(rows)} entries to {MANIFEST.relative_to(ROOT)}")
    return 0


def verify() -> int:
    if not REF.is_dir():
        print("ref_guard: ref/ is missing")
        return 2
    if not MANIFEST.exists():
        print("ref_guard: no baseline, run generate first")
        return 2
    base, now = read_manifest(), scan()
    added = sorted(set(now) - set(base))
    removed = sorted(set(base) - set(now))
    changed = sorted(r for r in set(base) & set(now) if base[r] != now[r])
    if not (added or removed or changed):
        print(f"ref_guard: ok, {len(now)} files unchanged")
        return 0
    print(f"ref_guard: FAILED, added={len(added)} removed={len(removed)} changed={len(changed)}")
    for label, paths in (("added", added), ("removed", removed), ("changed", changed)):
        for rel in paths[:SHOW]:
            print(f"  {label}: {rel}")
        if len(paths) > SHOW:
            print(f"  {label}: and {len(paths) - SHOW} more")
    return 1


def main(argv: list[str]) -> int:
    if len(argv) >= 2 and argv[1] == "generate":
        return generate("--force" in argv[2:])
    if len(argv) == 2 and argv[1] == "verify":
        return verify()
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
