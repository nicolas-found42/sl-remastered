#!/usr/bin/env python3
"""Convert a raw Mode-1 disc image to an ISO without changing the source."""

import argparse
import os
from pathlib import Path
import struct
import sys

RAW_SECTOR = 2352
DATA_SECTOR = 2048
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
REFERENCE = Path(__file__).resolve().parents[2] / "ref"


def volume_size(pvd):
    if pvd[:7] != b"\x01CD001\x01":
        raise ValueError("missing ISO9660 primary volume descriptor")
    little, big = struct.unpack_from("<I", pvd, 80)[0], struct.unpack_from(">I", pvd, 84)[0]
    block_little, block_big = struct.unpack_from("<H", pvd, 128)[0], struct.unpack_from(">H", pvd, 130)[0]
    if little != big or not little or block_little != DATA_SECTOR or block_big != DATA_SECTOR:
        raise ValueError("invalid ISO9660 volume size or block size")
    return little


def convert(source, destination):
    protected = REFERENCE.resolve()
    target = destination.resolve()
    if target == protected or protected in target.parents:
        raise ValueError("output must be outside ref/")
    with source.open("rb") as raw:
        size = os.fstat(raw.fileno()).st_size
        if size % RAW_SECTOR or size < RAW_SECTOR * 17:
            raise ValueError("raw image is truncated or too small")
        total = size // RAW_SECTOR
        raw.seek(16 * RAW_SECTOR)
        descriptor_sector = raw.read(RAW_SECTOR)
        if len(descriptor_sector) != RAW_SECTOR or descriptor_sector[:12] != SYNC or descriptor_sector[15] != 1:
            raise ValueError("primary descriptor sector is not raw Mode 1")
        count = volume_size(descriptor_sector[16:16 + DATA_SECTOR])
        if count < 17 or count > total:
            raise ValueError("ISO9660 declared volume exceeds or omits the data image")
        raw.seek(0)
        created = False
        try:
            with destination.open("xb") as iso:
                created = True
                for index in range(count):
                    sector = raw.read(RAW_SECTOR)
                    if len(sector) != RAW_SECTOR:
                        raise ValueError(f"truncated raw sector {index}")
                    if sector[:12] != SYNC or sector[15] != 1:
                        raise ValueError(f"sector {index} is not raw Mode 1")
                    payload = sector[16:16 + DATA_SECTOR]
                    if index == 16 and volume_size(payload) != count:
                        raise ValueError("ISO9660 descriptor changed during conversion")
                    iso.write(payload)
                if os.fstat(raw.fileno()).st_size != size:
                    raise ValueError("raw image changed during conversion")
        except BaseException:
            if created:
                destination.unlink(missing_ok=True)
            raise
    return count, total - count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    try:
        count, ignored = convert(args.source, args.destination)
    except (OSError, ValueError) as error:
        print(f"prepare_disc: {error}", file=sys.stderr)
        return 1
    print(f"created {args.destination}: {count} data sectors, {ignored} trailing sectors ignored")
    print("CD track metadata and any audio tracks are not included")
    return 0


if __name__ == "__main__":
    sys.exit(main())
