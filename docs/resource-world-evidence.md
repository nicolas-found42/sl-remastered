# Resource world evidence: `ResourceRef.set(I)V`, pack mount and node creation

Stock executable MD5 `e686099e7233bb7d7c461b1b35c5064c`, confirmed with live Ghidra `get_program_info` before any read. Everything below was read from live decompile or disassembly of that program, or from live memory (`read_memory`), unless it is marked **lead** (read only from `local/census/all.c`) or **inferred**. Nothing in `docs/natives.tsv` was changed and no row is matched.

Host model: `src/resource_world.*` (mount, lookup, link), `src/pack_registry.*` (`view`, `registerPackName`, `slotCount`), tests in `tests/resource_world_test.cpp` against the installed `system.rpk` and `frontend.rpk`.

## 1. The call chain

| Step | Address | What it does |
|---|---|---|
| handler | `0047cf80` `ResourceRef.set(I)V` | `FUN_0045d910` parses the receiver and the int. `FUN_0042ab50(receiver, DAT_0062e008)` reads the receiver's `ptr` int field (the address of its 16-byte link record). A zero record does nothing at all. Otherwise it calls `FUN_00545fc0(record, id)`. Returns void. |
| link | `00545fc0` `(this = record, id)` | see 2 |
| lookup | `00536820` `(world, id, type, flags)` | id 0 returns 0. A pack id below the pack count whose pack state is 0 is mounted (`00544170`). Then `00537000`. |
| find | `00537000` `(world, id, type, flags)` | lookup cache, then tree search, see 3 |
| mount | `00544170` `(this = pack record)` | see 4 |

A field read of an unknown or non-int field in `FUN_0042ab50` raises a script error and returns 0; the host VM already refuses this before the handler runs.

## 2. The link record and `FUN_00545fc0`

Record layout, 16 bytes, allocated by `ResourceRef.newNative` (`0047cea0`, **lead**): `+0` previous, `+4` next, `+8` id, `+0xc` node. A node's referrer list head is at node `+0x48`; the list sentinel is `node+0x44`.

`FUN_00545fc0(record, id)`, from the disassembly:
1. If `record.id == id`, return. This happens even when `record.node` is null.
2. If `record.node != 0`: unlink the record from the old node's list (head field `node+0x48` when `previous` is null), then zero `+0`, `+4`, `+8`, `+0xc`. When `record.node == 0` nothing is touched, so a stale id stays.
3. If `id == 0`, return.
4. `node = FUN_00536820(world, id, 0, 0)`. Zero means return with the record as step 2 left it.
5. Otherwise `record.id = id`, the record becomes the head of the node's list (`node+0x44`), and `record.node = node`.

`src/owner_links.*` is the same operation, with `OwnerLink::cachedValue` as the id word and `owner` as the node. The one thing the host helper does not do is keep an id with no node, so `resourceRefSet` handles step 2's stale id itself (tested).

## 3. Lookup by id

World manager global `00618d48`. Fields used: `+0x11c` default parent list, `+0xffe20` pack count, `+0xffe24` the 0xffff "dynamic" pack record, `+0xffe94 + slot*0x70` pack records, `+0x106e98` lookup cache list, `+0x106ecc` and `+0x106ed0` missing-id count and array.

`FUN_00537000(world, id, type, flags)`:
1. Walk the cache list (entries hold a link record: id at entry `+0x14`, node at `+0x18`). An entry whose id is 0 is recycled. An entry with the requested id: if `type != 0` and the node's type (`+0x4c`) differs, return 0 at once, else move the entry to the front and return its node.
2. No cache hit and `world+0x11c != 0`: search the tree by id (`FUN_00544ac0`, or `FUN_00544b10` when `type != 0`). The tree search compares the node id at `+0x50`. A found node gets a cache entry.
3. Nothing found: unless `flags & 1`, append the id to the missing-id array (`+0x106ed0`). Return 0.

The pack part of the id (`id >> 16`) only matters for the mount step in `00536820`. `0xffff` means a node created at run time (instance id `0xffff0000 | slot`, see `docs/game-creation-evidence.md`); `00536820` skips the mount for it and the same tree search finds it by `+0x50`. **Inferred, not read to the end:** `FUN_00544b10` with a type only descends into top-level children of that type. Every pack node has the same type as its parent or hangs under the default parent, so a type-filtered host lookup by id gives the same answer (the mount tests exercise this for every `frontend.rpk` entry).

