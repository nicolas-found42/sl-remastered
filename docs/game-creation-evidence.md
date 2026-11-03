# Game object creation evidence

The stock executable registers `java.lang.GameType.createNativeInstance(Ljava.util.resource.GameRef;ILjava.lang.String;Ljava.lang.String;)V` at `00481a70`; Ghidra identified it by MD5 `e686099e7233bb7d7c461b1b35c5064c`. The installed `Dummy` constructor passes a null parent, `GameRef.RID_DUMMY`, a null string and the literal `dummy`. `RID_DUMMY` resolves to `system.rpk` local ID `0x1b`, a type-8 resource containing `script java.util.resource.Dummy`.

For a nonzero resource ID, the handler resolves that script resource and calls `FUN_0053a2a0`. A null or empty parent falls back to a global parent. `FUN_0053a2a0` allocates an instance ID, creates a type-1 resource under the selected parent, loads the referenced script resource, links the script receiver and runs lifecycle routines. For a `script <class>` resource no native factory exists (see below), so no game object is created by this call. The handler then attaches the created resource to the receiver's native state. The exact allocation, fallback, loading and lifecycle semantics are incomplete; a no-op or invented attachment is not an implementation of this native.

`prepareGameCreation` validates a bounded nonzero pack-resource request. The registered handler in `src/natives/game_type.cpp` also handles zero IDs and null names before calling `createNativeInstance` against the host `GameWorld`. Full allocation and lifecycle semantics remain incomplete.

`FUN_0053a2a0` prepares a type-1 descriptor with flags `0x21`. Its name copy calls `FUN_00551120` with count `0x1f`; the underlying `FUN_005548c0` copies at most 30 bytes before the terminator. The host descriptor helper tests this boundary but does not allocate or bind a resource.

The resource factory `FUN_00536a10` appends each created child to its parent's resource list; `FUN_005517f0` destroys linked children, and `FUN_00544700` releases instance IDs and invalidates resource references. Spatial placement uses a separate list. `src/resource_tree.cpp` models ordered insertion and subtree removal with stable host-only handles; it does not model stock IDs, attachment invalidation, callbacks, reparenting, or the spatial list, and is not yet wired into game-object creation.

`FUN_00429060` links an owned record to an owner's list, moving or detaching it when the owner changes and caching an owner value. `src/owner_links.cpp` models that local operation with stable native pointers; tests cover insertion order, movement, detachment and repeated assignment. The caller must maintain object lifetimes and detach before destruction. This is not yet connected to the interpreter or the game-object lifecycle.

## Dummy path, read in full (stock MD5 `e686099e7233bb7d7c461b1b35c5064c`)

Method. Live Ghidra `disassemble_function` of `FUN_0053a2a0` fixed the hidden register arguments that the decompiler drops. The other bodies were read from `local/census/all.c`, which is a lead; vtable and table bytes were read from live Ghidra memory. Items marked inferred were not confirmed at instruction level.

