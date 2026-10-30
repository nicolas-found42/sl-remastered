#!/usr/bin/env python3
"""Estimate how much of the undefined code gaps is real code.

For each stretch of .text that no function covers (local/census/gaps_final.tsv from
tools/ghidra/CodeGaps.java), count E8 call bytes whose target is exactly the entry of a
known function (local/census/all.c). Compare with the count expected if the bytes were
random. A large excess means the gaps hold code that the scans never saw.

    python3 -I tools/gap_code_probe.py
"""

import csv
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import natives_extract  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
GAPS = ROOT / "local" / "census" / "gaps_final.tsv"
DUMP = ROOT / "local" / "census" / "all.c"


def main() -> int:
    img = natives_extract.Image(natives_extract.EXE.read_bytes())
    tva, tsize, _ = img.section(".text")
    entries = set()
    for line in DUMP.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("//// FUNCTION"):
            entries.add(int(line.split()[3], 16))
    rows = [(int(r["start"], 16), int(r["size"]), r["first_item"])
            for r in csv.DictReader(open(GAPS), delimiter="\t")]
    for kind in ("undefined", "instruction", "data"):
        total_bytes = e8 = hits = 0
        for start, size, k in rows:
            if k != kind:
                continue
            o = img.offset(start)
            if o is None:
                continue
            code = img.d[o:o + size]
            total_bytes += size
            for i in range(len(code) - 4):
                if code[i] != 0xE8:
                    continue
                e8 += 1
                target = (start + i + 5 + struct.unpack_from("<i", code, i + 1)[0]) & 0xFFFFFFFF
                if target in entries:
                    hits += 1
        chance = e8 * len(entries) / tsize
        print(f"{kind:<12} bytes {total_bytes:>7}  E8 bytes {e8:>6}  calls to a known function entry {hits:>5}"
              f"  expected by chance {chance:6.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
