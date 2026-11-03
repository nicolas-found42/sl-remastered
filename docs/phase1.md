# Phase 1 findings

This page records what phase 1 found about the stock executable and about `slrr-reverse`. Each result names the command that reproduces it. A result marked measured came from a script. A result marked inferred rests on an assumption that the text states. A second model reviewed the first version of this page, and its corrections are in this one.

## Exit checks

| Check | Status | Evidence |
|---|---|---|
| Every COM-style call attributed, with a stated rate | Done. 79% rest on the device global, 2% on an interface ID in the calling function, 19% on address range alone or nothing | `tools/run_com_census.sh` |
| `Present`, `Reset`, and `CreateDevice` located | `Present` and `Reset` found. `CreateDevice` not found | "Direct3D 9" |
| `docs/natives.tsv` with one row per native, all unread | Done, 360 rows | `tools/natives_extract.py` |
| Libraries loaded at run time listed | Done | "Libraries loaded at run time" |
| Pointer-width question answered | Done for one structure, with a recommendation | `docs/pointer-width.md` |
| `tools/check.sh` passes and `ref/` is unchanged | Done | `tools/check.sh` |

## Counting code in this executable

Every scan in this phase reads the functions that Ghidra defines. So the census is only as complete as that list, and the list was not complete.

Ghidra found 4,394 functions when it first analyzed the stock executable. They covered 68% of the 1,990,656 bytes of `.text`. Three results exposed the gap.

- Ghidra counted 360 calls to the native registration function at `0x00416b00`, and the decompiled text showed 353. The 7 missing calls register the `java.io.Controller` natives, and they sit in code that belongs to no function.
- A byte scan found a `Present` call at `0x004bb417`, where Ghidra had no function. The first instruction scan had reported no `Present` at all.
- The bytes there are `mov eax,[0x6495e4]`, `mov ecx,[eax]`, four `push 0`, `push eax`, `call [ecx+0x44]`, `inc [0x6495fc]`, and `ret`. That is `device->Present(0,0,0,0)` plus a frame counter, in a 22-byte function.

Two scripts close the gap. Both run once, when `tools/ghidra_run.sh` builds the project.

**Functions reached only by indirect calls.** The game's own classes call their methods through a vtable, a table of code pointers in the data sections. No instruction calls those methods directly, so Ghidra missed some. `tools/ghidra/DefineIndirectFunctions.java` scans `.rdata` and `.data` for runs of at least 3 consecutive 4-byte values that all point into `.text`, and it adds the handler column of `docs/natives.tsv`. It defines a function at each target that no function covers.

- 1,997 seeds, of which 1,640 came from vtable runs and 357 from native handlers.
- 715 seeds already lay inside a function. The script created 1,281 functions and failed on 1. The failed seed is `0x00480047`, and the cause is unknown.
- Coverage of `.text` rose to 84%.

**Code in undefined bytes.** After the first script, 289,732 bytes of `.text` were undefined, which means Ghidra had neither disassembled nor typed them. An earlier version of the gap-listing script could not report undefined bytes at all and printed 0. The fixed version found them. A probe, `tools/gap_code_probe.py`, counted `call` instructions in those bytes that land exactly on the entry of a known function. It found 2,296 of 2,824. Random bytes would give about 8. So the undefined bytes held a great deal of real code.

`tools/ghidra/DefineGapFunctions.java` disassembles each undefined stretch. It skips padding, defines a function where code starts, and lets the function follow its own jumps.

- 4,425 stretches. The script created 1,860 functions that cover 271,317 bytes.
- Coverage of `.text` is now 97%, which is 1,930,967 of 1,990,656 bytes.
- 49,540 bytes are still undefined. In the 47,513 of them that sit in undefined stretches, the probe finds 6 calls to a function entry, so they are mostly not code. The largest remaining gap is 2,027 bytes at `0x005e6815`, the end of the section.

The decompiler handled all 7,535 functions with 0 failures. `tools/define_quality.py` checks the new functions with three tests, none of which proves a function is real.

