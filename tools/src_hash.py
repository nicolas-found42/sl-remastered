#!/usr/bin/env python3
"""Hash the project's own code so a run can say which code it ran (the repo has no commits to point at).

Usage: python3 -I tools/src_hash.py            prints the tree hash and saves local/src-manifests/<hash>.txt
Covers src/, tools/, tests/ and CMakeLists.txt in sorted path order. Skips __pycache__, .DS_Store and the binary
files that tools/wine/capture builds. The manifest (path, TAB, file hash) lets tools/diagnose.sh --diff-code list
what changed between two runs. tools/play.sh and tools/replay.sh export the hash as SLRR_SRC_HASH.
"""
import hashlib
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SKIP_DIRS = {"__pycache__"}
SKIP_NAMES = {".DS_Store"}
CAPTURE = pathlib.Path("tools/wine/capture")


def wanted(rel, data_head):
    if SKIP_DIRS & set(rel.parts) or rel.name in SKIP_NAMES or rel.suffix == ".pyc":
        return False
    return not (CAPTURE in rel.parents and b"\0" in data_head)


def collect():
    files = [ROOT / "CMakeLists.txt"]
    for top in ("src", "tools", "tests"):
        files.extend(p for p in (ROOT / top).rglob("*") if p.is_file() and not p.is_symlink())
    lines = []
    for path in sorted(files, key=lambda p: p.relative_to(ROOT).as_posix()):
        rel = path.relative_to(ROOT)
        try:
            data = path.read_bytes()
        except OSError:
            continue
        if wanted(rel, data[:8192]):
            lines.append(f"{rel.as_posix()}\t{hashlib.sha256(data).hexdigest()}\n")
    return "".join(lines)


def main():
    listing = collect()
    tree = hashlib.sha256(listing.encode()).hexdigest()
    target = ROOT / "local/src-manifests" / f"{tree}.txt"
    if not target.exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(listing)
    print(tree)
    return 0


if __name__ == "__main__":
    sys.exit(main())
