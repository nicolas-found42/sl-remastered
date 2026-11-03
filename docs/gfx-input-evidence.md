# Graphics and input evidence

This is static evidence for implementing the script-facing graphics and input path. It is not a stock runtime comparison or a claim that any native matches stock. `ref/raw/StreetLegal_Redline.bak.exe` was open in live Ghidra with MD5 `e686099e7233bb7d7c461b1b35c5064c` and SHA-256 `7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`.

The live registration function `00487f20` supplies every class, name, descriptor, and handler address in the tables. The descriptor strings were read from live program memory at `006145a8` through `00615384`, rather than inferred from symbol names. The inventory in `docs/natives.tsv` agrees. Seven handler functions were decompiled and disassembled in live Ghidra. The other 35 are labels without defined live functions. Their bodies were decompiled and disassembled in the separate, read-only `local/ghidra-proj` using `tools/ghidra/DecompileAt.java`. That script checked the same executable MD5. Helper functions cited below were decompiled in the live window. The addresses are stock x86 virtual addresses, not portable handles.

A table entry marked `read` describes the handler and the stated helper operations. `partial` means the handler was read but a called implementation, indirect call, field meaning, exceptional path, or host mapping remains unresolved. Host facilities are implementation needs inferred from the stock operation, not claims that the port already provides them. The handler return convention in several thunks is lost in Ghidra's C types. The description uses the registered descriptor and, where needed, the x86 return registers.

## Available host facilities

`Render2D` in `src/render2d.hpp` uploads RGBA textures and clears, draws axis-aligned rectangles, presents, and queries output size. It does not expose text, scene nodes, viewports, clipping, transforms, or video. `src/ptx.hpp` parses and decodes PTX levels to RGBA, but loading a texture resource, interpreting font data, and connecting it to a render object remain separate work. `Platform::poll` in `src/platform.hpp` returns keyboard scancodes, mouse motion, mouse buttons, resize, and quit events. It does not expose the stock controller enumeration, DirectInput key numbering, axis maps, text input, or hotkey registry. `AudioOutput` accepts queued stereo PCM, and `MusicPlayerControl` controls music sets. Neither interface is a video decoder or a synchronization clock.

## GfxEngine natives

All names in this table have class `java.render.GfxEngine`.

| Native | Descriptor | Handler | Stock behavior and address risk | Host facility | Confidence |
|---|---|---|---|---|---|
| `changeVideoMode` | `(III)V` | `0047c3f0` | Unpacks three integers and passes them to renderer object slot `0x14` at engine `+0x204`. The virtual target and argument meanings were not resolved. | SDL window and display mode control. | Partial. |
| `closeVideo` | `()V` | `0047c380` | Jumps to `004f9320`. When the video subsystem is active, it clears playing state, stops and releases DirectShow objects, and invokes render-target restoration. COM object lifetimes are native pointers. The decompiler's loop in the close helper is suspect and was not independently reconstructed. | Video decoder, presentation, and possibly video audio. | Partial. |
| `currDisplayMode` | `()I` | `0047c450` | Jumps to `004b9ea0`, which searches the display-mode records for the current renderer width, height, and depth. It returns the first matching index, or zero if none matches. The index refers to a native mode array, not a pointer to expose to scripts. | Display mode enumeration and window state. | Read. |
| `displayModeName` | `(I)Ljava.lang.String;` | `0047c460` | Calls `004b9f00` with the index and a 256-byte local buffer, then creates a script string from the result. The helper formats the three mode integers using the stock format string. Invalid indices produce an empty string. | Display mode enumeration and VM string allocation. | Read. |
| `flush` | `()V` | `0047c2d0` | Performs seven passes of `005378d0`, `00537b40`, and `004fca30`, resets `0065aecc` on each pass, and increments frame counter `006200a4`. After the loop it calls `00537ed0(1)` and `004b7b90`. The indirect renderer effects and cleanup semantics are not fully read. | Scene updates, renderer, texture loading, and frame scheduling. | Partial. |
| `forceRendering` | `()V` | `0047c1d0` | Runs the same seven passes and frame-counter updates as `flush`, without its two final calls. One present operation is not evidence of equivalent behavior. | Scene updates, renderer, texture loading, and frame scheduling. | Partial. |

