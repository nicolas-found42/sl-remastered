# 3D drawing for SCX models

This page says what the host can now draw in 3D, which parts come from the stock executable, and which parts are host choices. Code: `src/raster3d.hpp`, `src/raster3d.cpp`. Tests: `tests/raster3d_test.cpp`. Viewer: `tools/model_view.cpp`.

In plain words: before this, the host could move points around and cut triangles to the screen, but it could only draw triangles through SDL's 2D helper, which has no depth sorting and bends textures wrongly on slanted surfaces. `Raster3D` is a small drawing engine we wrote ourselves. It works out every pixel of every triangle on the processor, keeps a depth value per pixel so near things hide far things, and bends textures correctly with perspective. `Render2D` is unchanged. `Raster3D::blit` hands the finished picture to it as one texture, and `Render2D` shows it.

## How to run it

    cmake -S . -B local/build-3d -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build local/build-3d --target raster3d_test model_view
    local/build-3d/raster3d_test
    local/build-3d/model_view "<2005 install>/cars/racers/Einvagen_data/meshes/chassis.scx" --auto-texture

`model_view` opens a window and orbits the model. Esc or closing the window quits. Useful options: `--seconds N`, `--capture FILE.bmp` (saves the last frame), `--headless` (no window), `--bench N` (time N frames), `--zoom X`, `--list` (print every piece with its material), `--texture FILE` (repeat, for texture index 0, 1, 2), `--cull none|cw|ccw`, `--skip-unsupported`, `--strict`. Convert a capture to PNG with `sips -s format png a.bmp --out a.png`.

## What is stock-derived

| Fact | Evidence |
|---|---|
| Projection and view come from `cameraViewState`: row vectors, right-handed projection, D3D clip volume (`-w<=x<=w`, `-w<=y<=w`, `0<=z<=w`). | `src/world_model.cpp` (from `004ba4b0`, `0058eadb`). |
| Viewport mapping: `x = (left + (nx+1)*width/2) * outputWidth`, `y = (top + (1-ny)*height/2) * outputHeight`. | `cameraViewportPoint`. A test checks `raster3dViewportPoint` gives identical numbers. |
| Mesh positions are scaled by 0.1 before the world matrix. World units are decimetres. A chassis is 378 mesh units long, 37.8 after scaling. | `sceneDrawCameraMesh`; `makeCameraResource` multiplies metres by 10. |
| Clipping uses the six homogeneous planes in the order and arithmetic of `clipCameraTriangle`. | A test compares 2,000 random triangles with that function: identical vertices and UVs. |
| The SCX material "flags" word is the stock material type: it is the value switched on by `004befc0` (`param_1 & 0xffffff`). Names for types 0-6 and 0x30-0x33 are the `CMaterial_*` strings registered in `004bf800`: 0 Tex, 1 MixTA, 2 MixVCA, 3 ColorMixTex, 4 TexSpecular, 5 MixTASpecular, 6 MixVCASpecular, 0x30 Color, 0x31 ColorSpecular, 0x32 ColorSpheremap, 0x33 ColorSpecularSpheremap. | `local/census/DecompileAt-004befc0.log`, `DecompileAt-004bfc53.log` (the function containing `004bf800`). |
| A type not in that switch falls to the default case, which builds the Tex material (`004c4a40`). Types 0x40, 0x100, 0x101, 0x901, 0x1000 occur in the data and take this path. | `004befc0` default case. |
| Function word bit `0x40000000` sets entry flag 1 and `0x80000000` sets flag 2 (type 0 constructor and the others). Flag 1 selects the `0x4cc` collection, which `00525930` draws far to near by object depth. That is the sorted transparent pass. The only `0x40000000` mesh with a name we know is `etalon_negyzet_alpha`. | `004befc0`, `docs/rectangle-scene-evidence.md`. |
| Function word bit `0x20000000`: `004e5890` stores the value of material property `0x05000000` at entry `+0xc`, or `0x38` when the property is absent. | `local/census/DecompileAt-004c4b10.log` (holds `004e5890`). |
| With `0x80000000`, the type 0 vertex format becomes `0x1c1` instead of `0x41`: it adds the emissive and colour streams. | `004c68e0` in the same log. |
| Stock texture address mode (clamp or wrap) is decided per texture object (flag byte at `+0x74`), not by the SCX map. | `004c4b90` in the same log. |

