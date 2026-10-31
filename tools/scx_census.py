#!/usr/bin/env python3
"""Independent SCX census. Run as: python3 -I tools/scx_census.py ref/raw [--per-file]

A second reader of the format, written apart from src/scx.cpp so the two can be
compared line by line. It only reads files. v4 chunks are walked one after another
from the end of the directory, as the stock executable does (FUN_00502de0), and
the directory entries are only compared against that walk.
"""
import math
import os
import struct
import sys

V3_SIZES = (56, 104, 136)
VERTEX_FLAG_MASK = 0x40FFF


class Bad(Exception):
    pass


class Stats:
    def __init__(self):
        self.materials = self.meshes = self.vertices = self.indices = 0
        self.indices_out_of_range = self.incomplete_triangles = 0
        self.non_finite_positions = self.trailing_bytes = 0
        self.directory_mismatches = 0
        self.entries = []


def need(buf, off, n, what):
    if off < 0 or n < 0 or off + n > len(buf):
        raise Bad("%s at offset %d" % (what, off))


def vertex_stride(flags):
    s = 0
    if flags & 0x1:
        s += 12
    if flags & 0x4:
        s += 4
    elif flags & 0x8:
        s += 8
    elif flags & 0x10:
        s += 12
    if flags & 0x20:
        s += 4
    if flags & 0x40:
        s += 12
    if flags & 0x80:
        s += 4
    if flags & 0x100:
        s += 4
    if flags & 0x200:
        s += 8
    if flags & 0x400:
        s += 8
    if flags & 0x800:
        s += 8
    if flags & 0x40000:
        s += 12
    return s


def parse_v3(buf, st):
    off = 8
    while True:
        if off == len(buf):
            return
        need(buf, off, 4, "truncated material size")
        size = struct.unpack_from("<I", buf, off)[0]
        if size == 0:
            st.trailing_bytes += len(buf) - (off + 4)
            return
        if size not in V3_SIZES:
            raise Bad("unsupported material size %d at offset %d" % (size, off))
        need(buf, off, size, "truncated material")
        name = buf[off + 104:off + 136].split(b"\0", 1)[0] if size == 136 else b""
        st.entries.append((name, size))
        stride = 44
        if size > 56:
            stride = struct.unpack_from("<I", buf, off + 60)[0]
            if stride != 64:
                raise Bad("unsupported vertex stride %d at offset %d" % (stride, off + 60))
        off += size
        need(buf, off, 4, "truncated vertex count")
        n = struct.unpack_from("<I", buf, off)[0]
        off += 4
        need(buf, off, n * stride, "truncated vertices")
        vbase = off
        off += n * stride
        need(buf, off, 4, "truncated triangle count")
        tris = struct.unpack_from("<I", buf, off)[0]
        off += 4
        need(buf, off, tris * 12, "truncated triangles")
        indices = struct.unpack_from("<%dI" % (tris * 3), buf, off)
        off += tris * 12
        st.materials += 1
        st.meshes += 1
        st.vertices += n
        st.indices += len(indices)
        st.indices_out_of_range += sum(1 for i in indices if i >= n)
        for k in range(n):
            if not all(math.isfinite(c) for c in struct.unpack_from("<3f", buf, vbase + k * stride)):
                st.non_finite_positions += 1


PROP_MAP_FIRST, PROP_MAP_LAST = 0x06000000, 0x06000005


def parse_material_v4(buf, off, end):
    off += 8
    name = b""
    need(buf, off, 12, "truncated material header")
    nprops = struct.unpack_from("<I", buf, off + 8)[0]
    off += 12
    for _ in range(nprops):
        if off + 4 > end:
            raise Bad("material property past chunk at offset %d" % off)
        t = struct.unpack_from("<I", buf, off)[0]
        off += 4
        if t <= 2:
            n = 4
        elif 0x01000000 <= t <= 0x01000002 or t in (0x05000000, 0x07000000):
            n = 4
        elif PROP_MAP_FIRST <= t <= PROP_MAP_LAST:
            n = 28
        elif t == 0x08000000:
            n = 32
            name = buf[off:off + n].split(b"\0", 1)[0]
        else:
            raise Bad("unknown material property 0x%x at offset %d" % (t, off - 4))
        if off + n > end:
            raise Bad("material property value past chunk at offset %d" % off)
        off += n
    if off != end:
        raise Bad("material chunk not consumed exactly at offset %d" % off)
    return name


