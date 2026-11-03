# Startup frontiers

`build/startup_probe` runs installed scripts from `ref/raw` or extracted 2005 scripts in the bounded VM against a host model. The stock executable has MD5 `e686099e7233bb7d7c461b1b35c5064c`. The installed data is a mod installation. Neither probe establishes runtime parity or a playable menu.

## Reproduce

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
mkdir -p local/vm-startup-data/save/game && cp ref/raw/save/game/options local/vm-startup-data/save/game/
build/startup_probe --init --data-root local/vm-startup-data
build/startup_probe --init --data-root local/stock-data '.scratch/2005-extract/Program Files/Street Legal Racing - Redline'
```

The probe exits with status 1 at the current frontier. Extra classes can be initialized up front with `--class <name>` (repeatable). `tools/tufa_dump.py <class file> [tree ...]` prints a class's trees, and `tools/ghidra_run.sh DecompileAt.java <address>...` decompiles and disassembles functions that the live Ghidra window has no function for. Classes the scripts name are loaded and initialized on first use, so `--class` is not needed for `java.render.Frontend`. `tests/startup_constructor_test.py` asserts the output below.

The relevant output is below. The script thread address-like suffix is a host arena position, not a stock pointer.

```
world node 'nulltype' type 0 instance 0x00000000 flags 0x0
world node   'dummy' type 1 instance 0xffff0080 flags 0x231 (own sector root)
world node   'Ljava.render.SimpleLoadingDial' type 8 instance 0xffff007f flags 0x2
world node   'Ljava.render.SimpleLoadingDial' type 1 instance 0xffff007e flags 0x221 (own sector root)
world: 4 nodes, 2 game instances created; resource world: 302 pack nodes, 0 missing ids
Frontend.defLoadingPic = resource 0x000100a8, node type 7
Frontend.largeFont = resource 0x00010025, node type 14
Frontend.mediumFont = resource 0x00010139, node type 14
Frontend.smallFont = resource 0x00010138, node type 14
Frontend.pointers = resource 0x0001003c, node type 14
script thread: Ljava.render.LoadingScreen;<10>
script thread: Hotkey watcher
Running queued script threads after the constructor failure for diagnostics only
script thread slices: Ljava.render.LoadingScreen;<10> = 4
script thread slices: Hotkey watcher = 5
script thread waiting: Ljava.render.LoadingScreen;<10> on java.render.GfxEngine
startup probe: Init constructor: native not registered: java.render.Viewport.create(IFFFF)V
  in java.render.Viewport (tree starting at source line 23)
  in java.render.osd.dialog.Dialog (tree starting at source line 69)
  in java.render.osd.dialog.Dialog (tree starting at source line 59)
  in java.render.osd.dialog.LoadingDialog (tree starting at source line 16)
  in java.render.SimpleLoadingDialog (tree starting at source line 16)
  in java.render.LoadingScreen (tree starting at source line 157)
  in java.render.LoadingScreen (tree starting at source line 162)
  in java.game.Init (tree starting at source line 16)
