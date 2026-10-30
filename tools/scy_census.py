#!/usr/bin/env python3
"""Independent reader for .scy files, written from the stock executable's SCY loader.

The stock executable gives .scy its own resource class (type name CRT_MESH_SCY, vtable at
0x005f2ce8) with its own loader, FUN_00503b00. That loader is not the SCX mesh loader. It
walks the file as a list of materials and skips exactly 8 bytes after each one without reading
them, which is where an SCX mesh would put its vertex count and triangle count. For version 3
it reads each material with FUN_00508160, whose size is the first u32 of the material, and
stops at a material size of 0 or at the end of the file.

This script follows that walk and then also looks at the 8 skipped bytes, because the stock
loader does not. A nonzero value there means a file carries mesh data that the stock SCY
loader never sees. It prints the same per-file line as `sl_port --scy-census --per-file`.

    python3 -I tools/scy_census.py <dir> [--per-file]
"""

import os
import struct
import sys

V3_SIZES = (56, 104, 136)


class Bad(Exception):
    pass


def walk(buf):
    if len(buf) < 8 or buf[:4] != b"INVO":
        raise Bad("bad magic")
    version = struct.unpack_from("<I", buf, 4)[0]
    if version != 3:
        raise Bad("version %d is not read by this script" % version)
    off = 8
    materials = 0
    skipped_nonzero = 0
    trailing = 0
    while off < len(buf):
        if len(buf) - off < 4:
            raise Bad("truncated material size")
        size = struct.unpack_from("<I", buf, off)[0]
        if size == 0:
            trailing = len(buf) - (off + 4)
            break
        if size not in V3_SIZES:
            raise Bad("unsupported material size %d at offset %d" % (size, off))
        if len(buf) - off < size + 8:
            raise Bad("truncated material at offset %d" % off)
        a, b = struct.unpack_from("<II", buf, off + size)
        if a or b:
            skipped_nonzero += 1
        off += size + 8
        materials += 1
    return version, materials, skipped_nonzero, trailing


def main():
    root = sys.argv[1]
    per_file = "--per-file" in sys.argv[2:]
    paths = sorted(
        os.path.join(d, f)
        for d, _, fs in os.walk(root)
        for f in fs
        if f.lower().endswith(".scy")
    )
    opened = 0
    failures = []
    total_materials = 0
    total_nonzero = 0
    total_trailing = 0
    for p in paths:
        rel = os.path.relpath(p, root)
        with open(p, "rb") as fh:
            buf = fh.read()
        try:
            version, materials, nonzero, trailing = walk(buf)
        except Bad as err:
            failures.append((rel, str(err)))
            continue
        opened += 1
        total_materials += materials
        total_nonzero += nonzero
        total_trailing += trailing
        if per_file:
            print("%s v%d materials=%d meshes=%d vertices=0 indices=0" % (rel, version, materials, materials))
    print("files found: %d" % len(paths))
    print("files opened: %d" % opened)
    print("files failed: %d" % len(failures))
    print("total materials: %d" % total_materials)
    print("skipped 8-byte tails that are not zero: %d" % total_nonzero)
    print("trailing bytes: %d" % total_trailing)
    print("failed files:")
    for p, err in failures:
        print("  %s: %s" % (p, err))
    if not failures:
        print("  none")
    sys.exit(1 if failures or total_nonzero or not paths else 0)


main()
