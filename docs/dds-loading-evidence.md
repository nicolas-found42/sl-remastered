# Loading picture DDS evidence

Live Ghidra identified `StreetLegal_Redline.bak.exe` with MD5
`e686099e7233bb7d7c461b1b35c5064c`. The saved stock decompile and assembly for
`FUN_0059c7e8` are in `local/census/DecompileAt-0059c7e8.log`.

The function checks the `DDS ` signature and at least 124 bytes after it. It
reads width at byte 16, height at byte 12, and mip count at byte 28, treating
zero as one. It reads the pixel-format record starting at byte 76 and recognizes
DXT1. For DXT1, each mip occupies `ceil(width / 4) * ceil(height / 4) * 8`
bytes. Each following mip halves each dimension, with a minimum of one. The
function stores a pointer into the input blocks and rejects insufficient data.
It also handles formats and texture shapes outside the new reader's scope.

`ref/raw/frontend/textures/Loading.dds` has a 1024 by 1024 DXT1 image, one mip,
and 524288 bytes of blocks after its 128-byte header. The Wine file trace at
`local/wine-runs/blackwindow-loading/game-run.log` records stock opening this
file. That trace does not establish its final on-screen appearance.

`ddsParseDxt1` reads 2D DXT1 blocks as offsets into the input buffer. It does not render them. It rejects zero dimensions, excessive mip counts,
nonstandard header sizes, cube maps, and volumes. These are host bounds and scope
checks, not claims that stock rejects the same inputs. The test reads the actual
loading file and checks block sizes, mip dimensions, and truncated data. A
synthetic mip fixture checks non-multiple-of-four dimensions. `ddsDecodeDxt1` expands the blocks to RGBA using the DXT1 palette rules in
[Microsoft's Direct3D 9 format documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d9/opaque-and-1-bit-alpha-textures).
The tests cover both palette modes, transparent pixels, equal endpoints, partial
blocks, and truncation. A separate run decoded the actual loading image with
FFmpeg and compared all 4194304 RGBA channel bytes. 236023 bytes differed, all
by one or less. This is consistent with different interpolation rounding and
does not prove stock pixel parity. The comparison used FFmpeg's DDS decoder,
not the stock game.

Texture filtering, resource cloning, scene traversal, and stock visual parity
remain unverified.

`resourceTextureLookup` resolves `frontend.rpk` local id `0xa8` to
`frontend/textures/Loading.dds`. `resourceTextureLoad` follows that path and
decodes the first mip to RGBA. Its test checks the file name, image dimensions,
and a decoded center pixel. It also loads the existing PTX fixture through the
same interface. The loader supports only PTX and DXT1 DDS, limits input files to
256 MiB, and retains the existing path containment and case-sensitive filesystem
lookup. It reports unsupported formats and failures without drawing anything.
The mip choice and `lod_bias` interpretation during stock rendering remain open.

The installed `Osd.class` tree 93 references `createRectangle` from `createBG`.
Tree 94 constructs a `Rectangle` using the last group, converted coordinates,
`SCALE_FS`, and viewport dimensions, and adds it to the rectangle list. These
are script references, not a stock frame trace. Rectangle material cloning,
coordinate conversion, texture selection, and draw ordering still need tracing.
The diagnostic `loading_view` loads resource `0xa8` and fits the image inside its
window. Its scale and position are diagnostic choices, not stock layout evidence.

The type-7 constructor `0053d060` installs vtable `005f36dc`. Demand setter
`0053d3c0` smooths and clamps the requested LOD; `0053d1a0` loads the payload
through `0053cc70`, which parses `sourcefile`, `lod_bias`, and `lod_amp` and
creates a renderer texture. `0053d310` releases the renderer objects on unload.
The saved per-address logs identify the stock executable by MD5. Stock texture
file I/O and decode may be asynchronous: `00505c00` queues completed file data,
and `00507e50` subsequently marks the renderer object ready. A later resource
update promotes its load priority. The host currently decodes and uploads to SDL
synchronously, keeps the stock initial priority at zero, and does not claim the
stock timing or later LOD promotion. It retains the uploaded texture on the
resource node, shares it with material drawing, and releases it when the node
dies. The renderer must outlive the resource world.

`resource_ref_set_test` caches the real Loading.dds and SLII24 font atlas,
draws each through its retained handle, verifies repeat-cache reuse, and checks
texture release on teardown. `scene_2d_test` checks that material drawing can use
the retained handle without a second pixel upload. Only the existing bounded
PNG, PTX, and DXT1 DDS decoders are supported. Nonzero texture flags are rejected;
renderer LOD promotion and stock pixel parity remain open.
