# Stock input consumption for boot → Main Menu → New Career → Cancel

Research date: 2026-11-01. This is a source-backed boundary inventory and proposed owned-helper experiment, not an input replay implementation or a Parity Proof. No game, container, visible app, audio device, or new Wine prefix was started for this research. The Stock Executable and Reference Folder were read only.

The missing input boundary has **three independent consumers**: immediate DirectInput state, buffered keyboard input, and Win32 window-message cursor coordinates. Capturing DirectInput and `GetCursorPos` alone misses the Main Menu cursor. The existing early clock interval remains useful but does not observe these boundaries.

## Evidence and scope

Authority is `ref/raw/StreetLegal_Redline.bak.exe`, SHA-256 `7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde` (recomputed in this investigation), and its Stock Decompile, [all.c](../../local/census/all.c). Static `i686-w64-mingw32-objdump -d --start-address=... --stop-address=...` reads resolved ambiguous decompile stack variables at `00556e00`, property construction at `0055622d`, and cursor extraction at `004b8210`. The existing [DecompileAt-00556e00.log](../../local/census/DecompileAt-00556e00.log) also includes the relevant assembly.

The retained [early-record-second result](../../local/verify/tracked-stock-early-record-second/result.json), [DirectInput log](../../local/verify/tracked-stock-early-record-second/game/dumps/dinput8proxy.log), and [flow controller](../../tools/parity/stock/state_flow.py) cover the requested normal boot and two mouse clicks. They show a functional flow and 105,927 clock/async-key calls, with 211,854 shared phases and no reported clock gaps; their declared omissions include DirectInput consumption. They do **not** contain a complete per-call DirectInput trace, all HRESULTs, buffered counts, input output bytes, window-message delivery, or keyboard translation calls. Therefore an exact dynamic input transcript cannot be reconstructed from these artifacts.

Discovery followed the required `jg`-first route: `jg "How are keyboard and mouse input consumed during boot and the menu dialog flow, including cursor coordinates and key translation?" src --max-requests 4 --max-output-bytes 22000 --concurrency 4` ended with `End context.` but reported discovery incomplete, `request-limit`, and zero relevant files. A file-root attempt was refused, and a negated-exclusion attempt was rejected by the CLI. Focused exact searches and direct reads supplied the evidence below; absence from Jevgrep is not treated as evidence of absence.

## Actual Stock calls and branch rules

