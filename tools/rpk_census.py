#!/usr/bin/env python3
"""Independent RPAK census. Run as: python3 -I tools/rpk_census.py ref/raw

A second reader of the format, written apart from src/rpk.cpp, so the two can be
compared. It only reads files. Output ends with the same totals line shape as
`sl_port --rpk-census`.
"""
import os
import struct
import sys


def parse(buf):
    if len(buf) < 16 or buf[:4] != b"RPAK":
        raise ValueError("bad magic")
    version, depsA, depsB = struct.unpack_from("<III", buf, 4)
    if version != 0x200:
        raise ValueError("version 0x%x" % version)
    ndeps = depsA + depsB
    off = 16
    if off + ndeps * 64 > len(buf):
        raise ValueError("dependency table truncated")
    off += ndeps * 64
    if off + 16 > len(buf):
        raise ValueError("table header truncated")
    table_size, count, _s1, _s2 = struct.unpack_from("<IIII", buf, off)
    off += 16
    table_end = off + table_size
    if table_end > len(buf):
        raise ValueError("table runs past end of file")
    out_of_range = overlapping = 0
    for i in range(count):
        if off + 23 > table_end:
            raise ValueError("record %d header past table" % i)
        flags = buf[off + 9]
        poff, psize = struct.unpack_from("<II", buf, off + 14)
        alias_len = buf[off + 22]
        end = off + 23 + alias_len + (48 if flags & 1 else 0)
        if end > table_end:
            raise ValueError("record %d past table" % i)
        if poff + psize > len(buf):
            out_of_range += 1
        if psize and poff < table_end:
            overlapping += 1
        off = end
    if off != table_end:
        raise ValueError("table consumed %d, expected %d" % (off, table_end))
    return ndeps, count, out_of_range, overlapping


def main():
    root = sys.argv[1]
    paths = sorted(
        os.path.join(d, f)
        for d, _, fs in os.walk(root)
        for f in fs
        if f.lower().endswith(".rpk")
    )
    opened = failed = deps = entries = oor = ovl = 0
    failures = []
    for p in paths:
        with open(p, "rb") as fh:
            buf = fh.read()
        try:
            d, e, a, b = parse(buf)
        except ValueError as err:
            failed += 1
            failures.append((p, str(err)))
            continue
        opened += 1
        deps += d
        entries += e
        oor += a
        ovl += b
    print("files found: %d" % len(paths))
    print("files opened: %d" % opened)
    print("files failed: %d" % failed)
    print("total dependencies: %d" % deps)
    print("total entries: %d" % entries)
    print("payloads out of range: %d" % oor)
    print("payloads overlapping table: %d" % ovl)
    print("failed files:")
    for p, err in failures:
        print("  %s: %s" % (p, err))
    if not failures:
        print("  none")
    sys.exit(1 if failures or not paths else 0)


main()
