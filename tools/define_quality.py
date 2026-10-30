#!/usr/bin/env python3
"""Check the functions that tools/ghidra/DefineIndirectFunctions.java created.

and tools/ghidra/DefineGapFunctions.java. Needs local/census/defined_first.tsv (the first run
of the first script on a fresh project), local/census/defined_gaps.tsv, and local/census/all.c
(tools/ghidra/DecompileAll.java). Writes local/census/define_quality.txt.

Three tests, none of which proves a function is real:
1. Decompiler output for the function contains no bad-instruction warning.
2. The byte before the function is padding or the end of another function
   (ret, ret imm16, int3, nop, or a null byte). A function that starts in the middle
   of another one rarely has this.
3. The address is even or odd. Reported only, the compiler does not require alignment.

    python3 -I tools/define_quality.py
"""

import csv
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import natives_extract  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DEFINED = ROOT / "local" / "census" / "defined_first.tsv"
GAP_DEFINED = ROOT / "local" / "census" / "defined_gaps.tsv"
DUMP = ROOT / "local" / "census" / "all.c"
OUT = ROOT / "local" / "census" / "define_quality.txt"
BAD = ("halt_baddata", "WARNING: Bad instruction", "Control flow encountered bad instruction data")


def boundary_ok(img: "natives_extract.Image", va: int) -> bool:
    o = img.offset(va)
    prev = img.d[o - 1]
    if prev in (0xC3, 0xCC, 0x90, 0x00):
        return True
    return o >= 3 and img.d[o - 3] == 0xC2


def main() -> int:
    img = natives_extract.Image(natives_extract.EXE.read_bytes())
    rows = list(csv.DictReader(open(DEFINED), delimiter="\t"))
    created = {int(r["address"], 16): r["origin"] for r in rows if r["result"] == "created"}
    failed = [r["address"] for r in rows if r["result"] == "failed"]
    for r in csv.DictReader(open(GAP_DEFINED), delimiter="\t"):
        if r["result"] == "created":
            created[int(r["address"], 16)] = "gap"
    body_of = {}
    for f in DUMP.read_text(encoding="utf-8", errors="replace").split("//// FUNCTION ")[1:]:
        head, _, body = f.partition("\n")
        body_of[int(head.split()[1], 16)] = body
    stat: dict[str, Counter] = {}
    for va, origin in created.items():
        s = stat.setdefault(origin, Counter())
        s["functions"] += 1
        s["bad-instruction warning"] += 0
        body = body_of.get(va)
        if body is None:
            s["not in dump"] += 1
            continue
        if any(b in body for b in BAD):
            s["bad-instruction warning"] += 1
        if boundary_ok(img, va):
            s["starts after padding or a return"] += 1
        if va % 2:
            s["odd address"] += 1
    lines = [f"failed seeds: {failed}"]
    for origin, s in sorted(stat.items()):
        lines.append(f"{origin}: " + ", ".join(f"{k} {v}" for k, v in s.items()))
        n = s["functions"]
        lines.append(f"  share that start after padding or a return: {100 * s['starts after padding or a return'] / n:.0f}%")
    OUT.write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