Not modelled: the 32-entry cache and its recycling. A type mismatch with a cached entry returns 0 without recording the id as missing; the host never records it.

## 4. Pack mount, `FUN_00544170`

Pack record (stride `0x70`, base `world+0xffe94`; built by `FUN_00543eb0`): `+8` slot index, `+0xc` pack name (0x40 bytes), `+0x4c` scratch, `+0x50` state (`0` unmounted, `1` mounting, `2` mounted; `FUN_00544000` returns it), `+0x54` and `+0x58` the two header counts, `+0x5c` the dependency table, `+0x68`, `+0x6c = 0xffff`.

Flow, from the decompile and disassembly:
1. State not 0: return it.
2. `FUN_0054c170(name)` (file exists) false: return with the state still 0, so every later lookup tries again.
3. State = 1. `FUN_0054c880` (open) null: state = 2 and return (an empty pack).
4. Read 8 header bytes (`RPAK`, `02 00 00 00`; the code writes these bytes into the buffer and then reads over them, so the header is not checked), then `+0x54` and `+0x58` (u32 each). The dependency table has `+0x54 + +0x58 + 1` entries of `0x42` bytes. Entry 0 is the pack itself (`+0` zero, `+2` own slot). Each later entry reads a `0x40`-byte record (id, path, trailer); table `+0` is the id's high 16 bits, `+2` is `FUN_00538380(path)` (find or add the dependency's slot), `+4` the path, `+0x3e` the trailer. This registers names; it mounts nothing.
5. Read four u32 (table size, entry count and two ignored values), read the table, then for each entry call `FUN_00544010(this, entry, parentIdRemapped, out, 0)`.
6. Close the file, `+0x4c = 0`, state = 2.

`rpk.cpp` already parses exactly this layout. The host refuses a file the stock reader would read without checking (`MountStatus::Invalid`).

### Id remap is `FUN_00544590`, not a type table

The brief called `FUN_00544590` the type table. It is the serialized-id remap that `src/resource_id.cpp` already implements: pack slot `0xffff` returns the id as is; low 16 bits 0 returns 0; a high part not below the pack count returns 0; high part 0 gives `(own slot << 16) | low`; a high part up to the dependency count gives `(table[high].slot << 16) | low`; otherwise 0. The node type comes from the entry's type byte through the creation switch (section 5).

### Entry to descriptor, `FUN_00544010`

Entry bytes: `+0` parent id, `+4` own id, `+8` type (byte), `+9` flags (byte), `+0xa` compat float, `+0xe` payload offset, `+0x12` payload size, `+0x16` alias length, `+0x17` alias, then `0x30` bytes of transform when `flags & 1`. This is `RpkEntry`.

It builds the descriptor `{type, id (remapped +4), flags, payload offset, payload size, name}`. The name is the alias copied into a new 0x20 buffer (at most 30 bytes) **only for a type-8 entry**; every other type gets a null name. The compat float is not passed on at all. With `flags & 1` it also builds a bounds record `{1, f0..f5}` from the first six transform floats and passes it along. It calls `FUN_00536900(world, id, remappedParent, &descriptor, parentNode = 0, bounds or 0, 0)`.

## 5. Node creation, `FUN_00536900` -> `FUN_00536a10`

`FUN_00536900`: if the node's own pack is below the pack count, call `FUN_00544170` for it (nothing for the pack being mounted, whose state is already 1). Then `FUN_00536a10`; on success `FUN_00544b90(0)`, which clears the loaded bit and puts the node on its type's unloaded list. The node's state word (`+0x64`) is then `0x40` and its priority (`+0x68`) 0.

