# Rectangle scene evidence

Live Ghidra identified `StreetLegal_Redline.bak.exe` with MD5
`e686099e7233bb7d7c461b1b35c5064c`. The headless runner checked the same stock
executable when it saved the functions below. The installed script files may be
modified, so their trees are leads rather than proof of stock script behavior.

`Osd.class` tree 93 references `createRectangle`. Tree 94 constructs a
`Rectangle` and adds it to `Osd.rectangles`. `Rectangle.class` tree 0 constructs
a `RenderRef` from the root and template's `dub`, and invokes `setMatrix` with
position and orientation. `RectangleTemplate.class` tree 5 calls `duplicate`,
`changeResource`, and `scaleMesh`. These references do not show which branches
run or establish the final pixels.

The stock `RenderRef.create` handler `00480ee0` checks the receiver and two
arguments, tests the first argument's linked node, then calls `00539880`.
The helper creates a type-3 node, associates it with the second resource, and
initializes render instance data. The handler attaches its native reference to
the resulting node and requests `bone00`. `RenderRef.setMatrix` at `00481240`
reads position and orientation fields, supplies zero defaults for absent
objects, and passes a transform through `0048bf50`. The latter changes a bone
matrix and marks the instance dirty. See
`local/census/DecompileAt-00480ee0.log` and
`local/census/DecompileAt-00539880.log`.

`GfxEngine.forceRendering` invokes `005378d0`, `00537b40`, and `004fca30`
seven times as described in `docs/gfx-input-evidence.md`. The saved decompile
shows `004fca30` iterating two renderer lists and invoking `00516b70` or
`00516a90` before further renderer calls. Those helpers traverse additional
lists and call virtual methods; `00516a90` also calls `0050e9e0`, `0050eba0`,
and `0050fcb0`. See `local/census/DecompileAt-00539880.log` and
`local/census/DecompileAt-00516b70.log`.

The list element types, the relevant virtual targets, material substitution,
coordinate conversion, clipping, and draw order are unresolved. These functions
do not establish that `Osd.rectangles` is the renderer's draw list. A flat
insertion-ordered rectangle scene would therefore invent stock behavior. No
scene implementation is introduced on this evidence. The next useful trace is
`0050eba0` and `0050fcb0`, their virtual targets, and the render node created by
`00539880` through to the corresponding renderer list.

## Further render pass trace

`00516a90` traverses its `+0x2c` list and, for each element, calls
`0050e9e0`, `0050eba0`, and `0050fcb0` with that element as the receiver.
The call sequence and receiver are visible in the assembly at `00516b34` to
`00516b4f`. `0050fcb0` is an empty function. `0050e9e0` sets camera-related
state and makes renderer calls. `0050eba0` runs multiple collection passes
through virtual functions and sends several batches to renderer slots. See
`local/census/DecompileAt-0050eba0.log` and
`local/census/DecompileAt-00516b70.log`.

`00527d40` creates entries with vtable `005f3254`, initializes them, and places
them in different collections based on flags. The stock memory at `005f3254`
identifies the initialization and subsequent virtual targets as `0052c590`,
`005298b0`, and `00529940`. The first stores its arguments and calls the next
virtual slot. `005298b0` obtains associated data through other virtual calls;
`00529940` sets a renderer matrix and performs more indirect calls. See
`local/census/DecompileAt-00527d40.log` and
`local/census/DecompileAt-0052c590.log`. These are entry vtable targets, not
proof of the collection vtable targets used in `0050eba0`.

This confirms that several collections and flag-dependent assignment participate
in the pass. The type-3 node's route into a particular collection and the
collection vtables are still unresolved. There is not enough evidence to choose
scene ordering or implement stock traversal.

## Render-object kind selection

