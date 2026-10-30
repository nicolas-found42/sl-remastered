#!/usr/bin/env python3
"""Count directive usage over every real car cfg (cars/**/_main_*.cfg, i.e. files with a `chassis` line).

Plain words: a car cfg is a text file, one directive per line. This script counts how many files use each
directive and how many lines in total, so the directive handlers can be read in order of how much they matter.
Tokenization mirrors stock 004b6520: lines cut at 255 bytes, text after any of ; # / dropped, first word
lower-cased, `eof` stops reading. Usage: python3 -I tools/car_cfg_census.py [root]
"""
import collections
import os
import sys

DEFAULT = ".scratch/2005-extract/Program Files/Street Legal Racing - Redline/cars"


def tokenize(data: bytes):
    for raw in data.split(b"\n"):
        line = raw[:255]
        for i, c in enumerate(line):
            if c in b";#/":
                line = line[:i]
                break
        words = line.split()
        if not words:
            continue
        key = words[0].decode("latin-1").lower()
        if key == "eof":
            return
        yield key, line.decode("latin-1").strip()


def car_files(root):
    for dirpath, _, names in os.walk(root):
        for n in sorted(names):
            if n.lower().startswith("_main") and n.lower().endswith(".cfg"):
                yield os.path.join(dirpath, n)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    files = sorted(car_files(root))
    lines = collections.Counter()
    in_files = collections.Counter()
    for f in files:
        with open(f, "rb") as fh:
            seen = set()
            for key, _ in tokenize(fh.read()):
                lines[key] += 1
                seen.add(key)
            in_files.update(seen)
    print(f"{len(files)} car cfg files under {root}")
    print(f"{'directive':<18}{'files':>6}{'lines':>8}")
    for key, n in lines.most_common():
        print(f"{key:<18}{in_files[key]:>6}{n:>8}")
    print(f"{len(lines)} distinct directives")


if __name__ == "__main__":
    main()
