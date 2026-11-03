# Stock capture before game initialization

2026-11-01. Design research, static PE inspection and an owned synthetic PE32
console debugger experiment. No Stock/game, GUI or audio process was launched;
private containers ran only the owned helpers. No production or `ref/` file was
changed. Authority: Stock Executable SHA256
`7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`.
The **INT3 entry-breakpoint candidate failed its console qualification** on the
inspected runtime. A subsequent **minimal self-IAT publication in an imported
DLL's process attach passed its owned-console constructor/main controls**; see
both measured follow-ups below. This is not an implemented
early Stock recorder, deterministic replay, restorable checkpoint or Parity Proof.

## Decision

The current next seam is the minimal self-IAT bootstrap qualified in the owned
console, including the copied shared-phase recorder follow-up below. Keep the
original INT3 route rejected. Neither result enables a Stock early recorder
without source review and a separately authorized integration experiment.

The candidate investigated was a **PE32/i386 Windows debugger controller**, inside the existing
private ARM64 Hangover11.16/wowbox64 environment, to stop at the actual loaded
Stock image entry. While all target threads are stopped, publish the recorder
configuration and hook pointers into the already imported test proxy DLL and
the main-image IAT, then restore the entry instruction and continue the original
entry exactly once. **[INFERENCE / PROPOSED]** This moves installation ahead of
Stock's CRT/game initialization without performing an installer in `DllMain`,
creating an injection thread, or restoring a partial snapshot. The API and Wine
source support a candidate route, but the measured packaged-runtime results
below prevent promoting it. The design that follows records the intended
mechanism and its constraints, not a working installation recipe for Stock.

The entry is **RVA `001514c0`**, preferred VA `005514c0`, not WinMain and not the
first Direct3D call. Static inspection confirms i386 `014c`, PE32 `010b`, preferred
base `00400000`, image size `003c0000`, and empty main-image TLS, export and
delay-import directories. The first instruction bytes are
`a1 00 70 60 00` (`mov eax,[00607000]` at the preferred base). Use the debug
event's actual image base plus the validated entry RVA; do not assume the
preferred base. Retained evidence:
[`pe-boundary.json`](../../local/verify/stock-early-capture/pe-boundary.json),
[`PE headers`](../../local/verify/stock-headless-20261101/pe-imports.txt),
and Microsoft's [entry RVA definition](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#optional-header-standard-fields-image-only).

This preserves the executable on disk. The declared instrumentation still
changes process memory: one temporary entry breakpoint byte, selected IAT
pointers, and later independently qualified RNG trampolines. Its manifest must
say so; an unchanged disk hash does not mean an uninstrumented execution.

## The lifecycle that matters

| Stage | What is true | Allowed capture action |
| --- | --- | --- |
| `CreateProcessW`, `DEBUG_ONLY_THIS_PROCESS` | The calling thread owns the debug loop. `CREATE_SUSPENDED` additionally prevents the primary thread from running until resumed; it does not promise resolved imports. | Give the controller an explicit absolute executable path, working directory, environment and bounded output location. Prefer the debug-event stop alone for the first helper to avoid mixing suspend counts. |
| `CREATE_PROCESS_DEBUG_EVENT` | Microsoft places this before user-mode execution; it supplies image base, process and initial-thread handles. A debug event suspends all target threads until continued. | Verify mapped PE headers/entry bytes against the immutable file; record actual image base; install the one-shot entry breakpoint. Do not call the present in-target installer or expect final IAT values here. |
| Load notifications and loader breakpoint | DLL-load events are observations, not a barrier saying all initialization has finished. | Record module identities and thread events; close image/DLL file handles as documented. Classify loader exceptions separately from the controller-owned entry breakpoint. |
| Verified breakpoint at `base+001514c0` | The original first entry instruction has not executed. In the inspected Wine loader, import fixing and initial DLL/TLS attachment precede return from the loader, and the loader lock is released before application execution. | Validate all original hook targets and publish the recorder state/IAT writes externally. No target-side installer runs. Read back the complete write set and original instruction before continuing. |
| Original entry and initialization | Execute the restored Stock instruction from the original entry context. | Record from the first covered consumption, including calls made before Direct3D. Preserve all original game, renderer and media work. |

