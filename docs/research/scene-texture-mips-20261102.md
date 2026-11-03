# Scene texture levels investigation

US29 remains unverified. The production `SceneRaster3D::draw` texture cache
loads `ResourceTexturePixels`, then passes only its base RGBA image to
`Raster3DTexture::build`. That method generates a box-filtered chain. The
resource loader preserves DDS/PTX authored lower levels in `mipLevels`, but
this caller ignores them. The separate file-backed raster loader already
preserves stored levels; its tests do not qualify this production scene path.

Fresh older-reference REA evidence is retained in
`local/verify/issue53-texture-rea-20261102/mips-batch.json`
(`ev_4cb4dbe1b432f0414afc1e626833f9fa2dae03b797bf7bba8ebccb624ddde02c`),
bound to executable SHA-256
`7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`.
It supplies static DDS header/level parsing (`0059c7e8`), material texture
binding (`004c4b90`) and configured sampler defaults (`004e9460`). Follow-up
records bind texture factory `00505960`, constructors and their vtables.
These do not yet prove the actual GPU upload or the newer reference's choice.

The subsequent shared-load-callback batch was oversized. Its post-call Jev
screen recommended **skip** (relevance 0.09); its contents were not read or used.
It remains retained as `mips-upload-unreviewed.json`, alongside the judgment.
The owned session was closed; its close response was also skipped for low
substance. The holder's final report confirms explicit release before deadline.
The next owner independently observed the REA session idle.

Next: inspect bounded concrete DDS/PTX upload operations and the newer mapping,
then test authored-level sampling through the real scene draw path. Preserve
truncated chains and distinct lower-level pixels if supported by that evidence.
The visible Garage texture difference is only a lead: filtering, lighting and
camera differences have not been isolated, and no paired visual claim follows.


## Bounded upload qualification and production correction

The further older callback batch in `upload-qualified-v1/ledger.json` also exceeded the intended useful scope (131,522 transported bytes); its post-Jev relevance was0.04/skip. That payload was retained without reading its target contents or rerolling the same query. Its owned session was closed and holder81341 released. A close review recommendation was independently inspected only as the trivial `result:null`; no target behavior was inferred from it.

Independent supplementary instruction decoding identified the reason the earlier selectors were poor leads: vtable+8 is cloning, while the common scheduler at old507eaf calls vtable+0x10. The existing reviewed older vtable bytes (`mips-vtables.json`, evidence `ev_f47890cfcf2f04720c77c41388a75e33fa233701bfa7035327d01f1620ce083c`) bind PTX+0x10=507110. Further older upload byte reconciliation remains pending; the skipped payloads do not establish it.

The newer RTTI path identifies CompatibleTexture_PTX and CompatibleTexture_DDS, with vtables599078/599090 and concrete load callbacks458120/457e90. `upload-exact-newer-v1/ledger.json` retains seventeen exact REA read_bytes slices (4–256bytes each), their complete source/provider bindings and every pre/post judgment. Every byte slice passed its post-call screen. The owned session closed; the low-substance close response was skipped and the holder24428 explicitly released before deadline. No whole-function pseudocode was requested in this session.

`upload-decoder-v1` compares all seventeen REA byte ranges with the exact initialized bytes of newer SHA256f44fb88a4335c657e20701cdb84afb8d1d85f7c3aceb93d30113e6e6e44fefc2, records source file offsets, and retains bounded LLVM disassembly of code ranges as a supplementary decoder. Source hashing before/after matches. The REA bytes, rather than the decoder alone, bind the shipped-artifact observations.

Newer DDS457e90 normalizes an absent mip count to one; the level count is passed when creating the texture. DDS4572a0 computes the selected level, obtains its data address through458290's compressed-block offset calculation and submits that pointer/stride through44d540. PTX457071 submits the selected decoded authored level through44d4f0;458241 advances the stored level record using its length plus0x14 and increments the level index. The two upload bridges select the destination texture's per-level surface (virtual+0x48) and retain the supplied source pointer/stride. These paths support preserving supplied levels rather than generating replacements from base pixels. They do not qualify live GPU contents, asynchronous streaming timing, quality/caps-dependent level selection or complete sampler/visual parity.

`ResourceTexturePixels` now records `authoredMipChain` for DDS/PTX, including a chain with only its base. Without this bit an empty lower-level vector cannot distinguish base-only DDS/PTX from raw PNG/TGA. SceneRaster3D's actual texture cache validates dimensions and byte extents and transfers all supplied levels without generating additional levels; invalid authored chains leave an explicit diagnostic and no partial cache insertion. Raw formats retain the existing generated chain. Unused scene variables exposed by the focused build were removed; no unrelated rendering behavior was changed.