`FUN_00536a10`:
1. The type switch (table below) allocates and constructs. A type with no case, or a failed allocation, logs `Node registration error: %s:%08X, (%s) type=%d` (format at `00619600`) and creates nothing. The mount goes on with the next entry.
2. **Parent.** A zero remapped parent id means the default parent (`*(world+0x11c)`, valid when its next pointer is non-null). Otherwise the parent id's pack is mounted when its state is 0 (this is the dependency mount), then `FUN_00537000(parentId, childType, 0)` finds the parent, so the parent must be of the child's own type.
3. No parent: log `Unregistrable node: %s:%08X (invalid parent %s:%08X)` (`006195c8`) and then write `*(0x14 + node) = 0` and read `*(0x38 + 0)`, a null dereference. The host reports `MountStatus::StockCrash` and stops, with the pack left in state 1.
4. Attach: `node+0x14 = parent`; append to the parent's child list (`parent+0x30` sentinel, `parent+0x38` tail).
5. If `DAT_00619d68[node type] != 0` run `FUN_00544fe0(this = node, parent)`. Inside, the table is indexed with the **parent's** type (`*(parent+0x4c)`). When the parent's type is not hooked, or the node flags have `0x10`, the node becomes its own sector root (`+0xcc` = itself, `+0xc8` = its id). Otherwise the parent's `+0xcc` is copied into the node.

### Per-type table

Live reads of `00619d10` (flags ORed into `+0x54` by `FUN_00544620`) and `00619d68` (sector hook). Allocation sizes and constructor addresses are from the switch. Alias is the name the type has in `system.rpk`. "Mount-time work" is what the constructor itself does: **every constructor listed only writes fields, vtable pointers and empty list heads in the new object and calls only other field initialisers (`FUN_00551750`, `FUN_00436a90`, `FUN_004fe620`, `FUN_004fe640`, `FUN_004fe910`, `FUN_0052b250`, `FUN_005404d0`, `FUN_005480e0`, `FUN_00549580`). None calls the renderer, the sound system or the file system.** Two helpers called from the type 3 and type 0xe constructors, `FUN_00428fc0` and `FUN_00429330`, and the pool allocator `FUN_00538720` for type 1 were not read; from how they are used (called on embedded sub-objects right before a vtable store) they are inferred to be list-head initialisers. `FUN_00549580` (type 0xe) does call `FUN_00545fc0(0)` on an embedded link, which is the same detach as in section 2 on a zero record.

| Type | Alias in system.rpk | Bytes | Constructor | `+0x54` flag OR | Sector hook |
|---|---|---|---|---|---|
| 1 | `ins_game` | node pool (`FUN_00538720`) | `0053e6e0` | 0 | yes |
| 2 | `ins_physics` | `0x164` | `0053f8b0` | `0x100` | yes |
| 3 | `ins_render` | `0x26c` | `00543000` | `0x100` | yes |
| 5 | `res_mesh` | `0xfc` | `0053b520` | `0x100` | no |
| 6 | `res_sound` | `0x90` | `0053bf40` | 0 | no |
| 7 | `res_texture` | `0xac` | `0053d060` | 0 | no |
| 8 | `typ_game_object` (script) | `0xa0` | `00546ae0` | 0 | no |
| 9 | `typ_physics_body` | `0x94` | `005473d0` | 0 | no |
| 10 | `typ_physics_constraint` | `0x90` | `00547650` | 0 | no |
| 12 | `typ_render_camera` | `0xbc` | `00547f60` | 0 | no |
| 13 | `typ_render_light` | `0x1a0` | `00549010` | 0 | no |
| 14 | `typ_render_object` | `0x140` | `0054a5b0` | 0 | no |
| 18 | `res_viewport` | `0xcc` | `0053d990` | 0 | no |
| 19 | `res_animation` | `0x90` | `005389e0` | 0 | no |
| 20 | `ins_render_sound` | `0x94` | `00549430` | 0 | no |
| 21 | (none) | `0xb8` | `0053c3d0` | 0 | no |

No case in the switch: 4 (`res_force_fx`), 11 (`typ_physics_particle`), 15, 16, 17 (`ins_render_sprite`, `ins_render_text`, `ins_render_horizon`), and everything else. `system.rpk` has one entry of each of types 1 to 20, so mounting it logs five registration errors and creates 23 nodes. Hook table rows 15, 16 and 17 are set too, but those types cannot be created.

Types 1, 2 and 3 pass the bounds record to `FUN_00544d40` (7 dwords into `+0x80`; `+0x80 = 2` and zeros when absent). The host keeps the first six floats as `ResourceNode::bounds`. For other types the record is ignored.

