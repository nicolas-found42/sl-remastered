#!/usr/bin/env python3
"""Independently decode installed PTX streams and print one row per level."""

import argparse
from io import BytesIO
from pathlib import Path
import struct
import sys

from PIL import Image


def checksum(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}"


def decode(path):
    data = path.read_bytes()
    if len(data) < 16:
        raise ValueError("truncated file header")
    _, count, width, height = struct.unpack_from("<IIII", data)
    if not count or count > 32 or not width or not height:
        raise ValueError("invalid dimensions or level count")
    offset = 16
    levels = []
    for _ in range(count):
        if len(data) - offset < 20:
            raise ValueError("truncated level header")
        length, color_size, alpha_size = struct.unpack_from("<III", data, offset)
        offset += 20
        if not color_size or color_size + alpha_size != length or length > len(data) - offset:
            raise ValueError("invalid or truncated level")
        with Image.open(BytesIO(data[offset:offset + color_size])) as source:
            rgb = source.convert("RGB")
        if alpha_size:
            with Image.open(BytesIO(data[offset + color_size:offset + length])) as source:
                alpha = source.convert("L")
            if alpha.size != rgb.size:
                raise ValueError("color and alpha dimensions differ")
        else:
            alpha = Image.new("L", rgb.size, 255)
        rgb.putalpha(alpha)
        levels.append((rgb.width, rgb.height, checksum(rgb.tobytes())))
        offset += length
    if offset != len(data):
        raise ValueError("trailing data")
    return width, height, levels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    if not args.root.is_dir():
        print(f"not a directory: {args.root}", file=sys.stderr)
        return 1
    paths = sorted(p for p in args.root.rglob("*") if p.is_file() and p.suffix.lower() == ".ptx")
    if not paths:
        print("no PTX files found", file=sys.stderr)
        return 1
    failed = False
    for path in paths:
        try:
            width, height, levels = decode(path)
            for i, (w, h, digest) in enumerate(levels):
                print(f"{path.relative_to(args.root).as_posix()}\t{width}\t{height}\t{i}\t{w}\t{h}\t{digest}")
        except (OSError, ValueError, struct.error, Image.DecompressionBombError) as error:
            print(f"{path}: {error}", file=sys.stderr)
            failed = True
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
