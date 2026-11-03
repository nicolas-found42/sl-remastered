# Stock CRT RNG capture after its original TLS lookup

2026-11-01. Research and an **owned console prototype**, with no Stock game
launch and no production instrumentation change. Executable authority: SHA256
`7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`.
The next seam is **after the original TLS getter returns**, at preferred VAs
`005d7400` (seed) and `005d740d` (draw). This avoids calling the getter early and
leaves its original relative CALL, allocation, initialization and failure
branches in place. The prototype qualifies byte copying, immediate return state
and a shared observation ring against an explicitly substituted TLS getter; it
does **not** qualify live Stock TLS, native-thread lifetimes or replay.

The current tracked early record already has 105,927 covered calls and 211,854
shared phases with no recorder loss. That is an early main-image API record,
not a CRT RNG record: see its [result](../../local/verify/tracked-stock-early-record-second/result.json),
the [tracked recorder](../../tools/parity/stock/control_tape.c) and
[bootstrap descriptor](../../tools/parity/stock/stock_bootstrap.c). This note
builds on that actual boundary; it does not use the older late-install gap as
the current state.

## Original code and exact overwrite spans

The complete checked instructions, TLS getter and caller excerpts are retained
in [primary-disassembly-excerpts.txt](../../local/verify/stock-live-rng-capture/primary-disassembly-excerpts.txt).
[check.py](../../local/verify/stock-live-rng-capture/check.py) checks the immutable
file hash, full seed/draw bodies, CALL targets, overwrite bytes, native void
descriptors, void dispatch and Escape tests against the file.

| Preferred VA | Original operation | Proposed mutation |
| --- | --- | --- |
| `005d73fb` | `CALL 005dd8d3` | Leave all five bytes unchanged. |
| `005d7400` | `MOV ECX,[ESP+4]` (4 bytes), `MOV [EAX+14],ECX` (3 bytes) | Replace these **seven whole bytes** with a JMP and two NOPs. Observe before executing a copied seven-byte body, observe after, then RET to the original caller. |
| `005d7408` | `CALL 005dd8d3` | Leave all five bytes unchanged. |
| `005d740d` | `MOV ECX,[EAX+14]` (3 bytes), `IMUL ECX,ECX,343fd` (6 bytes) | Replace these **nine whole bytes** with a JMP and four NOPs. Observe before executing the copied **entire 28-byte non-RET tail**, observe after, then RET to the original caller. |