The host now supports bounded rendering through
`forceRenderFrames`: each pass drains supported sound completions, checks the
modeled work and pressure conditions, ages supported resources, presents the
preceding frame, clears an SDL scene and traverses ordered active viewports
using the existing camera adapter, then increments the frame counter. It refuses
unknown resource work, and unbounded pressure. Active video is drawn before
camera traversal by the existing SDL video player; stock `004f9aa0` creates its
video textures directly and does not add them to the type-7 resource counter.
SDL presentation and device
state are approximations of the stock Direct3D backend; stock presents before
beginning its next scene (`004fca30` and `004bb410`). A test drives seven empty
passes, an actual queued frontend sound and an active viewport without cameras;
it also checks a broken camera attachment stops. `native_camera_test` runs
seven passes through a camera created and activated by the registered natives.
The installed 2005 Init has
two active viewports and cameras at entry but currently stops on loaded type-5
mesh pressure before drawing them. Its populated loading screen has not yet
been verified through this adapter.
Force-frame material draws require a bound type-5 mesh and a retained type-7
texture owned by the same renderer; missing resources stop before they can be
decoded or uploaded. This bounds the represented draw path, but does not by
itself account for independent stock device textures or secondary textures used
by the paint queue.
| `isPlayingVideo` | `()I` | `0047c390` | Jumps to `004f9750`, which returns global `0064a394`. `closeVideo` clears that state. | Video playback state. | Read. |
| `numDisplayModes` | `()I` | `0047c440` | Jumps to `004b9ef0`, which returns the count at `006495f4`. | Display mode enumeration. | Read. |
| `openVideo` | `(Ljava.lang.String;II)I` | `0047c330` | Returns `-1` for a null filename. Otherwise it forwards the filename and two integers to `004f8dd0` and returns its result. The helper starts a DirectShow graph, closes an existing video, and can return HRESULT failures. Its complete flags, lifetime, texture presentation, and audio behavior remain unread. | File access, video decoding, renderer, and video audio. | Partial. |
| `printScreen` | `(Ljava.lang.String;)V` | `0047c3c0` | Forwards the string through `004fdf60` to renderer slot `0x58`. Screenshot output is inferred from the name. The virtual target and filename handling were not resolved. | Renderer readback and file output. | Partial. |
| `setGlobalEnvmap` | `(Ljava.util.resource.ResourceRef;)V` | `0047c220` | Reads the resource reference's node pointer at `+0xc` and replaces an engine-held intrusive link at engine offsets `+0x58` to `+0x64`. It detaches the previous node before linking the next. The receiver resource handle is dereferenced without a visible null guard. | Resource world and renderer environment-map state. | Partial. |

## Text natives

All names in this table have class `java.render.Text`. The node and instance offsets below are stock layout evidence, not a proposed 64-bit structure layout.

| Native | Descriptor | Handler | Stock behavior and address risk | Host facility | Confidence |
|---|---|---|---|---|---|
| `changeAlign` | `(I)V` | `004871d0` | Resolves the `renderinst` node and its live instance, then writes the argument at instance `+0xc4`. A missing node or instance is a no-op. | Text scene instance and alignment. | Read. |
| `changeColor` | `(I)V` | `00487150` | Resolves `renderinst` and writes the argument at instance `+0xcc`. A missing node or instance is a no-op. The channel order of the integer was not checked. | Text scene instance and color. | Read. |
| `create` | `(Ljava.util.resource.ResourceRef;Ljava.util.resource.ResourceRef;FF)V` | `00487050` | Resolves `renderinst` and calls `0048c4b0` with both resource arguments, the coordinates, and global default color `00612c64`. The helper creates an `r_text` render node and `bone00`, links the native reference into a node-owned list, and sets text-instance fields. Creation depends on a resource attachment. The node and list contain raw stock pointers. | Resource world, font loading, and text scene rendering. | Partial. |
| `getDefaultColor` | `()I` | `00487040` | Returns global `00612c64`. | Text state. | Read. |
| `getPos` | `()Ljava.lang.Vector3;` | `00487250` | Queries `bone00` through `0048c3d0`. On its reported success it allocates a script `Vector3` and copies the returned coordinates multiplied by `0.1`; otherwise it returns null. `0048c3d0` has paths that report success without populating the output, so missing-instance behavior needs disassembly and a runtime check before it is reproduced. | Text transform and VM object allocation. | Partial. |
| `getWidthPixels` | `(Ljava.lang.String;Ljava.util.resource.ResourceRef;)F` | `00487410` | Returns zero for a null string. Otherwise `004fde50` resolves the font resource and invokes renderer slots `0x20c`, `0x218`, and `0x214`. An unavailable or unsuitable font also takes zero-return paths. The actual glyph width calculation is behind unresolved virtual calls. | Font resource, glyph metrics, and text measurement. | Partial. |
| `setDefaultColor` | `(I)V` | `00487020` | Stores the integer in global `00612c64`, which later text creation reads. | Text state. | Read. |
| `setPos` | `(FF)V` | `004870d0` | Resolves `renderinst`, constructs a position with z zero, resolves `bone00`, and passes the position and a transform to `0048c0f0`. The helper updates the bone transform and marks the render instance dirty. Exact coordinate conversion and absent-bone behavior remain open. | Text transform and scene updates. | Partial. |
| `update` | `()V` | `00487350` | Reads the script `text` field, gets its native string pointer, and stores it at the live render instance `+0xc0`. A null string becomes the shared empty string at `00640938`. Stock retains a raw pointer to string storage, so a native port must preserve the string lifetime without truncating a host pointer into a script word. | VM string lifetime and text scene instance. | Read. |