## Where the host guesses

These are choices, not proof. Each is visible in the code or the viewer output.

1. **Alpha blend factors.** `0x40000000` is drawn as source-alpha over the background. The stock ordering proves the pass is sorted transparency. The factors are the usual `SRCALPHA/INVSRCALPHA`, which the stock text path sets (`004f4de0`, states 0x13=5 and 0x14=6). Not read from a mesh material.
2. **Alpha test.** `0x20000000` is drawn as "alpha*255 >= reference", reference from property `0x05000000`, default `0x38`. That the stored word is the alpha reference is an inference from its values (0x14, 0x19) and default.
3. **`0x80000000`.** Vertex colours multiply the texture. The real meaning is probably "diffuse texture plus a second-UV-set light map" (garage and dealer meshes have a slot 4 map on channel 1 for these materials). The light map and the emissive stream are not drawn. Both `0x80000000` and `0xc0000000` (blend plus this) occur; the second is windows, railings and doors in the dealer mesh.
4. **Front faces.** On the Einvagen chassis, discarding triangles that wind clockwise on screen removes the inside faces and leaves a solid body (`.scratch/shots/r3d-cull-cw.png` against `r3d-cull-ccw.png`). The viewer defaults to that. It is an observation, not a stock read. Stock sets its cull state elsewhere and we have not found it for meshes.
5. **Lighting.** Stock lighting is not reproduced. The viewer uses one fixed directional light on the vertex normals (ambient 0.45, diffuse 0.65). `--no-light` turns it off. A textured material ignores the SCX diffuse colour tint (the stock applies it through D3D lighting); only its alpha is kept. Flat materials use the diffuse colour.
6. **Vertex colour byte order.** Assumed D3DCOLOR (B, G, R, A). Not checked against a lit scene.
7. **Mip maps.** DDS and PTX file loaders now retain the stored mip levels. Raw RGBA callers still generate a box-filtered chain through Raster3DTexture::build. Filtering (point, bilinear on the nearest level, trilinear) is the host's, not D3D's. Texture level of detail uses the exact screen-space derivative of the perspective-correct UVs.
8. **Texture lookup in the viewer.** `--auto-texture` finds `<Car>_<material name>.dds` in the model's sibling `textures` folder (for example material `F door` finds `Einvagen_F_door.dds`), then falls back to the mesh file name. Stock gets textures from the pack resource, not by name. Meshes whose material names are 3ds Max names (`Material #429`) get no texture and show the diffuse colour.

## Which material types draw

`raster3dScxMaterial` maps an SCX material to rasterizer states or refuses with an error that names the material.

- **Drawn exactly as a base layer:** type 0 (Tex) and 0x30 (Color).
- **Drawn as their base layer only, extra layers not reproduced** (refused with `--strict`): types 1-6, 8, 0xc, 0xd, 0xf, 0x15, 0x31-0x33 and their aliases 0x10, 0x14, 0x17, 0x20, 0x24, 0x25, 0x27. The extra layers are the specular, sphere-map, environment and two-texture mixes. The viewer prints one note per kind of omission.
- **Fail with "unsupported stock material type":** the other types the stock switch lists (7, 9, 0xa, 0xb, 0xe, 0x11-0x13, 0x16, 0x18-0x1f, 0x21-0x23, 0x26, 0x34, 0x35, 0x37). In the 2005 data, type 7 appears in the garage mesh. `--skip-unsupported` lists and skips those meshes.
- **Fail with "unsupported function word bits":** any bit other than `0x20000000`, `0x40000000`, `0x80000000` (the data also has `0x02000000`, `0x04000000`, `0x08000000`, `0x10000000`). `--lenient` ignores them. Blend plus test together is also refused.

The rasterizer core never guesses. `Raster3D::draw` returns an error for an unknown blend, cull, filter or address value, a missing UV array for a textured material, an index out of range, or a non-finite vertex or matrix.

## What the tests prove

`tests/raster3d_test.cpp` (built with address and undefined-behaviour checks):

