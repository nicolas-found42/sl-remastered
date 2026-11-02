#!/usr/bin/env python3
"""Independent reader for SDAT files, the game's saved-data container.

Written from the stock executable's file open routine (FUN_00485440) and its resource list
reader (FUN_00485190), not from ref/SLRR_rev. The stock routine reads the 8-byte header only
for its type byte (offset 6) and checks neither the magic nor the version. This script checks
both, because it is for files that are meant to be SDAT.

Layout (little-endian):

  header   8 bytes: "SDAT", u16 version (0x0100), u8 type, u8 flags
  payload  from byte 8 up to the start of the trailer
  trailer  type 1: [pack list] u32 trailerLength, u32 packCount     (last 8 bytes)
                   trailer starts at fileSize - 8 - trailerLength
           type 2: u32 unsavedCount, unsaved*, u32 packCount, pack*, u32 trailerLength (last 4 bytes)
                   trailer starts at fileSize - 4 - trailerLength, trailerLength must be at least 8
  unsaved  u32 ordinal, u32 kind, u32 size, then `size` bytes only when kind is 7
           (the stock reader reads nothing for kind 5 or any other kind, so a nonzero size there
           would desync it, which this script treats as an error)
  pack     u32 ordinal, u32 length, bytes[length] where the last byte is a NUL

    python3 -I tools/sdat_census.py <dir> [--per-file]

It scans every file under <dir> whose first four bytes are "SDAT".
"""

import os
import struct
import sys


class Bad(Exception):
    pass


class Cur:
    def __init__(self, d, p, limit):
        self.d, self.p, self.limit = d, p, limit

    def u32(self):
        if self.limit - self.p < 4:
            raise Bad("truncated u32 at offset %d" % self.p)
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def take(self, n):
        if n > self.limit - self.p:
            raise Bad("truncated block of %d bytes at offset %d" % (n, self.p))
        v = self.d[self.p:self.p + n]
        self.p += n
        return v


def parse(d):
    if len(d) < 12 or d[:4] != b"SDAT":
        raise Bad("bad magic")
    version = struct.unpack_from("<H", d, 4)[0]
    if version != 0x100:
        raise Bad("unsupported version 0x%x" % version)
    typ, flags = d[6], d[7]
    unsaved = []
    packs = []
    if typ == 2:
        length = struct.unpack_from("<I", d, len(d) - 4)[0]
        if length < 8:
            raise Bad("trailer length %d is under 8" % length)
        start = len(d) - 4 - length
        if start < 8:
            raise Bad("trailer starts inside the header")
        c = Cur(d, start, len(d) - 4)
        for _ in range(c.u32()):
            ordinal, kind, size = c.u32(), c.u32(), c.u32()
            if kind == 7:
                c.take(size)
            elif size != 0:
                raise Bad("kind %d record has size %d" % (kind, size))
            unsaved.append((ordinal, kind, size))
        count = c.u32()
    elif typ == 1:
        if len(d) < 16:
            raise Bad("file too short for a type 1 trailer")
        length, count = struct.unpack_from("<II", d, len(d) - 8)
        start = len(d) - 8 - length
        if start < 8:
            raise Bad("trailer starts inside the header")
        c = Cur(d, start, len(d) - 8)
    else:
        raise Bad("unsupported type %d" % typ)
    for _ in range(count):
        ordinal = c.u32()
        n = c.u32()
        raw = c.take(n)
        if n == 0 or raw[-1] != 0:
            raise Bad("pack string is not NUL terminated")
        packs.append((ordinal, raw[:-1].decode("cp1252", errors="replace"), raw[:-1]))
    if c.p != c.limit:
        raise Bad("trailer not consumed exactly, %d bytes left" % (c.limit - c.p))
    return typ, flags, start - 8, unsaved, packs


def main():
    root = sys.argv[1]
    per_file = "--per-file" in sys.argv[2:]
    paths = []
    for dp, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(dp, f)
            try:
                with open(p, "rb") as fh:
                    if fh.read(4) == b"SDAT":
                        paths.append(p)
            except OSError:
                pass
    paths.sort()
    opened = 0
    failures = []
    tot_unsaved = tot_packs = tot_blob = 0
    odd_zero = odd_dup = odd_big = 0
    for p in paths:
        rel = os.path.relpath(p, root)
        with open(p, "rb") as fh:
            d = fh.read()
        try:
            typ, flags, payload, unsaved, packs = parse(d)
        except Bad as e:
            failures.append((rel, str(e)))
            continue
        for ordinal, kind, size in unsaved:
            print("ENTRY %s U %d %d %d" % (rel, ordinal, kind, size))
        for ordinal, name, raw in packs:
            print("ENTRY %s P %d %s %d" % (rel, ordinal, raw.hex(), len(raw)))
        opened += 1
        blob = sum(s for _, k, s in unsaved if k == 7)
        tot_unsaved += len(unsaved)
        tot_packs += len(packs)
        tot_blob += blob
        seen = set()
        for o, _, _ in packs:
            if o == 0:
                odd_zero += 1
            if o in seen:
                odd_dup += 1
            if o >= 0xFFFF:
                odd_big += 1
            seen.add(o)
        if per_file:
            kinds = ",".join(str(k) for _, k, _ in unsaved)
            names = ",".join("%d=%s" % (o, n) for o, n, _ in packs)
            print("%s OK type=%d flags=%d payload=%d unsaved=%d blobBytes=%d kinds=[%s] packs=%d [%s]" % (
                rel, typ, flags, payload, len(unsaved), blob, kinds, len(packs), names))
    print("files found: %d" % len(paths))
    print("files opened: %d" % opened)
    print("files failed: %d" % len(failures))
    print("unsaved resource records: %d" % tot_unsaved)
    print("embedded blob bytes: %d" % tot_blob)
    print("pack records: %d" % tot_packs)
    print("pack ordinals of 0: %d" % odd_zero)
    print("duplicate pack ordinals: %d" % odd_dup)
    print("pack ordinals of 0xffff or more: %d" % odd_big)
    print("failed files:")
    for p, err in failures:
        print("  %s: %s" % (p, err))
    if not failures:
        print("  none")
    sys.exit(1 if failures or not paths else 0)


if __name__ == '__main__':
    main()