startup probe: scheduler reached its iteration limit without a frontier
```

The world root is the type-0 `nulltype` node of the stock world constructor `FUN_00535e70`. The 302 pack nodes are `system.rpk` (23) and `frontend.rpk` (279), mounted by `ResourceRef.set(I)V`. Instance ids come from a fresh allocator and are not stock's.

## Frontiers passed

1. **`GameType.createNativeInstance` (handler `00481a70`).** Needed: the open question of the `getInfo` argument, a way for a native to call a script method, and a host-owned world reachable from the VM. Read: `FUN_00426470` at `00426587`, `FUN_00425a90`, `FUN_004255e0`, `FUN_00425240`, `FUN_0040fe70`, and the installed `GameType.getInfo(I)I`. Result: the argument is the record kind as an int, and the answer is 0 (static evidence). Added `VmArena::callScript`, `VmGameState` and the handler (`src/natives/game_type.*`). Tests: `tests/vm_native_call_test.cpp`, `tests/game_type_native_test.cpp`. The row stays `unread`; `docs/game-creation-evidence.md` lists exactly what is missing.
2. **`GameRef.setFlags(I)V` (`00486c80`, with `FUN_00544f40`).** `Dummy.<init>(GameRef,I)` calls it with `GameRef.WORLDTREEROOT` (0x10). Read from the disassembly: flags are ORed into the node at `+0x54`, and bit 0x10 points the node's sector link at itself. Modelled by `GameWorld::orFlags`. Static evidence. The row stays `unread`.
3. **A `()V` script method that returns a value.** `Sound.init()V` ends with `29 2b`. The VM now pushes the returned value instead of failing, as stock's `2b` does. Test in `tests/vm_native_call_test.cpp`.

With these three, `Sound.init` completes. Its debug-dump branch is skipped because `Sound.enableDebugDump` returns 0.

4. **Loading and initializing classes on demand, and static initializers that run code.** Needed: `Init.<init>` names `java.render.Frontend`, which the probe had not preloaded, and `Frontend.defLoadingPic` is `new ResourceRef(<resource literal>)`. Read: `FUN_00410890`, `FUN_00403a90`, `FUN_00403e80`, `FUN_00407da0`, `FUN_00404d40` and the other callers of `FUN_00403e80` (full list and the failure behaviour in `docs/vm-ops.md`, section "Class loading and initialization order"). Result: the VM asks the host to load and initialize a class the first time a script names it (`VmArena::ensureClass`, `ClassRuntime` in `src/class_runtime.hpp`), and `vmInitializeClass` runs any static initializer tree as code in stock's order. Also added: operation `1c` (a field of an object by name) and two integer constructor arguments, which `new Vector()` needs. Tests: `tests/vm_class_init_test.cpp`, including the real stock `Frontend` and `Vector`. The probe now loads `Frontend` by itself and its static initialization passes as far as the next item.

5. **`ResourceRef.set(I)V` (handler `0047cf80`).** Registered in `src/natives/resource_ref.cpp` per the VM contract of `docs/resource-world-evidence.md` section 9: a receiver with `ptr` 0 is a no-op, otherwise `resourceRefSet` runs against the `ResourceWorld` in `VmGameState::resources` and the receiver's id word follows the link. The real `Frontend` static initializer now links `defLoadingPic` to the `frontend.rpk` `0xa8` type-7 node. Evidence: static, read from the live decompile and disassembly (`docs/resource-world-evidence.md`); behaviour tested in `tests/resource_ref_set_test.cpp` and `tests/vm_class_init_test.cpp`. Row stays `unread`.
6. **The world root and the sector link** (a correction, not a frontier). The default parent is the type-0 `nulltype` node, so the `Dummy` node is its own sector root, and `FUN_0048ae40` returns 0 for a different reason than first written. Verdict and changes in `docs/game-creation-evidence.md` ("Correction") and `docs/resource-world-evidence.md` section 6. Evidence: disassembly of `FUN_0053a2a0`, `FUN_00536a10` (call site `00536e26`), `FUN_00544fe0`, `FUN_00544f40`, `FUN_0048ae40`, `FUN_005447d0`, `FUN_0053dd80`.
7. **Operations `04` and `05`, and `07` suboperations 1, 3 and 5.** `FUN_00420ff0`, `FUN_00417840`, `FUN_0041bd90`, `FUN_0041bfb0`, `FUN_0041c290` and `FUN_0041c1a0`. The earlier attribution of bitwise OR to suboperation 1 was wrong. `docs/vm-ops.md` records the correction.
8. **Instance field initializers that run code** (`LoadingScreen.termSig = new Object()`). `FUN_00407da0`. Static evidence. `docs/vm-ops.md`.
9. **Constructor choice by assignability** (`Thread()` calling `this(this)`, `Thread(Runnable)` calling `this(target, target.toString())`). The port's ranking, not stock's `FUN_00405920`. `docs/vm-ops.md`.
10. **`Object.toString()` (`0047c870`).** The class descriptor part is read. The hex part is the caller's saved ESI in stock, which no script can define, so the port prints the arena position. Row stays `unread`.

## Current frontiers

`Frontend.init` creates and starts `LoadingScreen` and `HotkeyWatcher`. The port's `Thread.init`, `start`, `stop`, `sleep` and `isAlive` handlers use the cooperative scheduler described in `docs/vm-ops.md`. The probe reports each thread and the number of scheduler slices it ran. The thread native rows remain `unread`.

On both data roots `Init.<init>` now passes the zero-resource fallback and stops at unregistered `java.render.Viewport.create(IFFFF)V`. The viewport native needs scene-node ownership and renderer state described in `docs/gfx-input-evidence.md`. The constructor does not complete. After that error the probe runs the queued threads for diagnostics, not as a stock continuation.

The loading thread parks at `Frontend.render.wait()`. The watcher also runs and sleeps. The probe reaches its iteration limit without another error because neither thread has received the signal required to continue. No synthetic notification is sent. The stock 2005 Config constructor also passes `File.exists` before reaching this same viewport frontier.

## Continuation

1. Implement the real viewport creation and scene ownership path from stock evidence, then continue the constructor. Do not replace it with a dummy viewport.
2. Trace the actual notifier of `Frontend.render` and the loading completion path before waking the parked thread. `LoadingScreen.hide` notifies `termSig`; the caller that notifies `Frontend.render` is not established.
3. Convert synchronous native callbacks and class and instance initializer execution to resumable continuations. The current scheduler cannot stop inside those nested runs.
4. Read stock priority weighting and the `FUN_00420ff0` time-slice behavior before replacing the port's equal node budgets. Trace the remaining thread flags and natives.

The VM still stops on static initializer failures where stock logs and continues. It rejects reentrant ancestor initialization. Constructor ranking is the port's, not a verified copy of stock ranking.

## Open from earlier work

- Config and Init construction order in stock is unverified.
- Non-zero `getInfo` answers (global lists at `00636338`) remain unmodelled. The class fallback has bounded host assumptions documented in `docs/game-creation-evidence.md`.

## 2026-10-28: `Init.<init>` reaches `LoadingScreen.show`; fails in tree 10 node 5

The earlier stop at unregistered `Viewport.create` is gone because the viewport
node lifecycle was implemented separately (decision row
2026-10-28T03:36:51Z), not through this unit's changes. This unit moved the
probe past the ambiguous `show` call and into `show` itself.

The ambiguous `show` call no longer stops the constructor: the port now picks
the most specific overload the way stock `FUN_00405920` does
(`tests/vm_eval_name_test.cpp`). `Init.<init>` then executes into
`LoadingScreen.show(Ljava.render.osd.dialog.LoadingDialog;F)V` (tree 10, first
source line 146) and fails at node 5, operation `07` suboperation 21. Stock's
`0x1007` subop switch routes 21 to `FUN_0041bdb0`, integer logical not: it
requires the payload's type char to be `I` and stores `value == 0`. The value
the port holds on the stack there is not an int32, so the port refuses it. The
preceding node 4 is a `1b` field reference of `loadingDialog`
(`Ljava.render.osd.dialog.LoadingDialog;`), so what stock's stack holds at
node 5 is not yet established.

Evidence: `local/census/DecompileAt-00420ff0.log` (dispatcher subop switch),
the `FUN_0041bdb0` decompilation, and `tools/tufa_dump.py` of
`ref/raw/system/Scripts/render/LoadingScreen.class` tree 10.

Next: read stock's `0x101b` field-reference arm end to end to pin down whether
it pushes the field value or a slot object, then re-derive the stack at tree 10
nodes 4 through 6 (`07 21`, then `15` at node 6) before changing any handler.

## 2026-10-28 (later): `Init.<init>` runs Input and Controller construction; stops at `GameRef.queueEvent`

The chain `Init` to `Input.initControllers` to `Controller.<init>` to
`GameType.createNativeInstance` to `ControlSetState` static initializers now
executes. Changes, each read from the stock decompile this session: the
script-resource payload parser follows `FUN_005461a0` (text directives,
root-to-package table, `native <device>` recorded on the node); operation `07`
suboperation 2 is logical AND (`FUN_0041bea0`); suboperation 23 increments
through the value box (`FUN_0041b2d0`); `07` retains the old value while `08`
discards it, as established by the dispatcher at `00420ff0`; a local slot can be dropped while its
box is still on the stack, so the port copies the value out (`0x03`); and a
null call argument falls through to descriptor-driven overload selection.
Evidence and addresses are in `docs/vm-ops.md`,
`docs/game-creation-evidence.md`, and `tests/game_creation_test.cpp`.

The frontier is now the missing native
`java.util.resource.GameRef.queueEvent(Ljava.util.resource.ResourceRef;ILjava.lang.String;)V`,
called from `initControllers` after the controllers are added
(`GameType.queueEvent` with `EVENT_COMMAND` and `setcontrol`). Next: read
stock's `GameRef.queueEvent` (event queue identity, the global list at
`DAT_00636338` semantics) and implement it with a behavioral test, then
continue down `initControllers`.

## 2026-10-28: correction: native player creation is the frontier

The previous probe continued past an incomplete player creation. Stock's
`native player` factory initializes input state, config-dependent fields and
an event mask, and its `getInfo` registers two hooks; the port did none of
these. It now stops at `GameType.createNativeInstance` before creating that
incomplete node. The stock event dispatch and the `setcontrol` script path
are traced in `docs/player-startup-evidence.md`, with saved decompile logs and
the next implementation steps. `queueEvent` is synchronous and must not be
modeled as a general pending queue.

## 2026-10-28: player and cursor setup reach camera creation

The player factory, configuration links, synchronous `setcontrol` dispatch,
actual SDL device inventory, and the bounded native cursor creation path now
run in the startup probe. The next unsupported call is
`java.render.Camera.create` at `004861e0`; its identified-stock decompile is
saved in `local/census/DecompileAt-004861e0.log`. This is a diagnostic
frontier, not a claim that the renderer or gameplay is complete. Mouse and
keyboard readings depend on the actual host devices and SDL focus.

Stock `004861e0` creates a loaded type-12 camera through `00539500`, creates
a type-3 render instance through `00539880`, resolves `bone00`, and attaches
the instance to the viewport's renderer state. `004fd220` finds that state;
`004fcf10` and `004fd150` call `00516760` and `00516900` to create and order
renderer objects. These functions have individually saved decompile logs.
The typed resource tree now permits type 12, but the camera call remains
unsupported until the renderer object and viewport-list effects can also be
represented. Both available data roots reached this same call with SDL input.
The camera resource fields are viewport pixel width divided by height at
`+0x0c`, field of view at `+0x10`, near and far distances multiplied by 10 at
`+0x14` and `+0x18`, LOD bias and amplitude at `+0x34` and `+0x38`, culling
at `+0x30`, and plane test at `+0x2c`. The identified executable's constant at
`005e7334` is `10.0f`. The later renderer-object constructor `0050dbe0`
derives additional projection fields, so these resource fields alone do not
complete camera initialization. `ViewportRenderAttachments` models the
pending-to-active ordering separately, including the `-1` priority sentinel;
it does not create a renderer object or propagate transform updates.
The camera loader at `00547b30` identifies offsets `+0x1c`, `+0x20`,
`+0x24`, and `+0x28` as fog start, end, enable, and color. The type-12
constructor at `00547f60` sets enable to zero, but does not itself initialize
the other three. The base constructor `00544620` does not initialize those
fields either, and `0054f560` calls the malloc wrapper `00559500`, so their
values are unspecified for this native-created resource; `00539500` does not
load a payload to supply them. Fog enable is zero. Preserve that uncertainty
when modeling the fields read by the renderer object.

The frame update at `005100b0` selects the viewport aspect when the resource
aspect is zero and uses two square roots for its far metric, while `0050dbe0`
uses one during construction. It inverts the camera bone matrix through
`0054f3d0`, then requests a perspective matrix from graphics vtable slot `+80`.
The identified backend is `004ba4b0` -> `0058eadb`; the host now calculates
that matrix and a general nonsingular camera inverse. The render instance's
rotation updates and live renderer integration remain unsupported, and host
floating-point operations have not been shown to reproduce x87 rounding exactly.
The host can also transform points, map points inside the view volume to
viewport pixels, and clip triangles against its six homogeneous planes.
The bounded SDL draw path accepts equal-depth triangles; perspective-correct
texture interpolation and full live scene traversal remain unimplemented.
Stock's frame counter at `006200a4` starts at 1 and is process-global. The
host's `GameWorld` instances now share one counter, initialized to the same
value. The frame loop is not wired to advance it yet; the explicit setter is
only a model interface and tests use it to exercise wrap to zero. Nodes leave
their visibility fields unknown when that counter is zero.

## Later diagnostic startup

With `startup_probe --init --renderer`, the host creates an explicitly
diagnostic hidden 640×480 SDL window. Camera.create builds the supported
camera resource, render instance, viewport attachment and frame adapter only
when a real renderer is supplied. The cursor command links its viewport,
camera and target; its initial motion and `mode 0 J` use the decoded cursor
image from the stock executable. This advances both available data roots
through GameLogic's randomize and resource literals to `ResourceRef.cache`.
It does not establish a game loop or a playable result. The clock origin is
the diagnostic process entry; stock initializes its performance timer during
`00551520`, so exact elapsed seeds differ. Unsupported cursor modes, pointer
bone changes, visibility cleanup and several rendering paths still stop.

The diagnostic startup with the real renderer and both supplied data roots now passes `FindFile.first` for `parts\\engines\\*.rpk` and stops at the unregistered `System.openLib`. The search adapter snapshots entries, sorts names using the stock ASCII case-folded comparator, and implements 32 reusable search handles and the stock file/directory filters. Evidence: stock handlers `00487cb0`, `00487da0`, `00487e70`; search and comparator `0054cf50`, `0054d1c0`, `0054d240`, `0054d1a0`, `00554a10`, in the corresponding per-address logs. The adapter searches game assets read-only and routes save searches to the separate data root. It does not claim complete Windows wildcard, case-insensitive directory lookup, filesystem ordering for case-colliding names, or archive-backed search parity. The partially initialized game has not activated a viewport; no frame is presented.

After `System.openLib` and child traversal, the real game now begins constructing the car script named by its resource payload (`cars\\racers\\SuperDuty_data\\scripts\\Hauler_s_VT.class`). `GameRef.create` creates the world node and links a freshly initialized script receiver before invoking its `(I)` constructor, then performs `getInfo` hooks. The identified stock functions `0053a5e0`, `00404e20`, `00408a70`, and `0041fbc0` establish this normal ordering. The host executes this path synchronously and does not model a constructor that yields or is interrupted; it also rolls back the node on an unsupported operation. The real constructor now completes. On the 2005 data root, the current diagnostic completes `Init` and reaches the first camera frame; on the modded raw data root it stops while loading a native car factory for `MW_Mod/Scripts/Jack.cfg`. `--loop` remains gated on completed initialization, so this is not a playable startup.

`GameRef.isScripted` now supports loaded type-1 instances and script-only type-8 resources, including class ancestry checks and the stock recent-use stamp and per-type list (`00486da0`, `00544780`, `005447d0`, `00537d10`). Loading a native car resource is still unsupported: stock `005461a0` calls the car factory `00442500`, whose configuration and initialization effects are not represented. The 2005 data root and the modded raw data root must therefore be reported separately.

Stock `00537240` marks a zero-byte resource loaded with priority 1 and skips its type loader. The common host path now handles this case for type-6 and type-8 resources, including empty script cache requests, and is tested against real pack entries.

For frame maintenance, `ResourceWorld::ageRecentUses` models the `00537ed0` recent-use list pass for supported type-1 instances and type-5, type-6, type-8, and type-14 resources. Entries older than eight frames receive the type-specific zero-visibility update and return to the loaded list. Other node types and unknown visibility states fail explicitly; resource-pressure and unloading decisions in the rest of `00537ed0`, and other work in `00537b40`, remain unmodeled.

The 2005 `Init` constructor completes in the bounded VM, and a diagnostic frame displays the splash art through the active viewport and camera. The continuous loop now advances timers through the supported empty-body player physics path and dispatches animation callbacks. Stock player animation `0047a780` is supported when its overlay and debug links are empty. Cursor animation `00461c40` is supported when its controller link is detached, MouseHelp is disabled, and no software cursor creation is needed; active cursor picking and the other branches stop explicitly. The latest visible-loop run reaches a timer-driven transition through `Camera.destroy` and continues through menu construction and the preliminary active cursor path, then stops rendering at unsupported type-1 dummy node flags. Cursor event delivery is still under development. Camera destruction is deferred until the simulation work queue drains, as in `004864c0`, `00427620`, and `004277f0`. The loop, physics, input, and rendering remain bounded implementations, not a playable game or a stock visual comparison.

Display mode enumeration uses the actual SDL primary-display modes, retaining first-seen distinct width, height and 16- or 32-bit depth. The count, current-mode fallback and space-separated name format follow `004b9ef0`, `004b9ea0`, and `004b9f00`. SDL mode availability and ordering can differ from stock Direct3D capability filtering (`004b86b0`).

PhysicsRef.createBox now creates a zero-mass box and type-2 resource node with tracked geometry/body ownership, half extents and both position buffers. The linked box path follows `004805b0`, `0049ab60`, `0049b300`, `0049b0b0`, `0048a1e0`, and `00539fe0`; `setMatrix` supports zero rotation only (`00480920`). The bounded static-only simulation path supports these boxes only when no unmodeled bodies, descendants, callbacks or work are present. Active cursor picking and event delivery remain necessary for menu interaction.

During the menu transition, `OptionsDialog.show` calls `addCustomGroups` on the actual derived receiver. Stock `004208e0` resolves that override; the VM now does likewise and the actual MainMenuDialog button fields are populated. Logical OR and AND also use stock boolean conversion (`0041bfb0`, `0041bea0`, `00417840`) for floats and object references.

## 2026-10-28: Free Ride click reaches the loading screen, then the native car factory

Run: `local/build-fr/startup_probe --init --renderer --loop --visible --script tools/scripts/free-ride-click.txt --data-root local/stock-data '<2005 install>'`.
After the click the real loading screen is drawn (`.scratch/shots/fr-loading-screen.png`) and `GameLogic.loadDefaults` runs.
Three frontiers were passed on the way, then the car factory stops it.

### Texture counter

`GfxEngine.forceRendering` (`0047c1d0`) runs `00537b40`, which ends in `00537ed0(0)`. For type 7 that function evicts only when the
texture counter `DAT_0065c234` is at or above `mem_texture_max` (`DAT_00618d54`, here 50331648) or its hysteresis flag is set; below
the limit it skips the type. So the host only has to prove the counter is below the limit.

What the counter holds for the six cached pack textures (ids 0x100a8 and 0x10016 at 1024x1024, 0x10026 512x256, 0x10137 256x256,
0x10136 256x128, 0x10028 32x32):

* Stock loader `00506650` creates one device texture through D3DX and adds `004f6230(texture)` to the counter. It writes only slot
  `+0x4c`. The constructors `00506750` and `00507260` copy `+0x50` from the source and then zero it, and no other writer of `+0x50`
  exists in the texture classes (`00505860`-`00508220`), so the old "four slots" allowance had no stock basis. Clone objects
  (`00506750`/`00507260`/`00507b40`, reached through `0053cfb0` from the texture-target list walked by `004fd730`) are separate
  objects with their own slot; no host path creates one, and a future one must register its own allocation.
* `004f6230` sums, over the mip levels of the device texture, `floor(width * height * bytesPerPixel)` taking width and height from
  `GetLevelDesc`. Bytes per pixel come from a table read from the executable: 3.0 for R8G8B8 (0x14), 4.0 for A8R8G8B8 and X8R8G8B8
  and for every format not listed, 2.0 for the 16-bit formats (0x17-0x1a) and 0x3c, 0.5 for DXT1, 1.0 for DXT2-5.
* The size and level count come from `005914a9` (D3DXCheckTextureRequirements) reading three device caps: `TextureCaps`, the
  maximum width and height and the aspect limit. The host controls those. `hostDeviceTextureCaps` presents: mip chains allowed,
  maximum equal to SDL's reported maximum capped at 16384 (the host's own limit), and no power-of-two, square-only or aspect
  restriction. Under those caps a power-of-two source keeps its size and gets a full chain.
* The code is `stockDeviceTextureShape` and `stockTextureSlotUpperBound` (`src/resource_world.cpp`), used by
  `cacheResourceTexture`. Result for the six textures: 5,592,404 + 5,592,404 + 699,052 + 349,524 + 174,764 + 5,460 = 12,413,608 bytes, below 50,331,648.

Limits: the figure uses 4.0 bytes per pixel, so it is exact for 32-bit formats and an upper bound for DXT and 16-bit formats. The
exact format comes from the table at `00618330` and the format fallback in `005911d0`, neither read. Dimensions below 4 and sizes
not power of two are recorded as unknown, which still fails closed. The texture-target list (`004fd730`) is not modelled.

### Frontiers passed in this run

1. `07` suboperation 17, integer modulo (`FUN_0041bbf0`, case 0x11 of `00420ff0`). Both payloads must be integer; the left value becomes
   `left % right`. Division by zero and `INT_MIN % -1` trap in stock's `idiv`, so the port refuses them. Test: `tests/vm_thread_test.cpp`.
   Reached from `java.game.Bot`.
2. `07` suboperation 33 with a `06` descriptor, the primitive cast (case 0x21 of `00420ff0` then `FUN_00417da0`). `(int)` of a float
   truncates (`__ftol`), `(float)` of an int converts, the same type is unchanged; anything else is "cannot cast" in stock and an
   error here. Test: `tests/vm_thread_test.cpp`. Reached from `java.game.VehicleType`.
3. Texture counter, above.

### Current stop

`GameRef.create: unsupported script native device 'car' (script resource 0x00150006, payload "script cars\racers\Baiern_data\scripts\Baiern_CoupeSport.class\r\nnative car cars\racers\Baiern_data\scripts\_main_CoupeSport.cfg\r\n")`,
reported from `java.game.Vehicle` (tree at source line 68) called by `MainMenuDialog.osdCommand`. This is a large subsystem
(the car builder and the physics layer under it). Facts, the directive table and the size estimate are in `docs/car-factory-evidence.md`.
