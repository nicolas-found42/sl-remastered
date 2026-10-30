#!/usr/bin/env python3
"""Assign every COM-style call in local/census/com_calls.tsv to a subsystem.

Evidence, strongest first:
1. The object is the Direct3D device global 0x006495e4.
2. The calling function references a known interface ID (tools/guid_scan.py).
   This is coarse. A function that references one ID may also make other calls.
3. The call sits in the address range of the game's renderer, or at 0x550000 and above.
   An address range names no interface. It only groups calls.
Calls that match none of these stay "unexplained".

The census matches a call shape, not COM itself. Some matches are the game's own
virtual calls, which show up as unexplained or as large slot numbers.

    python3 -I tools/com_attrib.py
"""

import csv
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import guid_scan  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TSV = ROOT / "local" / "census" / "com_calls.tsv"
DEVICE = "global:006495e4"

SUBSYSTEM_OF_ID = (
    ("IID_IDirectSound", "DirectSound"),
    ("IID_IDirectInput", "DirectInput"),
    ("GUID_Sys", "DirectInput"),
    ("GUID_Joystick", "DirectInput"),
    ("CLSID_FilterGraph", "DirectShow"),
    ("IID_IGraphBuilder", "DirectShow"),
    ("IID_IMedia", "DirectShow"),
    ("IID_IBasicAudio", "DirectShow"),
    ("CLSID_SampleGrabber", "DirectShow"),
    ("IID_IDirect3D", "Direct3D texture interface ID"),
)
GAME_RENDERER = (0x4B0000, 0x500000)
HIGH_CODE = (0x550000, 0x5E7000)


def functions_by_subsystem() -> dict[str, str]:
    out: dict[str, str] = {}
    for name, hits in guid_scan.references().items():
        for prefix, subsystem in SUBSYSTEM_OF_ID:
            if name.startswith(prefix):
                for _, users in hits:
                    for fn in users:
                        out.setdefault(fn.removeprefix("FUN_"), subsystem)
                break
    return out


def classify(row: dict, by_function: dict[str, str]) -> tuple[str, str]:
    if row["object_source"] == DEVICE:
        return "device global 0x006495e4", "strong"
    sub = by_function.get(row["function"].removeprefix("FUN_"))
    if sub:
        return sub, "coarse"
    addr = int(row["call_addr"], 16)
    if GAME_RENDERER[0] <= addr < GAME_RENDERER[1]:
        return "other objects, 0x4b0000-0x4fffff", "address range"
    if HIGH_CODE[0] <= addr < HIGH_CODE[1]:
        return "other objects, 0x550000-0x5e6fff", "address range"
    return "unexplained", "none"


def main() -> int:
    rows = list(csv.DictReader(open(TSV), delimiter="\t"))
    by_function = functions_by_subsystem()
    by: Counter = Counter(classify(r, by_function) for r in rows)
    total = len(rows)
    print(f"{total} call sites that match the COM call shape")
    print(f"{'subsystem':<36}{'evidence':<16}{'calls':>6}")
    for (name, evidence), n in sorted(by.items(), key=lambda kv: -kv[1]):
        print(f"{name:<36}{evidence:<16}{n:>6}")
    strong = sum(n for (_, ev), n in by.items() if ev == "strong")
    coarse = sum(n for (_, ev), n in by.items() if ev == "coarse")
    print(f"\nstrong evidence (device global): {strong} ({100 * strong / total:.0f}%)")
    print(f"coarse evidence (interface ID in the calling function): {coarse} ({100 * coarse / total:.0f}%)")
    print(f"address range or nothing: {total - strong - coarse} ({100 * (total - strong - coarse) / total:.0f}%)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