## Input natives

All names in this table have class `java.io.Input`. Stock hotkeys occupy 512 records of 32 bytes at `0063c890` through `0064088f`. A record stores script object references and native state. The port needs stable typed references and explicit lifetime management rather than copying those 32-bit addresses.

| Native | Descriptor | Handler | Stock behavior and address risk | Host facility | Confidence |
|---|---|---|---|---|---|
| `activeAxis` | `(I)I` | `0047cae0` | Passes the device index to `00557aa0`, which scans device axes and current or prior values. Exact thresholds, tie behavior, and special input types need the rest of that helper. | Device enumeration, axis state, and input timing. | Partial. |
| `axisName` | `(II)Ljava.lang.String;` | `0047cb00` | Calls `005579f0` with device and axis indices and a 256-byte local buffer, then makes a script string. The helper supplies a keyboard table entry or a device-specific axis name, and leaves an empty buffer for invalid indices. | Device names and VM string allocation. | Read. |
| `checkHotkeys` | `(Ljava.util.resource.GameRef;Ljava.render.Osd;)V` | `0047cd40` | Resolves the GameRef's input instance and scans the 512 records whose OSD is null or equals the passed OSD. Type bit 1 reads a mapped axis; bit 2 reads a key. A value greater than `0.2` is active. The first sample arms the record without firing. Later transitions test event-mask bit 1 for press or bit 2 for release, update the script Hotkey `state`, and queue only the first eligible event through `00426800` with event type `0x100000`. The records retain object references and an event target. | Input state, mapped axes, VM object fields, event queue, and clock for smoothing. | Partial. |
| `createHotkey` | `(IILjava.io.Hotkey;Ljava.util.resource.GameRef;Ljava.render.Osd;I)V` | `0047cc50` | Finds the first record whose type word is zero and stores the supplied type, input index, Hotkey, owner, OSD, event mask, and zeroed prior and armed state. If all 512 records are occupied it returns without inserting. Script objects and owner are kept as raw 32-bit pointers in stock. | Hotkey registry, input state, and VM object lifetime. | Read. |
| `deleteHotkey` | `(Ljava.io.Hotkey;)V` | `0047cd00` | Finds the first record whose Hotkey pointer equals the argument and clears its type word. It does not clear the other words in that record. | Hotkey registry and VM object identity. | Read. |
| `flushHotkeys` | `()V` | `0047ce80` | Clears the armed word at record `+0x1c` for each active record. It does not delete records or clear their previous state. The next `checkHotkeys` sample arms them again without an event. | Hotkey registry. | Read. |
| `getAxis` | `(II)F` | `0047c990` | Returns the value computed by `00557430` for the requested device and axis. The helper handles keyboard, mouse, and controller-specific axis types, including clamping and special negative axis inputs. The device index is used to index device state before a visible range check. | Keyboard, mouse, controller axes, and timing. | Partial. |
| `getDeviceName` | `(I)Ljava.lang.String;` | `0047c9c0` | Gets the name pointer from `0054de20` and `00558100` and passes it to the VM string helper. The device helper returns null for an invalid index. The VM's null-string conversion is not read here. | Device enumeration and VM string allocation. | Partial. |
| `lastKey` | `()I` | `0047cc10` | Calls `00556e00`, which reads a DirectInput keyboard event and uses the Windows keyboard layout to obtain a character. The helper returns the scan code combined with the character in bits 16 and above, or zero. If bits 16 to 23 are nonzero, the handler writes that byte into a 16-byte circular character buffer. Disassembly shows EAX remains the helper result on return despite the decompiler's void signature. | Keyboard events, stock scan-code mapping, text input, and circular buffer. | Partial. |
| `mapAxis` | `(Ljava.util.resource.GameRef;IIIFFFF)V` | `0047c9f0` | Resolves the GameRef input instance using kind `0x1b`, then forwards the remaining arguments to `0054d650`. That helper inserts into the first free one of 152 mapping records, samples the initial axis, and stores four float parameters. Invalid negative device or axis indices return `-1` in the helper, but the script descriptor discards it. | Controller mappings, axis state, and VM-owned object lifetime. | Partial. |
| `setAxisSmooth` | `(Ljava.util.resource.GameRef;IFFFF)V` | `0047ca70` | Resolves the GameRef input instance and calls `0054d7b0`, which writes the four supplied values and a `1.0` constant into the selected axis record. `0054d830` subsequently uses that record with elapsed-time globals to smooth mapped values. The parameters' exact semantics and index bounds remain open. | Mapped axes and clock. | Partial. |

