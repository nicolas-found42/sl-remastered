# Observed Stock API intervals

`run_stock.py --record-controls` records the five main-image Win32 consumers
QPC, QPF, GetTickCount, GetAsyncKeyState and timeGetTime after first Direct3DCreate9.
`--early-controls` implies recording and installs their guarded IAT observers in
the imported proxy's process attach, before the Stock entry executes. Neither
mode controls their values or provides process replay.

The early installer checks the actual PE/import/page/prologue layout and five
already resolved import pointers. It uses bounded integer/data operations with
Kernel32 memory APIs, no file I/O, Crypto provider loading, clock call or worker
creation under the loader lock. The absolute-operand prologue descriptor only
qualifies the preferred main-image mapping at 0x400000; it refuses relocation.
It does not overwrite Stock instructions. It changes one IAT data page and
restores protection; an unsafe restore or refused early plan aborts attachment.
There is no late-start fallback. Host SHA validation precedes launch; full-file
in-process SHA confirmation happens outside attach before any tape can flush.

The API entry/return stubs preserve integer/flags/FX state and LastError. Output
pointer/caller guards record a gap rather than rewrite a return slot for an
unsupported request. Exception/unwind transparency remains unqualified. Existing
raw ABI/FX negative-control evidence and exact frozen sources are retained with
the research artifacts, not generalized into every possible runtime condition.

## RNG observer (format 5, experimental)

`--rng-observer` (requires `--early-controls`) is the only mode that changes Stock
instructions. At attach it overwrites two spans on the already-present page RVA
`0x1d7000`: the 7-byte tail of `srand` at `0x1d7400` and the 9-byte tail of `rand`
at `0x1d740d`. The original `CALL` to the CRT TLS getter at `005dd8d3` is untouched,
so the getter's own initialization/failure behaviour runs first. The original
non-RET tail is then executed from exact static bytes and followed by an
observation and a plain `RET`. Observations record the raw TLS-block pointer, the
LCG state before/after, raw EAX and EFLAGS before/after, sharing the clock phase
ring. Callers other than the two seed and 55 draw direct-CALL sites, a pointer that
is misaligned/unwritable, a block whose first word is not the current thread id, or a
block that changes within a lane produce a visible **gap** record; the original tail
still runs. A gap makes the tape unusable as proof, never a pass.

The offline validator additionally checks, per record: the exact installation row,
the caller allowlist, `state_after = state_before * 0x343fd + 0x269ec3 (mod 2^32)`,
`eax = (state_after >> 16) & 0x7fff`, the EFLAGS bits an `AND EAX,7FFF` must leave
(CF/OF/SF clear, PF/ZF from the result), seed semantics (state = argument, EAX
unchanged = block pointer, flags unchanged), continuity of the state across calls on
one block, and one-to-one lane/block binding. Those relations reject corrupted
records; they do not prove the recorder honest. The compiled format is
`stock-api-interval-offline/3` and carries raw values, not a consumed-value
projection.

**Independent review (fresh reviewer, code only; 2026-11-01).** Fixed and tested: the RNG state chain is
now checked in `seq` order, not file order (the recorder flushes in ring-slot order; a valid reordered
tape was falsely rejected and a broken one falsely accepted); a lane's `seq` order must agree with its
phase order; a format-5 tape with no RNG record is refused as a missing observation; RNG rows must show
unchanged LastError, x87 control word, MXCSR and non-arithmetic EFLAGS bits. Both real tapes (105,927
and 71,648 calls) still validate under the stricter rules. **Documented, not fixed:** (a) the stubs keep
the return target below ESP across four instructions (`-4(%ebx)` / `-40(%esp)`), so a synchronous
dispatch there (single-step with TF set, hardware breakpoint) would jump to garbage; the existing
`return_stub` has the same pattern; asynchronous APCs were judged not a risk. (b) `rng_publish` writes
the two jumps byte-wise, so it is safe only while no other thread can execute the spans; the claim that
no main-image caller runs before attach returns is an assumption, not measured. (c) Lanes are keyed by
numeric thread id and never released, so a recycled id reuses a lane; RNG records fail closed (guard
gap 14), but API records would silently merge two threads. None of these is exercised by the retained
runs.

**Qualified (owned fixture, `local/verify/stock-rng-static-tail/`):** the production
`control_tape_early_bootstrap` on the real mapped Stock bytes published exactly the
two spans (full-image byte audit); 272 accepted and 224 refused calls matched
unhooked execution in every integer register, EFLAGS, LastError and all 512 FXSAVE
bytes across four seeds and four x87/MXCSR configurations; the guard reasons
(out-of-image, indirect, bad thread id) were exact; and omitting FX restoration was
detected in every case. **Not qualified:** the real Stock TLS getter (the fixture
substitutes a private block, so lazy initialization, allocation failure and thread
lifetimes are unexercised), numeric thread-id recycling, exceptions/unwind, a second
concurrent caller, and the unattributed EFLAGS-only difference in the deliberately
broken negative build (retained in `checks.json`). The fixture image is relocated
(Wine occupies `0x400000` in a helper process), so its raw tape is refused by the
strict parser as a relocated image, and a retained rebased copy is used to validate
the remaining rules.

