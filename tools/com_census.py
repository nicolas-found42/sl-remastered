#!/usr/bin/env python3
"""Summarize local/census/com_calls.tsv, the output of tools/ghidra/ComCallCensus.java.

Labels the slots of one object source with the IDirect3DDevice9 method names
from the Direct3D 9 header, in vtable order, and prints a usage table.

    python3 -I tools/com_census.py [object_source]    # default global:006495e4
"""

import csv
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TSV = ROOT / "local" / "census" / "com_calls.tsv"

DEVICE9 = """QueryInterface AddRef Release TestCooperativeLevel GetAvailableTextureMem
EvictManagedResources GetDirect3D GetDeviceCaps GetDisplayMode GetCreationParameters
SetCursorProperties SetCursorPosition ShowCursor CreateAdditionalSwapChain GetSwapChain
GetNumberOfSwapChains Reset Present GetBackBuffer GetRasterStatus SetDialogBoxMode
SetGammaRamp GetGammaRamp CreateTexture CreateVolumeTexture CreateCubeTexture
CreateVertexBuffer CreateIndexBuffer CreateRenderTarget CreateDepthStencilSurface
UpdateSurface UpdateTexture GetRenderTargetData GetFrontBufferData StretchRect ColorFill
CreateOffscreenPlainSurface SetRenderTarget GetRenderTarget SetDepthStencilSurface
GetDepthStencilSurface BeginScene EndScene Clear SetTransform GetTransform
MultiplyTransform SetViewport GetViewport SetMaterial GetMaterial SetLight GetLight
LightEnable GetLightEnable SetClipPlane GetClipPlane SetRenderState GetRenderState
CreateStateBlock BeginStateBlock EndStateBlock SetClipStatus GetClipStatus GetTexture
SetTexture GetTextureStageState SetTextureStageState GetSamplerState SetSamplerState
ValidateDevice SetPaletteEntries GetPaletteEntries SetCurrentTexturePalette
GetCurrentTexturePalette SetScissorRect GetScissorRect SetSoftwareVertexProcessing
GetSoftwareVertexProcessing SetNPatchMode GetNPatchMode DrawPrimitive
DrawIndexedPrimitive DrawPrimitiveUP DrawIndexedPrimitiveUP ProcessVertices
CreateVertexDeclaration SetVertexDeclaration GetVertexDeclaration SetFVF GetFVF
CreateVertexShader SetVertexShader GetVertexShader SetVertexShaderConstantF
GetVertexShaderConstantF SetVertexShaderConstantI GetVertexShaderConstantI
SetVertexShaderConstantB GetVertexShaderConstantB SetStreamSource GetStreamSource
SetStreamSourceFreq GetStreamSourceFreq SetIndices GetIndices CreatePixelShader
SetPixelShader GetPixelShader SetPixelShaderConstantF GetPixelShaderConstantF
SetPixelShaderConstantI GetPixelShaderConstantI SetPixelShaderConstantB
GetPixelShaderConstantB DrawRectPatch DrawTriPatch DeletePatch CreateQuery""".split()

assert len(DEVICE9) == 119

PURPOSES = {
    "state setting": set("""SetRenderState SetTextureStageState SetSamplerState SetTexture SetTransform
        SetMaterial SetLight LightEnable SetViewport SetFVF SetVertexShader SetPixelShader
        SetVertexShaderConstantF SetPixelShaderConstantF SetStreamSource SetIndices
        SetVertexDeclaration SetRenderTarget SetDepthStencilSurface SetSoftwareVertexProcessing
        SetClipPlane SetScissorRect SetStreamSourceFreq""".split()),
    "drawing": set("""DrawPrimitive DrawIndexedPrimitive DrawPrimitiveUP DrawIndexedPrimitiveUP Clear""".split()),
    "frame control": set("BeginScene EndScene Present Reset".split()),
    "resource creation": set("""CreateTexture CreateCubeTexture CreateVertexBuffer CreateIndexBuffer
        CreateRenderTarget CreateVertexDeclaration CreateVertexShader CreatePixelShader
        CreateQuery CreateDepthStencilSurface""".split()),
    "queries": set("""GetDeviceCaps GetDisplayMode GetRenderTarget GetLight GetLightEnable
        GetTextureStageState ValidateDevice GetTexture GetDepthStencilSurface""".split()),
    "release": {"Release", "AddRef", "QueryInterface"},
}


def main(source: str) -> int:
    rows = [r for r in csv.DictReader(open(TSV), delimiter="\t") if r["object_source"] == source]
    if not rows:
        print(f"no calls from {source}")
        return 1
    calls = Counter(int(r["vtable_off"], 16) for r in rows)
    funcs: dict[int, set[str]] = {}
    for r in rows:
        funcs.setdefault(int(r["vtable_off"], 16), set()).add(r["function"])
    print(f"{source}: {len(rows)} call sites, {len(calls)} distinct slots")
    print(f'{"slot":>6}  {"method":<28}{"calls":>6}{"callers":>8}')
    for off, n in sorted(calls.items()):
        idx = off // 4
        name = DEVICE9[idx] if idx < len(DEVICE9) else "beyond IDirect3DDevice9"
        print(f"0x{off:03x}   {name:<28}{n:>6}{len(funcs[off]):>8}")
    used = sum(1 for i in range(3, 119) if i * 4 in calls)
    print(f"\n{used} of the 116 methods beyond the IUnknown three are called")
    if source == "global:006495e4":
        by_purpose = Counter()
        for off, n in calls.items():
            name = DEVICE9[off // 4] if off // 4 < len(DEVICE9) else "?"
            for purpose, names in PURPOSES.items():
                if name in names:
                    by_purpose[purpose] += n
        print(f"\n{'purpose':<20}{'calls':>6}")
        for purpose in PURPOSES:
            print(f"{purpose:<20}{by_purpose[purpose]:>6}")  # calls through the device global only
        other = sum(calls.values()) - sum(by_purpose.values())
        print(f"{'other':<20}{other:>6}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "global:006495e4"))