The new regression calls actual `SceneRaster3D::draw` with a bound world object, resource texture cache and software surface. Its base-red control succeeds; authored lower levels deliberately contain green/blue pixels that cannot arise by averaging that base. The pre-fix production scene call fails the blue assertion in `scene-mips-red-v1`. The final test also exercises truncated-chain clamping, base-only authored LOD0 under strong minification and the distinct raw-image generated average. This is a production scene rendering regression, not a standalone decoder or helper-copy assertion.

`scene-mips-final-v1` retains tested-source hashes/snapshots and the final focused build: scene_2d_unit, resource_texture_unit and raster3d_unit all pass (3/3,0.55seconds); no compiler warnings occur in that build. Real DDS/PTX loader metadata and raw TGA metadata are checked separately. The root's startup_probe link hold was preserved. No paired Garage/dealer visual agreement or full integration gate is claimed by this slice; US29 remains unverified.

Actual used-dealer capture adds independent next-boundary leads: `factory-entry-first/issue53-native-used-dealer-entry-v1/run/20261102T221342Z-51156/run.log` reports material `frame, engine bay` type0x11 unsupported around2979 and an unsupported mesh directive around2996. Those remain separate production rendering gaps; this mip correction does not resolve them.


The bounded final Jev packet is retained in `scene-mips-review-v1`. It escalates (safe-to-apply0.07, composite0.29): the red/final-green claim is contradicted at confidence0.33, and the metadata/preservation claim is unsupported at0.37. This is a stop on closure, not approval or a reason to reroll. The actual red log identifies the authored-blue failure after the base-red control, and the final log reports3/3 passed; both and the tested source are forwarded for independent resolution. The metadata claim's evidence packet contains source hashes/decoder excerpts but leaves the production implementation in the diff rather than duplicated in evidence, which limits that claim review. No US29 completion claim follows from this patch or these tests.


## Final scoped audit and older lifecycle repair

The final diagnostic addition records one cache-insertion event per texture with ID, dimensions, level count and authored/generated choice. `scene-mips-final-v2` rebuilds the focused targets and again passes all three tests, preserving the hashes of all five owned source/test files. It does not link startup_probe. The parent subsequently owns the combined probe build and actual journey validation.

The parent independently resolved the narrow low-confidence Jev contradiction by reading the raw red log, test snapshot and final result. The base-red check at92 precedes the blue failure at97, and CHECK returns on failure; the final-v2 result explicitly records three passing tests. The parent also checked actual loader reset/flag assignments and authored/raw scene branches. The Jev judgment remains retained without reroll; this manual resolution concerns the implementation/test slice, not full US29 or release acceptance.

`scene-mips-audit-v1/source-and-review-audit.json` asserts equality of tested-before, tested-after and current SHA-256 for all five owned files, successful final experiment stages and the bounded static lifecycle below. Its status remains implemented_unverified and release_accepted=false. The separate retention audit preserves the existing64MiB parent cap.

The first older exact session reached its240second deadline: admission22:36:52.891UTC, byte ledger saved22:40:07.929UTC, deadline22:40:52.891UTC, and final xrefs/close record22:41:24UTC. Holder92890 exited1. Despite useful raw bytes, that attempt is not admission-qualified as a complete lifecycle. Its files, including post-deadline additions, remain retained and explicitly limited; no deadline or cap was extended.

A replacement sequence avoids model idle time: prechecks precede admission; the script opens, queries exact byte/xref ranges, retains each result, closes and releases automatically, then obtains postchecks outside the provider lease before using target content. `upload-atomic-older-v1/sequence.json` records22:45:07.897–22:46:10.126UTC (62.229seconds), all18 requested operations, no error and successful close. Holder22476 exits0 under its180second deadline. Other Car/compound/clock slices in the same minimal sequence are forwarded to their owners and are not US29 acceptance claims.

All six older mip ranges pass post-screening in `upload-atomic-post-v1`: PTX50715c count/create and5071e1 iteration, DDS507a5b and PTX50702e selected-source submission, and bridges4f6880/4f6830. The PTX record walk advances by stored byte length plus0x14 and preserves the declared level iteration. Both bridges carry the selected destination level and supplied source pointer/stride, consistent with the newer path. `upload-atomic-decoder-v1` independently compares every post-passing read range against exact PE file bytes/SHA2567d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde, retaining bounded supplementary instruction decoding. Post-skipped Car-selector/clock-wrapper/close data are excluded from that decoder and from accepted semantic conclusions; no same-query screen was rerolled.

Both existing storage caps remain unchanged. The8MiB texture root last measured1,529,390 logical bytes with6,859,218 remaining; disk reserve remains above10GiB. No provider or admission handle remains owned by this worker. Paired rendering, streaming/caps selection, remaining used-dealer material/directive gaps and the full integration gate remain open.