- **Perspective.** A tilted floor with an 8x8 checkerboard texture. For every pixel the test intersects the view ray with the plane by algebra, computes the checker cell, and compares with the rendered pixel. 6,834 pixels away from cell edges: 0 wrong with perspective-correct interpolation, 2,433 wrong with the `perspectiveCorrect = false` control. The control proves the test can fail.
- **Mip filtering.** A floor of 1-texel checker cells seen at a slant: the far rows stay within 0 of mid grey when trilinear and swing 128 when point sampled.
- **Depth.** Near and far triangles give the same image in either draw order. With the depth test off, draw order wins. A draw with depth write off leaves the buffer alone.
- **Fill rule.** A quad of two triangles drawn with 50% blend covers exactly 80x60 pixels, none twice and none outside. This checks the top-left rule and the shared diagonal.
- **Alpha.** Alpha test rejects below the reference; alpha blend gives 255,128,128 over red for 50%; opaque ignores alpha.
- **Cull, viewport rectangle, clipping behind the camera, DXT3 and DXT5 decoding with known blocks, error cases, BMP writing, the `Render2D` upload path on a software surface.**
- **Real data**, when `.scratch/2005-extract` exists: `chassis.scx` through the whole path.

## Performance

Release build, one thread, 640x480, Apple Silicon. Einvagen `chassis.scx` (3 meshes, 3,054 triangles, 512x512 textures), measured with `model_view --headless --bench 300`:

| Case | Pixels written | Time per frame | Frames per second |
|---|---:|---:|---:|
| Model fitted to the frame (bilinear) | 69,000 | 1.4 ms | about 710 |
| Camera moved in so the car fills the frame | 280,000 | 3.6 ms | about 280 |
| Fitted, trilinear | 69,000 | 2.1 ms | about 475 |
| Fitted, point | 69,000 | 0.9 ms | about 1,070 |

In a real window the viewer ran at about 160 frames per second (487 frames in 3.0 s), with 1.4 ms of that in the rasterizer. The rest is the texture upload and present.

## Frame-loop integration

`renderActiveViewport` calls `CameraFrame::draw` and `SceneCameraRenderer`.
The camera renderer now has a bounded depth-buffered path for opaque spatial
meshes: it uses `Raster3D`, reuses its framebuffer and cached textures, and
composites the result before text. `raster3d.cpp` is part of `sl_data`.

This path is not a complete city or car renderer. It rejects fog, lighting and
normal streams, skinning, extra vertex streams, nontrivial material mappings,
and mixed transparent/spatial passes. Mounted type-1 polygons can be queued,
but their ground draw is unsupported. No live city or car rendering has been
verified.

Stock `00526da0` converts sector POLY groups into vertices with position,
normal, packed color and up to four transformed UV channels. For the observed
city group 9, legacy material flags `0x1840` map through `004be950` to MixTA;
the importer's `0x80000000` flag routes its pass through `004dfe40` for
lighting and shadow stages. A single-texture, unlit draw is not equivalent.
The host still needs ground candidate drawing, retained SCY material inputs,
multi-channel textures and UVs, and that lighting/pass ordering. Mounted
type-13 light payloads and per-camera candidate registration are modeled;
per-object selection and projector passes are not.

## Honest limits

- **Not a match of the stock rasterizer.** The original draws through Direct3D 9 on the graphics card. Nothing here compares our pixels with it. D3D9's pixel centre convention (half-pixel offset), its texture filtering, depth buffer format and blend arithmetic are not reproduced. Our rules are the common ones: pixel centres at +0.5, top-left fill rule on 1/256-pixel snapped vertices, 32-bit float depth with `<=`.
- **What was compared.** Perspective correctness was compared with an algebraic ray-plane calculation. Clipping was compared with the host's own `clipCameraTriangle`. Viewport mapping was compared with `cameraViewportPoint`. The visual shots were judged by eye. Stock x87 arithmetic differs from host float arithmetic at rounding boundaries, as noted elsewhere in the docs.
- **Material states** marked as guesses above are inferences. The next evidence step is the draw function of the `0x4cc`, `0x448/0x474` and `0x50c` collections (vtable `005f2e4c` and its neighbours) and the object behind `DAT_00649f38` (material type `0xfffffc`, `004dfdc0`), which the `0x80000000` path calls.
- **Ghidra.** The Ghidra window was not running, so `get_program_info` was unavailable. The extra decompiles (`004c4b10` and 10 neighbours, `004bfc53`) were run with `tools/ghidra_run.sh DecompileAt.java`, whose script refuses to run unless the loaded program's MD5 is `e686099e7233bb7d7c461b1b35c5064c`; the log header records it.

