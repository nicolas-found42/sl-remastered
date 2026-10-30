"""Compare a capture run's game folder with the extracted 2005 data it was copied from.
Usage: python3 -I tools/wine/capture/compare_data.py RUN-DIR [SOURCE-DIR]
Prints which files changed, are missing, or were added (dumps/ is only counted)."""
import hashlib
import os
import sys

run = os.path.join(sys.argv[1], "game")
src = sys.argv[2] if len(sys.argv) > 2 else ".scratch/2005-extract/Program Files/Street Legal Racing - Redline"


def sha(path):
    m = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            m.update(block)
    return m.hexdigest()


def walk(root):
    return {os.path.relpath(os.path.join(d, f), root): os.path.join(d, f) for d, _, fs in os.walk(root) for f in fs}


a, b = walk(src), walk(run)
print("source files compared:", len(a))
print("changed:", sorted(k for k in a if k in b and sha(a[k]) != sha(b[k])))
print("missing from run copy:", sorted(k for k in a if k not in b))
print("added (excluding dumps/):", sorted(k for k in b if k not in a and not k.startswith("dumps" + os.sep)))
print("files under dumps/:", len([k for k in b if k.startswith("dumps" + os.sep)]))