`005106a0` checks for a type-14 linked node and calls `0052ced0` on its
render-instance resource link. `0052ced0` loads the referenced resource and
selects a kind using its flags at `+0x34`. In priority order, bits `0x4`,
`0x100`, `0x2000`, `0x100000`, and `0x800` select kinds 3, 4, 5, 6, and 0.
If none is set, it selects kind 2. `0052d0d0` constructs a different object for
each kind. Kind 2 invokes `0052df50`, which installs vtable `005f3448`. Its
slot `+0x14` points to `0052eae0`, which constructs the mesh-pass entries in
`00527d40`. See `local/census/DecompileAt-0054a790.log`,
`local/census/DecompileAt-0052ced0.log`, and
`local/census/DecompileAt-00527d40.log`. The vtable pointer and slot were read
from stock program memory in Ghidra.

`00549720` reads a type-14 payload's `flags` directive into `+0x34`; other
directives can also modify that field. The existing bounded material parser
reports flags `4096` for `frontend.rpk` id `0xd7`, the rectangle template.
`sceneRenderKindFromFlags` applies the selection to a supplied flag word, and
its test checks every selection and their precedence, including that real
parsed value. It does not implement loading, cloning, draw passes, or prove the
final flags after all stock operations.

## Pass classification for the rectangle mesh

In `00527d40`, the ordinary branch selects collections from a renderer
material's word at `+8`. Low bits 0, 1, 2, and 3 select collection offsets
`0x41c`, `0x4cc`, both `0x448` and `0x474`, and `0x50c`, respectively. A
special branch selects `0x548` instead. The ordinary branch can also add an
entry to a separate collection when the resource's bit 8 and that collection
are present. The calls and their branches are in
`local/census/DecompileAt-00527d40.log`. `scenePasses` returns these destinations,
not their execution order.

`00502de0` hands each SCX material chunk to renderer slot `+0xcc`. Stock
renderer vtable `005f1550` maps that slot to `004beef0`; its slot `+0xc8` maps
to `004befc0`. For material type 0, `004befc0` sets renderer word `+8` bit 1
from the input function word's `0x40000000` bit and bit 2 from its
`0x80000000` bit. The base constructor `004e5810` initializes the word to zero.
See `local/census/DecompileAt-00508160.log`,
`local/census/DecompileAt-004beef0.log`,
`local/census/DecompileAt-004befc0.log`, and
`local/census/DecompileAt-004e5810.log`.

The actual rectangle template `frontend.rpk` id `0xd7` names mesh `0xd9`.
The existing mesh test identifies that mesh as
`frontend/meshes/etalon_negyzet_alpha.SCX`. Its parsed material is type 0
with function word `0x40000000`. Under the ordinary path, without later material
mutation, that produces renderer flag 1 and selects collection `0x4cc`. The test
covers this asset and synthetic flag combinations. Cloning, mutation, special
view selection, and whether this collection is reached at runtime remain open.

## Mesh scaling and resource replacement

`ResourceRef.scaleMesh` handler `00480390` calls `0048e7f0`, which iterates
the loaded mesh groups and their meshes. For the SCX mesh object installed by
`00502de0`, vtable `005f2c08` slot `+0x4c` resolves to `005046c0`. It reads each
vertex through `004ff590`, multiplies the three position components, then
writes the vertex through `004ff710`. The normal and texture coordinate values
are read and written without modification. See
`local/census/DecompileAt-00480390.log`,
`local/census/DecompileAt-005046c0.log`, and
`local/census/DecompileAt-004ff590.log`.

`sceneScaleMesh` performs that position operation on an already-copied parsed
model. It checks host vector lengths first and refuses malformed input without
changing the model. That check is a host boundary, not a stock behavior claim.
Its test scales every position of the real rectangle mesh and verifies normals,
UVs, and indices remain unchanged. It does not clone or attach a stock resource.

`RenderRef.changeResource` handler `00480220` reaches the loaded object's
virtual slot `+8`. The type-14 constructor `00549580` installs vtable
`005f3a5c`, whose slot `+8` points to `0054a450`. That function compares the
old ID with the mesh link first, then with texture links in order. It updates
only the first match and calls `0052d2a0` even if the target node is unchanged.
A missing match returns zero. See
`local/census/DecompileAt-00480ee0.log`,
`local/census/DecompileAt-00549580.log`, and
`local/census/DecompileAt-0054a450.log`.

