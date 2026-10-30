#!/usr/bin/env python3
"""Count calls through a global COM object in the decompiled text.

Reads local/census/all.c (tools/ghidra/DecompileAll.java). Matches
(**(code **)(*DAT_xxxxxxxx + 0xNN))( and the older spelling
(**(code **)(*_DAT_xxxxxxxx + 0xNN))( and reports calls per slot. This is an
independent check on tools/ghidra/ComCallCensus.java, which reads instructions.

    python3 -I tools/decomp_calls.py [global address, default 006495e4]
"""

import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEXT = ROOT / "local" / "census" / "all.c"


def slot_counts(global_addr: str) -> Counter:
    pat = re.compile(
        r"\(\*\*\(code \*\*\)\(\*_?DAT_" + global_addr + r"(?: \+ (0x[0-9a-f]+))?\)\)"
        r"|\(\*\*\(code \*\*\)\(\*\(int \*\)_?DAT_" + global_addr + r"(?: \+ (0x[0-9a-f]+))?\)\)"
        r"|\(\*\*\(code \*\*\)\(\*_?DAT_" + global_addr + r"\)\)"
    )
    counts: Counter = Counter()
    for m in pat.finditer(TEXT.read_text(encoding="utf-8", errors="replace")):
        off = m.group(1) or m.group(2) or "0x0"
        counts[int(off, 16)] += 1
    return counts


def main(global_addr: str) -> int:
    counts = slot_counts(global_addr)
    print(f"DAT_{global_addr}: {sum(counts.values())} calls in decompiled text, {len(counts)} slots")
    for off, n in sorted(counts.items()):
        print(f"  0x{off:03x} {n}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "006495e4"))
