#!/usr/bin/env python3
"""Build the CD table the stock disc check compares against, from texture.dat.

Evidence (stock exe, MD5 e686099e7233bb7d7c461b1b35c5064c):
  FUN_00401260 reads texture.dat, XORs bytes 4.. with the 400-byte key at 0x607240.
  FUN_004013f0 then hands FUN_00401480 the table that starts at dword 8 of the decoded
  file, with dword 8 replaced by (dword count - 8). So the table is
  [track_count, track1_len, track2_len, ...] in milliseconds.
  FUN_00401480 asks MCI (cdaudio, time format ms) for the track count and each track length,
  then requires: count equal, tracks 2..count-1 equal, and total length in
  [0x4434f0, 0x49b331) ms. Track 1 and the last track are not compared.
Everything the game compares is read from texture.dat. Only the values the game does not
compare are invented, and they are labelled SYNTHETIC in the output.
"""

import argparse
import hashlib
import struct
import sys
from pathlib import Path

KEY_ADDRESS = 0x607240
KEY_LENGTH = 400
TOTAL_MIN, TOTAL_MAX = 0x4434F0, 0x49B331


def key_from_exe(exe: bytes) -> bytes:
    pe = struct.unpack_from("<I", exe, 0x3C)[0]
    sections = struct.unpack_from("<H", exe, pe + 6)[0]
    optional = struct.unpack_from("<H", exe, pe + 20)[0]
    base = struct.unpack_from("<I", exe, pe + 24 + 28)[0]
    for i in range(sections):
        o = pe + 24 + optional + 40 * i
        size, rva, raw_size, raw = struct.unpack_from("<IIII", exe, o + 8)
        start = base + rva
        if start <= KEY_ADDRESS and KEY_ADDRESS + KEY_LENGTH <= start + min(size, raw_size):
            at = raw + KEY_ADDRESS - start
            return exe[at:at + KEY_LENGTH]
    raise ValueError("key address not found in exe sections")


def decode(texture: bytes, key: bytes):
    if len(texture) < 4 or len(texture) % 4:
        raise ValueError("texture.dat size must be a multiple of 4 and at least 4")
    d = bytearray(texture)
    for i in range(4, len(d)):
        d[i] ^= key[(i - 4) % KEY_LENGTH]
    words = struct.unpack("<%dI" % (len(d) // 4), d)
    if len(words) <= 9 or words[0] != len(words) or d[4] != 0x56 or d[7] != 0x2E:
        raise ValueError("decoded texture.dat has an unexpected shape")
    version = bytes(d[4:36]).split(b"\0")[0].decode("ascii", "replace")
    return version, list(words[9:])


def build(version, lengths):
    """lengths are the file's lengths for tracks 1..n-1; the last track is not in the file."""
    count = len(lengths) + 1
    last = lengths[-1]
    tracks = lengths + [last]
    total = sum(tracks)
    if not TOTAL_MIN <= total < TOTAL_MAX:
        raise ValueError("total %d outside the range the game accepts" % total)
    return count, tracks, total


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("exe", type=Path)
    ap.add_argument("texture", type=Path)
    ap.add_argument("machine_out", type=Path)
    ap.add_argument("report_out", type=Path)
    a = ap.parse_args()
    exe = a.exe.read_bytes()
    texture = a.texture.read_bytes()
    version, lengths = decode(texture, key_from_exe(exe))
    count, tracks, total = build(version, lengths)
    a.machine_out.write_text("%d %d\n%s\n" % (count, total, " ".join(map(str, tracks))))
    rows = []
    for i, v in enumerate(tracks, 1):
        if i == 1:
            origin = "from texture.dat (game does not compare track 1)"
        elif i == count:
            origin = "SYNTHETIC: copied from track %d, game does not compare the last track" % (count - 1)
        else:
            origin = "from texture.dat (game compares this)"
        rows.append("track %2d  %8d ms  %s" % (i, v, origin))
    a.report_out.write_text(
        "SYNTHETIC CD TABLE. This is not an authentic disc layout and not CD audio.\n"
        "exe md5 %s\ntexture.dat md5 %s\ntexture.dat version string: %s\n"
        "track count %d (from texture.dat)\n%s\n"
        "total %d ms  SYNTHETIC: sum of the rows above; game only requires %d <= total < %d\n"
        "The real disc's true track 1, last track and total are unknown.\n"
        % (hashlib.md5(exe).hexdigest(), hashlib.md5(texture).hexdigest(), version, count,
           "\n".join(rows), total, TOTAL_MIN, TOTAL_MAX))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as e:
        print("synthetic_metadata: %s" % e, file=sys.stderr)
        sys.exit(1)