## Fixed-function ground state, 2026-10-30

`src/raster3d.hpp` / `src/raster3d.cpp` now record the D3D fixed-function state the
stock ground passes set, and evaluate the texture-stage combiner for the evidenced
op/args. Tests: the `testGroundFvfPackedSplit`, `testCombiner` and
`testGroundStateMachine` cases in `tests/raster3d_test.cpp`.

- **State record.** `Raster3DFixedState` holds eight `Raster3DTexStage` slots whose
  op/arg fields are `std::optional`. There is no modeled device default: consuming a
  slot no applied pass set fails explicitly. Slots a pass leaves branch-dependent are
  marked in `Raster3DTexStage::unknown` and refuse evaluation even with an inherited
  value, because the pass may or may not have overwritten it. Untyped stage and
  sampler states retain the latest value per state key, rather than a bounded
  event log that fills after repeated passes.
- **Lighting cache.** `Raster3DGroundStateMachine::applyLightingMode` reproduces
  `FUN_004bb5f0`: the cached mode byte gates reapplication unless forced; a real
  change writes render state `0x89`, sets dirty bit `0x08`, and turning lighting off
  disables the eight tracked light slots. Slot reapplication on enable is not modeled
  beyond preserved state.
- **Mode 3 (`FUN_004c90a0`).** The state record handles ordinal zero for both
  material layouts and capability values 0, 1 and 2, ordinal one for 0 and 1,
  and nonzero ordinals for capability 2. `DAT_00617930` is selected when the
  material has a `+0x4c` texture, otherwise `DAT_00617928`. Observed slot
  presence, resolved device textures (including actual stock defaults), and
  texture-factor alpha are required; capability-two third-map stages write
  `TEMP` and consume it in the following stage.
  Render-state effects of `FUN_004e7760` and some sampler restoration are not
  represented; these records alone are not a complete draw implementation.
  Other ordinals fail explicitly.
- **Mode 0 (`FUN_004dfe40`).** `FUN_004e7b80` returns a projector, not a texture;
  the projector branch now records both resolved texture bindings and transforms
  per selected projector, the supplied device light/material values, and the
  stock sampler and stage writes. The caller must supply the exact mode hook
  and alpha-test state; actual projector selection, matrix construction and
  lighting remain outside this state recorder. The no-projector branch requires
  the observed blend and sampler defaults before applying the two-stage reset.
