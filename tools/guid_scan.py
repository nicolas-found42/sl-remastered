#!/usr/bin/env python3
"""Find known COM interface and class IDs in the stock executable and the functions that use them.

A hit is proof: a 16-byte match is not a coincidence. A miss proves nothing,
because the list below holds only the IDs we know. Needs local/census/all.c
(tools/ghidra_run.sh DecompileAll.java local/census/all.c).

    python3 -I tools/guid_scan.py
"""

import re
import struct
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "ref" / "raw" / "StreetLegal_Redline.bak.exe"
TEXT = ROOT / "local" / "census" / "all.c"

GUIDS = {
    "IID_IDirect3D9": "81BDCBCA-64D4-426D-AE8D-AD0147F4275C",
    "IID_IDirect3DDevice9": "D0223B96-BF7A-43FD-92BD-A43B0D82B9EB",
    "IID_IDirect3DTexture9": "85C31227-3DE5-4F00-9B3A-F11AC38C18B5",
    "IID_IDirect3DSurface9": "0CFBAF3A-9FF6-429A-99B3-A2796AF8B89B",
    "IID_IDirect3DCubeTexture9": "FFF32F81-D953-473A-9223-93D652ABA93F",
    "IID_IDirect3DVertexBuffer9": "B64BB1B5-FD70-4DF6-BF91-19D0A12455E3",
    "IID_IDirect3DIndexBuffer9": "7C9DD65E-D3F7-4529-ACEE-785830ACDE35",
    "IID_IDirectSoundBuffer": "279AFA85-4981-11CE-A521-0020AF0BE560",
    "IID_IDirectSound3DBuffer": "279AFA86-4981-11CE-A521-0020AF0BE560",
    "IID_IDirectSound3DListener": "279AFA84-4981-11CE-A521-0020AF0BE560",
    "IID_IDirectSound8": "C50A7E93-F395-4834-9EF6-7FA99DE50966",
    "IID_IDirectSoundBuffer8": "6825A449-7524-4D82-920F-50E36AB3AB1E",
    "IID_IDirectInput8A": "BF798030-483A-4DA2-AA99-5D64ED369700",
    "IID_IDirectInputDevice8A": "54D41080-DC15-4833-A41B-748F73A38179",
    "GUID_SysMouse": "6F1D2B60-D5A0-11CF-BFC7-444553540000",
    "GUID_SysKeyboard": "6F1D2B61-D5A0-11CF-BFC7-444553540000",
    "GUID_Joystick": "6F1D2B70-D5A0-11CF-BFC7-444553540000",
    "CLSID_FilterGraph": "E436EBB3-524F-11CE-9F53-0020AF0BA770",
    "IID_IGraphBuilder": "56A868A9-0AD4-11CE-B03A-0020AF0BA770",
    "IID_IMediaControl": "56A868B1-0AD4-11CE-B03A-0020AF0BA770",
    "IID_IMediaSeeking": "36B73880-C2C8-11CF-8B46-00805F6CEF60",
    "IID_IBasicAudio": "56A868B3-0AD4-11CE-B03A-0020AF0BA770",
    "CLSID_SampleGrabber": "C1F400A0-3F08-11D3-9F0B-006008039E37",
}


def sections(d: bytes) -> list[tuple[int, int, int]]:
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    count = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    first = pe + 24 + opt
    out = []
    for i in range(count):
        o = first + i * 40
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", d, o + 8)
        out.append((rawptr, min(rawsize, vsize), base + va))
    return out


def to_va(secs: list[tuple[int, int, int]], off: int) -> int | None:
    for raw, size, va in secs:
        if raw <= off < raw + size:
            return va + off - raw
    return None


def references() -> dict[str, list[tuple[int, list[str]]]]:
    """For each ID found in the executable, its addresses and the functions that reference each."""
    d = EXE.read_bytes()
    secs = sections(d)
    text = TEXT.read_text(encoding="utf-8", errors="replace") if TEXT.exists() else ""
    funcs = text.split("//// FUNCTION ")[1:]
    found: dict[str, list[tuple[int, list[str]]]] = {}
    for name, g in GUIDS.items():
        needle = uuid.UUID(g).bytes_le
        i = d.find(needle)
        while i != -1:
            va = to_va(secs, i)
            if va is not None:
                sym = re.compile(rf"\b_?DAT_{va:08x}\b")
                users = sorted({f.split()[1] for f in funcs if sym.search(f)})
                found.setdefault(name, []).append((va, users))
            i = d.find(needle, i + 1)
    return found


def main() -> int:
    found = references()
    print(f"{'name':<28}{'copies':>7}  address -> functions that reference it")
    for name in GUIDS:
        if name not in found:
            print(f"{name:<28}{0:>7}  not found")
            continue
        for va, users in found[name]:
            print(f"{name:<28}{len(found[name]):>7}  {va:08x} -> {', '.join(users) or 'no decompiled reference'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