The copied draw tail includes ADD `00269ec3`, the state store, EAX assignment,
SHR 16 and AND `00007fff`. It has no branch or relative operand. Copying the
whole tail lets its final observer keep the actual EAX/flags and saved TLS
pointer without rewriting a caller return slot. Both patches lie on image page
RVA `001d7000`. A five-byte patch at a one-byte RET would overwrite neighboring
instructions and is not this proposal. Exact code:
[all.c:379185](../../local/census/all.c#L379185).

The earlier [relocation proof](../../local/verify/stock-rng-relocation/README.md)
remains valid for its own entry detour: its copied first relative CALL must be
relocated, and its negative control rejects an unchanged displacement. The new
midbody candidate avoids relocating that CALL entirely. Neither proof validates
the real TLS getter's runtime branches.

## Observe the state Stock actually obtains

Stock CRT startup `005d8236` calls `005dd86c`. The latter allocates its dynamic
TLS index at global `0062c890`, allocates a `0x74`-byte block, sets that TLS slot,
initializes state word `+0x14` to one through `005dd8c0`, and writes native TID
at `+0` and `ffffffff` at `+4`. The regular getter `005dd8d3` saves LastError,
calls `TlsGetValue` with that same index, and lazily performs the allocation and
initialization when the slot is null. Allocation or TlsSetValue failure takes
the `__amsg_exit(0x10)` branch; success restores the saved LastError and returns
the pointer in EAX. Sources: [startup](../../local/census/all.c#L380608),
[TLS initialization/getter](../../local/census/all.c#L387815), and the retained
machine-code excerpt. Microsoft's [TlsGetValue contract](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsgetvalue)
also explains why an extra observer lookup needs LastError preservation.

Therefore the first RNG observation is explicitly named
`after_original_TLS_getter`, **not function entry**. Save its actual EAX pointer
and read `DWORD[EAX+0x14]` there; after the unchanged state-update instructions,
read that same saved pointer again. For seed, record the original `[ESP+4]`
argument; for draw, record raw EAX and the post-state. The checks are
`seed_after == seed_argument` and
`draw_after == (before * 0x343fd + 0x269ec3) mod 2^32`, with raw draw EAX
`(after >> 16) & 0x7fff`. This is per **native OS-thread TLS slot**, not a
separate generator for each cooperative script Thread. The script Thread
implementation's distinct cooperative records are described in
[vm-ops.md](../vm-ops.md#general-native-call-path); their names alone cannot
assign native RNG ownership.

This seam cannot say whether a valid state of one was just initialized by this
call or had already existed. It also cannot record a getter that exits or
raises before reaching the midbody. Those remain explicit coverage gaps; do
not fabricate an RNG entry or claim coverage of allocation failures. A future
init/getter lifecycle observer would be a separate qualified seam. Do not call
the getter, srand or rand from an attach callback to populate metadata.

The prototype reads only its known private block. Live pointer validity,
unexpected TLS-block replacement and recorder overflow/refusal need production
guards and controls before integration. A new pointer within an existing
native-thread lifetime must create an explicit block-change event/gap, not be
silently treated as the old stream.

## Raw diagnostics and values consumed by the game

Preserve raw observations in the tape and actual CPU state on return. A
comparison may add a separately versioned consumed-value view with exact
callsite evidence; it must never delete calls, phases, branch outcomes or raw
bits to manufacture agreement.

* **Seed:** `srand` is specified as void by [Microsoft](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/srand?view=msvc-170),
  but this binary leaves EAX equal to its TLS pointer. Its two direct wrappers
  at `0047c7d0` and `0047c7f0` also return that EAX unchanged. Their registered
  descriptors are `()V` at `00614564` and `(I)V` at `00614584`. The actual VM V
  dispatch uses the jump-table target `004200ab`, calls at `004200b7`, clears
  EBP at `004200bc`, and overwrites EAX at `004200be` before it can become a
  native result. This bounds a **semantically void VM-result annotation**; it
  is not permission to clobber EAX in a hook or infer every possible caller
  ignores it. Sources: [native registrations](../../local/census/all.c#L100946),
  [dispatcher](../../local/census/all.c#L27340), checked descriptors and dispatch
  machine code in the retained excerpts. Keep `eax_raw`, `tls_pointer_raw` and
  their equality relation; across runs an address can be identified as the
  observed CRT block only after its native lifetime/block binding is proven.
* **Draw:** preserve all calls and the exact 32-bit EAX. The Math wrapper masks
  to 15 bits, clamps `7fff` to `7ffe`, then uses x87 FILD/FMUL with the binary32
  constant at `005f1388`; raw rand is not that float result. Native sound code
  at `0055bf65` instead uses the low 12 bits. Other direct calls intentionally
  advance the stream even when their EAX is discarded, for example `00482826`
  and `00581ed1`. Sources: [Math wrapper](../../local/census/all.c#L92019),
  [sound consumer](../../local/census/all.c#L263213),
  [discarded draw](../../local/census/all.c#L96785), retained machine-code
  windows and [55 direct callsites](../../local/verify/stock-live-rng-capture/direct-rand-calls.json).
  The linear disassembly census is not a proof that no indirect caller exists.
* **GetAsyncKeyState:** the [API returns SHORT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate).
  At both currently guarded Stock returns, `0055c592` and `0055c5f4`, machine
  code is `TEST AH,80` followed by the loop/exit branch. Continuing the loop
  overwrites EAX at `0055c5b4`; taking exit overwrites it at `0055c5fc`. Neither
  path consumes upper EAX or other return bits. A callsite-specific comparison
  can expose `escape_down = (eax_raw & 0x8000) != 0`, while retaining raw EAX,
  low SHORT, both calls and the consequent loop branches. The declared SHORT
  contract alone does not establish the callers' consumption, and this does
  not authorize normalization for any unknown caller. Sources:
  [decompile](../../local/census/all.c#L263625) and retained machine code.

## ABI and the owned proof

Both routines are caller-cleaned x86 cdecl: seed's argument remains on the
original stack until its caller removes it; neither routine executes `RET n`.
The midbody wrapper saves all eight PUSHAD words and EFLAGS, clears DF for its
own C callback, saves aligned 512-byte FXSAVE state, calls the observer with
the existing 16-byte C-entry alignment, then restores FX, integer registers and
flags before executing the copied original instructions. Its post observer
repeats that save/restore and RETs with the original caller's return slot.
LastError is read/restored through the same x86 TEB seam as the existing
recorder. This preserves the **actual** original output; it does not restore
incoming flags over the draw's final AND flags. Sources: [cdecl contract](https://learn.microsoft.com/en-us/cpp/cpp/cdecl?view=msvc-170),
the exact Stock code, and
[owned observer source](../../local/verify/stock-live-rng-capture/run-flags-matrix/control_tape.c).

The final [owned proof](../../local/verify/stock-live-rng-capture/run-flags-matrix/proof.c)
maps the checked executable using `DONT_RESOLVE_DLL_REFERENCES` and **replaces
only its helper-mapped TLS getter with a known private state pointer**. It
executes the actual checked seed/draw instruction tails, never the game entry.
Four seeds (`0`, `1`, `ffffffff`, `80000000`) cross four explicit EFLAGS patterns,
four x87 control words and four MXCSR rounding modes. All eight XMM registers
have explicit input values; the x87 stack is nonempty. Immediate return
snapshots retain all 560 bytes, including raw integer/flags, LastError and every
FXSAVE byte; no comparator masks are applied.

The [final runtime result](../../local/verify/stock-live-rng-capture/run-flags-matrix/result.json),
[positive log](../../local/verify/stock-live-rng-capture/run-flags-matrix/positive.log)
and [24 independent checks](../../local/verify/stock-live-rng-capture/checks.json)
establish the following bounded result:

* 16 seed and 256 draw snapshots/states match unhooked execution exactly;
  [baseline](../../local/verify/stock-live-rng-capture/run-flags-matrix/positive-tape.jsonl-baseline.bin)
  and [hooked snapshots](../../local/verify/stock-live-rng-capture/run-flags-matrix/positive-tape.jsonl-hooked.bin)
  are retained. Sixteen Win32 calls also match immediate return state.
* The Win32 and RNG observers use the **same copied recorder's** `events`,
  sequence counter and `next_phase`: 288 events, 576 contiguous shared phases,
  zero dropped/gap/pending/active observations at stopped flush. The checker
  verifies every seed/draw chain, raw EAX and observed flags. RNG metadata
  explicitly names `after_original_TLS_getter`.
* Removing both RNG FX restores produces [272 FX mismatches](../../local/verify/stock-live-rng-capture/run-flags-matrix/negative.log)
  and exit 1; integer/flags/LastError and RNG states still agree. This is a
  sensitive FX-corruption control, not a successful recording qualification.

Two earlier failed harness runs remain at
[first result](../../local/verify/stock-live-rng-capture/result.json) and
[flags diagnostic](../../local/verify/stock-live-rng-capture/run-flags-diagnostic/result.json).
They reported snapshot mismatches; the first diagnostic includes EFLAGS and
XMM1 output differences, and that harness explicitly initialized only XMM0/7.
These source facts do not establish what the two entry states actually were.
The harness was changed to explicitly load
EFLAGS and every XMM register, producing
[controlled-input success](../../local/verify/stock-live-rng-capture/run-controlled-input/result.json)
before the final four-pattern matrix. No raw output bit was discarded. The
uncontrolled-input EFLAGS discrepancy was not independently attributed to a
runtime defect and is not claimed resolved for arbitrary incoming contexts.

A subsequent [entry-FLAGS measurement](../../local/verify/stock-live-rng-capture/run-entry-flags/result.json)
removes the explicit FLAGS load and instead executes `PUSHFL; POP [entry_flags]`
immediately before the same indirect call instruction for original and hooked
execution; output starts with `PUSHFL` immediately on return. All XMM inputs
remain explicitly initialized. Its
[272 raw input/output rows](../../local/verify/stock-live-rng-capture/run-entry-flags/positive.log),
[retained input FLAGS](../../local/verify/stock-live-rng-capture/run-entry-flags/positive-tape.jsonl-entry-flags.bin)
and [independent checks](../../local/verify/stock-live-rng-capture/entry-flags-checks.json)
show equal input EFLAGS (`202` for seed, `246` for draw), equal immediate output
EFLAGS and equal complete 560-byte snapshots in this new measured context.
The missing-FX-restore negative still differs in all 272 FX snapshots while
input/output FLAGS match. The compiled
[instruction-boundary review](../../local/verify/stock-live-rng-capture/entry-flags-instruction-boundaries.txt)
checks the observer saves FLAGS before CLD, restores them immediately before
JMP/RET, and adds no flag-changing instruction after restoration. This new
observation does **not** reveal the original failed runs' unrecorded entry
EFLAGS or establish their cause; those failures remain unresolved. It also
does not prove arbitrary contexts merely because measurement itself now
passes.
The one [verification of this new measured claim](../../local/verify/stock-live-rng-capture/entry-flags-jev-verification.json)
returned support `0.78`, contradiction `0.21`, confidence `0.67` and action
`review`. It is additional bounded evidence, not a rerun or replacement of the
earlier escalated gate.

Every console container used a cooperative lease of CPU 1, host-memory 2 and
VM-memory 2 (512 MiB units), enforced Docker limits of one CPU/one GiB, no
network, a read-only root, bounded tmpfs and a 45-second deadline. There were no
display/audio mounts. Each private writable prefix was an APFS clone of the
**stopped, initialized, same-image** persisted helper prefix, with 1,996-entry
full content/link manifests, distinct regular-file inodes and unchanged seed
checks before/after. The Stock file was mounted read-only and rehashed after.
Exact commands, image ID, prefix fixture and cleanup are in each result. Wine
reported unwritable Fontconfig caches; stderr is retained. No runtime remains
active. This is not a fresh Wine initialization or a Stock gameplay run.

The proof is single-native-thread and finite-capacity. It does not exercise
real getter allocation/failure, arbitrary corruption/refusal/overflow, SEH or
C++ unwind, asynchronous exceptions, all possible FP contexts, AVX/XSAVE state,
game-thread ownership or concurrent state writes. Avoiding caller return-slot
rewriting removes that particular complication; it does not make added C
callbacks and trampoline instruction addresses exception-transparent. A live
exception or missing completion must invalidate the claimed interval, never be
turned into a successful response. No full integration/check.sh claim is made
for this local proof.

## Native-thread lifetime and creation are still missing

The present recorder's `get_lane` permanently assigns a slot by numeric
GetCurrentThreadId alone. The [identifier is unique only until termination](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentthreadid).
Thus native TID, TEB address, CRT-block pointer and first-seen order cannot be
declared stable generation identities. The prototype intentionally retains
`role: unresolved` and does not qualify recycled TIDs.

Stock directly creates workers at `00554e10` (start `00554ea0`), the audio init
path (start `00559bc0`), and `0055c320` (start `0055c370`, which invokes a
function/argument pair from its worker record and eventually calls ExitThread).
These use creation flags zero. The last call writes its returned native TID to
a stack-local `param_1`; a global-only lookup would miss it. Sources:
[worker](../../local/census/all.c#L257057),
[audio](../../local/census/all.c#L261397),
[general worker](../../local/census/all.c#L263466).
[CreateThread may start the child before returning](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread),
so association cannot assume parent-return precedes child RNG or bind solely
by a matching start address. DLL-created threads are outside a main-IAT-only
CreateThread census.

**[PROPOSED, UNQUALIFIED]** Because the recorder is already statically imported,
its process attach can record the initial native lifetime, and its
`DLL_THREAD_ATTACH`/clean detach callbacks can assign monotonic, never-reused
generation tokens from a preallocated table. [Microsoft's callback contract](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain)
supports recording new-thread notifications in the child's context and warns
that dynamically loading a DLL does not notify it about existing threads.
This requires a separate console qualification on the actual packaged runtime;
no callback-to-RNG identity is asserted here. Keep callbacks bounded, with no
file I/O, clocks, worker creation, waits, heap allocation or Stock CRT access.
Do not disable those notifications. A missing attach, repeated active TID,
generation exhaustion or absent termination observation is a gap, not a cue to
reuse a lane. Bind a parent CreateThread event to an observed lifetime only
when the captured returned TID and lifecycle observations establish an
unambiguous relationship; keep unknown parents explicit. Forced termination
and process teardown can omit clean detach notifications and need their own
terminal/invalid status.

## Shared observer interface to implement next

**[PROPOSED]** Keep one recorder owner and one format across all observers.
The current [run adapter](../../tools/parity/run_stock.py#L129) links the recorder
into d3d9.dll; dinput8.dll does not share its static data. Compiling a private
copy into every proxy would create unrelated counters/rings. Expose a small
versioned C interface from the one owner for other proxy consumers, with no
cyclic module loading or resolution from hook/attach paths:

```c
/* Draft interface, not a production header or installed ABI. */
struct tape_token { unsigned slot, sequence, generation; };
int tape_begin(const struct observation_before*, struct tape_token*);
void tape_finish(struct tape_token, const struct observation_after*);
void tape_instant(const struct lifecycle_observation*);
```

`begin`/`finish`/`instant` reserve from the same ring and obtain their phase
ordinal by the same aligned atomic counter. The owner supplies generation,
nesting and bounds checks; callers cannot allocate their own phase numbers.
Callbacks run only with complete CPU state saved; the interface copies bounded
payloads and never calls Stock/Win32 time, RNG, input, a logger or allocator.
An accepted token remains owned until completion/abort publication; the single
outside-hook flusher cannot reclaim an entered record. Slot sequence and
generation prevent completion against a reused slot. Any refusal must become
an explicit loss/gap while leaving original game behavior untouched.

For RNG, `observation_before` contains kind, point
`after_original_TLS_getter`, original caller, saved TLS pointer, pre-state,
seed argument and raw register/control diagnostics. `observation_after`
contains point `after_original_state_update_before_RET`, post-state, raw EAX,
LastError and preserved output diagnostics. Its two phases enclose the state
update, **not the getter or whole function**. Thread attach/exit events are
single-phase records; a future getter-resolution phase is another explicit
kind. The offline schema must count declared phases per event rather than
assert every record has exactly two. A phase orders the atomic observation
inside its callback, not all OS scheduling or memory accesses between events.

Before an early live install, qualify static precompiled trampoline tails,
exact full-body/image guards, both same-page code writes, protection restoration,
instruction-cache publication and refusal-before-mutation in an owned early
console fixture. The current console allocates its executable tails outside
loader lock; that is not an early attach installer. The tracked early
descriptor currently describes IAT mutations only and must explicitly declare
any additional text changes. Reuse its delayed full-file identity gate before
allowing a complete flush. Then qualify actual TLS lazy initialization across
at least two created native lifetimes, creation-before-return ordering,
generation reuse controls and failure/exception limits before a bounded Stock
record. DirectInput consumption, synchronization and response replay remain
separate gaps. No deterministic replay or Parity Proof follows from this note.

The single [targeted Jev gate](../../local/verify/stock-live-rng-capture/jev-gate.json)
escalated, with safe-to-apply probability `0.19`, correctness confidence `0.12`
and test-gap confidence `0`. It verified the shared-ring claim at confidence
`0.88`, exact-span claim at `0.77`, bounded ABI result at `0.62`, and raw/consumed
caller interpretation at `0.26`; the combined limitation/earlier-failure claim
was unsupported at `0.49`. Its supplied evidence did not include the earlier
failure logs, which remain linked above. No concrete defect explanation was
returned and no confidence reroll was made. These judgments require independent
manual review of the cited source and raw results before promoting the
prototype. They do not override its explicit live-TLS, exception, lifetime and
arbitrary-input gaps.
