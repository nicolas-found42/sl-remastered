#!/usr/bin/env python3
"""Filter for decompile text: after every Ghidra string/data name that ends in an 8-digit address
(s_..._0060e0f8, DAT_00611e4c), append the real bytes from the stock exe as a comment, so scanf formats
are readable. Read-only. Usage: tools/decomp_fn.sh 0046c3a0 | python3 -I tools/decomp_annot.py"""
import re, struct, sys
d = open('ref/raw/StreetLegal_Redline.bak.exe', 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = []
for k in range(n):
    o = pe + 24 + opt + k * 40
    vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
    secs.append((va, vs, ro, rs))
def off(va):
    r = va - base
    for sva, vs, ro, rs in secs:
        if sva <= r < sva + max(vs, rs):
            return ro + r - sva
def show(va):
    o = off(va)
    if o is None:
        return None
    e = d.find(b'\0', o, o + 200)
    if e > o:
        s = d[o:e]
        if all(32 <= c < 127 or c in (9, 10, 13) for c in s):
            return s.decode('latin-1')
    return None
pat = re.compile(r'\b(?:s_\w*?_|DAT_|PTR_s_\w*?_)([0-9a-f]{8})\b')
def sub(m):
    s = show(int(m.group(1), 16))
    return m.group(0) + (f' /*{s!r}*/' if s is not None else '')
for line in sys.stdin:
    sys.stdout.write(pat.sub(sub, line))