## Viewport natives

All names in this table have class `java.render.Viewport`.

| Native | Descriptor | Handler | Stock behavior and address risk | Host facility | Confidence |
|---|---|---|---|---|---|
| `activate` | `(I)V` | `00481680` | Unpacks but does not use the integer. For a non-null native handle it calls `004fd020` with two `-1` values. That helper only forwards type `0x12` nodes to `004fd280`, which moves an existing viewport between engine lists. | Viewport registry and renderer. | Partial. |
| `create` | `(IFFFF)V` | `004814e0` | Resolves the receiver's native handle and calls `00538d60` with the integer and rectangle floats. It detaches a previous node and links the new type `0x12` node into the receiver's intrusive links. A missing handle reports a stock error. These links and node pointers are 32-bit stock addresses. | Scene node ownership, viewport state, and renderer. | Partial. |
| `deactivate` | `()V` | `004816c0` | For a non-null handle, calls `004fd050`. For a type `0x12` viewport, the helper searches the active list and unlinks the matching record. Other node types have distinct cases. | Viewport registry and renderer. | Partial. |
| `destroy` | `()V` | `00481600` | Calls `004fd050` and then `0048a8d0` to unbind the receiver. A missing native handle reports a stock error. Node and list lifetimes must outlive the stock-style links. | Viewport registry, scene ownership, and VM error reporting. | Partial. |
| `getAspect` | `()F` | `004817b0` | Calls `0048cda0` when the native handle exists, otherwise returns `1.0`. The helper multiplies both screen width and screen height by the viewport instance's `+0x18` value and divides the products. For nonzero finite width the viewport factor cancels. Zero and floating-point edge cases do not simplify safely. | Window output size and viewport state. | Read. |
| `getHeight` | `()F` | `00481870` | Returns live instance float `+0x1c`. If resolution fails, the handler returns the unpacked receiver slot interpreted as a float. That fallback is inferred from the unpacker's destination and the x86 return path, not observed in a stock run. | Viewport state. | Partial. |
| `getLeft` | `()F` | `00481970` | Returns live instance float `+0x10`, with the same inferred receiver-bits fallback as `getHeight`. | Viewport state. | Partial. |
| `getTop` | `()F` | `004818f0` | Returns live instance float `+0x14`, with the same inferred receiver-bits fallback as `getHeight`. | Viewport state. | Partial. |
| `getWidth` | `()F` | `004817f0` | Returns live instance float `+0x18`, with the same inferred receiver-bits fallback as `getHeight`. | Viewport state. | Partial. |
| `resize` | `(FFFF)V` | `00481700` | Writes the four floats to the live instance at `+0x10`, `+0x14`, `+0x18`, and `+0x1c`, in left, top, width, and height order. Missing handle, node, or instance is a no-op. | Viewport state and renderer. | Read. |
| `unproject` | `(Ljava.lang.Vector3;I)Ljava.lang.Vector3;` | `00487a60` | Reads the input vector, finds a viewport record with `004fd220`, then walks its camera records for a matching integer at camera `+0x630`. `004fdb70` and `00513840` apply camera matrices. The result is a new Vector3 with x and z multiplied by `0.1` and y by `-0.1`, or null if no matching camera exists. Null-vector and matrix edge cases remain unread. | Viewport and camera state, matrix math, and VM object allocation. | Partial. |

## Frontend initialization