Raw JSONL formats:

| Format | Boundary and ordering |
|---|---|
| 2 | Legacy post-install interval: independent entry and return ordinals; no recoverable combined phase order. |
| 3 | Post-install interval with one shared entry/return phase ordinal. |
| 4 | Shared phases plus one first installation record declaring the pre-entry main-IAT boundary, verified file SHA and observed-globals limitation. |
| 5 | Format 4 plus the optional `--rng-observer` (experimental): two exact Stock CRT code spans are replaced by jumps to observing stubs. The installation record declares `rng_boundary` and both `code_mutations`. `rng_return` rows share the clock records' phase ring. |

All have eight separately copied `initial_global` observations, completed calls
and status records ending in stopped state. Initial observations are **not** an
atomic or restorable checkpoint. Caller RVAs, argument/output shapes, raw u32
return values, LastError, x87 CW and MXCSR are retained. One entry/return ticket
pair identifies each call; format3/4 phase ordinals must form one contiguous
permutation, each return following its entry. These ordinal observation points
are not a trace or control of every intervening OS instruction or shared access.

The validator refuses unknown schemas/keys/APIs/callers, invalid integer widths,
wrong pointer shapes, duplicate/gapped ordinals, unknown initial observations,
nonfinite JSON, incomplete/unclosed tickets, reported losses, missing terminal
status or an unstopped recorder. It limits input to 128 MiB, 500,000 records and 16 KiB
per line, checks read stability and refuses to overwrite inputs/existing outputs
or write the Reference Folder. Raw call arguments referencing validated globals,
stopwatch slots or stack-local shapes are normalized; a file cannot independently
reconstruct the former live TEB bounds. Capture guards remain part of the trust
boundary. Hashes verify consistency, not an honest observer by themselves.

```sh
python3 -I tools/parity/control_interval.py RUN/game/dumps/stock-control-tape.jsonl \
  --output local/verify/NEW-UNUSED-NORMALIZED-INTERVAL.json
python3 -I tools/parity/control_interval.py EXPECTED.jsonl --compare OBSERVED.jsonl
```

Default output is a bounded summary; `--output` retains normalized data.
Comparison retains both input hashes and the exact first differing field.
An offline match is not execution replay or a game Parity Proof. Capture lanes
and native thread IDs are diagnostic identities; stable role/incarnation binding
remains missing. Other outstanding coverage includes DirectInput consumption,
CRT RNG, synchronization/completions, DLL-internal consumers and Port projection.
The independent whole-game proof evaluator does not promote these intervals.

Real evidence:

- `local/verify/tracked-stock-phase-record/`: 71,547 complete calls and 143,094
  shared phases, zero reported gaps/drops/pending calls; normal UI and private PCM.
- `local/verify/tracked-stock-early-record-first/`: early capture correctly failed
  qualification on five previously unobserved startup tick calls. Their records
  and valid UI/audio execution are retained; this attempt is not rewritten.
- `local/verify/tracked-stock-early-record-second/`: 105,927 complete calls and
  211,854 phases, zero gaps/drops/pending, with actual initial engine QPF/QPC,
  worker-tick initialization and playlist ticks. Exact source anchors established
  the two added no-argument callers before permitting a new attempt.
- `local/verify/tracked-stock-rng-record-first/`: first live format-5 record. 71,648
  calls / 143,296 phases, zero gaps/drops/pending, exactly one RNG record: a real
  `srand(35247)` from caller RVA `0x7c7e0` (state before 1, EAX equal to the CRT block
  pointer, flags unchanged), so the real getter's block passed the guard. Stock drew
  no `rand` values on the boot -> menu -> New Career -> Cancel route; draw-side live
  coverage, thread lifetimes and replay remain unexercised.
- `local/verify/stock-rng-static-tail/`: the owned qualification above, with four
  retained earlier attempts (cave off-by-one and an XMM harness-residue defect were
  harness defects, not observer defects).
- `local/verify/parity-workflow-implementation/real-control-mutations.json`:
  one raw epoch bit change produces the exact first difference; removing its
  call or corrupting its phase is rejected. Mutated copies never replace raw data.

`test_control_interval.py` supplies synthetic validation controls only. Thirteen
tests (five cover format 5: 18 corruption cases rejected for their intended reason) include early-identity/header refusal, preserved provenance, legacy phase
uncertainty, bounded parsing and protected output paths. Actual runtime artifacts
identify their precise producer version; source edits do not rewrite old results.