def parse_v4(buf, st):
    need(buf, 8, 4, "truncated directory count")
    count = struct.unpack_from("<I", buf, 8)[0]
    need(buf, 12, count * 8, "truncated directory")
    directory = [struct.unpack_from("<II", buf, 12 + 8 * i) for i in range(count)]
    physics_only = bool(directory) and directory[0][0] == 1
    off = 12 + count * 8
    chunk_index = 0
    have_group = False
    pending_vertices = None
    while off + 8 <= len(buf):
        ctype, size = struct.unpack_from("<II", buf, off)
        if size < 8:
            raise Bad("invalid chunk size %d at offset %d" % (size, off))
        end = off + size
        need(buf, off, size, "chunk past end of file")
        if chunk_index >= len(directory) or directory[chunk_index] != (ctype, off):
            st.directory_mismatches += 1
        chunk_index += 1
        if ctype == 0 or (ctype == 1 and physics_only):
            have_group = True
            pending_vertices = None
            if ctype == 0:
                name = parse_material_v4(buf, off, end)
                st.entries.append((name, size))
                st.materials += 1
        elif ctype == 4:
            if not have_group:
                raise Bad("vertices before any mesh group at offset %d" % off)
            need(buf, off + 8, 8, "truncated vertex header")
            n, flags = struct.unpack_from("<II", buf, off + 8)
            if not flags & 1 or flags & ~VERTEX_FLAG_MASK:
                raise Bad("unsupported vertex flags 0x%x at offset %d" % (flags, off + 12))
            stride = vertex_stride(flags)
            if end != off + 16 + n * stride:
                raise Bad("vertex chunk size mismatch at offset %d" % off)
            pending_vertices = (n, off + 16, stride)
            st.vertices += n
            st.meshes += 1
            for k in range(n):
                if not all(math.isfinite(c) for c in struct.unpack_from("<3f", buf, off + 16 + k * stride)):
                    st.non_finite_positions += 1
        elif ctype == 5:
            if pending_vertices is None:
                raise Bad("indices without vertices at offset %d" % off)
            need(buf, off + 8, 4, "truncated index header")
            n = struct.unpack_from("<I", buf, off + 8)[0]
            if end != off + 12 + n * 2:
                raise Bad("index chunk size mismatch at offset %d" % off)
            idx = struct.unpack_from("<%dH" % n, buf, off + 12)
            st.indices += n
            if n % 3:
                st.incomplete_triangles += 1
            st.indices_out_of_range += sum(1 for i in idx if i >= pending_vertices[0])
        elif ctype not in (1, 2, 3):
            raise Bad("unknown chunk type %d at offset %d" % (ctype, off))
        off = end
    st.trailing_bytes += len(buf) - off
    if chunk_index != len(directory):
        st.directory_mismatches += 1


def parse(buf):
    st = Stats()
    if len(buf) < 8 or buf[:4] != b"INVO":
        raise Bad("bad magic")
    version = struct.unpack_from("<I", buf, 4)[0]
    if version == 3:
        parse_v3(buf, st)
    elif version == 4:
        parse_v4(buf, st)
    else:
        raise Bad("unsupported version %d" % version)
    return version, st


def main():
    root = sys.argv[1]
    per_file = "--per-file" in sys.argv[2:]
    paths = sorted(
        os.path.join(d, f)
        for d, _, fs in os.walk(root)
        for f in fs
        if f.lower().endswith(".scx")
    )
    total = Stats()
    by_version = {}
    opened = 0
    failures = []
    for p in paths:
        with open(p, "rb") as fh:
            buf = fh.read()
        try:
            version, st = parse(buf)
        except Bad as err:
            failures.append((os.path.relpath(p, root), str(err)))
            continue
        rel = os.path.relpath(p, root)
        for name, size in st.entries:
            print("ENTRY %s %s %d" % (rel, name.hex(), size))
        opened += 1
        by_version[version] = by_version.get(version, 0) + 1
        for k, v in vars(st).items():
            if k != "entries":
                setattr(total, k, getattr(total, k) + v)
        if per_file:
            print("%s v%d materials=%d meshes=%d vertices=%d indices=%d" % (
                os.path.relpath(p, root), version, st.materials, st.meshes, st.vertices, st.indices))
    print("files found: %d" % len(paths))
    print("files opened: %d" % opened)
    print("files failed: %d" % len(failures))
    for v in sorted(by_version):
        print("version %d files: %d" % (v, by_version[v]))
    print("total materials: %d" % total.materials)
    print("total meshes: %d" % total.meshes)
    print("total vertices: %d" % total.vertices)
    print("total indices: %d" % total.indices)
    print("indices out of range: %d" % total.indices_out_of_range)
    print("incomplete triangles: %d" % total.incomplete_triangles)
    print("non-finite positions: %d" % total.non_finite_positions)
    print("trailing bytes: %d" % total.trailing_bytes)
    print("directory mismatches: %d" % total.directory_mismatches)
    print("failed files:")
    for p, err in failures:
        print("  %s: %s" % (p, err))
    if not failures:
        print("  none")
    sys.exit(1 if failures or not paths else 0)


main()