### What the host node holds, and what is deferred

`ResourceNode` holds exactly what the mount writes that other code reads: type, id, flags (`entry.flags | table flag`), payload offset and size, the type-8 name, parent id and parent, ordered children, the bounds record for types 1 to 3, the sector link, the unloaded state word `0x40` and priority 0. A type-8 node's links live on the `GameWorld` `ScriptNode` (created at mount with an empty class name and not loaded), so `GameType.createNativeInstance` and `ResourceRef.set` share one referrer list. The script class is read from the payload at load time (`FUN_005461a0`, see `docs/game-creation-evidence.md`), never at mount, and `createNativeInstance` now accepts an unloaded script node with no class yet.

Deferred, with a named state (`ResourceWorld::requestPayload` returns `PayloadStatus::Deferred` and says why; it is never a silent value): reading the payload, the per-type load virtuals reached from `FUN_005447d0`, texture upload, font and glyph tables, mesh and material data, sound buffers, animation, light, camera and viewport objects, physics bodies. For the types whose system.rpk alias names a renderer or sound object (3, 5, 6, 7, 12, 13, 14, 18, 19, 20, 21) the load will need the renderer; this was judged from the alias only because the per-type loaders were not read. `FUN_005447d0` and the loaded-node lists (`FUN_00537d80`, `FUN_00537de0`, `FUN_005378d0`) are not modelled beyond the `0x40` state word.

## 6. The world root is a type-0 "nulltype" node

`FUN_00535e70` (the world constructor, called first from `FUN_0058c700`, **lead** for the caller) creates a base object (`FUN_00544620`) with an all-zero descriptor and the name `nulltype`, sets its parent to null, appends it to the world's top-level list with `FUN_004a5d20(world+0x114, node)`, marks it loaded with priority 1.0, and registers pack record `0xffff`. Live xrefs show `FUN_004a5d20` is called only here and in `FUN_00498b10`, so this is the only top-level node and it is what `world+0x11c` points to. Startup then registers `system.rpk` as pack slot 0 (`FUN_00538380`, **lead** for the caller).

Consequence, now in the host model: the stock default parent has type 0, not 1. By section 5 step 5, a type 1, 2 or 3 node created under it (every `system.rpk` entry of those types, and the `Dummy` node of `createNativeInstance`) becomes **its own sector root** at creation, because `DAT_00619d68[0]` is 0. The call site was read at instruction level (`00536e17` to `00536e26`: `FUN_00544fe0` with `this` = the new node and the parent as its argument), so the argument order is no longer inferred. `GameWorld::addRoot()` makes that type-0 `nulltype` node (no instance id, flags 0, loaded, priority 1.0, no sector link; stock allocates only `0x80` bytes for it) and `GameWorld::addChild` applies the rule, so the `Dummy` node is its own sector root. `docs/game-creation-evidence.md` step 14 assumed the sector link was null and gave a wrong reason for skipping `FUN_004b3ee0`; the corrected reading is there: `FUN_0048ae40` does run on the node's sector target and returns 0 because that node has no native instance. The old type-1 stand-in root is gone. `ResourceWorld` treats `defaultParent()` as the type-0 root for parent and sector decisions, and now that is what it is.

## 7. What the real packs contain (measured with `rpkParse`)

