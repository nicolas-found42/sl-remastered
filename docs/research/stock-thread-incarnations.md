# Stock thread incarnations and synchronization boundaries

2026-11-02. Static research for Parity deliverable 5. No Stock process, console
runtime helper, REA session, GUI or audio device was launched by this investigation.
Authority: Stock Executable SHA256
`7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`,
matching [Stock Decompile](../../local/census/all.c). The existing recorder is
record-only; none of the lifecycle design below is implemented or qualified.

The initial `jg` behavioral query over `tools/parity/stock` returned an incomplete
request-limit notice, with a verbatim RNG-observer excerpt. Focused reads and a
fresh local `objdump` pass supplied the findings below. The
[callsite inventory](../../local/verify/stock-thread-incarnations-20261102/iat-callsite-inventory.json)
retains every decoded literal operand reference to twenty selected IAT slots,
separating direct `FF 15 <slot>` calls from other references. Its
[producer](../../local/verify/stock-thread-incarnations-20261102/inventory.py)
streams the complete `objdump -d` listing without retaining a large whole-image
dump. This is a bounded static search, not complete indirect-call dataflow or a
runtime thread census. Calls through dynamically computed pointers and external
DLLs remain unknown.

## Why the current lane cannot name an incarnation

[`control_tape.c:29`](../../tools/parity/stock/control_tape.c#L29) stores one
numeric `id`, read/depth counters, RNG block pointer and nested frames per lane.
[`get_lane:157`](../../tools/parity/stock/control_tape.c#L157) permanently claims
the first empty slot using `InterlockedCompareExchange`, then reuses that slot
whenever the same numeric TID appears. There is no birth, retirement or generation
record. A reused TID can therefore inherit the prior lane's read count, depth or
RNG binding. The RNG observer correctly calls its TID/block pair diagnostics,
not a qualified lifetime key. A changed block currently becomes a Gap; a recycled
TID with the same block address would not itself prove a new stream.

The early descriptor in
[`stock_bootstrap.c:10`](../../tools/parity/stock/stock_bootstrap.c#L10) selects
only QPC, QPF, GetTickCount, GetAsyncKeyState and timeGetTime, optionally the CRT
seed/draw body. The proxy's
[`DllMain`](../../tools/wine/capture/d3d9_proxy.c#L431) handles PROCESS_ATTACH
installation and otherwise returns TRUE. It does not record THREAD_ATTACH or
THREAD_DETACH. Import initialization before this proxy remains outside its scope.
Do not infer a stable main role from lane zero or from a callback's first clock.

## Exact main-image creation and exit candidates

The [saved PE import table](../../local/verify/stock-headless-20261101/pe-imports.txt)
binds `CreateThread` to IAT RVA `1e7054` (VA `005e7054`) and `ExitThread` to
`1e70c8`. No named `_beginthread`, `_beginthreadex`, `_endthread`,
`_endthreadex`, `TerminateThread`, `SuspendThread` or `ResumeThread` import occurs
in that table. No such named call occurs in the inspected `all.c`. This absence
does not exclude static CRT equivalents, computed dispatch or DLL-owned threads.

All three decoded direct `CreateThread` calls have six x86 stdcall arguments:
security attributes NULL, stack size zero, start address, argument, creation flags
zero, and a writable thread-ID output. The proposed allowlist is the following
exact set; unexpected callers or shapes must remain Gaps.

| Caller / instruction / return RVA | Exact start and argument | Ownership and end |
|---|---|---|
| `00554e10`; call `00554e7c`; return `154e82` | Start `00554ea0`, argument NULL; TID output global `0076aa28` | Handle stored at `0076f130`. The body is the queued-file worker: semaphore-protected selection, file read, completion callback and retry. It returns zero with `RET 4` at `00555358`. Shutdown `00555360` sets `0076f981`, releases its wake semaphore, polls a shared semaphore index with Sleep(10), then closes the thread handle. |
| `005598c0`; call `00559b19`; return `159b1f` | Start `00559bc0`, argument NULL; TID output global `0077e070` | Handle stored at `0077e488`, conditional on `00784ca0`. Worker waits on semaphore `0061b804`, conditionally calls DirectSound COM slot `+44`, closes its semaphore on stop, then returns zero with `RET 4` at `00559c12`. Shutdown `00559c90` sets `00784cc8`, signals, polls the shared semaphore index, then closes the thread handle. |
| `0055c320`; call `0055c352`; return `15c358` | Start `0055c370`; argument `00784ce8 + index*10h`, index in `[0,15]`; TID output is the caller's stack parameter cell | Slot layout: event handle `+0`, callback `+4`, callback argument `+8`, state `+c`. The caller writes callback/argument before creation, then calls SetThreadPriority(handle,-1), without a creation-failure branch in this function. Start sets state 2, repeatedly invokes the cdecl callback, and on stop sets state 4 before `ExitThread(0)` at `0055c397`. |

Sources: decompile
[`257040`](../../local/census/all.c#L257040),
[`257225`](../../local/census/all.c#L257225),
[`257287`](../../local/census/all.c#L257287),
[`261275`](../../local/census/all.c#L261275),
[`261397`](../../local/census/all.c#L261397),
[`261417`](../../local/census/all.c#L261417),
[`261464`](../../local/census/all.c#L261464),
[`263412`](../../local/census/all.c#L263412),
[`263459`](../../local/census/all.c#L263459);
raw [file creation](../../local/verify/stock-thread-incarnations-20261102/file-create.asm),
[file return/shutdown](../../local/verify/stock-thread-incarnations-20261102/file-exit.asm),
[sound creation](../../local/verify/stock-thread-incarnations-20261102/sound-create-prefix.asm),
[sound return](../../local/verify/stock-thread-incarnations-20261102/sound-create.asm),
[callback worker](../../local/verify/stock-thread-incarnations-20261102/callback-worker.asm).
The first two instructions of `sound-create.asm` begin inside the preceding
instruction: ignore that prefix and use `sound-create-prefix.asm` for creation.
Likewise the file-return excerpt begins inside the large worker; only its
independently anchored return/shutdown tail is used here.

There is one decoded direct ExitThread call. `15c39d` is its raw next-instruction
RVA, not an observed return or a valid expectation that ExitThread returns. A
normal after-call return-address shim cannot observe that exit. For the two
returning workers, observing the user's start routine return is still earlier
than any loader/CRT cleanup and kernel thread termination.

`00551620` passes `00551640` to the generic callback worker. That adapter invokes
the first function pointer through the supplied object. Consequently the fixed
start address `0055c370` does not identify the eventual behavioral role:
callback/object provenance and slot generation are also needed.
Source: [`all.c:252694`](../../local/census/all.c#L252694).

## Synchronization identities and Stock projections

| Boundary | Exact evidence and rule that recording must retain |
|---|---|
| Semaphore create/wait/release/close | `00559760` owns 64 slots at `0077c758`, stride 8. It supports a nullable name or formatted `RLib_Semaphore_%d_User_%d` name, and forwards initial/max counts. Create return RVA `1597dd`. `00559830` uses WaitForSingleObject at return `159859`, converts any signed-negative timeout to `ffffffff`, increments the adjacent Stock count only on raw result zero, and collapses other results to -2. `00559880` **decrements that Stock count before** ReleaseSemaphore(count 1, previous-count output NULL), at return `1598b4`, including a failed release. Preserve raw API results plus Stock's projection and this ordering; do not repair the bookkeeping. `00559800` closes then zeroes the slot. |
| Generic worker event | `0055c280` creates an unnamed, initially nonsignaled auto-reset event at return `15c2b0`. `0055c2e0` closes it and clears all four slot words. `0055c3d0` waits at return `15c3e9`; raw assembly maps zero to integer 2, `102h` to 1 and every other result to 0. The decompile's Boolean signature loses this three-way distinction. SetEvent return RVA `15c418`. Event generation belongs to creation/close, independently of thread termination. |
| Main-image media event | `005d48b0` creates an unnamed, initially nonsignaled event with the caller's manual-reset argument, stores its handle into the object, and `005d48d0` closes it. Create return RVA `1d48c4`. Preserve containing-object lifetime and handle generation. |
| Multiple/message waits | `005d4d70` uses an ordered one/two-handle array, wait-all false, initial/repeated zero-time WaitForMultipleObjects, and MsgWaitForMultipleObjects in slices up to 10ms. It pumps messages, updates remaining timeout from GetTickCount, changes/restores priority and posts a wake message in a queue-status branch. Direct return RVAs `1d4dc3`, `1d4ef0`, `1d4e06`. Another media path loads the WaitForMultipleObjects IAT into EDI at `005cfead`; its register-indirect calls are **not** represented by the direct-call list. Raw wait results, handle-array order, message consumption and timeout evolution all matter. |
| Critical sections | The import references include initialization/deletion and many enter/leave sites, including media object constructor `005cfad0` and destructor `005cfbf0`, CRT locks and heap/stdio objects. Key by containing-object/address generation and initialize/delete events, retain recursion/ownership observations, and inspect register-indirect sites before claiming coverage. This is not equivalent to recording only semaphore outcomes. |
| Instance mutex | `00551430` creates named `StreetLegalMutex` with initial ownership, branches on raw GetLastError, then later releases it. Instance isolation does not justify deleting the check or zeroing LastError. |
| Timer callback | `0055a4d3` calls timeSetEvent with interval `32h`, resolution 10, callback `0055ad10`, caller object as user value, flags 1. Another forwarding call is `005d4fb9`. Timer deletion calls are `0055affc` and `005d0dfe`. These callbacks are not bound by the three CreateThread records; registrations, callback entries/returns, cancellation races and executing-thread provenance need separate evidence. |

Sources: [`all.c:261143`](../../local/census/all.c#L261143),
[`261181`](../../local/census/all.c#L261181),
[`261235`](../../local/census/all.c#L261235),
[`261255`](../../local/census/all.c#L261255),
[`263500`](../../local/census/all.c#L263500),
[`375354`](../../local/census/all.c#L375354),
[`375655`](../../local/census/all.c#L375655),
[`370286`](../../local/census/all.c#L370286),
[`252553`](../../local/census/all.c#L252553),
[`261919`](../../local/census/all.c#L261919),
[`262346`](../../local/census/all.c#L262346);
raw [semaphore paths](../../local/verify/stock-thread-incarnations-20261102/semaphore.asm),
[semaphore creation](../../local/verify/stock-thread-incarnations-20261102/semaphore-create.asm),
[event object](../../local/verify/stock-thread-incarnations-20261102/media-event.asm),
and [complete bounded IAT inventory](../../local/verify/stock-thread-incarnations-20261102/iat-callsite-inventory.json).

Closing a thread handle must retire that **handle reference**, not fabricate a
thread-end event. Reused handle bits, semaphore/event slots, object addresses,
TIDs and CRT block addresses each require their own generation. No thread-handle
wait or actual exit notification has been added to the recorder.

## Proposed owned-helper qualification, before changing the common recorder

This is an implementation proposal, not observed behavior or a chosen qualified
scheduler. Keep one recorder owner and the actual `next_phase` source; do not add
a second DLL's ring/counter and later merge timestamps.

1. Reserve a bounded immutable creation record **before** calling original
   CreateThread. Give it a monotonically increasing incarnation key, creator
   incarnation, exact callsite/start/argument provenance and creation ticket.
   A small WINAPI start thunk receives this record, binds its thread to that key,
   records start, and invokes the original routine with its original argument.
   Do not wait for creator return in the child: explicitly qualify child-start
   before creation-return and child-end before creation-return orderings. Preserve
   creation flags, start stack/FPU state, outputs and failure side effects; adding
   CREATE_SUSPENDED would change the mechanism under examination.
2. Record original start-routine return as `routine_return` with raw return bits.
   For the exact ExitThread site, emit `exit_request` and tail-call the original
   without expecting a return. Actual termination, TLS destruction, exceptions,
   abnormal termination and process teardown remain different boundaries. Do not
   release/recycle an incarnation slot merely because its handle closed. Qualify
   any owner DLL thread notifications separately under loader lock; its current
   attach-only bootstrap cannot silently become a general callback implementation.
3. Bind an explicitly observed entry-thread incarnation at the actual PE entry;
   proxy attach or first clock is insufficient evidence of that boundary. Unknown
   incoming DLL/timer threads remain Gaps until callback/notification provenance
   binds them. Refuse TID collisions, mismatched active contexts, capacity overflow
   and unresolved end states. Do not derive a new birth from first RNG use.
4. Record sync-object creation request/return, logical object/generation, each
   wait/signal request/return and successful close/delete. Preserve raw handles as
   diagnostics, raw return and LastError bits, input/pre/post buffers, and the
   Stock semantic projection separately. Failed create/close, alias handles,
   named-object reuse and concurrent close/wait need explicit cases. Imported and
   inline interlocked operations plus shared polling are additional scheduling
   boundaries, not already covered by these IAT hooks.
5. Extend the schema deliberately. Existing validators require a contiguous
   `1..2*completed_calls` phase permutation. Lifecycle/object phases cannot be
   inserted into it without a new version that accounts for every tagged phase,
   outstanding creation ticket, live incarnation and object lifetime. Preserve
   incomplete callbacks/calls as Gaps; never write fictional matching returns.

Owned console matrix: three exact argument shapes and unknown caller/start/size
refusals; original and intercepted run snapshots of integer registers, EFLAGS,
LastError, 512 FX bytes, caller output cells and stack canaries; normal return and
explicit ExitThread; failed creation; all three creation/start/end race orders;
parent/child nested clock and RNG calls; concurrent births; fixed-capacity
exhaustion; same numeric diagnostic TID and same raw handle/object bits across
**explicit synthetic generations**; retire-then-recreate and retire-too-early
mutations; active-frame exit; unsupported exceptions; semaphore failed release
with Stock's decremented counter; three event-wait results 0/102h/ffffffff; object
close/reuse and two ordered wait handles; callback registration/cancel overlap.
Use real Win32 objects for the actual API/lifetime cases. Synthetic reused IDs
test registry rejection only; they do not prove real kernel TID reuse occurred.

Fault controls must remove an actual generation increment, reorder a real shared
phase, omit FX restoration, or skip an exit/object close. Require the appropriate
first-divergence/refusal, then run the original helper again unchanged. Only after
owned qualification should a fresh isolated Stock **record** be attempted.
Repeatability and response/schedule replay remain later acceptance steps.

## Remaining unknowns

The exact Hangover/Wine thread-start/loader/TLS ordering, actual Stock callback
lanes, non-main-image consumers, message/COM/media work, exceptions/unwind and
unobserved shared-memory scheduling remain unresolved. An API run token does not
control all instructions between hooks or the worker's flag-poll loops. See
[replay boundaries](stock-replay-boundaries.md) and
[primary replay design](replay-design-primary-sources.md). The static inventory
supplies narrow next candidates; it cannot establish deterministic execution or
turn a functional menu capture into Parity.

The [Jev claim check](../../local/verify/stock-thread-incarnations-20261102/jev-claims.json)
returned verified verdicts for the four bounded claims tested, but requires review
for the callsite inventory (confidence 0.40), semaphore ordering (0.48) and event
projection (0.64). The numeric-TID-only lookup claim stands at 0.88. These are
retained advisory judgments, not approval of the proposed lifecycle mechanism;
the raw instruction/source anchors remain available for independent review.

The parent subsequently corroborated the event-wait projection through REA MCP
`read_bytes` at `0055c3d0` (64 bytes), exactly matching the immutable PE. Evidence
`ev_a3409b688a20db00f13c85d361a05fd37e4dc1801f2c8953870abf9c8730b458`
and the [byte audit](../../local/verify/stock-thread-incarnations-20261102/mcp-byte-audit.json)
retain that bounded static result. Ghidra had no recognized function at that
address, so the failed function-instruction request is preserved separately;
the successful byte read establishes neither a function signature nor runtime
execution. The [MCP evidence](../../local/verify/stock-thread-incarnations-20261102/mcp-thread-evidence.json)
and [Jev checks](../../local/verify/stock-thread-incarnations-20261102/mcp-jev-before-after.json)
remain linked to the exact calls.

## Owned DLL notification prototype (later 2026-11-02)

The parent requested an alternate identity mechanism using actual DLL
notifications, without replacing thread start/return ABI. Microsoft's
[DllMain contract](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain)
documents notifications for threads created after DLL load, absent attach for
preexisting threads, clean-exit detach, missing detach for TerminateThread, and
process unload/termination without individual thread-detach notifications. The
reserved pointer distinguishes static from dynamic process attach. Notifications
run serially under loader lock. These limits directly constrain the prototype;
they do not establish the exact Wine implementation.

The [local prototype](../../local/verify/stock-thread-incarnations-20261102/notifications-prototype)
preserves all common production files. It freezes a recorder copy and changes
only that copy's numeric-TID `get_lane` into an incarnation-registry lookup;
[exact difference](../../local/verify/stock-thread-incarnations-20261102/notifications-prototype/recorder-copy.diff).
The owner DLL's custom entry directly calls its memory-only DllMain, bypassing
CRT entry initialization for this owned DLL. Fixed BSS holds 128 never-recycled
records and 1,024 notification events. A new observed birth consumes a new key
and fresh lane; closed records, old depth, read counts and RNG bindings remain
history. Attach, detach and explicit Gaps share the **actual frozen recorder's
`next_phase`**, used by clock, RNG and input callbacks. Duplicate live birth,
unknown observation/detach, pending depth, exhausted capacities and invalid
lifecycle order refuse a complete capture. No loader callback allocates, loads,
does file I/O, waits or calls an imported API; TID and saved LastError come from
x86 TEB reads. The generated DllMain closure is independently retained in
[assembly](../../local/verify/stock-thread-incarnations-20261102/notifications-prototype/dllmain-reachable.asm):
eight local functions, no unresolved/imported calls or x87/vector registers.
The actual PE entry was checked against that DllMain symbol.

Twelve named synthetic state scenarios pass with ASan/UBSan, including explicit
same-TID generations, duplicate/live-ID collision, unknown lookup/detach,
pending-depth detach/end, capacity/phase exhaustion and invalid process order.
An intentionally faulty variant that reuses a closed key fails the reuse
assertion with exit 1. This does **not** demonstrate actual OS TID recycling.
[Build/source identities and synthetic results](../../local/verify/stock-thread-incarnations-20261102/notifications-prototype/build-checks.json).

The [actual owned run](../../local/verify/stock-thread-incarnations-20261102/notifications-prototype/run-first/result.json)
completed in 6.59 seconds under the shared pool, including the Stock slot to
serialize container work. Docker arguments declare ARM64 Hangover image
`c3105bb5c0fe829f89f6f782e79d7bfdb31708209ee4456e3ab4be5e7f94d88f`,
one CPU, 1 GiB, 128 processes, no network, dropped capabilities, read-only root,
bounded tmpfs and only a private output/prefix mount. There is no display,
audio/input socket or Stock executable. It uses a checked stopped warm prefix
fixture, APFS private-inode clone and matching manifests; the seed remained
unchanged and the container was removed. Inner/outer deadlines are 75/90 seconds.
Frozen source and all three helper/DLL binaries remained unchanged.

The positive console fixture forces four workers to be live before releasing
them. Two return normally and two call ExitThread. Independent CreateThread TID
outputs/GetCurrentThreadId and exit codes match five distinct observed
incarnations: one static loader-attach thread and four attached/detached workers.
Each exercises one clock call, seed/draw tails and an owned input state method
containing a nested clock call. The combined artifacts retain **25 completed
calls and nine lifecycle notifications, exactly 59 contiguous shared phases**,
with zero gaps, drops, active or pending calls. Per-incarnation read counts are
1..5; every worker detach follows its final return. The main thread is explicitly
still live at the declared capture endpoint.

The two real refusal fixtures are retained alongside the accepted interval:

| Fixture | Observed refusal |
|---|---|
| Disable notifications before creating four workers | Eight lifecycle Gaps, four dropped observations, four original-clock passthroughs; helper exits 1 and the capture is rejected. |
| Start a worker and wait for its ready event **before** dynamically loading the owner DLL | The existing worker has no attach binding; lookup, actual clock observation and later detach produce three Gaps. Its original clock still returns 123; the helper verifies refusal. Dynamic loader-attach reserved flag is zero. |

The [independent artifact checker](../../local/verify/stock-thread-incarnations-20261102/notifications-prototype/check.py)
verifies phase union, lane/TID/key bindings, nesting, actual outputs, thread
notification ordering, owner-counter identity and producer hashes. Seven
corruptions of the actual captured records are rejected: missing attach, stale
lane, wrong detach TID, detach during a pending call, different phase source,
reused key and RNG state bit. [Results](../../local/verify/stock-thread-incarnations-20261102/notifications-prototype/run-first/checks.json).

These are helper identities, not Stock roles or scheduler control. The RNG test
uses a private per-thread block **after** the original-getter boundary; it does
not exercise real CRT TLS allocation/getter cleanup. Input uses an owned fake
vtable, not Wine devices or a complete COM lifetime flow. DLL notification
transparency has no complete integer/FLAGS/FX differential matrix yet; the
static memory-only callback audit does not substitute for that evidence.
Actual pending-frame thread exit, abnormal termination, real TID recycling,
preexisting Stock/DLL workers, loader-order differences and process-end capture
remain unqualified. The prototype does not capture DLL_PROCESS_DETACH in its
saved pre-exit interval and never fabricates missing thread deaths. Format-6
files here include extra lifecycle phases and are deliberately **not** admitted
by existing Stock interval/replay validators. A versioned production lifecycle
schema, early-bootstrap qualification and fresh Stock record are still required.
The helper joins all four workers before writing its lifecycle snapshot. It does
not qualify a concurrent notification flush/stop: the prototype has no per-event
publication marker or atomic snapshot across the lifecycle and call rings. A
live integration needs a bounded publication/drain protocol that cannot wait
inside loader lock, alongside an explicit policy for workers still live at the
capture endpoint.

The [prototype Jev gate](../../local/verify/stock-thread-incarnations-20261102/notifications-prototype/jev-gate.json)
remains unaccepted (`safe_to_apply=0.09`, composite `0.68646875`, several limiting
rubric confidences zero). It verified the bounded accepted-run figures at 0.98
and real-refusal/mutation claim at 0.85; synthetic and cleanup/scope claims remain
review judgments at 0.50/0.72. It supplied no actionable defect description.
These results are retained without reroll, and no production/live-Stock approval
follows.

## Publication closure continuation (2026-11-02)

The later retained `publication-prototype/run-held` capture was independently
rechecked without rewriting it. The new
[audit](../../local/verify/parity-thread-publication-continuation-20261102/prior-capture-audit.json)
validates its 2,025 complete calls and 4,059 shared phases, two committed-subset
snapshots, four deliberately held pending calls, source/binary identities and
seven rejected record corruptions. These snapshots are explicitly incomplete
subsets, not atomic checkpoints. The earlier wrapper's `passed:false` remains
unchanged; its `execution_finished:true` is not itself an evidence verdict.

Source inspection found a concrete closure defect in that local prototype:
closing admission rejects nested observations made by an already-admitted parent.
The parent can subsequently publish its return with zero recorded Gaps, silently
omitting its nested work. A focused native regression first failed on this rule.
The local candidate now permits continuation only when the current native lane
owns an active parent frame and the gate still has an in-flight observation. It
continues refusing new roots after close and any admission after drain. The
[native regression and omission control](../../local/verify/parity-thread-publication-continuation-20261102/green.json)
pass under ASan/UBSan; removing continuation returns the expected failing exit5.

The [actual owned Wine audit](../../local/verify/parity-thread-publication-continuation-20261102/owned-audit.json)
then checks four real workers held inside their outer calls, closure from the
parent, release, one nested call per worker and actual thread joins. Two accepted
executions retain eight calls and 21 shared phases. An independently compiled
omission variant still returns 123 from every worker but retains only four calls
and 13 phases, with zero self-reported Gaps; the independent nested-coverage audit
rejects it. Missing-call, changed-result and changed-phase mutations are also
rejected. Notifications after closure are outside this capture: five incarnations
remain live at its declared endpoint, and the audit never fabricates detach events.

All attempts remain intact. The first runner refused its 2GiB artifact limit
before launching Wine; the qualified prefix has 2,413,198,943 logical bytes, so the
next attempts used the existing Stock-standard 4GiB cap. The second fixture used
observer depth as its recursion guard, making the omission variant recurse; that
failed attempt is retained. The final fixture uses its own real TLS guard, so its
control flow does not depend on whether nested observation is admitted. Actual
container isolation, private prefix inodes, unchanged fixture/source bytes and
container removal were checked. No display, audio, Stock process or reference
file was used or changed.

The [Jev gate](../../local/verify/parity-thread-publication-continuation-20261102/jev-gate.json)
remains unaccepted (safe-to-apply0.10, composite0.66475). It verifies the focused
regression at0.97, leaves the prior counts uncertain and marks the combined Wine
claim unsupported at review confidence0.40. Direct independent inspection of the
retained raw call records and worker outputs supports the bounded counts above;
that does not approve production readiness. Complete integer/FLAGS/FX notification
transparency, concurrent process shutdown/unwind, versioned production lifecycle
schema, actual Stock role provenance and synchronization/replay remain pending.
The candidate changes are confined to new local evidence copies; the common
production recorder has not adopted this prototype.
