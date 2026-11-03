# Owned input interception qualification

`CONTROL_TAPE_INPUT` is an experimental, **owned-console-only** recorder build.
`run_stock.py` does not enable it. No actual Stock input tape, device lifetime,
input replay, Port consumption projection or paired Parity is qualified here.
The proposed `input_interval.py` format is still separate and has no producer.

The recorder owns the exported versioned table in `input_observer.h`.
`input_bind.c` binds an already-loaded owner outside loader lock, checks table
and function origins, and refuses an absent or unready owner. It never loads
either proxy or creates its own phase source. The host must still verify both
producer files: the ABI identity is not a cryptographic identity check.

The owned configuration installs saved-target wrappers for device vtable slots
9 (state), 7 (acquire) and 10 (buffered data). Each vtable has dedicated original
target cells, so two registered devices sharing a vtable retain their identities;
an unregistered alias still forwards to the correct original and emits a Gap.
Configuration and explicit retirement require quiescent caller-owned objects.
The state-only API does not observe COM construction, QueryInterface or Release.
The separate optional COM table below adds a bounded owned lifecycle experiment.

State recording admits keyboard256/mouse16 and snapshots their actual bounded
pre/post bytes. Current format7 buffered recording admits keyboard20/count1..16/
flags0 and retains both raw counts and the full caller buffer at each boundary,
including on negative HRESULTs. Historical format6 captures admit count1 only.
No extra device read, injection, zero-on-error replacement, file I/O or DLL
discovery occurs in these callbacks. Snapshots establish changed bytes, not a
write mask. Method responses use raw HRESULT bits rather than success booleans.

Input calls share the clock/RNG recorder's sequence, return, lane, phase,
nesting and active-call counters. They use a bounded 4096-slot payload ring;
overflow, unsupported shapes and identity failures remain dropped/Gap records.
Stopping drains the same active-call count. Format6 introduced owned
`input_return` and `input_observer_status` records; format7 adds the explicit
buffer width described below. Owned captures cannot silently pass as a Stock
proof; the separate mixed-input inventory is also not a proof evaluator.

## Commands and retained evidence

On this checkout, with the previously qualified, stopped persisted fixture:

```sh
python3 -I tools/parity/stock/qualify_input.py \
  --out local/verify/NEW-UNUSED-OWNED-INPUT-OUTPUT \
  --prefix-seed local/verify/stock-early-capture/run-owned-early-recorder-persisted/prefix
python3 -I tools/parity/stock/check_input.py local/verify/NEW-UNUSED-OWNED-INPUT-OUTPUT
```

The runner checks image/fixture identity and stopped-container exposure, clones
the prefix without hardlinks, checks its complete manifest/private inodes, joins
the shared pool and freezes exact sources and commands. The container has no
network, host display/input/audio socket or device; one CPU, 1 GiB, 128 processes,
a read-only root and deadlines bound the console probes. No game, actual input
device, window, audio sink or host Wine process is started. Prefix reuse is warm,
not a fresh initialization. Original seed preservation and container removal
are retained. The runner's `execution_finished` is not the independent audit.

`local/verify/stock-input-observer-owned-20261102/run-cross-dll/` retains:

- 64 accepted and 64 refusal-path outer calls matching the unhooked targets in
  all integer registers, stack cleanup, EFLAGS, LastError, 512 FX bytes and output
  buffers/counts. Targets have explicitly controlled entry/return flags, four
  x87/MXCSR settings, buffer canaries, partial/same-value failure writes, acquire
  0/1/negative returns and buffered overflow/failure responses.
- Accepted recording: 68 input calls (including four nested device calls), four
  nested clock calls, 144 contiguous shared phases, zero gaps/drops/pending.
  The refusal run retains eight expected unknown-alias/size gaps and 136 phases;
  matching raw target execution does not turn those records into valid captures.
- Two actual owned DLL load orders, refusal before owner availability/readiness,
  one table identity, and nested device/clock calls forming one six-phase sequence.
- Deliberately omitting FX restoration fails all 64 cases. Six mutations of the
  actual accepted tape are rejected: removal, duplicate phase, zeroed failure
  postimage, changed acquire branch, invalid buffered count and device swap.
- `checks-with-source-audit.json`: 16 independent checks pass, including frozen
  source hashes and the exact negative-control source mutation. The checker is
  fixture-specific, not a general input-tape verifier.

`non-input-compatibility/checks.json` compares `.text`, `.rdata` and `.data`
sections of plain and RNG recorder builds to the retained prior qualified source.
All six comparisons are byte-identical; optional input code is absent from them.