The installed `ref/raw/system/Scripts/render/Frontend.class` contains the main Frontend and an embedded `java.render.HotkeyWatcher` class. `Frontend.init`, class 0 tree 9, constructs `GfxEngine`, calls `setFonts`, constructs `LoadingScreen`, constructs a Thread with a HotkeyWatcher target, and calls `Thread.start`. `Frontend.setFonts`, tree 12, creates font ResourceRefs based on configured video dimensions. The startup probe in `docs/startup-frontier.md` reached `Thread.init` inside the LoadingScreen constructor; it did not execute the watcher or prove the subsequent sequence in stock.

After `Frontend.init`, the installed `Init` constructor contains calls to `LoadingScreen.show`, `Input.initControllers`, and `Input.enablePointingDevice`. These last two are script methods, not registered Input natives. Input tree 168 loops over `getDeviceName`, creates controller objects, and queues setup events. Tree 175 constructs `MouseCursor`. The exact controller constructor and cursor setup chains remain unread.

## LoadingScreen

`LoadingScreen.class` class 0 tree 1 calls `Thread.start` in its constructor. Its `termSig` field initializer is class 0 tree 0. Thread initialization and start use the scheduler behavior recorded in `docs/vm-ops.md`; starting does not prove that the new thread runs before its caller continues.

The run tree is class 0 tree 2. It first calls inherited `wait` on `Frontend.render`, then checks `init` and `System.isLoading`. Its branches can handle `waitForUser`, construct a `FlashText` thread when a loading dialog has text, manage the pointer state, change loading priority, create a `SoftTimer`, wait on the dialog, stop the flasher, hide the dialog, wait again, or call `sleep` with literal `300`. These are serialized references and branch structure, not a recorded first-frame execution. The first call is the inherited `Object.wait` path, followed by the conditional native `System.isLoading`. The tree also references `Thread.sleep`, `Thread.start`, and `Thread.stop`. A reached pointer-restoration branch calls `MouseCursor.enable`. Scheduler and object state determine which later calls execute.

`show()` is tree 12. It passes `Frontend.defLoadingPic` to the resource overload, tree 11. That overload constructs `SimpleLoadingDialog` with modal, fullscreen, freeze, and leave-pointer flags and calls the dialog overload, tree 10. Tree 10 sets `init`, saves the dialog, calls its `show`, and changes loading priority. `SimpleLoadingDialog.show`, tree 1, caches the picture, calls `Osd.createBG`, and invokes its superclass show. `LoadingDialog.show`, tree 2, constructs a style, creates a button and cancel hotkey, caches `Frontend.largeFont`, conditionally creates loading text, invokes its superclass, and calls `GfxEngine.forceRendering`. The `Text` constructor tree 15 calls native `Text.create` and then `changeText`, whose tree 25 calls native `Text.update`. The exact `Osd` and superclass paths to those methods are not fully traced. A missing `loadingString` skips the text creation branch.

The helper classes in `LoadingScreen.class` are class 1 `SoftTimer` and class 2 `FlashText`. The timer's run tree 1 computes a sleep argument using `waitTime` and literal `1000`, then calls `Object.notify` on its listener. The flasher's run tree 1 repeatedly calls `Text.changeText` with `PRESS ENTER TO CONTINUE...`, sleeps with literal `600`, calls `changeText` with null, and sleeps with literal `600` again. `changeText` reaches native `Text.update`. The main run tree starts these helpers only on the corresponding dialog branches. `hide`, tree 13, changes loading priority, calls the dialog's `hide`, clears the dialog field, and notifies `termSig` when a dialog exists.

## HotkeyWatcher

`Frontend.class` class 1 tree 0 is `HotkeyWatcher.run`. Its loop calls `Thread.sleep(F)` with integer literal `50`, obtains the last element of `Frontend.inputQueue` as an `Osd`, and only when that object, `Input.cursor`, and `cursor.controller` are non-null calls `Input.checkHotkeys` with the controller and OSD. There is no direct text or rendering call in this tree. The first explicit native is `Thread.sleep`, followed by `Input.checkHotkeys` only when the guards pass. The literal's conversion and sleep units should be checked against the scheduler and the installed class rather than inferred from the number alone.

## Menu entry and text path

