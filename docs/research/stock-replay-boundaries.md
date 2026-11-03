# Stock replay boundaries and first-divergence contract

2026-11-01. Research and an offline experiment, **not process replay or a Parity
Proof**. Authority: Stock Executable SHA256
`7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`,
with its matching Stock Decompile in `local/census/all.c`. No production code,
Reference Folder file, game process, GUI or audio device was changed or launched
for the initial offline investigation. The follow-up at the end of this note
uses isolated console helpers in bounded containers, without launching Stock.

## What the existing tape can establish

The retained
[`run-aligned-guarded` tape](../../local/verify/stock-control-tape/run-aligned-guarded/game/dumps/stock-control-tape.jsonl)
has SHA256 `5fb5dca84dc279946d0616408574f5a554f21c1982c59ec7b356936618da395e`.
Its 86,788 completed main-image API calls cover the observed interval after
installation at the first `Direct3DCreate9`. The hook explicitly reports earlier
startup as unobserved. Its eight initial-global records are separately copied
observations; they do not represent an atomic, restorable game checkpoint.
Source: [`control_tape.c:37`](../../local/verify/stock-control-tape/source/control_tape.c#L37),
[`control_tape.c:252`](../../local/verify/stock-control-tape/source/control_tape.c#L252),
[`d3d9_proxy.c:365`](../../local/verify/stock-control-tape/source/d3d9_proxy.c#L365),
and the retained [`tape-analysis.json`](../../local/verify/stock-control-tape/run-aligned-guarded/tape-analysis.json).

There are **two independent global orders**, not one phase timeline: `seq` is
allocated on entry, and `return_seq` on return. Exactly 320 actual calls have
different entry and return ordinals. An entry order and a return order cannot
identify whether another lane entered before or after a particular return.
Never interleave the two counters by treating their numeric values as timestamps.
The recorder also flushes completed slots in buffer order, so JSONL line order
cannot recover that information. Sources:
[`control_tape.c:147`](../../local/verify/stock-control-tape/source/control_tape.c#L147),
[`control_tape.c:196`](../../local/verify/stock-control-tape/source/control_tape.c#L196),
[`control_tape.c:296`](../../local/verify/stock-control-tape/source/control_tape.c#L296),
and the tape itself.

The native thread IDs and capture lanes below describe this recording only.
The observed callsite sets are factual; the suggested roles are **[INFERENCE]**
until creation/start/callback provenance binds each thread incarnation.

| Capture lane / native thread | Observed calls | Implementation lead |
|---|---|---|
| 0 / 252 | 84,244 engine/input/stopwatch/movie reads, plus one multimedia initialization read | Candidate main execution lane; record actual entry-thread identity and creation provenance before assigning this role. |
| 1 / 300 | 1,923 `GetTickCount` calls; return RVAs `155278` / `1552bc` | Worker `FUN_00554ea0`, created by `FUN_00554e10`. Its body selects queued file jobs, reads, invokes completion callbacks and updates wait/work timing. [`all.c:257040`](../../local/census/all.c#L257040), [`all.c:257225`](../../local/census/all.c#L257225). |
| 2 / 392 | One `GetTickCount` at `1d4da1`; one `timeGetTime` at `1d150d` | Wait/media initialization candidate. `FUN_005d4d70` includes message pumping and timed waits; a caller address alone does not establish a stable thread role. [`all.c:375655`](../../local/census/all.c#L375655). |
| 3 / 480 | 619 `timeGetTime` reads at `1d17c1` / `1d17d9` | Media pacing candidate: `FUN_005d17a0` stores a time; `FUN_005d17d0` derives an interval and calls the sleep helper. [`all.c:372130`](../../local/census/all.c#L372130). |

One shared ordinal for entry **and** return, thread incarnation records and
explicit synchronization outcomes are needed in a fresh recording. Even that
does not make all intervening game instructions deterministic: shared-memory
access, completion callbacks, message queues and media work can affect execution
between those observation points. Replaying API values alone must retain that
limitation.

## Clocks: preserve domains, existing epochs and integer bits

| Stock boundary | Actual rule and replay requirement |
|---|---|
| `FUN_00551690` / `005516c0` | Initialize frequency at `0076a280` and epoch at `0076a288`; subsequent reads put the counter at `0076a290`, subtract the existing epoch, divide by frequency and multiply by the Stock scale. Capture initialization and every consumption; replacing only later counter reads leaves a different epoch. [`all.c:252748`](../../local/census/all.c#L252748). |
| `FUN_00552310` / `00552330` | Separate frequency at `0076a298` and indexed stopwatch starts at `0076a3a0 + index*8`; elapsed reads use a stack-local counter. Capture the stopwatch index at the caller seam; the API-level output pointer alone identifies an elapsed read's local storage, not which timer index its caller will subtract. [`all.c:253673`](../../local/census/all.c#L253673). |
| `FUN_00556150` / `00556bc0` | Input initialization stores frequency at `0076f9a8` and previous counter at `00777440`; each input pass stores a float elapsed-millisecond value at `0076f9a0`. Mouse normalization and mapping smoothing consume this time. Capture the derived float bits and its consumption boundary as well as raw QPC outputs. [`all.c:258223`](../../local/census/all.c#L258223), [`all.c:258807`](../../local/census/all.c#L258807), [`all.c:259000`](../../local/census/all.c#L259000). |
| `GetTickCount` | Playlist selection also uses this domain and directory enumeration order; a Forced Seed does not control it. Record tick reads at actual consumers, including initialization calls absent from this interval. [`all.c:251990`](../../local/census/all.c#L251990), [`all.c:252024`](../../local/census/all.c#L252024). |
| `timeGetTime` | Main-image reads include media pacing and input force-feedback throttling; imported DLL clocks and wait completion remain outside this IAT tap. `FUN_00555960` / `00555ac0` use the unsigned elapsed value against `0x14`; `005d17d0` multiplies the elapsed milliseconds by 10,000. Preserve each consumer's arithmetic and units. [`all.c:257571`](../../local/census/all.c#L257571), [`all.c:257641`](../../local/census/all.c#L257641), [`all.c:372145`](../../local/census/all.c#L372145). |

Microsoft specifies QPF in counts/second, fixed at boot and common to processors.
QPC has no UTC epoch and is separate from tick count. The captured interval's
37,287 QPF outputs were all `10,000,000`; do not substitute the replay machine's
frequency or normalize the recorded counters without transforming every
dependent state value. Sources: [QPF](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancefrequency),
[QPC timing FAQ](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps),
and the retained tape.

Both [GetTickCount](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount)
and [timeGetTime](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timegettime)
return a 32-bit millisecond count which wraps after about 49.7 days. Store raw
`u32` bits; `(0x10 - 0xfffffff0) mod 2^32 = 32`. Do not reject wrap as backwards
time, widen before subtraction, merge the two domains, or smooth repeated values.
Their resolutions can differ. The offline arithmetic control exercises this
wrap example; the actual retained run is **not** evidence that a wrap occurred.

The eight initial observations omit, among other state, `0076f9a0`, multimedia
objects' prior timestamps, live stopwatch caller indices and the whole world.
Their recorded zero values must remain zero observations, not invented
initialization defaults. Restoring just these words into a separately booted
process is not a qualified checkpoint.

## Input consumption is wider than asynchronous polling

`GetAsyncKeyState` appears in the intro/movie loop at `FUN_0055c470`; Stock tests
Escape's high bit. The recorded main-image return RVAs `15c592` and `15c5f4`
correspond to those checks. They cannot describe menu mouse consumption. Sources:
[`all.c:263567`](../../local/census/all.c#L263567) and the tape caller counts.
Microsoft defines a signed 16-bit result, with the down bit high and the
historical pressed bit unreliable across applications. Preserve the observed
return bits instead of adding another poll or synthesizing a Boolean result.
[GetAsyncKeyState](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate).

`FUN_00556bc0` reads immediate device state through vtable offset `0x24`, with
256 keyboard bytes, 16 mouse bytes or 272 joystick bytes. Acquisition failures
lead to reacquisition/retries or cleared state; joystick polling also affects
this path. `FUN_00556e00` separately uses offset `0x28` for buffered keyboard
events and calls `MapVirtualKeyExA`, `GetKeyboardState` and `ToAsciiEx`. Therefore
a name-dialog test must observe whether this text-consumption path runs and bind
its keyboard layout/state when used; clicking Cancel is not evidence that the
path was never read. Sources:
[`all.c:258807`](../../local/census/all.c#L258807),
[`all.c:258947`](../../local/census/all.c#L258947).

Microsoft distinguishes [immediate GetDeviceState](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417897(v=vs.85))
from [buffered GetDeviceData](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417894(v=vs.85)).
Buffered reads normally remove events; `DIGDD_PEEK` preserves them, null-buffer
reads can still have side effects, and overflow is an explicit result. A replay
record needs device identity/format, method, caller, request size/count/flags,
HRESULT bits, returned count and exact defined bytes, plus acquire/unacquire,
poll and layout changes. A sampled down bit or successful injection is not a
consumed click. Add a downstream ledger at `00556bc0`/`00556e00` and mapping/UI
dispatch that identifies which poll/text event caused the transition. The
existing adapter's poll samples and screenshots are functional flow evidence;
they do not fill the missing API-consumption tape.

## RNG: record every stream, draw and reseed

Stock's seed function `005d73fb` obtains CRT state through `005dd8d3` and writes
offset `+0x14`. `_rand` at `005d7408` updates it with
`state = state*0x343fd + 0x269ec3 (mod 2^32)`, returning bits 16–30. The TLS
initializer `005dd8c0` sets the state to 1; `005dd8d3` obtains/creates it through
`TlsGetValue`/`TlsSetValue`, records the native thread ID and preserves LastError.
These exact Stock functions, rather than current CRT documentation, establish
the ownership rule. Sources:
[`all.c:379185`](../../local/census/all.c#L379185),
[`all.c:387843`](../../local/census/all.c#L387843).

`Math.randomize` (`0047c7d0`) reseeds from elapsed engine time, `0047c7f0` accepts
an explicit seed, and `0047c820` draws, clamps raw 32767 to 32766, then applies
Stock's scale. Native consumers outside Math also call `_rand`, including
object sound. Sources: [`all.c:91991`](../../local/census/all.c#L91991),
[`all.c:75935`](../../local/census/all.c#L75935),
[`all.c:76883`](../../local/census/all.c#L76883).

The new seam belongs around **both** CRT seed/draw functions, recording thread
incarnation/stream identity, caller RVA, pre/post `u32` state, seed argument or raw
draw, plus downstream float bits where used. Thread first-use initializes a new
stream; thread-ID reuse cannot merge it with an old one. Cooperative Script VM
threads are not automatically separate CRT streams: bind their actual native
execution lane. The current tape contains no RNG events and cannot certify that
seeding is complete. `005d73fb`/`005d7408` start with relative calls; relocation,
ABI and failure behavior need isolated helper evidence before an instruction
hook is used in Stock. See the earlier
[disassembly finding](stock-headless-implementation.md).

## Concrete strict replay format for a fresh recording

Proposed format `stock-control-replay/1` uses strict UTF-8 JSONL with no duplicate
or unknown keys, bounded lengths, integer widths and lowercase fixed-width hex
byte fields. The schema version selects an exhaustive tagged union:

| Record | Required fields and validation |
|---|---|
| `header` (exactly first) | `format`, Stock binary SHA256, content/save/config manifests, instrumentation bundle SHA256, pointer/endianness/ABI declaration, declared start/stop boundary, clock domains, lane-binding policy, and the explicit covered/uncovered capability list. |
| `lane_start` / `lane_end` | One shared `ordinal`, stable role plus generation, diagnostic native ID, creator lane, Stock start/callback RVA and creation ordinal. Bind from observed provenance; reject duplicate or unresolved bindings for a claimed complete replay. |
| `initial_state` | Checkpoint identifier, exact byte digests and state version, with an explicit atomicity/restoration contract. An `observed_globals` tag may retain the current eight observations but cannot satisfy a checkpoint requirement. |
| `call_enter` | Shared `ordinal`, `ticket`, lane/generation, per-lane call number, API/method identity, caller RVA, typed request arguments, logical device/stream identity and defined input bytes, LastError and x87/MXCSR before. No absolute pointer identity: classify validated global RVA, stopwatch slot or bounded stack-local shape. Unknown shape is a hard gap. |
| `call_return` | Shared `ordinal`, matching ticket, raw return register/HRESULT bits, LastError, output validity and exact defined output bytes/count, post x87/MXCSR and RNG state when relevant. Failed output calls leave the caller's buffer unchanged; do not write zero output just because no successful output was captured. |
| `boundary` | Shared `ordinal`, phase/caller, engine/input/frame/VM dispatch number as applicable, clock/derived float bits, runnable or completion order, consumed stimulus identifiers, and hashes/records of the state being compared. |
| `footer` (exactly last) | Counts/digests, closed ticket/lane sets, no gaps/drops/overflow/pending work, stopped state and declared end-boundary observation. Premature EOF and surplus records fail. |

Use one shared phase counter, while retaining per-lane call numbers and ticket
pairing. All phase ordinals must be contiguous and each return must follow its
own entry. Buffer exhaustion and unmatched calls fail; no searching ahead for a
convenient match. A lane arriving before its recorded turn may wait under a
bounded, declared scheduler policy; a different call on that lane is an immediate
divergence. An exhausted tape cannot fall through to a live nondeterministic API.

The first divergence must name the expected phase/ticket/lane/callsite, observed
request or output, previous successful boundary, remaining lane positions and
timeout reason if blocked. Distinguish request mismatch, wrong output, wrong
lane/order, missing event, surplus event, invalid capture and unavailable
observation. Report exact integer/byte differences before any permitted
field-specific float tolerance. Preserve the failing tape and recent bounded
context; never silently resynchronize.

This proposed schema is not implemented by relabeling the old tape. The old
interval can be compiled only into a narrower format which explicitly lacks
the missing phase timeline, stable roles and complete start state.

## Implemented offline experiment

[`tape_match.py`](../../local/verify/stock-replay-boundaries/tape_match.py)
implements `stock-api-interval-offline/1` for the existing format-2 tape. It
checks exact fields and integer widths, eight unique initial observations,
known API/caller pairs, validated global/slot pointer shapes, complete entry and
return permutations, ordered lane reads, consistent native-thread/lane identity,
defined output validity and a lossless stopped footer. It refuses same-lane
return inversion because nested-call provenance is absent. Stack-local addresses
are normalized using the recorder's declared guard shape; the offline file
cannot independently revalidate the historical stack or memory protections.

Comparison preserves request/reply bytes, initial observations and both orders;
it returns the first differing field. Raw native IDs and absolute stack
addresses are diagnostic and not replay identities. Numeric capture lanes remain
unresolved and may be compared only under the explicit same-binding assumption.
The helper always reports `process_replay_qualified: false`.

Run `python3 local/verify/stock-replay-boundaries/check.py`. Retained
[`checks.json`](../../local/verify/stock-replay-boundaries/checks.json)
records **13 passing controls** against all 86,788 calls: self-comparison; nine
invalid-capture mutations; a one-bit QPC output corruption; a wrong lane binding;
and source-derived wrap/RNG arithmetic cases. The QPC corruption is identified
at `$.calls[1].reply.output_u32[0]`, expected `849126068`, observed `849126069`.
Seed 1's model draws are `41,18467,6334`; these arithmetic cases are not captured
Stock RNG execution. Self-comparison is a comparator control, not same-side
repeatability. No full integration/build gate is claimed for this research-only
addition.

## Smallest valid next experiments and Port seams

1. **Qualify response replay in an isolated console helper first.** Extend the
   existing ABI helper with one declared single-thread API request sequence;
   record raw real API results, then replay the replies without consulting those
   APIs. Compare immediate return snapshots and buffers, preserving failure
   buffer contents and LastError. Add changed caller/argument, missing/surplus
   call, failed output, wrap bits and FX corruption controls. Then add two
   explicitly created lanes, shared phase ordinals and a deliberately reordered
   completion. This can validate the mechanism without asserting menu replay.
2. **Record a fresh controlled Stock boot.** Install before every initialization
   claimed in scope, outside the loader-lock-sensitive path, with verified
   installation provenance. The existing first-D3D install cannot cover the
   earlier engine epoch initialization. Alternatively, establish a complete
   restorable checkpoint; the eight copied globals cannot supply one. Include
   immediate/buffered input, CRT TLS streams, worker creation/completions,
   message/wait/media outcomes and filesystem enumeration. Keep normal intro,
   rendering and audio processing. Prove two independent same-side executions
   agree at declared state/input/time boundaries before comparing with the Port.
3. **Use the Port's existing dependency seams.**
   [`VmArena::performanceMilliseconds`](../../src/vm.hpp#L131), assigned from a
   steady clock in [`startup_probe.cpp:209`](../../tools/startup_probe.cpp#L209),
   supplies System time, randomize and video consumers; it must be keyed by
   semantic callsite/boundary, not one cached value per rendered frame.
   [`PlatformInput::beginFrame`](../../src/platform.cpp#L145) has its own SDL
   nanosecond interval; [`sample`](../../src/platform.cpp#L505),
   [`nextKey`](../../src/platform.cpp#L471) and
   [`keyHeld`](../../src/platform.cpp#L552) expose different consumption
   semantics. Add recorded consumption IDs and supply the input-pass delta
   explicitly. Do not equate movie Escape polls, immediate device state and
   buffered text events.
4. **Keep every Port clock/stream honest.**
   [`startup_probe.cpp:2055`](../../tools/startup_probe.cpp#L2055) advances input
   and timers, while [`startup_probe.cpp:2291`](../../tools/startup_probe.cpp#L2291)
   derives the next frame interval from elapsed execution. Supply the observed
   Stock interval at its matched phase and retain subdivision/dispatch ledgers;
   no new fixed timestep is justified here.
   [`MusicPlayer::TickSource`](../../src/music.cpp#L212) already accepts a tick
   dependency, separate from QPC time; playlist enumeration and backend completion
   are additional inputs. [`mathRandom`](../../src/natives/random.cpp#L41),
   [`mathRandomize`](../../src/natives/random.cpp#L10) and
   [`ObjectSoundState::randRaw`](../../src/object_sound.cpp#L28) need a shared
   stream/draw ledger consistent with proven Stock TLS ownership. The startup
   probe currently passes the same `mainRandom.state` to object sound
   ([line 400](../../tools/startup_probe.cpp#L400)); this does not prove all Stock
   native threads share that stream. A Forced Seed remains insufficient.

Targeted [Jev verification](../../local/verify/stock-replay-boundaries/jev-verify.json)
reported support `0.88` for the missing total phase timeline, `0.99` for the
non-atomic initial observations, `0.98` for Stock TLS RNG ownership and `0.99` for
the insufficiency of this tape for whole-menu replay. Its offline-check summary
had support `0.83`, confidence `0.74`, action `review`; that uncertainty is
retained for parent/manual review despite the explicit successful local output.
No retry was made to improve the score. No missing observation is promoted to a
verified whole-menu/dialog repeat.

## Follow-up: shared phase ordinal in an owned prototype copy

The owned copy at
[`phase-source/control_tape.c`](../../local/verify/stock-replay-boundaries/phase-source/control_tape.c)
adds one shared `InterlockedIncrement(&next_phase)` at the entry and return
observation points. It retains the separate `seq` and `return_seq` fields, all
pointer/caller guards, and the exact assembly ABI/alignment wrappers. The
counter is a naturally aligned static `LONG`; Microsoft's
[InterlockedIncrement contract](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-interlockedincrement)
defines an atomic 32-bit increment returning the incremented value and requires
32-bit alignment. The [compiled symbol/disassembly](../../local/verify/stock-replay-boundaries/phase-atomic-disassembly.txt)
retains the aligned counter symbol and entry/return atomic instructions. The
[small source patch](../../local/verify/stock-replay-boundaries/phase-source.patch)
also adds `entry_phase` / `return_phase` to each call and `phases` to format-3
status records. Counter exhaustion increments the existing drop count; a stopped
capture requires a positive contiguous permutation `1..2*completed_calls` within
the signed 31-bit range. A guarded refusal remains an explicit gap with no
observed return; it cannot qualify as a complete phase capture.

The original prototype sources and old format-2 game capture remain unchanged.
The [original hashes](../../local/verify/stock-replay-boundaries/phase-source-originals.json)
were rechecked; [final source/binary hashes](../../local/verify/stock-replay-boundaries/phase-provenance-final.json)
identify the new helper builds. No Stock game run using format 3 has occurred in
this subtask.

`tape_match.py` now accepts two explicit schemas: format 2 still compiles to
`stock-api-interval-offline/1`, with no invented phase order; format 3 compiles to
`stock-api-interval-offline/2`, adding an ordered `phase_order` array.
[`phase_order.py`](../../local/verify/stock-replay-boundaries/phase_order.py)
rejects duplicate/missing/zero/reversed phase ordinals, wrong/exhausted counts and
incorrect per-lane call nesting. It checks each return follows its own entry.
The separate entry counter and phase counter are different atomic observation
points: cross-lane preemption may rank them differently. The checker deliberately
does not invent a constraint equating those rankings.

Two console-only containers ran through the cooperative resource pool with one
CPU permit, one host-memory unit and one VM-memory unit; Docker enforced one CPU,
512 MiB, a 128-process cap, no network, a read-only root and a private writable
prefix/output directory. There were no display/audio/network mounts or game
files. The host runner had an 85-second deadline and unconditional container
cleanup. Runs completed in 12.141 and 11.761 seconds; exact invocations, Wine
diagnostics, exits and cleanup are retained in
[`phase-helper-runtime`](../../local/verify/stock-replay-boundaries/phase-helper-runtime)
and [`phase-interleave-runtime`](../../local/verify/stock-replay-boundaries/phase-interleave-runtime).
Wine initialization attempted unavailable GUI services and logged those failures;
the actual test programs are console helpers with no display server.

- The existing ABI helper passed all 640 immediate-return snapshots (560 bytes
  each), with zero failures and both C-entry stack alignments at 12 modulo 16.
  Its tape has 1,280 contiguous phases and zero gaps/drops/pending/active calls.
- The unchanged guard helper passed all six cases. Unknown caller, invalid
  pointer and inaccessible output still leave the original return slot intact.
  Its three explicit refusal gaps remain invalid for complete capture.
- A separate helper build which removes FX restoration exited 1 and detected an
  immediate FX mismatch for all five API kinds. Its printed `failures=6` includes
  the additional incomplete-case-count assertion; this is intentional fault
  evidence, not 640 successful cases.
- The new [two-thread helper](../../local/verify/stock-replay-boundaries/phase-source/interleave_probe.c)
  forces `enter A -> enter B -> return B -> return A` using private Win32 events.
  All 128 rounds passed: 256 calls and 512 shared phases, with zero reported
  failures or tape losses. The independent phase checker verifies the complete
  four-phase order for every round.
- [`check_phase.py`](../../local/verify/stock-replay-boundaries/check_phase.py)
  passed 13 controls, including eight malformed-phase cases, synthetic overlap,
  distinct-counter ordering, legacy-format preservation and the two actual
  helper tapes. The synthetic fixture is named as synthetic and was never
  represented as a new Stock record. The original 13 offline controls were also
  rerun successfully against the immutable 86,788-call tape. Results:
  [`phase-checks.json`](../../local/verify/stock-replay-boundaries/phase-checks.json)
  and [`checks.json`](../../local/verify/stock-replay-boundaries/checks.json).

This makes the **hook's observed entry/return order** recoverable for new
format-3 captures. The ordinal names one atomic counter operation inside each
callback. It does not record all OS scheduling, the exact API start/end instant,
intervening shared-memory accesses, or an atomic snapshot of every output and
global. Output bytes are still copied after the return observation point; a
concurrent writer can matter between those actions. Thread roles, startup,
RNG/input coverage and exceptions/unwind retain their previous limits. A fresh
bounded Stock format-3 capture is the next evidence step, after the parent's
tracked adapter qualification releases its runtime slot; it has not been
silently substituted for the existing format-2 evidence.

The [single follow-up Jev gate](../../local/verify/stock-replay-boundaries/phase-jev-gate.json)
escalated: `safe_to_apply=0.44`, composite `0.789`, correctness confidence `0.38`
and test-gap confidence `0.17`. It marked the source-extension claim unsupported
(`0.89`), the phase-checker claim verified with confidence `0.50`, the console
ABI/guard/fault claim verified with confidence `0.82`, and the two-thread result
verified with confidence `0.62`. It supplied no concrete defect description.
Those uncertainties remain for parent/manual review; the retained source diff
and raw outputs are inspectable, and no retry was made to improve the judgment.
This local mechanism is not promoted to production or whole-process replay.