- **Combiner.** `raster3dCombineStage` evaluates the evidenced ops (`DISABLE`,
  `SELECTARG1/2`, `MODULATE`, `ADD`, `BLENDTEXTUREALPHA`, `MULTIPLYADD` with args `DIFFUSE`,
  `CURRENT`, `TEXTURE`, `TFACTOR`, `SPECULAR`, `TEMP` plus `COMPLEMENT` /
  `ALPHAREPLICATE`. Fixed draws require an observed CURRENT or TEMP result
  target and reject TEMP reads before its first write.
  Stage 0 `DISABLE` yields the diffuse colour (D3D first-stage convention); later
  stages leave the running result final. Clamp is 0..1 on every operation.
- **Fixed draw subset.** `Raster3DFixedDraw` consumes explicitly resolved per-stage
  textures and (u,v,q) coordinates, vertex diffuse/specular and inherited stage
  state. It evaluates enabled stages after perspective interpolation, checking
  binding identity and refusing unknown stage operations or sampler settings.
  It currently admits matching point or bilinear min/mag filters with explicit
  none/point/linear mip filtering, using supplied texture levels, projected
  coordinate gradients, and observed LOD bias and maximum-level state. It
  requires caller-resolved lighting, rejects alpha blending until its factors
  are modeled, and is not connected to the ground scene.
  `raster3dProjectorCoordinates` applies an observed camera-space texture matrix
  in projected COUNT3 or unprojected COUNT2 form; it does not derive the stock
  projector matrices or select the projector's resources.
- **FVF 0x15003f.** `raster3dGroundFvf` decodes the 56-byte engine ground vertex
  (3f position, 3f normal, two packed words, three float2 UV sets). The packed
  colour split is exactly the stock read of POLY source words [6]/[7]:
  `diffuse = word0 & 0xff000000`, `specular = (word0 & 0x00ffffff) | (word1 << 24)`.

The camera scene now submits visible POLY groups and non-UI SCX instances to
`SceneRaster3D`, using a shared depth buffer before the 2D overlays. POLY uses
the first SCY texture layer and UV set; additional ground layers, projectors,
skinning and stock lighting are not implemented. Missing textures fall back to
the diffuse colour. A chassis mesh can be drawn at the live car root while part
render bindings remain unavailable. Fog is a host approximation: linear in
camera-space depth, rather than the radial distance used by the 2D path.

## Ground blend, depth and clipping in live frames (2026-11-01)

`SceneRaster3D` draws every visible POLY group, non-UI SCX instance and car part
into one depth buffer, so a nearer piece occludes a farther one regardless of
submission order. Clipping is the six-plane homogeneous clip in the plane order
and arithmetic of `clipCameraTriangle`; the rasterizer counts clipped, culled,
outside and degenerate triangles per layer in `run log` lines
`world raster <layer> triangles`.

Two live captures exercise the path (seed 235, Free Ride, 800x600 content in a
1265x949 window):

| Run | Scene | Observed |
|---|---|---|
| `local/runs/20261101T110831Z-47746` | Emer Nonus Street GT at the wall ahead of the spawn, driven into it and braked | ground layer: 48 pieces, **48 blended**; car 2646 triangles, 30,841 depth-rejected pixels; ground 405,677 written pixels with no depth write |
| `local/runs/20261101T112856Z-82555`, `...T112935Z-83910` | same scene and script after the fix | ground layer: 47 pieces, **0 blended**; ground pixels now depth-tested (787,916 written), car still occludes correctly in the frame; car-part 52,000+ triangles, 89 depth-rejected |

### Correction: legacy flag 0x1000 is not a transparency switch

Stock `FUN_004be950` (`local/census/all.c:138340`) chooses the renderer material
type from the SCY material's `+0x28` flags, then decides blending separately:
`(flags & 1) || diffuse.a < DAT_005f0d28` selects the transparent branch, and
only inside it does `flags & 0x800` select the alpha test (`FUN_004e6b50(0x5000000,
0x38)`) instead of blending. `DAT_005f0d28` is `0.99f` (read from the stock
executable). Flag `0x1000` only selects type 2/10/6/0xe (the VCA variants).

The port previously read `flags & 0x1000` as "transparent" and drew every city
piece with `AlphaBlend` and `depthWrite = false`. City groups carry flags
`0x1840` and `0x1800`, so all ground skipped the depth buffer, which is exactly
the case the live frames show. `sceneScyBlendState` now returns the stock
decision; the test case in `tests/scene_2d_test.cpp` failed under the old rule
(FAIL 60) and passes with it. Residual: the transparent pass is still sorted by
model-origin camera depth with `stable_sort`; stock `00525930` builds a binary
heap on the object's camera-space depth at `+0xb8` and pops it, so exact ties
and per-object ordering on that heap are not modeled.

## Stored texture levels, 2026-10-28

The file-backed texture path no longer replaces stored levels with generated
ones. DXT1/3/5 preserve dimensions, pixels and supplied level count; PTX preserves
its decoded level list. A real Badge_frame.dds test checks8 levels from512x512
through4x4, so no1x1 or2x2 level is silently added. Distinct synthetic lower
levels check the sampled input rather than merely counting generated arrays.

`tools/wine/dds_layout_oracle.sh` executes the original0059c7e8 parser after
verifying its PE code. All1462 available2005DDS files and3 synthetic DXT1/3/5
cases match format, dimensions, level offsets, pitch and byte sizes exactly.
The fixture manifest records input hashes. This proves layout interpretation,
not GPU pixel, lighting or filtering parity. Cubemaps and volumes remain outside
the host decoder's scope and fail explicitly.