`ref/raw/sl/Scripts/game/MainMenu.class` class 0 tree 1 constructs `MainMenuDialog` and calls its `show`. The executable's route to `MainMenu.enter` is not established. `MainMenuDialog` is embedded class 1 in the same file. Its show tree 57 starts with three random and check branches using `Math.random`, the `TrackSelect`, `Garage`, and `Track` checks, and `Math.sqrt`. It then references `Sound.changeMusicSet` by name, creates text through OSD, calls `System.buildNumber`, and calls `GfxEngine.openVideo` at node 194 with `data\\fmv\\prime.avi` and two literal ones. Its result participates in the `bgVideoActive` assignment and a branch. The alternate section creates a scene, camera, render objects, vehicle, and camera path. The exact order of executed calls depends on branch results and dynamic dispatch.

The installed menu tree contains the strings `Miran Wichur Mod BETA` and `Mirans_Mod_protection_error______Please_Resintal_Mirans_Mod`. It is evidence for the installed script files, not proof that those scripts are unmodified stock scripts. `MainMenuDialog.addCustomGroups`, tree 59, references `Osd.createBG`, `Osd.createHeader`, `createMenu`, `Menu.addItem`, and cancel hotkeys. It supplies a concrete menu construction target, but this reading does not establish the path that calls it.

There is no registered `java.render.Font` native in `docs/natives.tsv`. Scripts pass font `ResourceRef` objects to `Text` and OSD methods. `Text.getWidth` tree 27 calls native `getWidthPixels` and `Viewport.getWidth`; `Text.getHeight` tree 26 calls `Viewport.getHeight`. The seven-argument text constructor tree 16 also uses `getFontSize`, configured video height, and viewport height. The font nodes selected by the startup probe are type 14, while the default loading picture is type 7. `docs/resource-world-evidence.md` records their resource IDs and states that font and glyph payloads are deferred. A texture decoder alone does not establish glyph layout, advances, alignment, or stock text pixels.

## Remaining unread or unverified

- The indirect renderer targets for display changes, screenshots, text measurement, glyph drawing, scene submission, and the seven render passes. Font payload format, glyph metrics, atlas sampling, blend and color conventions, texture lifetime, and pixel parity are not established.
- Full DirectShow argument meanings, video completion and looping, file errors, rendering and audio synchronization. `Render2D` and `AudioOutput` do not implement this pipeline.
- Complete controller enumeration, axis-name translation, all `activeAxis` and `getAxis` cases, DirectInput-to-SDL scancode and keyboard-layout mapping, smoothing parameters, and actual input event ordering.
- `0048a8d0`, viewport allocation and deletion edge cases, camera-list ordering, and `unproject` matrix conventions beyond `00513840`. `Text.getPos` output initialization and all null or missing object cases need direct disassembly and stock behavior comparison.
- `Osd` construction and teardown, inherited `Dialog.show`, the cursor constructor and controller setup, and the path from startup to `MainMenu.enter`. Serialized tree references do not prove these branches ran or identify a first rendered frame.
- The installed menu script's provenance and its differences from any unmodified stock scripts. The stock executable has not been run to the menu because of the disc problem. No handler here has a native implementation, stock behavioral test, and `tools/native_audit.py` join established by this document.

The embedded trees were first printed with an in-memory correction to the `tools/tufa_dump.py` class-and-tree selector. The shared script was subsequently fixed and its documented `class:tree` arguments now work. For example, `python3 -B -I tools/tufa_dump.py ref/raw/system/Scripts/render/LoadingScreen.class 1:1 2:1` prints the helper run trees.

The identified stock main loop `00428960` calls `00408ce0` (notify all)
on the `render` static object of `java.render.Frontend` after its render and
frame-counter step when the VM and that object exist. Its saved per-address
log is `local/census/DecompileAt-00428960.log`. The host's bounded frame
adapter now exposes that notification, and the probe calls it only after a
supported frame is presented and the Init constructor completes. It does not
wake this thread after a failed partial startup. This is not yet a continuous
stock frame loop.

The startup probe also offers `--renderer --visible --loop`, which enters a
bounded host frame loop only if the real Init constructor completes. It polls
SDL events, draws active viewport cameras in their modeled order, presents,
advances the frame counter, sends the stock-derived Frontend notification,
and runs a scheduler slice. Missing camera, unsupported traversal, or a VM
error stops the loop. Stock `00418d10` invokes the script scheduler `00416940` before rendering
with a 10 ms budget. The host uses the same time budget with bounded VM node
slices, but does not reproduce stock priority weighting; its 1 ms pacing is
also an approximation, and this path is not a stock-timing comparison.
The visible menu now starts, but Free Ride still stops at mounted type-1
traversal; the car is not yet visibly driveable. The SDL input adapter buffers
short button and key transitions and retains button coordinates across a
suspended animation pass. Live physical-input behavior requires rechecking.