**World manager.** The global at `00618d48` is the `this` pointer of `FUN_00536ed0` (instance ids), `FUN_00536890`, `FUN_00536900`, `FUN_00536a10` and `FUN_005383f0`. Its field `+0x11c` is the default parent (first top-level node, valid when that node's next pointer at `+4` is non-null).

**Handler `FUN_00481a70`.** `FUN_0045d910` parses the signature into the parent record, the id, the argument string and the name. `FUN_00546070` is a `__thiscall` on a local 16-byte link record `{previous, next, id, node}`. It resolves the id through `FUN_00536820` and `FUN_00537000` with type 8, which builds the type-8 node from the pack on demand, and links the record into that node's referrer list at `+0x48`. A null or empty parent record is replaced by the zeroed global record `DAT_00636460` (live read: all zero), so parent id 0 selects the world default parent. After `FUN_0053a2a0` returns, the handler reads the receiver's `ptr` field (field index `DAT_0062e008`, an `I` field holding the address of the 16-byte link record that `FUN_0047cea0` `ResourceRef.newNative` allocates). A null record is the "Mighty ERROR" script error. If the record already points at another node it is detached first. Then `FUN_004290f0` (`this` = node `+0x44`, so list head at node `+0x48`) links it to the new node and stores the node's instance id (`+0x50`) in record `+8`. The handler's temporary script link is unlinked at the end. The native returns void.

**`FUN_0053a2a0`, confirmed from the disassembly.** Arguments: parent record, script link record, receiver, argument string, name. Steps:
1. The script link's node must be type 8, otherwise `FUN_00551330` logs "create: Wrong GameType" and the function carries on.
2. `FUN_00536ed0` returns an id (`0xffff0000 | slot`). It never returns 0, so its "failed" test is dead code.
3. Descriptor on the stack: `{type 1, id, flags 0x21, 0, 0, name pointer}`. The name buffer is 0x20 bytes filled by `FUN_00551120` with count 0x1f. The default is `_gameinst` at `006198d0`.
4. `FUN_00536900` is used when the parent record has a node, `FUN_00536890` when it has only an id. Both call `FUN_00536a10`.
5. `FUN_00536a10` case 1 takes a slot from `FUN_00538720` and runs `FUN_0053e6e0`: base constructor `FUN_00544d40` (copies the descriptor to `+0x4c..+0x5c`, copies the name to a new 0x20-byte buffer at `+0x60`, ORs the per-type flag word `DAT_00619d10[type]` which is 0 for type 1, sets bounds kind `+0x80 = 2`, scale `+0xbc = 1.0f`, offset `+0xb8 = 0`), then `FUN_0053dd80` on the embedded sub-object at `+0xd8` (the destructor `FUN_0053f140` confirms this: it writes vtable `005f3750` at word `0x36`). Vtable `005f375c` slot `+0xc` is `FUN_0053efa0`, which returns `this + 0xd8`.
6. With no node passed, the parent is `FUN_00537000(parent id, type 1)`, which requires a type-1 parent. If no parent is found the function logs "Unregistrable node" and then dereferences a null pointer. The node is appended at the tail of the parent's child list: new next = parent `+0x30`, new previous = old tail, `parent+0x38` = new node.
7. Type 1 has a non-zero entry in `DAT_00619d68`, so `FUN_00544fe0(this = new node, parent)` runs (call site `00536e17` to `00536e26`, read from the disassembly: `MOV EAX,[ESI+0x4c]`, `MOV ECX,[EAX*4+0x619d68]`, `PUSH EDI` with EDI the parent, `MOV ECX,ESI`). Inside, the table is indexed with the **parent's** type (`*(parent+0x4c)`). A parent whose type is not hooked, or a node with flag `0x10`, makes the node its own sector root (`+0xcc` = itself, `+0xc8` = its id, and its link record joins its own referrer list at `+0x48`). Otherwise the parent's `+0xcc` is copied into the node. The default parent is the type-0 `nulltype` node (see "Correction" below), which is not hooked, so **the `Dummy` node is its own sector root as soon as it exists.**
8. Back in `FUN_0053a2a0`: the parent's `+0x70`, `+0x6c` and `+0x68` are copied into the new node through vtable slots `+0x18` and `+0x14` (`FUN_0053f040`, `FUN_0053eff0`) and the load-state helper `FUN_005447d0` runs on an unloaded parent. These are load-priority floats stamped with the global frame counter `DAT_006200a4`. They are not read by anything on the Dummy path. This is bookkeeping and is not modelled.
9. `DAT_00765f60` is incremented. Flags `+0x54 |= 0x200`, so the node holds `0x221`. `FUN_00544b90(1)` sets the loaded bit (`+0x64` bit 0). `+0x68 = 1.0f`.
10. `FUN_00429060` with `this` = node `+0x110` links the node to the script node. That link caches the script resource id.
11. The script node is made loaded (vtable slots `+0x14` and `FUN_005447d0`). Its embedded sub-object is at `+0x80` (vtable `005f38a8` slot `+0xc` is `FUN_00546ce0`, `return this + 0x80`). Its `+0x18` and `+0x1c` copy to the node's `+0xbc` and `+0xb8`.
12. If the sub-object's factory at `+0xc` is non-null, its vtable slot `+0xc` creates a native instance, stored at node `+0x124`, and slot `+0x40` is called. The node receiver is stored at `+0x128`.
13. `FUN_00427210` with `this` = `00636338` (see "open items").
14. `FUN_004988a0` unions the bounds of type-2 children in the list at `+0x20` and is empty for a new node. `FUN_00498b10` returns 0 at once because `+0x80 == 2` (its first statement; this does not depend on the sector link). `FUN_0048ae40` is called with `ECX` = node `+0xc0` (disassembly at `0053a565`), so its `this+0xc` is the node's sector target. **Earlier text said it returns 0 because the sector link is unset. That reason was wrong** (the link is set, see step 7). It does run on the Dummy node, and returns 0 for another reason: it loads the sector target (type is 1, so no vtable `+0x14` call; `FUN_005447d0(0xa0000000, 0, 0)` on a loaded node with priority 1.0 and no load flag only reaches the loaded-list bookkeeping `FUN_00537d10`), takes the target's sub-object through vtable `+0xc` (`this + 0xd8` for type 1), and returns non-zero only when the sub-object word `+0x40` is `0x35` and its native instance `+0x4c` (node `+0x124`) is non-null. `FUN_0053dd80` zeroes both. A `script <class>` payload has no factory, so the native instance is never set and `FUN_004b3ee0` is not called. Read from the decompile of `FUN_0048ae40`, `FUN_005447d0`, `FUN_0053dd80` and the disassembly of `FUN_0048ae40` (live Ghidra, MD5 confirmed). Static evidence only.

**Script load (`FUN_005461a0`, reached by `FUN_005447d0` -> `FUN_00537240` -> `FUN_00546c10`).** The payload `script java.util.resource.Dummy` takes the `script` branch when the detail value is above `_DAT_005f09d0` (0.5; the creation path passes 1.0). It calls `FUN_00410890(name)`, a class lookup that raises a script error ("JVM.getClass: class not found") when the class is missing, and stores the class at the sub-object's `+0x10`. The factory pointer at `+0xc` is only set by a `native <name>` payload. So for Dummy the factory is null and step 12 is skipped.

**Corrections to earlier text in this file.** The earlier sentence that the call may "create a game object" is wrong for a `script` resource. The 16-byte receiver record is a link, not a resource node.

### Open items (named, not guessed)

1. **`FUN_00427210` (resolved for the installed scripts, partly modelled).** It calls `FUN_00426470` three times with the new node's receiver link and record kinds 8, 9 and 0x1c. For a type-1 node with no native instance and a receiver set, `FUN_00426470` runs `FUN_00425a90(sub-object, "getInfo", 1, 2, kind, 0x4f)`, which forwards to `FUN_004255e0`. Read from the disassembly: the instruction at `00426587` loads the function's second stack argument, which is the record kind, and pushes it after the literals `1` and `2`. `FUN_004255e0` hands marker `2` and a pointer to that value to `FUN_00425240`, which reads marker `2` as an integer (the type stored at `DAT_0062e00c` is the string `I`, from `FUN_0040fe70`, live read of `0060ab54`) and stops at marker `0x4f`. So the Java argument is the record kind as an `int`: 8, then 9, then 0x1c. The callee is the receiver's `getInfo(I)I`, found by name through its class and ancestors. For `Dummy` that is `GameType.getInfo(I)I`, tree 78, which pushes the literal 0 and returns it, so all three answers are 0. A non-zero answer allocates 0x1c-byte entries in the global lists at `00636338`. That branch is not modelled, so a non-zero answer is reported Unsupported. Evidence strength: static disassembly plus the script dump, no runtime observation. A Wine breakpoint on `00425a90` would still be the independent confirmation.
2. The world root is now modelled (`GameWorld::addRoot`, see the correction below). What makes `FUN_00537000`'s pack path (`FUN_00544ac0`, `FUN_00544b10`) build the type-8 node is the pack mount in `src/resource_world.*` (`docs/resource-world-evidence.md`).
3. Parent not loaded and `FUN_005447d0` load scheduling. Not modelled (the unloaded-parent case is the Unsupported issue `ParentNotLoaded`). A parent that has a sector link is modelled: the child copies it.
4. The zero-ID class fallback and null-name default are modelled for the bounded host world, as described below. The state of the stock global parent link at `0062f260` across the full engine lifecycle and exact type-8 destruction behavior remain open.
5. Instance ids: stock's counter is a 16-bit value that wraps. The host allocator stops at 0xffff.
6. The loaded-node bookkeeping calls `FUN_00537d80`, `FUN_00537de0`, `FUN_00545fc0` and the frame counter stamps are host-invisible and omitted.

### Host model (`src/world_model.*`, `src/game_creation.*`)

`GameWorld` owns the type-1 `WorldNode`s (on top of `ResourceTree` for order and subtree removal), the type-8 `ScriptNode`s, `InstanceIds`, and the links of `OwnerLink`. A node is the `LinkOwner` of the links that point at it, with its instance id as the cached value; a script node caches its resource id. `createNativeInstance(world, request, receiver)` returns `Created`, `StockError` (stock logs a fatal error or crashes) or `Unsupported` (named `CreationIssue`; tree and ids rolled back). It is tested in `tests/game_creation_test.cpp` against the real installed `Dummy` resource.

### Registered handler (`src/natives/game_type.*`)

`java.lang.GameType.createNativeInstance(GameRef,I,String,String)V` at `00481a70` is now registered. The handler reads its four arguments, requires the receiver's `ptr` handle to be non-zero (`ResourceRef.newNative` has run), keeps one `ReceiverBinding` per handle in `VmGameState` (reachable from `VmArena::gameState`), supplies `getInfo` through `VmArena::callScript` (see `docs/vm-ops.md`), calls `createNativeInstance`, and on `Created` copies the binding's cached id into the receiver's `VmResourceState.words[2]`. A non-null parent GameRef contributes that same `words[2]` as the parent instance id. `StockError` and `Unsupported` become a VM error whose text names the issue.

`tests/game_type_native_test.cpp` exercises the real `Dummy.<init>` chain and checks resource attachment, flags, sector links and the three `getInfo` calls. It also checks that zero-ID creation caches a type-8 class resource, uses the runtime descriptor for a null name and reuses the class resource on a second creation.

`docs/natives.tsv` keeps this row `unread`. Non-zero `getInfo` answers, native-factory resources, unloaded-parent scheduling, frame-counter stamps, 16-bit instance ID wrap-around and the shared parameter parser remain incomplete. The parent and receiver handles are host handles, not stock link-record addresses.

### Zero-ID class fallback

Headless decompilation and disassembly of `FUN_00481a70`, `FUN_0053a1a0` and `FUN_00404ea0` confirmed the stock MD5. When the script ID is zero, the handler reads the receiver's runtime class cache at class offset `0x1d4`. If it is zero, it calls `FUN_0053a1a0` with the global link at `0062f260`, the runtime class and the class descriptor returned by `FUN_00404ea0`, then caches the new node's ID. That function allocates an ID, creates a type-8 node with descriptor flags `2`, copies at most 30 bytes of its supplied name, marks it loaded at priority 1, and stores the class in the type-8 subobject at `+0x10`. The `_gametype` name is only used when the supplied name is null. The handler supplies the descriptor, so that default is not used here. For a null instance name it also supplies the runtime class descriptor to `FUN_0053a2a0`.

The host caches the generated resource by runtime class name, allocates it from the same instance-ID allocator, places it under the host default root and links the new type-1 instance to its loaded script record. This assumes the global link selects the default root, as its zero-initialized state does. The host class name represents the class pointer and the two host records represent one stock type-8 node. Instance creation still rejects nonzero `getInfo` registration, unloaded parents and unsupported resource payloads. No stock runtime comparison has established the lifecycle or global-parent assumptions.

### `GameRef.setFlags(I)V` at `00486c80`

Disassembled this session (MD5 confirmed). After `FUN_0045d910` parses the receiver and the integer, the handler reads the receiver's `ptr` field (link record) and its node at record `+0xc`. A null record would crash. When the node is non-null it does `node+0x54 |= flags`, and when `flags & 0x10` (`GameRef.WORLDTREEROOT` is `0x10`, read from the initialized class) it calls `FUN_00544f40(node, record)`. That function relinks the node's sector link at `+0xc0` to the record's node, which is the node itself, and caches the node's instance id. `GameWorld::orFlags` models exactly this. Installed `Dummy.<init>(GameRef,I)` calls `setFlags(I)` with the integer it was given, and `Sound.init` passes `WORLDTREEROOT`, and the sound dummy, a child of the type-0 root, is already its own sector root, so the call changes only the flags: the probe prints `flags 0x231 (own sector root)`. A node whose sector link a type-1 parent gave it is relinked to itself by this call (`GameWorld::orFlags`, tested). Not modelled: the `-0x44` sentinel branch in `FUN_00544f40`. The row stays `unread`.

### Correction: the default parent and the sector link (resolved)

The world constructor `FUN_00535e70` creates the only top-level node: a type-0 `nulltype` base object (a bare `FUN_00544620` node of `0x80` bytes with an all-zero descriptor, so id 0 and flags 0, loaded with priority 1.0), which `world+0x11c` points to. In `FUN_00544fe0` the sector-hook table `DAT_00619d68` is indexed with the parent's type. For a type-0 parent the entry is 0, so the node becomes its own sector root at creation. Verdict, read from the disassembly of `FUN_0053a2a0`, `FUN_00536a10` and `FUN_00544fe0`:

- The earlier claim that the `Dummy` node has no sector link was wrong. It is its own sector root, with or without `GameRef.WORLDTREEROOT`.
- Because of that, `GameRef.setFlags(0x10)` (`FUN_00544f40`) is a no-op on a node created under the root (its early return, the link already points at the node). It still ORs `0x10` into the flags, so the probe's `0x231` is unchanged.
- A node created under a type-1 node is a hooked case and copies the parent's sector link, so it is not a sector root (`GameWorld::addChild`, tested in `tests/game_creation_test.cpp` and through the real `Dummy.<init>(GameRef)` in `tests/game_type_native_test.cpp`).
- The creation tail still ends the same way: `FUN_004988a0` finds no children, `FUN_00498b10` returns at once, and `FUN_0048ae40` returns 0 because the sector target has no native instance. So `FUN_004b3ee0` is not reached for a `script <class>` node. This holds for every world node the host can create. A sector target that is a pack-mounted node, or a script node with a `native` factory, is not modelled (`GameWorld` only holds nodes made by `createNativeInstance`, which never have a factory).
- Host model: `GameWorld::addRoot()` now makes the type-0 `nulltype` root (no instance id, not found by id, flags 0, loaded, priority 1.0, no sector link). `GameWorld::addChild` runs the `FUN_00544fe0` rule. The old type-1 `diagnostic_root` stand-in and the `SpatialSectorLink` Unsupported status are gone. Instance ids no longer include one for the root: the first node a run creates gets the first id.


## Script resource payload format (read 2026-10-28)

`FUN_005461a0` (stock MD5 verified) walks a type-8 script resource as text
lines, and as a chunked form (magics `ICAL`, `ICFG`, `XCAL`, `XCFG`, `LODA`)
the port has not read. Text directives:

- `script <path>` names the class. A path ending `.class` maps through the
  stock script-root table (strings at 0060ab78-0060ac3c:
  `system\scripts\lang/io/util/render/sound`, `sl\scripts\game`), whose root
  suffix must equal the class's package (`FUN_00410430` errors on a package
  conflict after reading the class file's own header name). The port keeps the
  same table and derives `java.<package>.<Leaf>`; any other path form is a
  bare class name (`FUN_00410890` loads it directly). An unknown root has no
  mapping in the port; stock would read the class file's header.
- `native <device>` builds the node's native device from stock's factory
  table and stores it at node +0xc. The port records the name on the node
  (`WorldNode::nativeDevice`); the device factories are unported and recorded
  as an open gap.
- `lod <value>` and `description <text>` store node metadata (+0x18/+0x14);
  the port skips them. Unrecognized lines are skipped, as stock skips them.

The installed `io.Controller` resource (id 50) is
`script system\Scripts\io\Controller.class\r\nnative player\r\n`; the Init
resource is the bare-name form `script java.game.Init`.

Correction: a resource with a `native` directive cannot follow the script-only
creation path above. The player factory initializes its own instance and
event mask, and its native `getInfo` registers two hooks, rather than returning
the script's zero values. Creation now reports this device as unsupported
before creating a node. See `docs/player-startup-evidence.md` for the stock
addresses, saved logs, command path and work needed to continue.