- `system.rpk`: 28 entries, no dependencies. Nine type-8 scripts (`typ_game_object` id 1; `Config` 0x47, `Dummy` 0x1b, `game` 0x4c and `native` 0x15 under it; `camera`, `Controller`, `map`, `trigger` under `native`) and one entry of each other type 1 to 20. 20 entries have parent 0; 5 of them are skipped.
- `frontend.rpk`: 279 entries, dependencies `system.rpk`, `misc\garage.rpk`, `cars.rpk`. Types: 201 texture (7), 31 type 14, 22 mesh (5), 14 sound (6), 5 script, 3 type 13, 2 type 9, 1 type 3. Every entry has a parent. All 279 are created. No entry names `garage.rpk` or `cars.rpk` as a parent, so mounting `frontend.rpk` registers their slots and mounts only `system.rpk`.
- **Frontend ids.** `Frontend.defLoadingPic` is `frontend.rpk` id `0xa8` (`loading`, type 7), under `backgrounds` `0xd2`, `OSD` `3`, and the `system.rpk` `res_texture` node. `Text.RID_*` (read from `Text.class` literals) are type-14 nodes under the `frontend.rpk` node `0x1f` (`fonts`), which sits under the `system.rpk` `typ_render_object` node (id 3): `console5` `0xb2`, `console10` `0x23`, `sl28` `0x2e`, `slii11` `0x138`, `slii17` `0x139`, `slii24` `0x25`, `sl14` `0x8c`, `simple20` `0x20`, `simple40` `0x22`, `sym_pointers` `0x3c`. They are **not** the type-7 textures of the same names (ids `0xa`, `0xb0`, `0x26`, ...). The sound effects (`money` `0x9b`, `question` `0x7e`, `ok` `0x80`, `warn` `0x81`, `move` `0x79`, `back` `0x78`, `select` `0x7a`) are type-6 nodes under `0x2c`.
- Mounting `frontend.rpk` first looks up its first entry's parent (`system.rpk` id 8), which mounts `system.rpk` in the middle of the frontend mount. The tests check this.

## 8. Other `ResourceRef` natives and what they need

All from `local/census/all.c` (**lead**), and the triage files; none verified against the live program here.

| Native | Address | Needs |
|---|---|---|
| `newNative()V` | `0047cea0` | allocate a zeroed 16-byte record when the `ptr` field is 0 |
| `deleteNative()V` | `0047cf00` | unlink as in section 2 step 2, free, set `ptr` to 0 |
| `id()I` | `0047d290` | returns record `+8`; a null record is the "Mighty ERROR" script error |
| `type()I` | `0047d210` | returns `node+0x4c` of record `+0xc`, 0 for no node; null record is the error |
| `set(ResourceRef)V` | `0047cfc0` | copies the other ref's **node** (not its id): record `+8` becomes `node+0x50`; null other ref clears like `set(0)` |
| `getParentID()I`, `getParent`, `getWTRoot`, `getFirstChild`, `getNextChild` | `0047d3e0`, `0047d4b0`, `0047d570`, `0047d630`, `0047d6f0` | parent and child pointers (`node+0x14`, `+0x30` list); `ResourceNode::parent` and `children` carry these for pack nodes |
| `load`, `unload`, `cache`, `precache` | `0047d0a0`, `0047d170`, `0047e8d0`, `0047e950` | the node load path `FUN_005447d0`: deferred |

`ResourceRef.precache` passes `(1, 1)` to `0048a920`, unlike the synchronous
`cache` call's `(0, 1)`. For a mounted, unloaded nonempty type-6 sound,
`00537240` queues the pack read and sets flag `0x02000000`; `00544540` marks
the read complete and `005378d0` processes completed requests. The latter
increments its count before comparing with `resource_loadrate`, which
`00427bf0` reads from Config. The host keeps a node-owned pending entry,
coalesces repeat requests, and loads the actual sample at the frame drain.
Because pack contents are already in memory, the host marks its read ready
immediately; this models neither stock worker scheduling nor disk timing. The
test queues the actual frontend move, select and question sounds and checks
the configurable per-frame limit and resulting samples. Other resource types
and synchronous `cache` while a read is pending remain unsupported.

`set(I)V` needs none of these.

## 9. VM contract for `ResourceRef.set(I)V` (implemented in `src/natives/resource_ref.cpp`)

Registered, handler `resourceRefSetInt`. `VmGameState::resources` holds the `ResourceWorld*`. The handler is tested directly in `tests/resource_ref_set_test.cpp` (handle 0 no-op without a binding, link and clear, repeat id, mount of a second pack, a miss that is a normal return and records the missing id, a run-time node id with the high bit set, a second receiver sharing a node, argument and receiver errors, no resource world, `StockCrash` with no world root) and through the real `Frontend` static initializer in `tests/vm_class_init_test.cpp` (`defLoadingPic` links to the `frontend.rpk` `0xa8` type-7 node under `backgrounds` `0xd2` under `OSD` `3`). `MountRefused` is mapped to a VM error but no test makes the host refuse a pack. The row in `docs/natives.tsv` stays `unread`: the lookup cache, `FUN_00551330`, the loaders and the `ResourceRef.<init>(I)` caller semantics are not read.