Sources: [process creation flags](https://learn.microsoft.com/en-us/windows/win32/procthread/process-creation-flags),
[debug event ordering and thread suspension](https://learn.microsoft.com/en-us/windows/win32/debug/debugging-events),
and [debugger event-loop memory/context operations](https://learn.microsoft.com/en-us/windows/win32/debug/writing-the-debugger-s-main-loop).
The table's publication strategy is a proposed use of these contracts, not a
claim that the existing recorder exposes the necessary external interface.

Do **not** equate the initial debugger breakpoint with safe application entry.
Microsoft documents a new-target initial breakpoint before DLL initialization
and recommends a subsequent application breakpoint. The tagged Hangover Wine
source has a different detailed ordering: `loader_init` enters `loader_section`,
fixes imports, attaches dependency DLLs, runs executable TLS callbacks, checks
the debug port, calls `process_breakpoint`, and only then leaves
`loader_section`. Both sources require continuing to an actual application
boundary before executing a substantial in-process initializer. They must not
be combined into a claim that Wine's loader breakpoint is pre-DLL-init or
outside loader lock. Sources:
[Microsoft initial breakpoint](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/initial-breakpoint),
[tagged Hangover loader](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/dlls/ntdll/loader.c).

The inspected Wine tag `hangover-11.16` resolves to commit
`628d1aeb09803d3fa9170023e7203060e955bfe8` via the
[upstream tag API](https://api.github.com/repos/AndreRH/wine/git/ref/tags/hangover-11.16).
The existing adapter selects `HODLL=wowbox64.dll` and verifies the official
Debian12 ARM64 bundle SHA256
`dd7373202a168b7200cc36627a25b588511f3783756c2661bf4c022be8380aa8` in
[`Dockerfile.hangover`](../../local/verify/stock-post-intro/Dockerfile.hangover).
Tagged source is not independent proof of every packaged binary's build
provenance. The experiment must retain the image digest, Wine/emulator DLL
hashes and actual versions as well.

## Why the controller should be PE32

The exact tagged `kernelbase/thread.c` makes `Wow64GetThreadContext` and
`Wow64SetThreadContext` return `STATUS_NOT_IMPLEMENTED` on native ARM64; their
implemented branches are i386 and x86_64. A native ARM64 controller using these
public wrappers is therefore not the proposed first experiment. For an i386
controller, `GetThreadContext` calls `NtGetContextThread`; the WoW64 syscall
implementation forwards to `RtlWow64GetThreadContext`, with corresponding set
support. The WoW64 layer also implements debug-object creation, continuation and
wait-event conversion. Its wait conversion filters DLL events belonging to a
different machine, so a PE32 module ledger cannot claim to enumerate every
native ARM64 module. Sources:
[kernelbase thread APIs](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/dlls/kernelbase/thread.c),
[WoW64 context syscalls](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/dlls/wow64/syscall.c),
[WoW64 debug events](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/dlls/wow64/sync.c).

Software breakpoint semantics and code-cache invalidation are separate gates.
At the owned `INT3` event, validate exception address, process/thread identity,
original byte and observed x86 EIP before changing anything. The helper must
establish whether that runtime reports EIP at entry or entry+1; restore the
original byte and set EIP to the original entry using that proved rule. Do not
blindly subtract one from every breakpoint. No single-step is needed to rearm a
one-shot breakpoint that will be removed permanently.

Call `FlushInstructionCache` after adding and restoring executable bytes, and
retain its result. More importantly, test the resulting execution. The native
ARM64 `ntdll/unix/virtual.c` path does not implement another-process flushing
and still returns success; the WoW64 wrapper adds a `CrossProcessFlushCache`
notification before invoking it. The notifier's return is not checked by that
wrapper. Thus a nonzero API result alone cannot prove that the packaged
wowbox64 discarded an old translation. Require cached-code mutation/restoration
controls before Stock entry breakpoints or RNG instruction patches. Sources:
[Microsoft cache requirement](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-flushinstructioncache),
[WoW64 cache notification](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/dlls/wow64/virtual.c),
[native cache implementation](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/dlls/ntdll/unix/virtual.c).
The actual consumer/emulator acknowledgement remains unqualified here.

## External publication, without an injection thread

**[PROPOSED]** Extend only the private test proxy with a versioned exported
data descriptor and precompiled x86 wrappers. The existing proxy is already a
static `d3d9.dll` dependency; its explicit `DllMain` is trivial. Keep it trivial
and audit its CRT/TLS initialization too. Do not move hashing, DLL loading,
file I/O, worker creation or recorder installation there. Microsoft's
[DllMain contract](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain)
states both the loader-lock constraint and the need to move substantial
initialization into a separate function. Current proxy evidence:
[`d3d9_proxy.c:359`](../../local/verify/stock-control-tape/source/d3d9_proxy.c#L359),
[`DllMain:407`](../../local/verify/stock-control-tape/source/d3d9_proxy.c#L407).

The descriptor should expose fixed-width fields: magic/version, structure size,
bundle hash, module bounds, wrapper RVAs, writable recorder-state range, event
buffer bounds/capacity and publication state. Resolve exports from the
**target** mapped PE32 module and validate every RVA/range. A controller-side
`GetProcAddress` pointer is not a target-process address. Place original API
pointers in writable exported state; do not rely on undocumented offsets into
the current static C globals or on compiler symbol layout.

While the entry debug event remains pending, the controller:

1. Verifies the exact disk image and module bundle, mapped machine/header/entry,
   intended API slots and expected target exports/forwarders. It records the
   original five import pointers before replacing them. A resolved address in a
   DLL does not by itself prove semantic identity; use the same narrow identity
   checks as the current installer, adapted to target exports.
2. Writes a zeroed bounded recorder configuration, original API addresses,
   initial-thread incarnation and declared capability set into the test DLL's
   validated writable region. Recorder readiness is still false.
3. Obtains and records protections for every affected page with
   `VirtualProtectEx`, writes only the intended 32-bit IAT words and independently
   qualified RNG patch/trampoline bytes, restores protections, flushes changed
   executable regions, and reads back every byte. All target threads are still
   stopped; partial installation is never resumed. Publish readiness last and
   read it back. This is a controller transaction, not an assertion that the
   existing in-target `control_tape_install` can be called remotely.
4. Restores and verifies the entry byte and x86 context, recording an
   `entry_release` provenance record before continuing. Context readback must
   show only the intended EIP correction. Do not modify argument registers,
   flags/DF, segment selectors, x87/SSE state, debug registers, stack bytes or
   TEB LastError. No target callback is executed just to initialize observation.
5. Pumps debug events on the creating controller thread, handling only its
   owned breakpoints. Pass unrelated first-chance exceptions through with the
   appropriate unhandled status; never swallow an access violation as success.
   Keep finite deadlines, event/count limits and a target-scoped termination
   path. Do not launch a host debugger GUI or attach a second debugger.

For this design, `ReadProcessMemory` can drain completed records from the DLL's
bounded buffer into controller output. Use explicit slot publication/acknowledge
states and a bounded no-overwrite protocol; process-memory reads of a running
buffer are not an atomic snapshot. A sequence mismatch, partial read, overflow,
unclosed ticket or raced slot invalidates the recording. This avoids making the
old first-Direct3D worker a prerequisite for flushing early events. It does not
license target suspension at arbitrary times as an invisible operation.
Microsoft documents debugger memory/context access in the
[debug loop](https://learn.microsoft.com/en-us/windows/win32/debug/writing-the-debugger-s-main-loop).

The existing wrapper ABI work remains necessary: correct stdcall/cdecl cleanup,
relative-CALL relocation, flags/registers, aligned FXSAVE/FXRSTOR, LastError,
return registers, defined output bytes and explicit failure paths. It is not
replaced by a successful debugger launch. The present API return shim still
does not establish generic exception/unwind transparency; unknown callers or
output shapes must remain explicit gaps. Retain the previous
[control-tape ABI limitations](stock-control-tape.md) and independently qualify
the seed/draw trampolines before including RNG in the capability set.

## Which early consumers this boundary would cover

Stock's `entry` at `005514c0` installs `00551520` as an initializer then invokes
`005d8236`. The initializer creates the file worker through `00554e10` before
engine time initialization `00551690`, which reads QPF and the QPC epoch. These
are executable source-order facts, not a complete traced startup order. Sources:
[`entry`](../../local/census/all.c#L252578),
[`initializer`](../../local/census/all.c#L252609),
[`engine epoch`](../../local/census/all.c#L252749).

| Domain | Required armed coverage before entry release | Evidence that the old start gap is closed |
| --- | --- | --- |
| Engine and stopwatch clocks | QPF/QPC wrappers plus known output-pointer guards; preserve raw frequency/counter bits, epoch and indexed stopwatch consumption. | Actual records for `00551690` QPF/QPC initialization, before first Direct3D; indices and later derived time bits remain bound to consumers. |
| Playlist/time-dependent enumeration | Main-image `GetTickCount` from startup onward, plus actual `FindFirstFileA`/`FindNextFileA` results/order and file/config manifests. | Startup playlist calls and ordered results appear before selection. Tick replay alone is insufficient. |
| Input | Install DirectInput factory/device-method recording before first device creation/format/acquire; record full immediate/buffered results, HRESULT/count/flags, layout/text APIs and logical consumption. | First input QPF/QPC initialization and first state/data consumption are present; intro Escape polling is separate from menu/text input. |
| CRT RNG | Qualified hooks at `005d73fb` and `005d7408`, TLS ownership/first-use and thread incarnation records; seed/draw caller, raw output and pre/post state. | First stream initialization, every reseed and draw are present, including CRT/game initialization preceding Direct3D. No forced-seed substitute. |
| Workers and completion | Arm `CreateThread`/actual start provenance before `00554e10`; bind creator and start RVA to debugger thread events, record lifetimes and relevant waits/completions/messages. | The first worker is created under the ledger, instead of assigning a role later from a native thread ID. DLL/native-runtime workers remain separately scoped. |

Detailed consumer evidence, field requirements and open scheduling/media gaps
remain in [replay boundaries](stock-replay-boundaries.md). For clocks/RNG/input,
the earliest observed call must be validated against the startup decompile;
an empty event list is a gap, not proof nothing happened. Earlier startup may
exercise callers not present in the old allowlist; preserve original behavior
and emit a gap instead of silently bypassing their observation.

No Stock PE TLS directory was found, so there is no declared main-image TLS
callback which the normal loader would run before AEP. Imported DLL callbacks,
their private clocks/state, emulator work and pre-entry thread activity still
exist outside this boundary. The empty export directory also removes a normal
named-export entry route; it does not prove no DLL can ever call an executable
address. Do not call this all-process-from-creation capture. Later imports via
dynamic lookup, DLL-internal media clocks, filesystem outcomes and shared-memory
scheduling require their own coverage or an explicit excluded capability.

This route uses a fresh boot each time. It neither restores the eight observed
globals nor claims that pre-entry DLL/OS state is restorable. A real checkpoint
would additionally need a proven process/OS/DLL/thread/device restoration
contract. Starting earlier closes a specific missing-initialization interval;
it does not establish deterministic menu replay by itself.

## Rejected routes

| Route | Reason |
| --- | --- |
| First `Direct3DCreate9` | Actual prior engine epoch and possible CRT/playlist/RNG work remain unobserved. Keeping later globals cannot reconstruct their history. |
| Heavy `DllMain`, TLS callback or loader notification installer | Executes under, or inside a lifecycle constrained by, loader initialization. Adding `LoadLibrary`, waiting on a created worker, or running the current installer there has no safe-lock proof. |
| `CREATE_SUSPENDED`, immediate IAT patch, then resume | Suspension alone does not mean imports are fixed; loader work can replace IAT contents. It is a preparation stop, not the capture start barrier. |
| Initial loader breakpoint as application entry | Tagged Wine explicitly still holds `loader_section` there. DLL-load notification order is not an entry proof. |
| `CreateRemoteThread(LoadLibrary)` before initialization | Adds thread/loader scheduling and initialization side effects without an established entry barrier. It is unnecessary when the private proxy is already a static import. |
| Break at WinMain or replace the startup routine | Can miss CRT/static initialization; the PE header and decompile supply an earlier exact boundary. |
| Hardware debug-register entry breakpoint | No exact wowbox64 proof of execute-breakpoint support, exception delivery and debug-register round trip was established. It is a separate helper experiment, not an assumed fallback. |
| Native ARM64 debugger using public `Wow64Get/SetThreadContext` | The inspected tagged implementation returns not-implemented on that architecture. |
| Manual-map/rewrite the EXE, native macOS Wine, bypass intros/audio, fixed clocks or restored eight globals | Changes the launch/runtime/behavior under test or invents an unsupported restoration contract. |

## Smallest bounded experiment

This plan predates the measured owned-helper follow-up below. No Stock step
was executed, and the console entry-stop gate remains red. The parent owns the
shared runtime/helper slots. Progress only after its current work releases the
slot; use one private named container/prefix, the existing CPU/RAM bound,
private Xvfb and private Pulse null sink, no host display/audio/HID mounts and
no user focus. Keep real intro, rendering and audio processing when eventually
running Stock; output audio only to retained private PCM.

1. **Isolated PE32 launch probe.** Build a console helper and imported inert test
   DLL using the installed cross compiler, without package installation. Give
   the helper an exact PE AEP and counters proving DLL attach/TLS versus entry
   versus first API call. The controller must see create/load/initial-break/entry
   events, stop before any entry side effect, resolve the final target IAT and
   resume the original entry once. Include relocated-base and wrong-hash/entry
   controls. Bound target wall time to 10s and controller events to 4,096.
2. **Context and code-cache probe.** At the owned entry stop read and set the
   x86 context, round trip EIP/integer/flags/segments/FX/debug fields, preserve
   LastError and verify immediate target snapshots against a control launch.
   Force an already executed test block to acquire a different value, execute
   it, restore it, and execute it again through the same remote-write/flush
   path. An intentional omitted-flush/notification fault must be detectable
   when the runtime caches that block; if the fault is not observable, record
   the negative control as inconclusive and strengthen it before claiming cache
   qualification. A successful flush return or byte readback is insufficient.
3. **External activation probe.** Publish a tiny bounded recorder into the
   imported helper DLL while at entry; record a known first QPF/QPC call,
   synthetic stdcall return/failure buffers and two explicitly created lanes.
   Prove the existing FX/LastError/stack-alignment fault controls still fail.
   Drop a slot, corrupt an RVA, force protection/write/readback failure, exceed
   capacity and omit a footer: every case must fail closed before a qualified
   recording is produced. Do not resume partial hook installation.
4. **Stock entry-only probe.** Only after steps 1–3 pass on the exact image,
   stop at Stock AEP, collect module/context/resolved-IAT provenance, and
   terminate that disposable process without executing Stock entry. This
   qualifies the stop, not capture. The source game tree stays read-only.
5. **Fresh record-only boot.** Use a fresh copy/prefix and original full scenario,
   120s target deadline, bounded recorder capacity and explicit stopped footer.
   Require the actual early engine epoch, first worker, playlist/input and
   qualified RNG records before claiming those capabilities. Check binary and
   source manifests before/after. Reject any gap/overflow/unknown lane/unclosed
   event. Then perform independent Stock A/Stock B response replay and compare
   consumption/state boundaries before any Port comparison or Parity claim.

Every run should retain the full controller event ledger, loaded addresses and
module hashes, original/installed/restored bytes, page protections, x86 context
readbacks, API error codes, all interventions, timing/deadline result and a
capability statement. A timeout or missing entry event produces a diagnostic
failure; do not fall through to the old first-Direct3D start and call it early
capture. Keep debugger attachment identical in subsequent record/replay tests
and declare its schedule/debug-state effects.

## Evidence handling and remaining review

Primary pages/source were fetched with Firecrawl and Jev-screened before use.
Rate-limit responses and `skip` results were not used as evidence. No game
binary/decompile was uploaded to web retrieval. The bounded repository `jg`
query preceded focused reads; truncated/missing search output was not treated
as an exhaustive result. Exact static PE/decompile reads supplied the local
addresses. The design does not claim a full repository build/test gate; the
parent coordinates `tools/check.sh` and production promotion.

Retained [targeted Jev verification](../../local/verify/stock-early-capture/jev-verify.json)
supports the loader-lock ordering (`0.87`, confidence `0.80`), context API route
(`0.87`, confidence `0.80`) and cache limitation (`0.94`, confidence `0.91`).
The static-PE summary was labelled verified but `review` (support `0.69`,
confidence `0.53`); its exact JSON and headers remain available for manual
inspection. The external-publication proposal was `unsupported` (support
`0.38`, says-nothing `0.52`, confidence `0.28`), correctly leaving its runtime
feasibility unresolved. No retry was made to improve those judgments. The
parent must retain both review items and the helper gates; no supported early
Stock recording is asserted by this note.

The independent [document review](../../local/verify/stock-early-capture/jev-review.json)
also escalated: `safe_to_apply=0.70`, composite `0.906`, with limiting
`test_gap` confidence `0.02`. It supplied no concrete defect explanation. This
is unresolved review evidence, not authorization to promote an implementation;
the note remains design-only. Its supplied check summary mistakenly called
`git diff --no-index --check` exit 0: the actual command returned 1 with no
whitespace diagnostics because the new file differs from `/dev/null`. Separate
Python checks found no trailing whitespace/NUL bytes, a final newline and no
missing local links. No score was retried after that correction.

## Owned debugger follow-up: INT3 route did not qualify

The authorized owned PE32 console experiment lives in
[`probe-source/`](../../local/verify/stock-early-capture/probe-source/).
It contains no Stock code/data. Its child's custom AEP consumes a publication
word in its first instruction; its DLL exposes attachment status. The owned
32-byte report/assembly offsets have `_Static_assert` checks. Context fields
use the installed MinGW SDK, not hard-coded Windows layout offsets. Runtime
`CONTEXT_ALL` is `0001003f`, `sizeof(CONTEXT)=716`, pointers four bytes;
`4000001f` is the SDK's `STATUS_WX86_BREAKPOINT`.

Every run used resource-pool permits `cpu=1, host_memory=2, vm_memory=2`, Docker
one CPU/1 GiB memory and swap/128 PIDs, no network/capabilities, and
no-new-privileges. Only its owned output directory was mounted. No Stock,
game/reference mount, host display/audio/HID sockets, X/audio server or GUI
debugger was supplied. The target deadline was 10s/4,096 events; the outer
deadline was 75s with cleanup of only the unique container. Full source,
build/Docker argv, binary/PE manifests, runtime/DLL hashes and controller logs
are retained in every result directory. All five containers were cleaned up.

Actual runtime: ARM64 image
`sha256:c3105bb5c0fe829f89f6f782e79d7bfdb31708209ee4456e3ab4be5e7f94d88f`,
Wine11.16 Hangover, WowBox64 0.4.4 built 2026-08-30;
`wowbox64.dll` SHA256
`58d036888d638f05a72193702f97071aabc9baf7959e7736d48d9c450acc3440`.

| Owned run | Measured outcome |
| --- | --- |
| [`first`](../../local/verify/stock-early-capture/run-owned-entry-first/result.json) | CREATE_PROCESS gave base `00400000`, AEP RVA `1110`, entry `00401110`. Original byte `a1` was replaced with `cc`, read back and protection restored with a successful flush. Seven DLL loads followed. Event11 was a first-chance access violation; deadline cleanup followed, with no loader/AEP breakpoint. |
| [`diagnostic`](../../local/verify/stock-early-capture/run-owned-entry-diagnostic/result.json) | Plain child executed once with imported DLL attached once and zero publication, exiting31 as designed. Debug child faulted at37ms: `c0000005`, reported address `fe971694`, read address `803df010`. `GetThreadContext(CONTEXT_ALL)` succeeded with EIP `7bded57c`, ESP `0022fff0`. This was a startup-exception context, not an AEP context. |
| [`loader-arm`](../../local/verify/stock-early-capture/run-owned-entry-loader-arm/result.json) | Deferring all writes until a natural loader breakpoint produced no such event. Debug child executed once with zero publication/DLL attached and exited31 at38ms. |
| [`direct-write`](../../local/verify/stock-early-capture/run-owned-entry-direct-write/result.json) | Removing explicit controller protection/flush calls, while retaining WriteProcessMemory, still faulted at39ms. This is not a no-invalidation control: Wine WriteProcessMemory already sends protection/flush notifications. |
| [`DLL checkpoint`](../../local/verify/stock-early-capture/run-owned-entry-dll-checkpoint/result.json) | An explicit owned-DLL DebugBreak during attachment also produced no breakpoint; child execution/zero word/exit31 remained observable. Static DLL disassembly confirms the call was emitted. This synthetic checkpoint was not relabelled a natural loader event. |

[`owned-attempt-summary.json`](../../local/verify/stock-early-capture/owned-attempt-summary.json)
retains the failures. No API returned not-implemented. The runner stops at the
first failed qualification, so AEP missing-publication, wrong-boundary and
missing-restoration cases were not reached. No SetThreadContext round trip,
EIP rewind, original-byte restoration, publication at AEP, resumed original
entry or cache mutation control passed. The plain child's zero-word exit31
proves only its own missing-publication check. Code-write/readback success is
not an entry stop.

Primary-source follow-up pins Hangover11.16's Box64 submodule to
`2f130fab1d6e1a4ee8a71dc60cfdfcc839ad192a` using the
[upstream submodule API](https://api.github.com/repos/AndreRH/hangover/contents/box64?ref=hangover-11.16).
At that commit, ordinary ARM64 dynarec opcode `cc` tests the emulated SIGTRAP
handler slot before calling `native_int3`. Its interpreter helper also ignores
INT3 when no handler exists or ignoring is configured. The Windows module's
`EmitInterruptionImpl` handles `2e` syscalls and raises access violation for
other interrupt numbers, so two-byte INT03 is not a source-supported workaround.
Sources: [pinned INT3 translation](https://github.com/AndreRH/box64/blob/2f130fab1d6e1a4ee8a71dc60cfdfcc839ad192a/src/dynarec/arm64/dynarec_arm64_00.c),
[pinned interpreter helper](https://github.com/AndreRH/box64/blob/2f130fab1d6e1a4ee8a71dc60cfdfcc839ad192a/src/emu/x64int3.c),
[pinned Windows CPU module](https://github.com/AndreRH/box64/blob/2f130fab1d6e1a4ee8a71dc60cfdfcc839ad192a/wine/wow64/wowbox64.c).
Wine's [x86 breakpoint routines](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/dlls/ntdll/signal_i386.c)
use INT3. The Wine [WriteProcessMemory implementation](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/dlls/kernelbase/memory.c)
establishes why the direct-write test cannot be labelled a no-flush control.

**[INFERENCE]** Missing automatic and explicit breakpoints are consistent with
the emulator's guarded INT3 path. This does not locate the native fault's exact
instruction or prove every packaged-build detail. No trap-handler setting was
guessed, no emulator was patched and no stale-cache cause was asserted. Keep
this INT3 route rejected for Stock. An AEP detour or hardware breakpoint would
need its own complete ABI, relocation, cache and continuation qualification.

## Owned minimal self-IAT bootstrap: constructor/main controls pass

The next candidate avoids the debugger and executable-byte patches entirely.
The exact Wine `loader_init` source completes ordinary main-module
`fixup_imports` before attaching its dependency DLLs; `process_attach` walks
dependencies before calling a DLL's entry. Therefore a statically imported test
DLL can see the main image's resolved normal imports during process attach.
This does not apply to delay imports or runtime lookup. Microsoft's
[DLL best practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices)
allows minimal static initialization and Kernel32 calls outside its forbidden
operations. Its warnings still prohibit assuming an arbitrary installer is
safe. Source-based reasoning and review are retained in
[`source-verification.json`](../../local/verify/stock-early-capture/bootstrap-source/source-verification.json):
Jev labelled the ordering verified with confidence0.77 and the combined narrow
operations inference verified with confidence0.37, both `review`.

The owned implementation is
[`bootstrap-source/bootstrap.c`](../../local/verify/stock-early-capture/bootstrap-source/bootstrap.c).
It has a custom PE DLL entry, links with `-nostdlib`, and imports **only
Kernel32**. It uses compile-time static storage, `GetModuleHandleA(NULL)`, a
bounded integer-only PE/import scan, and compares the main QPC IAT target with
the DLL's own already resolved QPC import. It never calls QPC during attach.
For the known slot, `VirtualProtect`, one `InterlockedExchange` data-pointer
store, protection restoration and LastError restoration complete publication.
There is no Crypto/provider lookup, file/console I/O, module loading,
GetProcAddress/forwarder resolution, allocation, private lock, worker thread,
wait, executable patch or cache flush in that callback. Do not replace it with
the existing full `control_tape_install`.

The observer itself is a narrow stdcall C QPC wrapper for the owned child. It
calls the real API once, captures the actual Boolean/output/LastError, and
restores the real call's LastError before returning. This experiment does not
replace the previously required flags/GPR/FX/stack and exception/unwind ABI
qualification for Stock wrappers. Its report's restored protection is based
on the successful VirtualProtect call with the original protection, not an
independent page-protection query.

[`run-owned-self-iat-first/result.json`](../../local/verify/stock-early-capture/run-owned-self-iat-first/result.json)
passed all three cases in14.158s, Docker exit0 and cleanup0, on the same runtime
image and DLL hashes above. It used the same cooperative resource permits and
one-CPU/1-GiB/no-network/no-display/no-audio container policy, with a60s outer
deadline. Host source/binary hashes, compile argv, DLL imports, PE manifests,
raw child reports and child JSONL are retained.

| Owned case | Actual observation |
| --- | --- |
| Observer enabled | Attach count1; all47 main import thunks had resolved-address shape; the QPC target additionally matched the DLL import. The constructor already saw status1 and one constructor call, then main produced a second call. No phase-zero calls occurred in this child. The first recorded QPC Boolean/output words and LastError matched the constructor's directly observed values. Original IAT target `7a80e6ec` changed to wrapper `7a111000`; original page protection `02` was supplied on the successful restore. |
| No-hook control | Status2, calls0, original and final IAT pointers identical; constructor/main still consumed real QPC successfully. |
| Wrong-import refusal | Child consumes QPF instead of QPC, so no QPC main-image import exists. Status3, calls0 and no selected slot; constructor/main still consume real QPF successfully. |

The report's `iat_resolved` count checks pointer shape against the known owned
image; it is not a semantic export audit of all47 functions. The QPC identity
check is stronger and specific. This is an owned-image guard, not a hardened
parser for arbitrary hostile PE files or a replacement for the host's full
immutable Stock SHA verification. The custom DLL has no CRT; the child's real
CRT runs its constructor before main. Therefore the measured result proves
publication before that constructor's QPC consumption without restoring any
game globals or introducing a debugger scheduling dependency.

The earliest feasible next Stock boundary is now **minimal self-IAT
publication during the already imported private proxy's process attach**, not
INT3 interception. **[PROPOSED / NOT STOCK-QUALIFIED]** Keep host SHA/content and
instrumentation verification before launch; keep the attach bootstrap bounded
to main-image import shape/identity, static recorder state, protected atomic
pointer publication and exact failure/LastError handling. Publish readiness
last, and move all file/Crypto/log/worker/complex setup out of attach. Acquire
all affected page protections before a multi-slot transaction, restore them on
every path and refuse partial installation. Keep observers safe for unexpected
early callers: record a gap and preserve behavior rather than silently claim
coverage. Retain the original C/assembly ABI tests before integrating those
wrappers; this owned QPC wrapper is not their replacement.

This covers a plausible earlier **IAT** seam for engine/input/playlist clocks.
It does not qualify early instruction hooks for Stock RNG, DLL-internal API
consumers, total scheduler/media/input coverage or deterministic replay.
Dependency DLL initialization can still precede this proxy, and dynamically
looked-up APIs remain separate. No Stock was mounted or launched. The
[`owned-run-index.json`](../../local/verify/stock-early-capture/owned-run-index.json)
retains result hashes for both failed debugger attempts and the successful
owned bootstrap; the failures were not overwritten by the new result.

## Copied full recorder: early attach, late identity, owned controls

A local copy of the shared-phase recorder now provides the separate interface
[`control_tape_early_bootstrap`](../../local/verify/stock-early-capture/early-recorder-source/control_tape.h).
Original source hashes are retained in that directory. The source is local
research; the parent's tracked late recorder was not changed. Its assembly
entry/return wrappers remain the existing aligned FXSAVE/FXRSTOR implementation.
The legacy `control_tape_install` remains an **outside-loader-lock** function
and is never called by the new attach path.

The trusted compile-time plan contains exact PE image size/base/entry, selected
IAT RVAs and directly imported expected pointers, one IAT page RVA, bounded
entry/prologue bytes, metadata RVAs/sizes and expected full-file SHA256. The
bootstrap validates the mapped PE32/i386 headers, exact prologue, no delay
imports, slot alignment/distinctness/IAT-directory containment, every pointer
identity and the bounded metadata ranges. It acquires the single IAT page,
copies initial words with integer loads, performs atomic pointer publication,
restores protection and publishes readiness last. It restores LastError on
every return. Return1 means installed,0 means refused before slot writes, and
−1 is fatal protection-restoration failure: abort attachment/process and never
fall back to a later start. No helper has exercised an injected VirtualProtect
failure; parent source review must retain that untested failure-path limit.

The bootstrap calls only memory/atomic operations and private integer guards;
it does not call GetProcAddress, ReadProcessMemory, a clock, file/Crypto APIs,
module loading, allocation, logging, thread creation or waits. Compiler
inspection initially found integer XMM setup in local pointer arrays. The
final function has a no-SSE target with a non-inline integer InterlockedExchange
bridge; retained
[`final bootstrap disassembly`](../../local/verify/stock-early-capture/static-review/early-bootstrap-final-disassembly.txt)
contains no XMM/FX instructions. Separately, pinned Wine's
[GetModuleHandleA(NULL) implementation](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/dlls/kernelbase/loader.c)
uses the unchanged-reference-count path and returns the PEB image base without
name conversion, module loading or another loader lookup. This is the exact
NULL path used by the owned DLL; named-module lookup is not substituted.

`control_tape_confirm_file(path)` hashes the complete file **outside attach**.
Until it succeeds against the plan's exact digest, flush/stop returns−2 before
opening any tape file. The first output record then declares
`boundary=dll_process_attach_main_iat`, late full-file identity verification,
`scope=owned_console_only`, and observations that are not a restorable
checkpoint. The conservative existing footer still says
`early_startup=unobserved`: it is not relabelled complete process capture. The
new installation record and owned metadata are not automatically compatible
with the parent's strict Stock interval compiler/schema.

The owned standard-CRT child exports two exact assembly return labels and two
output globals. A test-only build macro permits only those QPF/QPC callers and
pointers, with existing memory-protection checks; unknown callers become gaps.
It does not broaden Stock's guards. The standard MinGW child has inspected TLS
callbacks, so that exact owned TLS directory is allowed only by this test
macro. Without it, the bootstrap rejects a main TLS directory, as required for
the inspected Stock image. No Stock callsite was guessed from the owned test.

The first full-recorder control run passed but its stopped prefix had never
persisted registry hives. A later APFS clone reproduced successful attach and
eight API returns, then correctly refused file identity: CryptAcquireContext
returned `NTE_PROV_TYPE_NOT_DEF` (`80090017`), not a file-open or digest mismatch.
Those failures are retained at
[`integer run`](../../local/verify/stock-early-capture/run-owned-early-recorder-integer/result.json)
and [`identity diagnostic`](../../local/verify/stock-early-capture/run-owned-early-recorder-identity-diagnostic/result.json).
The hash gate was not bypassed.

In a **new private clone only**, an owned console invoked Wine rsaenh's real
DllRegisterServer outside attach; its HRESULT was0, and SHA verification then
succeeded. The pinned
[RSA/AES registration resources](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/dlls/rsaenh/rsaenh.rgs)
define the Type024 provider. This environment preparation is separate from
recorder installation. Prefix-scoped `wineserver -k` followed by `-w` ended the
owned session; actual system/user/default-user hives and Type024 keys were
then inspected on disk. Do not infer persistence merely from RegFlushKey:
the pinned [KernelBase API](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/dlls/kernelbase/registry.c)
forwards through [NtFlushKey](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/dlls/ntdll/unix/registry.c)
to a [server handler that does no flushing](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/server/registry.c).
The [wineserver manual](https://github.com/AndreRH/wine/blob/628d1aeb09803d3fa9170023e7203060e955bfe8/server/wineserver.man.in)
documents WINEPREFIX selection and shutdown/wait behavior; no arbitrary sleep
is used as a persistence proof.

Finally, a new clone of that persisted fixture passed **without invoking any
provider repair**:
[`run-owned-early-recorder-persisted/result.json`](../../local/verify/stock-early-capture/run-owned-early-recorder-persisted/result.json).
Its source `control_tape.c` SHA256 is
`3020b33d3ed64262e9cbe5f9f304df2abcdfdc1ca4d687f70dc8d316fa2e9aaf`.
Container time was0.781s, with exit0/cleanup0. This is a declared warm fixture,
not fresh Wine initialization. Its1,996-entry file/link manifest matched its
seed before execution, every regular file had a private inode, and the seed
remained unchanged after the run. The manifest SHA256 is
`3e4612983afdd39ef78e7eef0ba0060a3b4546e307a843b648a4b233f768a981`.
The runner now requires an explicit stopped same-image prefix seed for further
experiments. No live/shared prefix or hardlink copy is used.

| Final owned control | Actual result |
| --- | --- |
| Before constructor | First QPF/QPC were already hooked: constructor2 entries/2 returns/4 phases. Initial frequency/counter metadata were copied zero observations. |
| Multiple consumption | Eight completed calls,16 contiguous shared phases, first API QPF, stopped with zero active/pending/gaps/drops. |
| No hook / wrong prologue identity | Refused/disabled before observation; no tape file. |
| Missing late confirmation | Eight captured calls; flush−2, no tape file. |
| Unknown caller | Original known eight returns preserved, plus one explicit gap; nine entries/eight returns/17 phases. This is an invalid capture, not a successful complete tape. |
| Existing ABI and guards |640 ABI cases, zero failures;640 entry/return alignment checks at modulo16=12; six guard cases passed. |
| Missing FX restoration | Deliberately removed FXRSTOR variant detected all five API variants and exited1. |

For Stock, all five declared IAT words share page RVA`1e7000`, so the one-page
transaction is a concrete candidate. Host full SHA verification still precedes
launch, and late full SHA verification must precede a qualifying footer. The
normal loader phase and empty main TLS directory support quiescence of Stock's
main-image callers, **not all DLL/native thread execution**. Full five-slot
Stock integration, startup caller completeness, fatal restoration behavior,
RNG instruction hooks and full replay remain unqualified. This local result
permits parent source review; it does not authorize or claim a Stock run.

The final independent
[`owned recorder Jev gate`](../../local/verify/stock-early-capture/final-owned-recorder-jev-gate.json)
did not accept the candidate: `safe_to_apply=0.17`, composite0.677, with low
test-gap confidence and further source/fixture claims needing review. It
verified the narrow constructor/phase result at confidence0.86 and the refusal/
unknown-caller result at0.91. ABI evidence remained below automatic confidence;
the combined fixture-history and bootstrap/source claims were unsupported at
confidence0.38 against its supplied evidence. No contradiction or concrete
defect explanation was returned. These unresolved judgments are retained for
parent/manual review; no score was retried and no early Stock source was
promoted on the strength of helper success alone.