## Failed controls and qualification limits

The first helper failed eight EFLAGS comparisons. An added unhooked repeat also
changed those eight outputs, so that fixture cannot attribute the differences
to interception. Controlling only target return flags then exposed one unstable
first-call entry-flags observation. Explicitly controlled caller/target flags
produce the passing matrices above. Earlier attempts and raw binaries remain in
`run-first/`, `run-unhooked-control/` and `run-controlled-flags/`. This does **not**
explain actual Wine/Stock FLAGS behavior, nor promote earlier failed observations.

The reused clock return shim also retains its documented below-ESP return-slot
and exception/unwind limitations. Native TIDs are not lifetime/incarnation keys.
Concurrent remapping/writers, concurrent vtable publication, thread recycling,
COM aliases/Release, format/property/cooperative revisions, real Wine device
failure behavior, Win32 translation/geometry and WndProc input remain unqualified.
The fixture does not exercise every proposed helper case in the input research.
The generic owned caller boundary is not a Stock callsite allowlist.

## Optional owned COM lifecycle boundary

`parity_input_com_observer` supplies a separate versioned table, preserving the
state-only table's ABI. Its explicit registration observes slots 0/1/2
(QueryInterface/AddRef/Release), alongside state/acquire/data. New vtables are
published only during quiescent registration. A successful QI binds a returned
alias only when it shares that already-published vtable and the same owned
identity/incarnation; unknown layouts and conflicting identities emit Gaps.
QI keeps the raw GUID, HRESULT and output-pointer pre/post images, including
failure writes. AddRef/Release keep their raw ULONG return values. A zero Release
retires every observed alias for that identity without dereferencing the released
object. Explicit COM retirement also retires its observed aliases.
This qualification assumes the admitted aliases share one object reference count,
as the owned targets do. The all-alias retirement rule is confined to that fixture
contract; it does not establish a general Wine COM lifetime rule. Tear-offs or
per-interface lifetime behavior require separate identity and retirement evidence.

`local/verify/stock-input-com-lifecycle-20261102/run-final/` retains 48
accepted lifecycle calls across four x87/MXCSR settings, with full raw integer,
stack, EFLAGS, LastError and 512-byte FX snapshots equal to unhooked targets.
The targets actually free their private object page on zero Release, then reuse
the same address with an increasing incarnation. The accepted tape has 96
contiguous phases and zero gaps. A separate 15-call refusal run preserves two
expected gaps (retired alias use and a stale incarnation registration); it is
not a valid capture. Omitting FX restoration fails all 48 comparisons; omitting
zero-Release retirement fails at the next object reuse. Five actual lifecycle
tape mutations are rejected independently. Existing state/refusal/binding and
FX fault controls are rerun by the same bounded console runner.

These are owned COM-shaped target methods, not actual Wine DirectInput devices.
They do not qualify COM construction, canonical IUnknown identity, separate
vtable aliases, aggregation/tear-offs, concurrent callbacks/publication, driver
refcount quirks, exceptions/unwind, real thread incarnation or caller allowlists.
The existing binding client still exposes only the state-only table. No Stock
adapter enables either input mode; no replay or paired Parity follows.

All six `.text`/`.rdata`/`.data` comparisons for plain and RNG builds remain
byte-identical to the previous qualified common source; artifacts are in
`local/verify/stock-input-com-lifecycle-20261102/non-input-compatibility/`.
Jev verified the bounded lifecycle/phase and section-identity claims at confidence
0.97/0.94/0.83. Its per-file review remains unaccepted (`safe_to_apply=0.10`,
limiting test-gap confidence 0); retained judgments are in `jev-review.json` and
`jev-claims.json` beneath that evidence root. No live-use approval follows.

Jev verifies the bounded audit and unchanged non-input sections at confidence
0.96/0.98, but its patch review remains unaccepted (`safe_to_apply=0.19`, limiting
correctness confidence 0.01). Raw output is retained in `jev-gate.json`; it is
not approval to enable live capture. Complete the missing lifetime, caller,
input-channel and synchronization mechanisms and their independent controls
before promoting any real Stock recording or replay claim.

## Actual Wine DirectInput qualification, experimental COM v2

The separate `parity_input_com_observer_v2` table preserves the v1 ABI. It admits
explicitly observed Device8 A/W layouts during quiescent setup, then cold-binds
observed aliases to an existing identity/incarnation. It refuses unknown GUIDs,
vtable or hook-cell drift, conflicting identities and stale incarnations. Real
Wine's ANSI QI delegates through its Unicode sibling before returning; therefore
preparing and binding that observed sibling before hooking is necessary. This is
caller-attested association, not automatic canonical identity discovery.