DirectInput vtable offsets below are PE32 byte offsets, with `this` included in the ABI call. Stock device records start at `0076f9b0`, stride `0x7a8`; type at `+0x30` is keyboard 1, mouse 2, joystick 3, and state buffer starts at `+0x644`. Sources: [initialization](../../local/census/all.c#L258025), [sampling](../../local/census/all.c#L258810), [teardown](../../local/census/all.c#L258752).

| Boundary | Exact relevant request and use |
|---|---|
| `DirectInput8Create` | `FUN_00556150`, version `0x800`; retain full requested IID and outer pointer shape. The original proxy currently reports only `IID.Data1`. |
| `CreateDevice`, `+0x0c` | Keyboard GUID `6f1d2b61-d5a0-11cf-bfc7-444553540000`, then mouse GUID `6f1d2b60-d5a0-11cf-bfc7-444553540000`; output owner/device identity must be observed. Setup branches require HRESULT **equal to zero**, not merely nonnegative. |
| `SetDataFormat`, `+0x2c` | Keyboard descriptor at `0060310c`: `(dwSize=24,dwObjSize=16,dwFlags=2,dwDataSize=256,dwNumObjs=256)`. Mouse descriptor at `00602f04`: `(24,16,2,16,7)`. Descriptor and referenced object arrays are input, not just size diagnostics. |
| `SetCooperativeLevel`, `+0x34` | Both use Stock HWND `DAT_006495a0`, flags `6`. This is foreground/nonexclusive in the Microsoft contract; losing foreground can automatically unacquire. Preserve the virtual guest window identity rather than changing it to the user's active window. |
| `GetCapabilities`, `+0x0c` on device | A 44-byte structure with `dwSize=0x2c`; its result gates successful registration. It is a separate interface from owner `CreateDevice` despite the same byte offset. |
| Keyboard `SetProperty`, `+0x18` | Property pseudo-GUID value `1`; 20-byte `DIPROPDWORD` words are `{20,16,0,0,16}`: buffer size 16, header size 16, device-wide object/how zero. This is established by pushes/stores `0055622d..00556259`; retain the pseudo-GUID discriminator rather than dereferencing address 1. Stock ignores this call's HRESULT. |
| Initial `Acquire`, `+0x1c` | Keyboard is acquired after the property call; its result is ignored. No corresponding mouse acquire occurs in the successful initialization branch. |
| Joystick enumeration | Owner `EnumDevices(4,callback,0,1)` follows keyboard/mouse setup. `FUN_005565a0` can create further devices with different format/property/poll behavior. The retained log registers only keyboard/mouse; a replay profile must explicitly validate the enumeration result rather than assume all machines have no joysticks. |
| `GetKeyboardLayout(0)` | Executed at the end of successful input initialization and cached in `DAT_00777430`, even when the scenario types nothing. |
| Shutdown | `FUN_00556b10` and its thunk release force-feedback effects, then `Unacquire` (`+0x20`) and `Release` (`+8`) each device, clear device count, and release the DirectInput owner. This does not imply teardown happened within the retained interval: its stop precedes forced process shutdown. |

Static descriptor details, read directly from the hashed PE:

* Keyboard object `i=0..255`: GUID at `006033ac` (`55728220-d33c-11cf-bfc7-444553540000`), byte offset `i`, type `0x8000000c | (i<<8)`, flags 0. Every one of the 256 descriptors was checked against that formula.
* Mouse offsets/types are `(0,0x00ffff03)`, `(4,0x00ffff03)`, `(8,0x80ffff03)`, `(12,0x00ffff0c)`, `(13,0x00ffff0c)`, `(14,0x80ffff0c)`, `(15,0x80ffff0c)`, all object flags zero. The first three GUIDs are X/Y/Z axis (`a36d02e0/e1/e2-c9f3-11cf-bfc7-444553540000`); button GUID pointers are null. This is the 16-byte/four-button format, not the 20-byte/eight-button mouse-state format. The other **20** below is a buffered object size, not a mouse-state size.

### Immediate state: `FUN_00556bc0`

Each invocation first obtains input QPF/QPC and stores its elapsed time. It then samples all registered devices in record order. Sources: [all.c:258825](../../local/census/all.c#L258825), static assembly `00556bc0..00556dfb`.

| Kind | Exact sequence and failure behavior |
|---|---|
| Keyboard | `GetDeviceState(256,state)`. If HRESULT is **not zero**, call `Acquire`. If acquire returns **zero**, retry `GetDeviceState(256,state)` and ignore the retry HRESULT. Otherwise clear all 256 state bytes. Consequently a retry that fails and leaves state untouched preserves stale bytes; a retry that writes some bytes on failure leaves those writes. Replacing every failure with zero is incorrect. |
| Mouse | `GetDeviceState(16,state)`. Any nonzero HRESULT causes `Acquire`; retry only if acquire returns **zero**. Initial or retry state result must equal zero to accumulate relative axes. Otherwise clear all 16 bytes. State offsets 0/4/8 are signed LONG deltas; 12..15 are four button bytes. Axis accumulation uses the saved input sensitivity and Stock float grouping/clamps. |
| Joystick, if present | `Poll` (`+0x64`); on negative result, acquire with a bounded retry for `0x8007001e` (11 attempts), possibly restart effects, then `GetDeviceState(0x110,state)`. Some negative acquire exits preserve state; exhausted input-lost retries clear `0x110` bytes; the state HRESULT is ignored. Do not silently admit this kind under a keyboard/mouse-only helper qualification. |

Exact return-address allowlist leads for the first helper/observer:

| Consumer | Post-call VA (RVA = VA − `00400000`) |
|---|---|
| Keyboard initial state / acquire / retry state | `00556da6` / `00556db2` / `00556dc4` |
| Mouse initial state / acquire / retry state | `00556ce0` / `00556cec` / `00556cfb` |
| Buffered keyboard acquire / data | `00556e13` / `00556e3d` |
| Key map / keyboard-state / ASCII translation | `00556e72` / `00556e7f` / `00556e9e` |

The retained DirectInput log reports keyboard `Acquire` returning `0x00000001` on repeated polls. Microsoft documents this as `S_FALSE` when already acquired. Preserve its raw u32 value: immediate reacquire checks `==0`, whereas the buffered path below accepts `>=0`. This is a real branch distinction, not interchangeable success booleans. [Acquire contract](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417818(v=vs.85)).

### Buffered keyboard: `FUN_00556e00`

The assembly resolves misleading decompile stack aliases into this sequence:

1. Acquire the first registered device (`DAT_0076f9b0`). A negative HRESULT returns zero immediately.
2. Call `GetDeviceData(cbObjectData=20, one stack object, &count, flags=0)` with `count` initialized to **1**. This is one destructive dequeue request per call, not a peek or full queue drain.
3. Return zero on negative HRESULT, returned count other than 1, unsigned `dwOfs >=256`, or `(low_byte(dwData)&0x80)==0`.
4. On accepted key-down, initialize a local output DWORD to zero; call `MapVirtualKeyExA(dwOfs,1,cachedHKL)`, then `GetKeyboardState(256-byte stack array)`. If the latter succeeds, call `ToAsciiEx(mappedVK,dwOfs,keyState,&localWORD,0,cachedHKL)`.
5. Ignore `ToAsciiEx`'s integer return value. Return `((localDWORD & 0xffff)<<16) | dwOfs`; if keyboard-state retrieval fails, the high word stays zero. The caller `FUN_0047cc10` appends only byte `(result>>16)&0xff` when that byte is nonzero to its 16-byte character ring.

Sources: [Stock decompile](../../local/census/all.c#L258945), [disassembly](../../local/census/DecompileAt-00556e00.log), [character-ring consumer](../../local/census/all.c#L92287). `dwTimeStamp`, `dwSequence`, and `uAppData` of the 20-byte event are not read by this handler; preserve them as raw diagnostics without declaring them semantic input for the Port.

Microsoft confirms that `GetDeviceData` returns input/output counts, consumes records unless `DIGDD_PEEK` is specified, and can succeed with `DI_BUFFEROVERFLOW`; thus zero events, overflow, key-up, and error are distinct records. Its documentation also cautions that a debug implementation may place random bytes in an unacquired device's buffer, so failure does not justify inventing an all-zero output contract. [GetDeviceData](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417894(v=vs.85)), [object fields](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416611(v=vs.85)).

For the mouse-only flow, `GetKeyboardLayout(0)` is definitely consumed at initialization. The other three Win32 functions are conditional on an accepted buffered key-down; absence of intentional typing is insufficient evidence that none executed. A complete future trace must record their actual call counts, including zero, and account for any leftover/physical key events. `GetKeyboardState` is tied to the calling thread's keyboard-message removals, not equivalent to an async key bitmap. `ToAsciiEx` may depend on an earlier dead key; recording only HKL and the current state does not reconstruct that hidden translation state. Recording the actual response avoids that inference. [Layout](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeyboardlayout), [mapping](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-mapvirtualkeyexa), [state](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getkeyboardstate), [translation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-toasciiex).

## Cursor positions come from a second input channel

`FUN_004b8000` is the registered window procedure. Messages `0x200..0x202` and `0x204..0x205` update the absolute position from the **client** coordinates packed in `lParam`. In the exact assembly (`004b8210..004b826c`), Stock masks/shifts into unsigned 16-bit x/y, converts them to x87 values, divides by the stored width/height, doubles by `fadd st,st`, subtracts 1, and stores float32 bits to `0064959c`/`00649598`. The button/flags global preserves its previous high word then ORs the full `wParam`; do not reduce the recorded request to a left-button boolean. `WM_MOUSEMOVE` specifies signed coordinates in the API, but Stock's unsigned extraction is the authority here. [WndProc](../../local/census/all.c#L133312), [official message contract](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mousemove).

The denominator comes from `GetClientRect` on windowed show/move/size/activation/system-command paths, or configured dimensions in the fullscreen path. `WM_ACTIVATEAPP` also writes `DAT_00617724` from `wParam`. `FUN_00460140` copies the normalized cursor globals while the system cursor is enabled, detects movement, and clamps to [-1,1]; its other branch integrates mapped relative axes. [Geometry updates](../../local/census/all.c#L133328), [display setup](../../local/census/all.c#L134552), [cursor animation](../../local/census/all.c#L71609).

`GetCursorPos` has one direct Stock Decompile call, `FUN_004b7c50`: its screen POINT is copied into a zero-area rectangle and passed to `ClipCursor`. Its BOOL return is unchecked. This is not the Main Menu coordinate sampling loop. No `ScreenToClient`/`ClientToScreen` call appears in this Stock Decompile. The existing d3d9 proxy's `where` command separately obtains screen/window/client geometry and `ClientToScreen` for the external XTEST controller; those diagnostic calls are not Stock consumers. [Stock clip operation](../../local/census/all.c#L132993), [proxy geometry diagnostics](../../tools/wine/capture/d3d9_proxy.c#L237), [GetCursorPos contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcursorpos).

The main pump `FUN_005522a0` repeatedly calls `PeekMessageA(MSG*,0,0,0,PM_REMOVE)`, checks `WM_QUIT`, translates and dispatches. The intro video loop has another pump interleaved with `GetAsyncKeyState(VK_ESCAPE)` and DirectShow completion polling. Therefore the boot interval needs message delivery/geometry/focus observations in the same order as DirectInput and clocks, even before any menu clicks. A WndProc observer sees actual callbacks, including callbacks not reached through this particular pump; recording only `DispatchMessageA` is not a complete callback inventory. [Main pump](../../local/census/all.c#L253652), [intro loop](../../local/census/all.c#L263615).

## Existing proxy and Port boundaries

The current [dinput8 proxy](../../tools/wine/capture/dinput8_proxy.c) is functional input tooling, not a transparent input recorder. It registers only 16 raw pointers, does not track owner lifetimes, QueryInterface aliases, Release or Unacquire, logs only the first three acquire results and transition-level mouse button bits, and does not retain complete state/data request-response records. Its data callback calls the real handler first, then may append synthetic events with a fresh clock timestamp; state callbacks OR injected bits into successful results. It performs logging/file I/O and a critical section inside callbacks. Reuse this code as a source of method signatures and saved-handler mechanics, not as already-qualified replay evidence.

The [Port's PlatformInput](../../src/platform.cpp) watches SDL relative movement, button and key events; initialization samples initial mouse/key state. `beginFrame` drains relative deltas and advances one effective button transition and one effective transition per key; mouse buttons can be held across a suspended animation pass. `nextKey` removes one buffered SDL key event, returns zero for key-up/empty, and constructs the scan/character result using a local ASCII/modifier rule. Those are separate immediate and buffered lifecycles, not one generic event queue. [Sampling](../../src/platform.cpp#L145), [key queue](../../src/platform.cpp#L460).

The actual Port host also supplies absolute cursor input through `startup_probe.cpp`: current SDL mouse state, motion/button events, and frame-button positions call `updateCursorPosition`; its renderer maps window coordinates into content coordinates, then normalizes. The host's animation/scheduler ordering makes a frame-number-only projection insufficient. [Cursor mapping](../../tools/startup_probe.cpp#L1365), [host loop](../../tools/startup_probe.cpp#L2008), [render transform](../../src/render2d.cpp#L214). A replay adapter must replace these concrete platform-boundary inputs together at a declared consumption step; continuing to pump physical SDL input would mix two sources.

## Proposed tagged request/response tape

This is a **new proposed schema**, not something the current format-4 parser accepts. Keep native response bytes and semantic projection separate. Reuse the current recorder's shared entry/return phase semantics, guarded caller allowlist, bounded storage, stop/drain protocol and provenance checks, extending the schema explicitly rather than appending unrecognized keys.

Every event has `version`, discriminant `tag`, unique `ticket`, `entry_phase`, `return_phase`, `lane_role`, `lane_incarnation`, `lane_call`, module build hash + caller RVA, logical owner/device/window IDs where applicable, and an optional `diagnostic` object holding native thread/pointer identities. `native_thread`, COM pointer addresses and HWND values are not portable matching keys. Record HRESULT/BOOL/integer returns as raw u32 bits plus diagnostic signed decoding, never replace them with a success flag. Retain pre/post LastError and x87 CW/MXCSR as diagnostic preservation evidence.

| `tag` | Request payload | Response payload / interpretation |
|---|---|---|
| `di.create_owner` | instance shape, version, full IID, aggregation shape | HRESULT, output-pointer before/after shape, new owner identity on usable output |
| `di.create_device` | owner ID, complete device GUID, aggregation shape, output shape | HRESULT; device ID/kind/incarnation; pointer only diagnostic |
| `di.lifecycle` | method enum `QueryInterface/AddRef/Release/Acquire/Unacquire/Poll`, owner/device ID, IID when needed | Exact return; alias relation and retirement only when observed; acquisition transition observations |
| `di.format` | bounded descriptor words and per-object `{guid-or-null,offset,type,flags}`; pointer-free digest | HRESULT, format revision only after accepted configuration |
| `di.cooperative` | device ID, logical window ID, flags | HRESULT; configuration revision |
| `di.property` | pseudo-GUID value or full GUID, exact bounded header/payload bytes | HRESULT; property revision; unused return remains raw |
| `di.capabilities` / `di.enumeration` | method, size/class/flags, callback association | HRESULT, bounded outputs, ordered callbacks and their returns; absent joysticks must be evidenced |
| `di.state` | device ID + format/config revision, `cbData`, guarded output-buffer role | HRESULT, bounded pre/post snapshots (256 keyboard /16 mouse for this profile), byte-difference mask, response definedness; decoded semantic axes/button bits separately |
| `di.data` | device ID + format revision, `cbObjectData=20`, requested count1, flags0, buffer/count-pointer roles | HRESULT, count before/after, object pre/post20 bytes with validity/definedness, changed-byte mask; semantic projection uses count, offset and low-byte bit7 only after Stock's branch conditions |
| `win.layout` | thread selector0 / lane identity | HKL raw bits, logical layout ID; cached generation |
| `win.map_key` | code, map type1, layout ID | mapped virtual-key u32 |
| `win.key_state` | guarded 256-byte output role | BOOL, pre/post bytes, byte differences and definedness |
| `win.to_ascii` | VK, scan, flags0, layout ID, exact input state256 | integer return, pre/post output WORD (zero-initialized in this caller); do not derive WORD from return count |
| `win.cursor_pos` | guarded POINT output role, caller ID | BOOL, POINT pre/post and definedness; important if unchecked failure is admitted |
| `win.client_rect` | window ID, caller ID, guarded RECT output role | BOOL, RECT pre/post and definedness, geometry revision |
| `win.message_poll` | pump caller, window filter, min/max, removal flags, MSG output role | BOOL, bounded MSG pre/post; callback correlation; empty poll is still an observed request |
| `win.window_callback` | window ID/incarnation, message u32, complete wParam/lParam bits, callback nesting/cause | LRESULT, optional diagnostic globals before/after; normalized cursor float bits/geometry revision as derived evidence |
| `status` | recorder boundaries and exact producer hashes | counts by tag, phases, drops/gaps/pending, stopped state, outstanding owner/device/lane states |

Failure output policy needs an explicit distinction:

* A pre/post byte difference proves a changed value, **not** a write instruction: a same-value write is invisible to snapshots. Call the field `changed_bytes`, never `write_mask`.
* Always record post-call count and bounded buffer observations when safe, including on failure. Never assume success-only writes or invent zero outputs. Preserve failure postimages where later Stock code can read them, especially the ignored keyboard retry HRESULT and unchecked cursor/geometry returns.
* Do not compare uninitialized stack bytes as semantic inputs. Mark defined bytes and caller-consumed bytes separately. The buffered handler ignores event bytes when count/HRESULT reject them and ignores timestamp/sequence/appdata even when accepted. Raw before/after bytes can remain restricted diagnostics.
* A replay change-set can preserve unchanged caller memory and apply observed changed bytes only after validating the relevant preimage and no alias/concurrent writer. This is **not** a generic reconstruction of an API's write set. Unknown definedness, aliasing, caller shape, output size or uncovered failure behavior must yield a Gap. A complete process-replay claim needs an owned qualification of these assumptions, not a permissive parser switch.

Owner/device identity proposal: assign an owner incarnation on each successful usable `DirectInput8Create`; assign a device incarnation from owner ID + ordered create request + GUID, not address. Bind QueryInterface aliases to that object; do not infer physical uniqueness from equal GUIDs. Keep record-slot identity separate from COM identity. Increment format/property/cooperative revisions on qualified configuration changes. Repeated successful acquire is not reference-counted; `S_FALSE` does not create a new owner/device incarnation. Record focus/automatic-loss observations without pretending all hidden acquisition transitions are visible. Refuse pointer reuse whose previous lifetime was not closed or otherwise explicitly reconciled. The existing pointer-only registry cannot provide these guarantees. [Acquire lifecycle contract](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417818(v=vs.85)).

## One phase counter across both proxy DLLs

The current `control_tape.c` owns one static `next_phase` inside the d3d9 proxy. Compiling the same C file into dinput8 would create a second timeline. Joining two independent counters by timestamps later cannot recover the required cross-consumer entry/return order. [Current ownership](../../tools/parity/stock/control_tape.c#L33), [producer link commands](../../tools/parity/run_stock.py#L159).

Smallest actionable seam proposal: keep the already-early-installed recorder owned by d3d9 and export a versioned `parity_observer_v1` function-table getter. At the first normal `DirectInput8Create` call, **outside DllMain**, dinput8 uses `GetModuleHandle`/`GetProcAddress` to bind the expected already-loaded d3d9 proxy and validates API version/size, expected module/observer identity and ready state. It must not `LoadLibrary` either proxy, install its own recorder, reset the counter, or silently fall back when the owner is absent. Both adapters reserve/complete records through the same memory-only API and therefore the same atomic phase source. Early owner state must accept DirectInput observations before the first `Direct3DCreate9` call, with final file-identity confirmation still required before any complete tape flush. This is proposed work, not a validated change.

The Stock PE imports both DLLs, and the early control observer currently installs in d3d9 process attach before the main entry. Binding input only on a main-engine call avoids cross-proxy loader-lock calls. An owned helper must verify both DLL load orders and refusal when dinput is called before its recorder owner is available. A separate leaf `parity_observer.dll`, imported by both proxies but importing neither, is the clean dependency-DAG alternative if the minimal binding cannot meet early availability. Do not make either proxy import the other; do not initialize a phase source independently in both. [Existing attach and confirmation](../../tools/wine/capture/d3d9_proxy.c#L392), [early-bootstrap interface](../../tools/parity/stock/control_tape.h).

Hot callbacks may only perform bounded guarded copies, integer/atomic memory bookkeeping and transparent saved-target invocation. All output allocation, JSON serialization, file I/O, hashing, DLL discovery, clock-based diagnostics and flushing stay outside them. The reserve/complete API must preserve LastError, calling convention, flags/registers and FX state with the same independently checked raw ABI discipline as the current clock stubs; regular C logging is not a preservation proof. Reserve before the original call, complete after it, and publish fully initialized records atomically. Capacity/depth/identity/shape failure increments an observable Gap/drop while leaving record-only execution intact; it cannot be relabeled complete.

## Bounded owned-console helper proposal

Next authorized development step can be an **owned PE32 console** helper, with no game, real input device, window, GUI, audio sink or new prefix. Compile the exact new interception callbacks/observer API into test DLLs; do not copy callback logic into a test-only reimplementation. Give fake COM objects real owned vtables and save each original owned handler through the same mechanism the adapter uses. Invoking a hook must reach that saved handler exactly once with identical arguments; the handler may return an injected HRESULT or perform controlled writes. This checks actual forwarding machinery without pretending a fake device qualifies Wine's complete DirectInput implementation.

Bound the experiment to two owners, four devices (including shared vtable and reused-address incarnations), 64 logical calls and 256 phases, a 16-frame reentrancy limit, fixed payloads of at most 256 bytes, canaries around each destination and independent expected call/phase counts. Use a host-generated manifest with helper/DLL/source hashes, no unbounded command parser, a short wall timeout and a unique unused output directory. Flush once after explicit stop; retain refusal/failure results as failures. Do not use Stock addresses in the helper allowlist; give it a distinct caller manifest and `scope=owned_console_only`.

Required cases before touching the real game:

1. Keyboard256/mouse16 success; signed delta extrema and arbitrary button low bits; format identity/size mismatch refusal.
2. Failure with unchanged buffer, partial write then failure, same-value write then failure, acquire `0`, `1`, and negative values; ignored keyboard retry failure must preserve the correct resulting bytes. Mouse failure-clear is Stock behavior to model at the consumer layer, not inside the recorder.
3. Buffered count0/count1, key-up, offset255/256, `DI_BUFFEROVERFLOW` plus valid one event, invalid count>capacity, peek/size/count/null-pointer mismatch refusal. Check count postimages even on error.
4. GetKeyboardState failure and ToAscii output WORD independent of its return count; nonzero high byte of the WORD, no-translation, dead-key-like two-character response; raw diagnostic fields cannot change semantic equality.
5. Logical object aliasing, two devices sharing one vtable, Release/retirement/reused address, registry overflow, unknown owner/device and unclosed lifetime refusal.
6. Two DLL load orders, unavailable-owner refusal, no duplicate phase source; nested input and clock calls interleave into one contiguous phase permutation. Assert one phase source at runtime, not by matching two counters that happen to have equal values.
7. An independent assembly caller checks stack cleanup, integer/flags/FX and LastError before/after the saved original; a handler that deliberately clobbers FX exposes missing restoration. Negative controls remove restoration, corrupt counts, omit a phase and swap device identities; each must fail the intended assertion.
8. Fake WndProc callbacks with different geometry revisions, negative packed coordinates (Stock unsigned extraction), button/motion ordering and nested dispatch. Compare normalized float bits and consumption order separately from raw message diagnostics. This tests the proposed adapter's handling of bounded responses, not real OS message scheduling.

A later separate API-contract experiment could use a real original Wine DirectInput handler in an owned console and retained existing prefix, with explicit bounds and no game. It would test implementation-specific failure-output behavior that the fake handler cannot establish. It is not performed or implied by this research.

## Port seam and admission criteria

Proposed Port interface: a deep `PlatformInput` source selected at construction, exposing the existing device metadata, `beginFrame`, `sample`, `nextKey` and absolute cursor consumption through one owned input context. Its live adapter owns SDL event watching; its replay adapter owns the tagged input responses and explicit consumption index. Both are concrete behavior variants, which justifies this platform seam. Keep rendering, audio mixing, script dispatch, timers and the rest of the game running normally; private sinks affect presentation only. Do not route the replay through wall-clock `input.cmd`, synthetic held-bit ORs, button labels, OCR timing or an extra public input flow.

The first projection must declare how one Stock sample invocation maps to one Port sample/animation consumption step, and separately how each Stock buffered request maps to `nextKey`. Record consumed cursor globals and geometry revision at that step. Normalized API equality and a matching sequence of intended clicks are insufficient when Port and Stock consume different transitions before a suspended animation completes. Preserve raw response evidence for diagnosis; define projection equality using fields the identified consumer actually reads. The retained flow's New Career `(300,120)` and Cancel `(480,470)` coordinates and OCR checkpoints are useful functional anchors, not deterministic consumption ordinals. [Flow actions](../../tools/parity/stock/state_flow.py#L97).

Admission remains closed for whole-process replay until a new complete real trace includes all three input channels, stable lane/device/window binding, exact API request matching and failure output qualification; the owned helper passes its independent negative controls; the Port projection reproduces actual consumed states; and the parent clock/RNG/scheduling/completion boundaries are addressed. Headless focus isolation and privately processed audio remain required execution properties, not reasons to remove input or audio work. No existing early clock interval, this source audit, or a future fake-device helper alone proves repeatable whole-game Parity.