| Origin | Functions | Bad-instruction warnings | Start after padding or a return |
|---|---:|---:|---:|
| Vtable seeds | 1,019 | 0 | 1,007 (99%) |
| Native handler seeds | 262 | 0 | 262 (100%) |
| Undefined stretches | 1,860 | 39 | 1,731 (93%) |

The 39 warnings and the 129 functions from undefined stretches that do not start after padding may be fragments or false functions. 47 vtable functions and 180 gap functions start at odd addresses. The compiler does not require alignment, so that alone shows nothing. The vtable rule only requires 3 pointers in a row into `.text`, and it never checks that a target is the start of a function. A table that points into the middle of functions could create false ones. A rebuild from an empty project reproduces every count, and that proves repeatability, not correctness. The gap script is not idempotent. A second pass over a finished project created 49 more functions covering 11,142 bytes. That is why the project build runs both scripts once and never again.

Every count below is a lower bound, because code that no function covers is still invisible to the scans.

## slrr-reverse on arm64 macOS

Command: `python3 -I tools/probe_slrr_reverse.py`. It copies the engine to `local/slrr-reverse/` and never writes to `ref/`.

Measured:

- CMake configures, and the build fails. 7 of the 43 units fail.
- 36 units pass a syntax check. This overstates readiness. The Windows platform files compile with their `_WIN32` blocks switched off, and 192 source lines mention `_WIN32`. Linking and running are untested.
- The 7 failing units report 84 errors. 56 are layout assertions, 27 are missing Windows names, and 1 is a type error in `main_part1.inc` line 767.
- The missing names are `ExitProcess`, `IDirect3DTexture9`, `Sleep`, `UINT`, `WAVEFORMATEX`, `WAVE_FORMAT_PCM`, and `_stricmp`.

The cost sits in memory layout and not in function names. `docs/pointer-width.md` covers that.

## Direct3D 9

Command: `tools/run_com_census.sh`, then `python3 -I tools/com_census.py`. The census lists every call that loads an object's function table, pushes the object, and calls through a numbered slot. It records the slot and where the object pointer came from. It matches a call shape and not COM itself, so some matches are the game's own virtual calls.

Measured:

- 4,324 calls use the global at `0x006495e4`, in 192 functions, through 57 distinct slots. 56 of the 116 device methods beyond the three `IUnknown` ones are called.
- Evidence that the global is the `IDirect3DDevice9`. The constructor `FUN_004b7d00` stores the result of `Direct3DCreate9` at `0x006495e0`. Calls through `0x006495e4` reach slot `0x1d8`, the last method of the device interface. `FUN_004e6440` calls them in the order `SetFVF`, `BeginScene`, `DrawPrimitiveUP`, `EndScene`.
- `Present` and `Reset` each appear once on the device global. `Present` is `FUN_004bb410`. `CreateDevice` does not appear. It is a method of `IDirect3D9`, and the census did not trace that object.

Inferred: the method names come from the order of methods in the Direct3D 9 header. `tools/com_census.py` lists them in that order, and no one has checked the list against the header file.

| Purpose | Calls |
|---|---:|
| State setting | 4,156 |
| Drawing and clearing | 57 |
| Queries | 57 |
| Resource creation | 22 |
| Frame control (`BeginScene`, `EndScene`, `Present`, `Reset`) | 8 |
| `Release` | 1 |
| Other slots | 23 |
| Total | 4,324 |

The other slots include `GetRenderState` (10 calls), `GetSamplerState` (6), `StretchRect`, `SetGammaRamp`, `GetFrontBufferData`, `GetViewport`, `CreateOffscreenPlainSurface`, `TestCooperativeLevel`, and `GetAvailableTextureMem`.