`SceneMaterialResources` owns stable `OwnerLink` records and attaches them to
existing `LinkOwner` objects. Matching a resource replaces its link and calls the
supplied refresh callback, including for an unchanged owner. No match does neither.
The owners must outlive the material. Tests use the rectangle template's mesh
and texture IDs and the loading picture ID, and check attachment, detachment,
same-ID, missing-ID, and duplicate-link behavior. The test uses local IDs within
one pack; it does not establish cross-pack identity or complete script behavior.

`0052d2a0` looks up the render object cached for the material's own ID and
invokes its virtual refresh method when present. For kind 2, that slot resolves
to `0052e5c0`, which clears cached mesh and texture arrays and rebuilds them
from the resource. The callback is a port boundary for this work; the renderer
cache and its full refresh are not yet connected. See
`local/census/DecompileAt-0052d2a0.log` and
`local/census/DecompileAt-0052e5c0.log`.

`sceneLoadMaterial` joins the bounded type-14 parser to `ResourceWorld` lookup.
It maps the payload's mesh and texture IDs through the owning pack's dependency
mapping, checks for type 5 and type 7 nodes, and attaches the material links to
their owners. The result must be destroyed before the resource world. Its test
loads the real `0xd7` resource, checks the resolved pack-qualified links, and
replaces the default texture with the real loading texture node. It rejects a
missing or wrong-type material. This host helper supports only the three-directive
payload accepted by `resourceMaterialParse`; it does not mark a stock node as
loaded, clone it, or refresh a renderer cache.

`sceneRefreshTexture` is a host-only part of a refresh. It resolves the first
linked type-7 texture and decodes the first mip using the existing PTX and DXT1
DDS readers. It clears the output on failure. The scene test loads the real
rectangle material, refreshes its default texture, changes its link to the
loading picture, refreshes synchronously, and draws the result to an SDL
software surface. The test checks a pixel on that surface. This does not
implement `0052e5c0` in full, update a live renderer cache, draw the SCX mesh,
or establish stock visual layout.

## Diagnostic mesh draw

The real `etalon_negyzet_alpha.SCX` has four vertices and six indices. Its
first UV set maps the whole texture across the two triangles. `sceneProjectMesh`
applies a caller-provided 2-by-4 projection to positions and preserves UVs;
`Render2D::drawMesh` submits the projected vertices and indices to SDL. The
projection in the test is a diagnostic choice, not the stock camera transform.
The test draws the real mesh with the loading texture into a 128-by-128 software
surface and checks a visible pixel. A synthetic textured triangle checks alpha
blending and a 2-by-2 texture checks UV sampling.

