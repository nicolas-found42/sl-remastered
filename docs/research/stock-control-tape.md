# Stock control tape: record-first experiment

2026-11-01. The authority is the 2005 Stock Executable, MD5
`e686099e7233bb7d7c461b1b35c5064c`, SHA256
`7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`.
This is a private experiment, **not a deterministic replay implementation or a
Parity result**. No production file or game/reference file is changed. The tap
changes declared in-process import pointers in a disposable private game copy.

## Implemented record seam

Reusable C lives in `local/verify/stock-control-tape/source/control_tape.c` and
`control_tape.h`. Its small interface installs the tap, flushes completed events,
and stops recording before a final flush. A copied ARM64 adapter in that same
directory links the C into its private D3D proxy. The original adapter's source
is not modified.

The main-image import slot mapping comes from the immutable binary's objdump
output in `local/verify/stock-headless-20261101/pe-imports.txt`:

| Import | Preferred VA | RVA |
|---|---|---|
| QueryPerformanceCounter | `005e7048` | `001e7048` |
| QueryPerformanceFrequency | `005e7050` | `001e7050` |
| GetTickCount | `005e7058` | `001e7058` |
| GetAsyncKeyState | `005e725c` | `001e725c` |
| timeGetTime | `005e72cc` | `001e72cc` |