Register `java.util.resource.ResourceRef.set(I)V`, stock handler `0047cf80`, native table entry `00489354`.

- **Host objects.** One `ResourceWorld(GameWorld&, PackRegistry&)` per run, owned next to the `GameWorld` and `PackRegistry` that `VmGameState` already reaches (add a `ResourceWorld* resources`). It must be destroyed before them. The `GameWorld` needs its root (`addRoot` until the world root is modelled); without one the first mount ends in `StockCrash`.
- **Arguments.** The receiver object and one `int`. Pass the int's bits as `uint32_t` (ids use the high bit freely, for example `0xffff0000 | slot`).
- **Receiver state.** Read the receiver's `ptr` int field through the existing receiver handle (the same `ReceiverBinding` in `VmGameState::bindings` that `GameType.createNativeInstance` uses). Handle 0 (`newNative` has not run) is a silent no-op, not an error, as in stock. Do not create a binding here.
- **Call.** `resourceRefSet(*resources, binding.link, id)`.
- **What it writes.** `binding.link` (owner and `cachedValue`) and, through the owner's list, the node's referrer list. After the call, in every case including `NotFound`, copy `binding.link.cachedValue` into the receiver's `VmResourceState.words[2]` (the id word), as the `createNativeInstance` handler does. It also mounts packs on the way, appends unresolved ids to `missingIds()` and adds messages to `log()`.
- **Result.** Void, nothing is pushed. `Unchanged`, `Cleared`, `Linked` and `NotFound` are all normal returns: stock reports an unknown id only through the missing-id array, not as a script error. `StockCrash` and `MountRefused` become a VM error whose text is `RefSetResult::detail` (stock would crash or, for a refused file, read garbage).
- **Receiver `ResourceRef` natives that follow** use `resources->resolve(link.owner)` to get the node's type and id (`type()`, `id()`), the same link for `deleteNative`, and `ResourceNode::parent` and `children` for the tree natives.

## 10. Verified, inferred, unknown

Verified (live disassembly or decompile, or live memory): sections 1, 2, 4 (including the unchecked header and the state transitions), 5 (switch, parent rules, hook arguments and the parent-type indexing, per-type tables), 6 (nulltype root and its single caller), 7 (measured on the installed files), and the constructors' lack of renderer calls.

Inferred, not read to the end: the tree search's type filtering (`FUN_00544ac0`, `FUN_00544b10`) and the list layout details behind `world+0x11c`; that the "unloaded list" calls `FUN_00537d80` and `FUN_00537de0` have no effect on lookup; that a name `%s` of null prints as `(null)` in the registration error (`FUN_00551330` was not read, and a message is all the host records).

Unknown or not modelled:
- `FUN_00551330` itself (whether it only logs).
- The lookup cache, and the missing-id array's later use (`FUN_005360d0` writes `res_ban.log` from it).
- The loader, `FUN_005447d0`, and every per-type payload loader.
- Duplicate ids inside one pack: stock creates both nodes and finds the first in tree order; the host indexes the first (tested). A node whose remapped own id is 0 is created and cannot be found (the host does the same).
- `PackRegistry` numbers slots when a literal is resolved and when a pack file is parsed; stock numbers them when a literal is resolved and when a pack is mounted. The slot numbers of a pack's dependencies can therefore differ from stock if a literal for another pack is resolved between resolving the first literal and the first mount.
- The transform record's last six floats, and the 12-float transform's use for types other than 1 to 3.
- Whether `ResourceRef.<init>(I)` calls `set(I)` (not checked).
- Nothing here was compared with a run of the stock game.

For a mounted resource with payload size zero, the identified stock
`00537240` calls `00544b90(1)` and sets load priority to 1 without calling its
type loader. The host now applies that fast path to type-6 and type-8 nodes
for `ResourceRef.cache`, and to type 8 for `GameRef.isScripted`. The real
`humans.rpk` `walk` (0x6b, type 8) and `humans` (0x4, type 6) entries have
empty payloads. Tests check loaded state, priority, empty script identity and
repeat calls. Nonempty unsupported types remain explicit.
