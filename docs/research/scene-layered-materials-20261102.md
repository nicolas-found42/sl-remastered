# Used-dealer layered material and writable meshes, 2026-11-02

Status: production common type-0x11 path implemented; focused controls pass. Paired Stock/native visual parity and full US29 acceptance remain unverified. No startup_probe was linked by this slice.

## First causal boundary

Read the actual used-dealer log before choosing a hypothesis:
`local/verify/parity-goal-20261102-092437/factory-entry-first/issue53-native-used-dealer-entry-v1/run/20261102T221342Z-51156/run.log`
SHA256 `01baf5618eca7083f4028b4e3f433d5db67d58adfa7aa1b3be5396827e3703af`.
Line2979 rejects chassis material `frame, engine bay`, type0x11. Line2996 reports an unsupported mesh directive without resource identity. The latter original log cannot identify a unique first failing resource; the new diagnostic preserves resource ID and the rejected directive.

Duhen.rpk SHA256 `11f46dde2a7271713f4c86737118a16886317846d4fc078b0338c5c0d8a6e3a8`, resource50 points to chassis.scx SHA256 `8a088ca7f945960ad4768d64b9534d681a53aadb3e9fb6b90d962b9c538c1143`. Its rejected material has map0/index2/channel1, map1/index3/channel0, map3/index4/channel-2, reflection property01000001=0.3. Its bound type14 material has four textures, so map3 resolves the actual global environment resource rather than an in-range texture slot.

## Shipped behavior and admission

Existing older all.c material switch004befc0 maps types9/11/21 to MixTASpheremap factory004d5130; binding004d5430 capability3 mixes map0/map1, modulates vertex diffuse, then adds environment times TFACTOR. The new implementation does not whitelist type11 into the existing base-only approximation list.

Fresh newer bounded REA evidence is retained in `local/verify/issue53-texture-rea-20261102/material-mesh-newer-v1`; screens in `material-mesh-review-v1`; exact PE byte equality and LLVM supplements in `material-mesh-decoder-v1`. Source SHA256 `f44fb88a4335c657e20701cdb84afb8d1d85f7c3aceb93d30113e6e6e44fefc2`. Addresses433006..43307a show stages SelectArg1, BlendTextureAlpha, Modulate(diffuse), MultiplyAdd;432f72 binds three map records.441ad0 shows reflection-coordinate matrix setup, supplemented by older004e7660.

The automatic open/query/close interval is23:12:59.056–23:13:47.243UTC (48.187s). Holder91061 released exit0 within its180-second deadline. The existing registered8MiB parent was not resized. Six postchecks returned REVIEW, not PASS or SKIP, with injection probabilities0.28–0.39. Manual review found normalized target payloads contain fixed hexadecimal only; provider metadata has instruction-like boilerplate. They were consumed as data, with no directives followed. Earlier skipped oversized mip callbacks were not read or reused.

Correction: newer mesh+0x64 is `instances`, not flags; flags are stack-0x14 and stored+0x68. The bounded mesh-parser slice captured the adjacent instances branch, not a complete flags parser. No production change relied on calling+0x64 flags. Older0053af40 explicitly passes bit8 into the mesh factory's private/writable argument; the port already supplies private CPU vertex vectors on load/copy. Flag8 now accepts those semantics. Flag128/billboard and other unqualified flags still refuse.

Direct3D defines camera-space reflection coordinates from position and normal: [TCI documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dtss-tci), [reflection formula](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-reflect). The production path applies the observed yaw/half-scale transform to that vector. Exact GPU arithmetic and global capability equivalence need paired visual qualification.

## Production and controls

CameraFrame passes the actual VmGameState globalEnvmap ID through SceneCameraRenderer into SceneRaster3D. Type11 resolves all three actual textures; absent maps/textures/UV channels and unsupported function states refuse explicitly. Authored UV channels/scale/offset and map address flags feed existing explicit fixed-function raster stages. CPU vertex lighting precedes reflection; reflection factor is converted to8-bit TFACTOR. The existing fixed-raster validation was preserved, including required explicit sampler/LOD state. The host path supplies bilinear/trilinear zero-bias/minLOD0 sampling; Stock configuration equivalence is not claimed. Authored DDS/PTX mip levels continue through the existing preserved texture cache.

`scene-layered-material-red-v2`: build0, semantic test1 at the actual SceneRaster3D mixed-color assertion. V1 is a preserved compile failure caused by an incomplete VmGameState include and was corrected before the semantic red. `scene-layered-material-final-v6`: build0, six focused tests pass, all eight owned source hashes match before/after. Tests cover the lit mix with an independent blue reflection, coefficient0 removing only reflection, and a selected uv2 stream choosing a distinct texel instead of uv1.

`scene-writable-mesh-fault-red-v1`: disabling only flag8 acceptance makes the actual shipped parts.rpk12f load fail with `flags 8.000`; expected/actual exit1. Source was restored in finally, then final-v6 rebuilt it. Positive tests load parts12f/6d, scale one model, and retain the other model's original vertices. The first Duhen-only fixture exposed absent retained SCX files after flag acceptance; that failure is preserved in final-v3. It was replaced with actual present running-gear resources, with no invented mesh fallback. Initial red-v1 segfaulted after the continuing CHECK macro accessed an empty vector; red-v2 added a guard and retained exit1. No assertion was weakened to turn these failures green.

The first final attempt names a nonexistent camera_frame_test target; V2/V3 preserve intermediate rendering failures; V4–V6 pass increasingly complete focused controls. Full integration gate and real used-dealer capture are parent-owned and were not run by this slice.

## Review limits

A summary-only Jev verification returned two unsupported claims (confidence0.70/0.46) and one verified limitation (0.66), all review. This is not acceptance; it is retained and escalated to the parent for independent source/log inspection, without an unchanged reroll. Rendering remains implemented/unverified pending that review and paired visual evidence. Nonzero material function words, other material families, billboard meshes, absent Duhen source files, global graphics settings, and complete visual parity remain separate boundaries.