Four methods make up 3,953 of the calls. They are `SetSamplerState` (1,320), `SetTextureStageState` (1,255), `SetTexture` (1,011), and `SetRenderState` (367). The code has 46 draw call sites and 11 `Clear` calls. It has 6 `CreateTexture` call sites, 1 `CreateCubeTexture`, 3 `CreateVertexBuffer`, 2 `CreateRenderTarget`, 2 `CreateVertexDeclaration`, and 5 `CreateQuery`. It uses few programmable shaders, with 2 `CreateVertexShader` call sites, 13 `SetVertexShader`, and 12 `SetPixelShader`.

Inferred: texture stage states belong to the Direct3D 9 fixed-function pipeline, where a list of stage settings describes how textures blend. Metal has no fixed-function pipeline, so the graphics port has to turn each combination of states into a shader. That is a larger task than 46 draw call sites suggest.

Inferred from the decompiled `FUN_004e6440`: the game has its own renderer object on top of Direct3D. The global `0x006495dc` points to it, and it has its own virtual methods at slots `0xe0` to `0x120`. The port has to rebuild that layer too.

The instruction scan found 4,324 calls on the device global. A second method, which matches call syntax in the decompiled text, found 4,163, which is 3.7% fewer. Each misses calls the other catches. Both are lower bounds. The instruction scan looks back 40 instructions through the listing and not through the program's flow. It labels a call "argument" when it finds no origin in that window, and it did so for 199 calls. The cross-check covers only the device global, and nothing cross-checks the other 1,164 calls.

## DirectSound, DirectInput, and DirectShow

Command: `python3 -I tools/guid_scan.py`. It searches the executable for known interface IDs and lists the functions that reference each one. A hit is proof, because a 16-byte match is not a coincidence. A miss proves nothing, because the list holds only the IDs we know.

| Interface ID | Referenced by |
|---|---|
| `IID_IDirectSound3DBuffer` | `FUN_0055b490` |
| `IID_IDirectSound3DListener` | `FUN_005598c0` |
| `GUID_SysKeyboard`, `GUID_SysMouse` | `FUN_00556150` |
| `CLSID_FilterGraph`, `IID_IGraphBuilder` | `FUN_004f8dd0`, `FUN_00559ed0`, `FUN_0055c470` |
| `IID_IMediaControl` | `FUN_004f8dd0` |
| `IID_IMediaSeeking` | `FUN_004f8dd0`, `FUN_005cfe00`, `FUN_005d53d0`, `FUN_005d54a0` |
| `IID_IBasicAudio` | `FUN_00559ed0` |
| `IID_IDirect3DTexture9` | `FUN_005a0b25` |
| `IID_IDirectInput8A` | Found in the data, no decompiled reference |

No Direct3D device or `IDirect3D9` ID appears, which is expected, because the game never asks those objects for another interface.

Inferred: the DirectShow IDs mean the game plays video or audio through DirectShow. The executable also imports `mciSendCommandA`, so some playback may go through the older media control interface.

Slot numbers mean different methods on different interfaces. Slots `0x40` and `0x44` in `FUN_0055b490` are `SetMaxDistance` and `SetMinDistance` on a DirectSound 3D buffer, and that function is the only reference to `IID_IDirectSound3DBuffer`. The same slots in `FUN_004bdc10` are a texture's `GetLevelDesc`. My first count had treated them as one thing.

`python3 -I tools/com_attrib.py` assigns each of the 5,488 calls to a group.

| Group | Evidence | Calls |
|---|---|---:|
| Device global `0x006495e4` | Strong | 4,324 |
| Other objects at `0x550000` to `0x5e6fff` | Address range only | 718 |
| Other objects at `0x4b0000` to `0x4fffff` | Address range only | 323 |
| DirectShow | Coarse, an interface ID in the calling function | 62 |
| DirectSound | Coarse | 23 |
| Direct3D texture interface ID | Coarse | 18 |
| DirectInput | Coarse | 16 |
| Unexplained | None | 4 |

An address range names no interface. It only groups calls. The range from `0x550000` is not library code. The executable's entry point is `0x005514c0`, and the game's own sound, input, and video functions (`FUN_0055b490`, `FUN_005598c0`, `FUN_00556150`, `FUN_00559ed0`, `FUN_0055c470`) sit inside it. The 4 unexplained calls use slots `0x228` and `0x22c`, which lie beyond every interface the census attributes to. They are probably the game's own virtual calls.