`local/verify/parity-completion-20261102/real-input-final/` retains the actual
private Wine keyboard/mouse fixture, immutable sources and binaries, three raw
comparison passes, Docker isolation inspection and process exits. Under the exact
runtime identity in `checks-reviewed.json`, 32 state/acquire/data cases and 64 COM
cases preserve the full raw integer/stack/EFLAGS/LastError/512-byte FX snapshots
and output bytes against unhooked calls; the independent unhooked repeat is
stable. The COM tape has 124 calls, 248 phases and zero gaps, including nested
Wine delegation and zero-Release retirement across four logical generations.
Replacement devices were precreated: actual raw-address reuse and post-hook
construction interception are not qualified. The observed A/W interfaces share
a reference count; arbitrary COM tear-offs or independent interface counts are
not admitted by this evidence.

The runtime requirement includes `BOX64_DYNAREC_SAFEFLAGS=2`, image
`sha256:c3105bb5c0fe829f89f6f782e79d7bfdb31708209ee4456e3ab4be5e7f94d88f`,
and the exact WowBox64/dinput8 hashes in that report. The same retained binaries
under explicit `SAFEFLAGS=1` have 18 EFLAGS-only COM differences, with stable
unhooked repeats (`real-input-eighth/`). The earlier default-backend failures and
rejected diagnostics remain preserved. No FLAGS differences are masked. Box64's
[documented SAFEFLAGS settings](https://github.com/ptitSeb/box64/blob/main/docs/USAGE.md)
identify 1 as the default and 2 as the setting that preserves flag requirements
across every call/return; that documentation is explanatory, while the retained
countercontrol establishes this fixture's requirement.

The final cold guard has 16 cases, including six expected refusals; its vtable
omission mutant fails. FX-restore, alias-preparation and retirement omission
mutants fail; legacy v1 controls also pass. `real-input-record-mutations-v2/` and
its sibling log retain 18 offline tests, including the positive qualification
control and 17 corrupt-artifact rejection controls. Independent review discovered
missing COM machine-state/raw binding and a nonzero-launcher-exit exemption in
the earlier validator. Those reports and `checks-final.json` remain preserved;
`checks-reviewed.json` uses the corrected auditor, binds measured CW/MXCSR to raw
snapshots and rejects nonzero exits or failed recorder drain.

This qualifies only the owned real Wine fixture under the recorded backend.
It does not qualify Stock input consumption, caller admission, configuration
interception, concurrent publication, thread lifetimes, WndProc/translation,
input injection, exceptions/unwind or process replay. The experimental
`--input-observer` adapter is separate from the injection/telemetry proxy. It
registers only the actual ANSI factory result, adds no COM calls and preserves
unsupported aliases as gaps. Its mixed raw inventory does not compile API/RNG
semantics or qualify caller admission. Source integration and tests are pending
execution after the storage pressure floor permits admission; it supplies no
Stock route qualification yet. The newer shared close/drain gate also requires
fresh owned controls because the frozen final capture predates that change.

## Record format 7: bounded bulk keyboard buffers

Format 7 adds `buffer_bytes` to every input row. Buffered keyboard calls admit
20-byte elements, flags zero and caller counts 1 through 16. The recorder keeps
the complete caller buffer before and after the call (`20 * count_in` bytes,
up to 320), including untouched tails when fewer events return, and both raw
counts. Returned counts above the caller capacity invalidate capture. State and
QI payload widths remain unchanged. Unsupported element widths, flags, kinds,
zero counts and counts above 16 still forward to the original target with an
explicit gap; no target result is invented.

Format 6 remains readable as the earlier count-one shape; readers must not
interpret its rows as bulk evidence. This is not an observation of Stock's
requested buffer counts. The [experimental binary wire](durable-wire.md) now
has a separate typed format for these full buffers and counts. Its generated
serialization controls do not qualify publication from an actual input callback.

Actual private Wine bulk16 qualification is retained in
`local/verify/parity-goal-20261102-092437/real-bulk16-final-current/`. The earlier
`real-bulk16-first/` capture demonstrates only owner-first loading: its fixture
ignored the requested order. Final captures use a corrected helper and retain
actual load-order markers for both orders. Earlier bulk16 captures with format6
retain their two gap12 records and remain invalid captures. The independent
review under `audit/bulk-load-order-final/` checks those markers, frozen sources
and module imports, and rejects a corrupted marker.