Microsoft's PE specification says the loader fills import address table entries
with imported-symbol addresses. The prototype checks the complete on-disk SHA256,
PE32/i386 headers, preferred base and image size, then verifies every original
slot against that DLL's `GetProcAddress` result before changing any pointer.
It acquires all page protections first, uses atomic pointer writes, and restores
the original page protections. It never overwrites executable instructions.
[PE import address table](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#import-address-table)

Each x86 wrapper saves flags, all general registers and an aligned 512-byte
FXSAVE area, clears DF for C, records entry, restores the original state, and
tail-jumps to the original API. A per-thread return-address shim records the
original API's return register, LastError, successful QPC/QPF output, and
pre/post x87 control word and MXCSR, then restores its exact return CPU/FX state.
The test-only TEB LastError access is explicitly x86 `fs:0x34`; it is not a
portable public-API abstraction. Exceptions/unwind through these wrappers are
unsupported and must be resolved before production promotion.

QPC/QPF return an eight-byte output separately from their Boolean return; the tap
records both. GetAsyncKeyState's meaningful return is the low 16 bits of EAX:
the high bit indicates down and the low bit is unreliable under preemptive
multitasking. The tap preserves and records the full original EAX instead of
normalizing it to Boolean down/up. Neither API is called an extra time by the
hook to infer state. [QPC contract](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter),
[GetAsyncKeyState contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate)

The tap performs no file I/O in API hooks. It uses a bounded 65,536-event buffer,
128 diagnostic thread lanes and depth 16. It records entry and return ordinals
separately; a flush can see completed calls out of entry order. The analyzer
sorts by entry ordinal and checks loss, duplicate/missing ordinals, per-lane read
continuity and final stopped state. A full buffer, lane/depth exhaustion or
counter exhaustion increments a drop count and invalidates lossless capture.
A separate existing proxy command thread flushes periodically. `tape_stop`
disables new observations, waits boundedly for in-flight API calls and flushes
before the adapter's scoped Wine shutdown. Recording deliberately ends there.

## ABI validation and actual Stock evidence

`source/abi_probe.c` supplies pure console callees with stdcall zero/one-argument
cleanup, deterministic return values, flags, LastError, x87 stack and SSE state.
An assembly harness captures the immediate return state before compiler code.
The initial ARM64 Hangover11.16 run reports
`abi_cases=640 failures=0 flush=640 snapshot_bytes=560` in `abi-probe.log`.
This checks hook ABI transparency for those helper cases, not game determinism.
The helper has no display, speaker, HID or network connection. Further stress
validation and the actual Stock record-only result are appended below.

The strengthened helper deliberately dirties x87 state, MXCSR and XMM7 inside
both C observation callbacks, so saving an already unchanged state cannot create
a false positive. `abi-stress.log` again reports 640 cases and zero failures.
A separately generated fault variant replaces both FXRSTOR instructions with
NOP; `abi-fault.log` detects an immediate FX mismatch in all five API variants
and exits 1. `abi-stress-verdict.txt` records `stress_exit=0 fault_exit=1`.
The fault variant is only an isolated console helper; it is never loaded into
Stock. General-register/flags and LastError comparisons are included in the
560-byte snapshot; unused/padding fields are initialized before each comparison.

The full adapter retains both intro movies, native ARM64 media/graphics work,
private Xvfb, a private Pulse null sink and real captured PCM. It uses the
existing synthetic CD table (which cannot certify real CD-track audio).
The initial scenario attempts NEW CAREER and Cancel using the existing guest
input proxies and wall-clock schedule. Actual screenshot/OCR milestones decide
whether those actions succeeded; observing a returned mouse-down bit alone
does not prove the name dialog appeared.

The actual captured run **passed** the copied adapter's full scenario gate:
`local/verify/stock-control-tape/run-first-record/result.json` records
`name_dialog=true`, all three final menu labels, 1,472 DirectInput polls, two
returned mouse-down observations, zero injection-queue drops, nonzero PCM and
`source_preserved=true`. Container time was 82.190 seconds. The WAV contains
3,026,387 stereo s16 frames at 48,000 Hz; 4,884,105 channel samples are nonzero,
peak 32,768, RMS 4,279.183. These measurements establish real signal capture,
not waveform or complete CD-audio Parity.

`frame-12.png` shows the actual Activision movie, `frame-42.png` shows the actual
Enter Player Name dialog and Cancel control, and `root.png` shows the menu after
Cancel. The same directory retains all capture timestamps, injected-command
timestamps, Quartz trace, PCM and proxy logs. I inspected the movie and name
dialog frames directly as well as the automatic result. The canonical adapter's
fixed wall-clock action schedule is known to be intermittent in other runs;
this success does not establish it as a robust or deterministic scenario driver.

The stopped `game/dumps/stock-control-tape.jsonl` contains 140,082 API-return
events. `tape-analysis.json` passes entry/return count, complete entry sequence,
per-lane read continuity and final `accepting=0, active=0, pending=0, dropped=0`.
The final stop is also recorded by `d3d9proxy.log`. This is a lossless interval
of the observed seams, from installation to explicit stop, with earlier startup
and other domains excluded as declared below.

| API | Recorded calls |
|---|---:|
| QueryPerformanceCounter | 72,700 |
| QueryPerformanceFrequency | 60,490 |
| GetTickCount | 3,116 |
| GetAsyncKeyState | 2,566 |
| timeGetTime | 1,210 |

Four diagnostic lanes contain 135,757, 3,115, 2 and 1,208 returned calls. These
counts demonstrate that a single main-thread read stream would miss observed
native-thread consumers. The ledger does not yet assign stable roles to them.

Reproduce the validated tape analysis with:

```sh
python3 -I local/verify/stock-control-tape/analyze.py \
  local/verify/stock-control-tape/run-first-record/game/dumps/stock-control-tape.jsonl
```

Repeat the actual full-intro recording with a fresh output directory, after
coordinating a single 3 GiB container slot with other experiments:

```sh
python3 -I local/verify/stock-control-tape/run.py \
  --source '.scratch/2005-extract/Program Files/Street Legal Racing - Redline' \
  --stock ref/raw/StreetLegal_Redline.bak.exe \
  --texture-table ref/raw/texture.dat \
  --disc local/wine-runs/synthetic-run/stock-2005/disc \
  --out local/verify/stock-control-tape/NEW-UNUSED-OUTPUT
```

All interventions: disposable source clone and fresh prefix; synthetic CD table;
guest-only D3D/DirectInput proxy DLLs and input command worker; five declared
main-image IAT pointer replacements after first Direct3DCreate9; bounded tape
buffer and flushes from that existing worker; explicit tape stop after the final
capture; scoped Wine shutdown. No intro/audio bypass, fixed clock, RNG override,
physics patch, host focus change or host audio connection was used. The exact
Docker argv and runtime image digest are retained in the run's `result.json`.

## What this tape does not control

Installation currently occurs on first `Direct3DCreate9`, outside loader lock.
Startup clock/RNG/input consumers before that point are unobserved. The tape
observes only these five main-image IAT slots; DLL-internal clocks, dynamic API
lookup, media clocks, filesystem enumeration and native scheduling remain
uncontrolled. It adds observation cost and can change the original schedule.

Diagnostic lane IDs and native thread IDs are **not stable worker roles**.
Thread creation start routines/creator sites and an actual role/lifetime ledger
are needed before replay can pair RNG TLS or clock streams across runs. Native
IDs can be recycled. A caller RVA plus per-lane read ordinal is useful evidence,
not a proven semantic identity.

GetAsyncKeyState output is consumed at that API boundary. The existing DirectInput
proxy separately logs injected event delivery and polled down bits; this tape
does not yet record complete returned GetDeviceState bytes or GetDeviceData
arrays/capacity/peek/overflow. Delivery to a queue, return to Stock, and Stock's
later logical input sample are distinct. A replay must record all three layers
and replace both polled state and buffered events coherently.

## Minimal next replay design

1. First record the actual full scenario schedule, then identify input and
   physics boundaries from its own consumption ledger. Preserve outer Stock
   subdivision and the actual float dt bits at `00428450`/`00428790` and
   `004a5190`. Do not invent 60 Hz or advance time on each intercepted read.
   The shared Stock frame delta is at `0063c534`; exact semantics remain subject
   to the decompile and live ledger in the implementation research note.
2. Add complete DirectInput return records plus the logical sample boundary
   `0054dda0` → `00556bc0` and counter `007686ec`. Record which logical sample
   consumed each returned state/event, and mark pending/peek/overflow distinctly.
3. Establish stable native worker roles. Replay consumes recorded outputs by
   role, API/caller and local read ordinal, rejecting any mismatch. Global
   completion order cannot simply replace a role-specific clock stream; forcing
   it without a scheduler design can deadlock independent threads.
4. Record CRT seed/rand at `005d73fb`/`005d7408`, TLS ownership from `005dd8d3`,
   state before/after and every caller/reseed. Both entries start with relative
   CALL; a trampoline must relocate that CALL to preserve its target. Never copy
   those five bytes blindly. The Stock agent owns the separate isolated proof.
5. Only enable replay after the recording closes without gaps. Replace the
   recorded API result/output/LastError at the original consumption boundary,
   leave original world/media/renderer work active, and reject an exhausted or
   mismatched tape. Freeze/fixed-delta modes are separate interventions, not
   implicit substitutes for recording the real schedule.
6. Require actual same-side Stock A/Stock B replay agreement at inputs, seed/RNG,
   dt/substeps, milestones and selected state before comparing the Port.
   A helper pass, identical injected commands or menu screenshots cannot establish
   deterministic simulation or Stock/Port Parity.

## Research handling

The bounded `jg` search used the explicit adapter root and returned locations
only with a request-limit incomplete notice. Focused local source/disassembly
reads supplied the details; missing search results were not treated as absence.
Microsoft primary PE/API pages were fetched and Jev-screened `pass`. Jev verified
the narrow IAT/QPC/async-key contracts with no unsupported/contradicted claims.
No sensitive credentials or game binary/source were sent to those web tools.
The parent coordinates the full `tools/check.sh` gate and production promotion;
this experiment does not claim that repository-wide gate has passed.

The required final Jev gate did **not** accept prototype promotion. It returned
`escalate` with `safe_to_apply=0.05` and correctness confidence 0.22, without a
specific defect explanation. Its three measured-result claims were labelled
verified but below automatic-confidence thresholds (0.70, 0.64, 0.24). This is
an unresolved independent-review result, not permission to rerun the judgment
until it passes. The parent was notified to review before promotion. The local
prototype and raw evidence remain available; no production hook was added.

## Follow-up: callback stack alignment and guarded output pointers

The parent's independent source review identified that the first wrappers did
not promise a 16-byte aligned C call site. Their three/two pushed arguments gave
callback entry ESP modulo16 of 0/4. The installed i686 MinGW GCC16.2 target's
`config/i386/cygming.h:30–33` overrides the generic x86 preferred stack boundary:
32-bit defaults to `MIN_STACK_BOUNDARY`, and `i386.h:781–782` defines that as
32 bits. Thus the original layout satisfies this compiler's four-byte default;
it must not be generalized to a compiler/function expecting 16-byte alignment.
Exact installed compiler/version/target evidence is retained in `gcc-version.txt`,
`gcc-target.txt` and `gcc-stack-defaults.txt` beside the prototype.

The revised entry stub reserves four padding bytes before its three arguments;
the return stub reserves eight before its two arguments. Both clean up16 bytes,
restoring the aligned FXSAVE base exactly. The C call site is now16-byte aligned,
and callback entry ESP modulo16 is12 after CALL pushes the return address.
Test-only naked callback front ends measure that value before any compiler
prologue and perform an actual MOVAPS spill/reload at aligned ESP−28.

`abi-aligned.log` records640 entry and640 return checks, zero alignment failures,
entry/return modulo16 both12, and640 matching560-byte return-state snapshots.
`abi-old-alignment.log` is a deliberately generated old-layout control: it
records640 failures on each side, modulo16 values0/4, and exits1 despite matching
return-state snapshots. This proves the new alignment check detects something
the original snapshot-only test did not. `alignment-verdict.txt` records
`aligned_exit=0 old_alignment_exit=1 guard_exit=0`.

The actual first tape's QPC/QPF argument/caller pairs were inspected in
`run-first-record/clock-output-pointer-analysis.json`. They include both main-image
globals and current-thread stack locals, so treating every successful output as
a global would be incorrect. The revised tap only rewrites a return address for
verified Stock callers with a recognized output shape and currently committed,
readable/writable, non-guarded output bytes. The allowlist uses loaded-base-plus-RVA:

| Consumer | Caller RVA(s) | Required output |
|---|---|---|
| Engine frequency | `15169b`, `1516cb` | image RVA `36a280`,8bytes |
| Engine epoch/current QPC | `1516a6`, `1516d6` | image RVA `36a288`/`36a290`,8bytes |
| Stopwatch frequency | `15231b` | image RVA `36a298`,8bytes |
| Stopwatch start | `15232d` | slots at `36a3a0 + 8*i`, literal indices0,1,2,3,4,7,8,11,14 |
| Stopwatch elapsed | `15233e` | exactly API-entry ESP+8,8bytes inside current TEB stack bounds |
| Input frequency | `156531`, `156bcf` | image RVA `36f9a8`,8bytes |
| Input initial previous counter | `15653c` | image RVA `377440`,8bytes |
| Input current counter | `156bda` | exactly API-entry ESP+12,8bytes inside current TEB stack bounds |

The pointer offsets and engine/input globals are proved by retained immutable
binary disassembly (`clock-guard-disassembly.txt`) and saved `local/census/all.c`
at252744–252777,253674–253700,258227–258228,258806–258833. The stopwatch indices
come from exact literal calls to `FUN_00552310` in that decompile; unfamiliar
indices remain gaps rather than an invented array bound. Non-output APIs also
use the actual captured caller allowlist. An unfamiliar caller, unexpected
pointer shape or inaccessible output produces a `gap` event (reason1/2/3), with
the original API return slot untouched. Any gap invalidates a complete tape.

`guard-probe.log` verifies six isolated cases: known global, stopwatch local and
input local are observed; unknown caller, invalid pointer and PAGE_NOACCESS
output preserve the original return address as seen by the original helper
callee. Its final tape contains three observed returns and three explicit gaps.
The helper callee intentionally does not dereference the invalid pointer; this
tests the hook's refusal and original return address, not Windows exception
dispatch. **Generic SEH/unwind transparency is not claimed.** Unexpected API
exceptions through an accepted return shim remain unsupported. The narrow
guards reduce this experiment's scope; they do not prove generic interception
safe or remove memory-protection races in arbitrary applications.

At installation the revised prototype snapshots existing bytes, without asking
QPC/QPF for new values: engine frequency/epoch/last counter (`36a280/288/290`),
stopwatch frequency/slots (`36a298`, `36a3a0`), input frequency/previous counter
(`36f9a8`, `377440`) and frame-delta float bits (`23c534`). These eight metadata
records retain validity, loaded image base, RVA, byte count and exact words.
They are observations at install time, not an atomic initial world snapshot.
Startup calls preceding installation are still unobserved.

The strengthened actual-game run uses the current canonical adapter's
observed-state XTEST action path, copied into this private prototype. It records
an immutable `instrumentation-source` bundle in its output before compiling.
Its result is appended after the bounded full-intro run finishes. No broad Jev
score was rerun to replace the earlier escalation; parent manual review remains
the promotion decision.

That strengthened run **passed** in72.860s at
`local/verify/stock-control-tape/run-aligned-guarded/result.json`: source preserved,
exactly New Career then Cancel through private XTEST, observed name dialog,
observed menu return,886 sampled input polls, native mouse down/up observations
2/3 and zero injection-queue drops. PCM has2,575,553 stereo s16 frames at48kHz,
3,720,966 nonzero channel samples, peak32,768 and RMS4,099.468. I directly inspected
`state-034-await-name-dialog.png` and `state-037-await-menu-return.png`; they show
the name dialog and the menu after Cancel, respectively. The flow records actual
checkpoint and action timestamps. This still proves neither deterministic replay
nor waveform Parity.

The revised tape contains86,788 API returns (QPC44,436; QPF37,287; tick1,924;
async-key2,520; multimedia-time621). The final status has zero gaps, drops,
pending and active calls, accepting0, with matching complete entry/return and
per-lane ordinals. All eight initial-global records are valid. At install,
engine frequency was10,000,000 and existing epoch/last-counter were843,914,937 /
844,717,034. Stopwatch/input initialization fields and frame-delta bits were
still zero at that instant; that is recorded state, not a replacement value.
`tape-analysis.json` passes the strengthened analyzer, which now requires the
eight valid initial records for format2 and rejects any explicit guard gap.

The actual DLL was compiled from the retained
`run-aligned-guarded/instrumentation-source` snapshot; the corresponding hash
manifest is `instrumentation-source.json`. This avoids attributing later source
edits to an earlier binary. The complete run uses image digest
`sha256:c3105bb5c0fe829f89f6f782e79d7bfdb31708209ee4456e3ab4be5e7f94d88f`,
CPU limit4 and memory limit3GiB, with the same no-network/no-host-device isolation
and cleanup. Its container is removed. New narrow Jev verification accepted the
alignment logs, guard logs and reported adapter flags (support0.97/0.98/1.00);
this verifies those reported facts and does not override the unresolved broad
prototype review or authorize production promotion.