## Libraries loaded at run time

`LoadLibraryA` appears in 4 functions, and `GetProcAddress` in 5. They load `d3d9.dll` and `d3d9d.dll` for the entry points `DebugSetMute` and `Direct3DShaderValidatorCreate9`, `OleAut32.dll` for `LoadRegTypeLib` and `LoadTypeLib`, and `user32.dll` for `MessageBoxA`, `GetActiveWindow`, and `GetLastActivePopup`. The executable loads no game plug-ins.

The executable's strings include "D3DX9 Shader Assembler", "D3DX9 Shader Compiler", a PNG library message ("Resetting write_data_fn to NULL."), and a zlib message ("incorrect header check"). It imports no D3DX, PNG, or zlib library. That code is therefore linked into the executable. This work has not located it.

The strings also show that the executable contains its own compiler for the game's Java-like scripts ("JVM::compileSource: Syntax error"). `SLRR-JVM-compiler` reimplements it.

## Native-function registry

Command: `python3 -I tools/natives_extract.py`. The game registers each native by calling `FUN_00416b00` with a class name, a method name, a type descriptor, and a handler address. The script scans the code bytes for every such call and reads the strings from the executable.

Measured:

- The registry holds 360 natives in 37 classes, with no duplicate rows. The largest classes are `java.game.parts.WheelRef` (35), `java.game.parts.Part` (29), and `java.util.resource.GroundRef` (25).
- Three methods count 360 registrations. They are the byte scan, Ghidra's reference list for `FUN_00416b00`, and the decompiled text after the function fixes. The Ghidra count came from an interactive check through the MCP server and is not saved. Before the fixes the text showed 353.
- The byte scan and the text differ in 6 rows, and only in the handler address. Those 6 handlers are one-instruction jumps. The bytes hold the jump's own address, and the text shows its target. The script checks this against the raw bytes, and all 6 hold. The natives are `GfxEngine.closeVideo`, `currDisplayMode`, `isPlayingVideo`, `numDisplayModes`, and `Sound.has3DHardware` and `hasMixHardware`.
- All 8 handler addresses that `slrr-reverse`'s export tool hard-codes match the stock registry. That is evidence that its address knowledge applies to this build. It covers the render area and nothing wider.

Comparison with the 376 declarations in `slrr-reverse`'s `natives_table.inc`:

- 343 distinct names match, after removing the `_1` and `_2` overload suffixes from their side.
- 3 more match once the constructor spelling is normalized, because `<init>` becomes `init_Ypr` and so on.
- 16 names appear only in `slrr-reverse`. They include `netHost`, `netJoin`, `netLeave`, `pollTimers`, two `finalize` methods, `Camera.activate`, `Camera.deactivate`, and several `PhysicsRef` and `RenderRef` methods.
- One overload count differs. `GroundRef.getRouteLength` has 2 overloads in the stock registry and 1 in `slrr-reverse`.

Inferred: those 16 either come from a different build or are registered by a route other than this function. The stock executable imports no Winsock library, which fits the missing network natives. Phase 3 has to settle where the 16 come from before it relies on them.

## Open gaps

- `CreateDevice` is not located. `IDirect3D9` is the object that calls it.
- 3% of `.text` is still outside any function, and the 1,860 functions found in undefined stretches carry the weakest evidence of the three groups.
- 1,045 COM-style calls rest on address range alone or on nothing.
- The census counts call sites. It does not count calls per frame.
- `slrr-reverse` has not been linked or run.
- No check yet shows that every vtable target is a true function start.

## Next

1. Trace the `IDirect3D9` object, which holds `CreateDevice` and the display-mode calls.
2. Decide the pointer-width rule by porting one native handler against a clean structure and against the copied layout.
3. Start the data loaders of phase 2, with the parsers in `SLRR_rev` as the reference.
