#!/usr/bin/env python3
"""Create a fresh oracle evidence directory outside the reference tree."""
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
base = (root / 'local/verify').resolve()
ref = (root / 'ref').resolve()
if base == ref or ref in base.parents:
    sys.exit('oracle output must be outside ref/')
if len(sys.argv) != 2 or not sys.argv[1].replace('-', '').isalnum():
    sys.exit('invalid oracle output name')
base.mkdir(parents=True, exist_ok=True)
print(tempfile.mkdtemp(prefix=sys.argv[1] + '.', dir=base))