SDL 3.4.18's software renderer fails on a 1024-by-1024 texture drawn over a
1024-by-1024 quad. It leaves the center black and sets `triangle area overflow`,
even though the public rendering calls report success. Its
[triangle code](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/render/software/SDL_triangle.c)
rejects a texture-coordinate and triangle-area product that exceeds a signed
32-bit value. Its [command queue](https://github.com/libsdl-org/SDL/blob/release-3.4.18/src/render/software/SDL_render_sw.c)
ignores the triangle function's failure. The same test on this Mac's Metal
window backend read the expected loading-picture center pixel `(84,82,76,255)`.
`tests/geometry_backend_probe.cpp` reproduces both paths. Its `--software` mode
returns failure while this SDL issue persists; `--window` exercises the selected
window backend. No stock draw order, viewport transform, filtering, or pixel
parity is claimed.

## Bounded material clone

`00545230` creates a new node and invokes the source instance's copy slot.
For type 14, vtable `005f3a5c` points that slot to `0054a190`. It attaches the
new material's mesh and texture links to the same resource nodes and copies
other fields. See `local/census/DecompileAt-00545230.log` and
`local/census/DecompileAt-0054a190.log`. Type-5 mesh copying uses
`0053b380`, which obtains a separate mesh object through a virtual call; see
`local/census/DecompileAt-0053b520.log` and
`local/census/DecompileAt-0053b380.log`.

`SceneMaterialResources::clone` covers the independent mesh and texture links
only. The scene test clones the real rectangle material, changes the clone's
texture, destroys the original, and refreshes and draws the clone. Node creation,
other material fields, mesh cloning, and full renderer-cache behavior are not
connected by this helper. `ResourceRef.duplicate` supports the bounded type-14
rectangle material and the rectangle's type-5 mesh clone.

`sceneCloneMaterial` now creates a host type-14 runtime node under the source
node's parent and binds a separately linked material to it. The source and clone
can change independently. The resource world keeps these host bindings by node
identity and releases a binding when its runtime node is destroyed. Tests cover
clone lifetime, ID reuse, and resource-world teardown. This remains a bounded
clone: it does not copy every type-14 field or implement the stock renderer
cache. The script native supports bounded type-14 and type-5 cases; type-7 and type-13 clones remain unsupported.

`SceneMaterialResources` now retains and clones the type-14 flags read by the
bounded payload parser. `sceneRefreshMaterial` rebuilds a host snapshot with
those flags, the linked mesh, and the linked texture. The stock type-5 node
vtable `005f3608` routes load slot `+0x28` to `0053b710`, which calls
`0053af40`; that helper reads `sourcefile` and loads the named mesh. The host
accepts only a single `sourcefile` directive, confines the path to the supplied
root, and reads the SCX through the existing parser. Other mesh directives stay
unsupported. See `local/census/DecompileAt-0053b710.log` and
`local/census/DecompileAt-0053af40.log`.

The scene test rebuilds the snapshot from the real rectangle material, replaces
its texture with the loading picture, and draws the refreshed mesh and texture
together on an SDL software surface. It also checks that a missing mesh or bad
root clears the old snapshot. This is not the full `0052e5c0` refresh: it does
not model all material fields, pending loads, renderer objects, GPU uploads,
or the script native. It deliberately reports unsupported inputs rather than
claiming those cases match stock.

`RenderRef.changeResource` now supports a bound type-14 material. The native
resolves its two resource references, replaces the first matching mesh or
texture link, and invokes a registered host refresh callback on a match,
including when the replacement is unchanged. A missing renderer cache needs no
callback, as in the stock lookup at `0052d2a0`. Non-type-14 instances, absent
material bindings, null arguments, and invalid host handles remain explicitly
unsupported or errors. This does not implement the stock renderer cache itself.

`native_render_change_vm_test` executes a synthetic VM tree that calls the
registered native with real frontend material and texture resources. It checks
the new link and refresh callback. The native test also checks a missing match,
a same-owner match, clone independence, and a callback that removes its own
material binding. These tests are not a full execution of `RectangleTemplate`
or evidence of stock visual parity.


## Mesh clone and scaling through the VM

`00545230` invokes `0053b380` for a type-5 source. The copy operation asks the
underlying mesh for a separate object, and `005046c0` multiplies its vertex
positions without changing normals or UVs. The bounded native implementation
loads or copies an SCX model into a new type-5 runtime node, then applies
`ResourceRef.scaleMesh` to that model. The type-5 world node has its own ID,
uses the source's parent, and has no sector link. See
`local/census/DecompileAt-0053b380.log`,
`local/census/DecompileAt-005046c0.log`, and
`local/census/DecompileAt-00536a10.log`.

`native_render_change_vm_test` uses the real frontend mesh resource, duplicates
it through the registered native, executes the scale operation in a VM tree,
and replaces the cloned material's mesh link through another VM call. It
checks the refreshed mesh positions, unmodified normals and UVs, and cleanup
when the cloned node is destroyed. Updating an already active renderer mesh
buffer remains unsupported, so the scale handler rejects that case before
changing the CPU model. Additional mesh source directives, skinning semantics,
GPU upload, and full stock rendering remain open.

## Render instance boundary

`RenderRef.create` calls `00539880` to create a type-3 node and link its
type. The native then requests `bone00` through `0048bc40`; `005439e0`
attaches the resource link. The host now supports this path for a loaded
type-1 parent, a live renderer, and the bounded rectangle material. It
inherits the parent bounds and visibility fields and creates a linked
`SceneRenderInstance`; material draw objects are created later by camera
traversal. Other parents and material formats remain unsupported, and the
handler refuses the call before node creation when no renderer exists. See
`local/census/DecompileAt-00539880.log`,
`local/census/DecompileAt-005439e0.log`, and
`local/census/DecompileAt-0048bc40.log`.

`RenderRef.setMatrix` accepts a null or zero YPR orientation, and stores
identity rotation with position multiplied by 10. The VM behavior test creates
a real type-3 node through the registered native, executes setMatrix, and
draws its cloned and scaled original mesh with `Loading.dds` to an SDL
surface. It verifies pixels, node cleanup, and explicit refusal of nonzero
rotation. The test is a synthetic call sequence, not a game startup run.
See `local/census/DecompileAt-00480ee0.log` and
`local/census/DecompileAt-0054f4c0.log`.

The renderer lifecycle has separate stages: `00516760` searches existing
viewport renderer objects by instance ID before allocating and queueing one;
`005106a0` resolves a type-14 instance to a material-specific draw object.
`0048bf50` writes the bone matrix, marks the bone dirty, and queues it on the
instance for further processing. `0053ab00` performs the same matrix copy and
marks the bone dirty. `00541fe0` consumes queued bones. For a root bone with a dirty matrix, it
copies the local matrix into the derived matrix and removes the bone from the
queue. The host represents this root-bone step and tests it after a VM matrix
update. Parent-bone resolution, later draw-object consumption, and live render
propagation remain unsupported. See `local/census/DecompileAt-00516760.log`,
`local/census/DecompileAt-0053ab00.log`,
`local/census/DecompileAt-00541fe0.log`, and
`local/census/DecompileAt-00539880.log`.

`00528fe0` copies a single bone's derived matrix at bone `+0xb0` to the
material draw object's matrix at `+0xd8`, or selects identity when no bone is
available. `SceneRenderInstance::refreshSingleBoneDrawMatrix` models the
single present root-bone copy after update processing. The VM test verifies
that the draw snapshot remains unchanged until refreshed and then receives the
processed translation. The material draw object itself and its viewport draw
loop are still not implemented; multiple-bone selection is also outside this
boundary. See `local/census/DecompileAt-00528fe0.log`.

`00528fe0` also copies its bone-derived matrix into the draw object's `+0x30`
matrix. `00529940` passes that matrix to the graphics backend for the ordinary
mesh path. `sceneProjectTransformedMesh` composes a supplied affine world
matrix with a caller-supplied projection for the existing SDL mesh path; it
is not the stock camera projection. A test processes a translated bone, projects
the real rectangle mesh, and checks the shifted pixels on an SDL software
surface with a one-pixel texture. With the real 1024-pixel Loading texture,
the shifted software-surface draw was black; this remains an explicit backend
limitation, not evidence of stock rendering parity. See
`local/census/DecompileAt-00528fe0.log` and
`local/census/DecompileAt-0052c590.log`.

`sceneDrawSingleBoneMesh` connects the bounded root-bone update and draw-matrix
refresh to SDL mesh drawing for an explicitly supplied mesh, texture, and
projection. Its surface test uses the original rectangle mesh and verifies the
shifted pixels. It does not discover material draw objects, load or invalidate
textures, or perform viewport traversal. In particular, `00516760` creates the
camera renderer object held by the viewport lists; these are not lists of
material draw objects.

Material discovery has another stage before object creation. `00512e00` calls
`005121b0` to populate a priority queue, then consumes that queue with
`005106a0` to create or retrieve draw objects. The queue population depends on
camera and visibility state and also considers already-created objects. The
separate `00511180` and `00511940` paths supply queue entries after visibility
checks; their complete inputs and ordering are not represented in the host.
This rules out treating all children of a viewport as visible materials. See
`local/census/DecompileAt-00512e00.log`.

For candidates already accepted by the visibility stage, the saved
`005121b0` trace inserts a four-word entry into a min-heap by its float score.
`00512e00` consumes the lowest score while accumulated creation cost is less
than or equal to its budget, unless its override is active, and then clears
remaining entries. `00515a80` implements the heap removal; its assembly
confirms the child index calculation. `SceneVisibleQueue` represents that
bounded queue with stable owner links and rejects non-finite host scores.
Visibility testing and score calculation remain unimplemented. The test covers
score order, the inclusive budget boundary, override, discarded entries, and
link cleanup. See `local/census/DecompileAt-00512e00.log` and
`local/census/DecompileAt-00515a80.log`.

`00511180` can compare a candidate sphere against a camera bound through
`0054b850`, and rejects its outside result (bit 1) before the candidate is
queued. The host implements the sphere-against-sphere branch for finite,
nonnegative radii, including the stock boundary comparisons. Tests cover all
four results. The rectangle draw test uses the separate type-3 candidate
branch described below. This does not establish the actual loading rectangle's bounds or cover
box bounds, frustum planes, node states, or the rest of visibility discovery.
Host double arithmetic approximates stock x87 precision. See
`local/census/DecompileAt-00511180.log` and
`local/census/DecompileAt-0054b850.log`.

`0053ae40` initializes the type-5 mesh bound mode to 2. `0053af40`
changes it for a `radius` directive, but not for the sourcefile-only payload
accepted for the rectangle mesh. `00539880` has two different paths for the
instance bound depending on bone-parent state, so the final rectangle bound
cannot be inferred from the mesh alone. The earlier interpretation that it
always inherits the parent is not established. See
`local/census/DecompileAt-0053ae40.log` and
`local/census/DecompileAt-00539880.log`.

The bounded `sceneDrawCameraMesh` path now uses the camera view, projection,
and viewport mapping to draw the original rectangle mesh on an SDL surface.
It rejects vertices outside the frustum and triangles with different clip W,
which require clipping or perspective-correct UV interpolation. Its test uses
a one-pixel texture and checks center pixels and rejection behind the camera.
This does not connect viewport discovery, live resource refresh, or native
renderer creation.

For type-3 nodes, `00511180` substitutes relation 2 and queues the candidate
unless its `+0x6c` field is below the shared threshold or its per-camera record
has bits `0xb0000000`. It clears record bits 0–1 and sets bit 2 on queueing. Mode 0
sets shared priority to `1 - field(+0x70)`; mode 2 reuses the prior shared
priority. The host implements those two modes for supplied finite values and
rejects mode 1, whose additional call is not modeled. Tests exercise shared
priority, threshold, existing-record flags, and drive a rectangle draw through
this bounded branch. Actual traversal, per-camera record creation, node field
initialization, and live renderer creation remain unsupported. See
`local/census/DecompileAt-00511180.log`.

For the particular `RenderRef.create` call, `00480ee0` supplies zero bones to
`00539880`, and only requests bone00 afterward. `005404d0` and `004fe640`
initialize the list examined by `00429390` as empty. Together with
`005439e0`'s link-only operation, this selects the parent-bound copy branch.
The exact parent bound and later changes still depend on runtime state. See
`local/census/DecompileAt-005404d0.log` and
`local/census/DecompileAt-004fe640.log`.

`SceneCameraCandidates` applies the supported type-3 admission branch to actual
runtime `WorldNode` objects, keeping a separate owner-linked record and queue
per camera. It refuses nodes whose `+0x6c` or `+0x70` inputs are unknown; the
constructor's initialization of those fields depends on the frame counter, so
missing values are not assumed to be zero. A test submits one node to two
camera records, processes one, and destroys the node before the other drains.
This models camera-local candidate bookkeeping, not discovery of world nodes
or creation of renderer objects.

In the fully-inside traversal `00511940`, type-3 priority for bound mode 0
is calculated locally, unlike `00511180`, which also updates shared priority.
The candidate API distinguishes these paths, and its test checks that the
fully-inside path leaves shared priority untouched. The caller still must
establish which traversal applies; the host does not yet discover it.

Before either visibility traversal checks `+0x6c`, `00513cc0` calculates an
input and invokes the node's virtual setters. For type 3, stock vtable
`005f37f4` slots `+0x14` and `+0x18` point to `00543610` and `00543cc0`.
Both clamp `input * gain(+0xbc) + bias(+0xb8)` to 0–1 and compare against
existing values depending on frame stamps. The first also updates stamp
`+0x78`; the second compares `+0x7c` without updating it. The host models
these setters separately from the input calculation, rejecting unknown old
values if a comparison requires them. The test covers new-frame replacement,
same-frame maximum, clamping, and the asymmetric stamps. Host arithmetic may
round differently from x87. See `local/census/DecompileAt-00513cc0.log` and
the stock vtable memory at `005f3808`.

For the normal zero-override path, `00513cc0` computes visibility input from
bounds, camera distance, renderer factor, LOD amplitude and LOD bias; mode 2
uses `1 + LOD bias`. `sceneVisibilityInput` models finite modes 0, 1 and 2,
and `sceneUpdateType3NodeVisibility` applies the previously modeled setters
to an actual type-3 node. Tests cover all three modes, near-zero distance,
and the node update before camera-candidate submission. The renderer factor
is supplied by the caller: `0050e9e0` derives it from the camera field of
view. Host double arithmetic and trigonometry may round differently from
stock x87. This still does not perform world traversal or instantiate draw
objects. See `local/census/DecompileAt-00513cc0.log` and
`local/census/DecompileAt-0050eba0.log`.

`SceneMaterialRenderer` binds a runtime type-3 node to its linked type-14
material, reloads the bounded SCX/DDS snapshot on each draw, uploads a temporary
texture, and releases it after flushing queued SDL work. This prevents stale
material and mesh snapshots without claiming the stock GPU cache is modeled.
`SceneCameraRenderer` connects accepted camera candidates to this renderer
and retains the bounded material object across frames without imposing an
unverified creation budget. Multiple objects are limited to the verified
rectangle transparent pass described below. The scene test uses
`traverseCameraCandidates` on a type-3 fixture linked to a cloned original
frontend rectangle and replaced `Loading.dds`, then checks actual output pixels,
missing-texture failure, restored texture, and node destruction. The fixture is
at camera z=-400: on SDL software the same 1024-pixel asset was black at z=-200.
The software issue remains unresolved; the real startup traversal, persistent
stock draw objects, clipping, and perspective-correct texture mapping are not
implemented. This diagnostic does not establish the game startup path.

`005106a0` sets per-camera record bit `0x10000000` on the material-object
creation path. The host sets that bit only after its bounded object is created;
it then reuses that object on subsequent frames and drops it when the node's
owner link detaches. The test checks that a missing replacement texture fails,
that restoring it makes the retained object draw pixels again, and that node
destruction prevents stale use. Stock also marks the record in some failure
cases; the host deliberately stops instead of claiming a usable object there.
Full render-object lifetime and stock sorting are not implemented.

The 2005 `RectangleTemplate` constructor intentionally leaves its material
link empty when its texture is `Osd.RRT_NONE`. `00480ee0` still calls
`00539880`, whose empty-material branch copies the parent's bounds. In
`005106a0`, a zero material ID creates the small owner-linked object initialized
by `005157e0` and marks its camera record with `0x80000000`; it adds no mesh
draw entry. The host now creates that bounded instance and camera record. A VM
test constructs one under a loaded parent, confirms inherited bounds and no
pixels, and checks that its record is retained. The actual 2005 loop passes
this rectangle and reaches `PhysicsRef.createBox` while building the OSD.

The integrated fixture now starts at a loaded, zero-POLY type-1 runtime node,
uses the supported first-visit traversal to reach its type-3 child, and draws
the cloned frontend rectangle with `Loading.dds`. Repeated type-1 traversal
and mounted roots still fail explicitly; the second-frame draw test starts at
the type-3 node to exercise persistent material reuse. This does not represent
an actual game startup path.

The rectangle's `0x4cc` collection installs vtable `005f2e4c`; stock memory
maps its draw slot to `00525930`. That function builds a min-heap ordered by
the material object's `+0xb8` depth. `00528840` computes this depth from the
draw matrix translation, the camera's position scaled by the stock 0.1
constant at `005f08e8`, and the camera's forward vector. The host now orders
multiple objects for the bounded rectangle material and mesh pass only. A
surface test draws two differently positioned copies using the original
Loading texture: the resulting pixels match explicit far-then-near drawing and
differ from the reverse order. Other material passes and their ordering remain
unsupported. See `local/census/DecompileAt-005150f0.log` and
`local/census/DecompileAt-00528840.log`.

`sceneBoxRelation` and `sceneBoundPlaneRelation` implement the finite
sphere/box and plane classifications used by the camera traversal, including
stock tangency comparisons. Tests exercise both bound modes. Unsupported or
nonfinite host inputs return zero; host arithmetic may differ from x87 at
rounding boundaries. See `local/census/DecompileAt-0054ba20.log` and
`local/census/DecompileAt-0054bc80.log`.

`CameraFrame` connects a loaded type-3 camera and its type-12 resource to an
active viewport attachment, camera-derived matrices/bounds/planes, bounded
traversal, and persistent material drawing. Its test uses an explicit runtime
fixture with real frontend mesh and Loading texture; it is not yet attached to
the game startup or frame loop. Unknown global visibility controls remain
caller-supplied. A frame object binds to one camera and viewport, and drops
pending candidates after traversal failure.

Stock `00511e60` removes a per-camera record and invokes `00513fb0`, which
destroys its material object and clears the record. `SceneCameraRenderer::remove`
implements that camera-local record/object removal and detaches pending queue
references. A test checks that a removed rectangle disappears from pixels and
can be recreated. The full stock cleanup also updates visibility across camera
records and recurses through loaded nodes; those effects remain separate. See
`local/census/DecompileAt-00511e60.log` and
`local/census/DecompileAt-00513fb0.log`.

`camera_frame_view` is a native-window diagnostic for this bounded path. It
constructs a labeled synthetic scene, uses the stock frontend rectangle and
`Loading.dds`, and reads the first rendered center pixel from the window's SDL
renderer. On the macOS window backend the diagnostic returned RGBA
`115,113,107,255` and exited successfully. Computer-use inspection could not
select the unbundled process, and an unrelated microphone permission dialog
covered the desktop, so this is renderer readback rather than visual inspection.
It does not run the game startup or establish stock visual parity.

The startup path reaches `ResourceRef.id()`. Stock handler `0047d290` returns
the native link record's cached ID at `+8`, or zero when no record is present.
The host reads the live owner link when one exists, so destroying its target
clears the returned ID instead of exposing the stale copy in VM state. The
registered native test covers an uninitialized reference, real pack IDs,
clearing, and dynamic-node destruction. See
`local/census/DecompileAt-0047d290.log` and `tests/resource_ref_set_test.cpp`.

`renderActiveViewport` draws the ordered camera attachments of an active
runtime viewport through their native-created `CameraFrame` objects. The
caller provides current visibility controls and handles clear/present. The
native camera test now creates and activates a viewport and camera through the
registered handlers, then runs this host path; the test scene contains no
rectangle. Global viewport enumeration and the real startup frame loop remain
separate and are not inferred from VM bindings.

On the actual 2005 transition, the hidden `dummy` type-1 node has flag `0x40`
and 173 children. `00511180` sends this case to `00511e60`, which applies
zero-input visibility, updates load priority for non-type-1 nodes, and
recurses through loaded children. The bounded host implements this for
runtime types 1, 2 and 3 when the current camera has no existing records in
the subtree. Type-2 setters are `00544780` and `0053fcc0`; type-3 setters
are described above. It preflights the subtree and refuses cases with existing
camera records, mounted children or unsupported types. The traversal test
checks both visibility changes and refusal of an existing record. With this
path, the real 2005 loop presented its main menu; that run later stopped at
the then-unregistered game-time native. This is not a stock frame comparison.
