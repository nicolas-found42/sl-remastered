#!/usr/bin/env python3
"""Build docs/natives.tsv, the registry of native functions the stock executable offers to game scripts.

The executable registers each native by calling the function at 0x00416b00 with
a class name, a method name, a type descriptor and a handler address. The
compiler pushes those four values in reverse order, so the bytes before each
call read: push handler, push descriptor, push method, push class, call.

This script scans the executable's code bytes for every such call and reads each
string from the executable itself. It does not depend on Ghidra's function
boundaries. If local/census/all.c exists, it also parses the decompiled text and
reports any difference between the two methods.

Every row starts as status "unread". A later phase moves a row to "read" once its
handler is understood, and to "matched" once our code behaves the same way.

    python3 -I tools/natives_extract.py
"""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "ref" / "raw" / "StreetLegal_Redline.bak.exe"
TEXT = ROOT / "local" / "census" / "all.c"
OUT = ROOT / "docs" / "natives.tsv"
REGISTER = 0x00416B00
PUSH_IMM32 = 0x68
SKIP_STORE = bytes([0xC7, 0x05])
SKIP_LOAD_ECX = bytes([0x8B, 0x0D])


class Image:
    def __init__(self, data: bytes):
        self.d = data
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        count = struct.unpack_from("<H", data, pe + 6)[0]
        opt = struct.unpack_from("<H", data, pe + 20)[0]
        self.base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
        self.secs = []
        for i in range(count):
            o = pe + 24 + opt + i * 40
            name = data[o:o + 8].rstrip(b"\0").decode()
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
            self.secs.append((name, self.base + va, min(vsize, rawsize), rawptr))

    def offset(self, va: int) -> int | None:
        for _, sva, size, raw in self.secs:
            if sva <= va < sva + size:
                return raw + va - sva
        return None

    def section(self, name: str) -> tuple[int, int, int]:
        for n, va, size, raw in self.secs:
            if n == name:
                return va, size, raw
        raise KeyError(name)

    def cstr(self, va: int) -> str | None:
        o = self.offset(va)
        if o is None:
            return None
        end = self.d.find(b"\0", o)
        s = self.d[o:end]
        if not s.isascii() or not all(c >= 0x20 for c in s):
            return None
        return s.decode("ascii")


def scan_bytes(img: Image) -> tuple[list[tuple], list[int]]:
    tva, tsize, traw = img.section(".text")
    code = img.d[traw:traw + tsize]
    rows, failed = [], []
    for i in range(len(code) - 4):
        if code[i] != 0xE8:
            continue
        site = tva + i
        rel = struct.unpack_from("<i", code, i + 1)[0]
        if (site + 5 + rel) & 0xFFFFFFFF != REGISTER:
            continue
        p = i
        vals = []
        for _ in range(4):
            while True:
                if p >= 10 and code[p - 10:p - 8] == SKIP_STORE:
                    p -= 10
                elif p >= 6 and code[p - 6:p - 4] == SKIP_LOAD_ECX:
                    p -= 6
                else:
                    break
            if p < 5 or code[p - 5] != PUSH_IMM32:
                break
            vals.append(struct.unpack_from("<I", code, p - 4)[0])
            p -= 5
        if len(vals) != 4:
            failed.append(site)
            continue
        cls, method, desc, handler = vals
        names = [img.cstr(v) for v in (cls, method, desc)]
        if None in names or not tva <= handler < tva + tsize:
            failed.append(site)
            continue
        rows.append((*names, f"{handler:08x}", f"{site:08x}"))
    return rows, failed


def scan_text(img: Image) -> set[tuple]:
    sym = re.compile(r"_([0-9a-f]{8})$|^&?(?:DAT|LAB|FUN|PTR_LAB|PTR_DAT)_([0-9a-f]{8})$")
    call = re.compile(r"FUN_00416b00\s*\(([^;]*?)\)\s*;", re.S)
    out = set()
    for body in TEXT.read_text(encoding="utf-8", errors="replace").split("//// FUNCTION ")[1:]:
        for m in call.finditer(body):
            args = [a.strip() for a in re.sub(r"\s+", " ", m.group(1)).split(",")]
            if len(args) != 4:
                continue
            vas = []
            for a in args:
                s = sym.search(a)
                vas.append(int(s.group(1) or s.group(2), 16) if s else None)
            if None in vas:
                continue
            names = [img.cstr(v) for v in vas[:3]]
            if None not in names:
                out.add((*names, f"{vas[3]:08x}"))
    return out


def main() -> int:
    img = Image(EXE.read_bytes())
    rows, failed = scan_bytes(img)
    rows.sort()
    with open(OUT, "w", encoding="utf-8") as w:
        w.write("class\tmethod\tdescriptor\thandler\tcall_site\tstatus\n")
        for r in rows:
            w.write("\t".join(r) + "\tunread\n")
    keys = {r[:4] for r in rows}
    print(f"wrote {len(rows)} natives to {OUT.relative_to(ROOT)}, {len(failed)} call sites not parsed")
    for site in failed[:10]:
        print(f"  not parsed: {site:08x}")
    if len(failed) > 10:
        print(f"  and {len(failed) - 10} more")
    print(f"distinct classes: {len({r[0] for r in rows})}")
    print(f"duplicate (class, method, descriptor) rows: {len(rows) - len({r[:3] for r in rows})}")
    if TEXT.exists():
        text = scan_text(img)
        print(f"decompiled-text method: {len(text)} rows")
        print(f"  only in bytes: {len(keys - text)}, only in text: {len(text - keys)}")
        thunks = {}
        for r in rows:
            o = img.offset(int(r[3], 16))
            if img.d[o] == 0xE9:
                rel = struct.unpack_from("<i", img.d, o + 1)[0]
                thunks[r[:3]] = (int(r[3], 16) + 5 + rel) & 0xFFFFFFFF
        by_text = {t[:3]: t[3] for t in text}
        explained = sum(1 for k in keys - text if k[:3] in thunks and by_text.get(k[:3]) == f"{thunks[k[:3]]:08x}")
        print(f"  handlers that are a one-instruction jump: {len(thunks)}")
        print(f"  rows that differ only because the text shows a jump target: {explained} of {len(keys - text)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
