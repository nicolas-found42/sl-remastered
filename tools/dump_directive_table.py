#!/usr/bin/env python3
"""Print the directive table at a given address of the stock exe (pairs of name pointer + handler pointer,
ended by the entry named "eof", which stock 004b6340 also tests before looking in the table), reading ref/raw/StreetLegal_Redline.bak.exe read-only. Usage: python3 -I tools/dump_directive_table.py 0060e0f8"""
import struct, sys
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
def cstr(va):
    o = off(va)
    e = d.index(b'\0', o)
    return d[o:e].decode('latin-1')
addr = int(sys.argv[1], 16)
i = 0
while True:
    name_p, handler = struct.unpack_from('<II', d, off(addr + 8 * i))
    name = cstr(name_p)
    if name == 'eof':
        break
    print(f"{i:3d} {name:<18} {handler:08x}")
    i += 1
print(i, "entries")
