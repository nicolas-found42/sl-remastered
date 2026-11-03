# Script clock consumption observation

The optional CONTROL_TAPE_NATIVE_CLOCK producer observes the pinned older
Stock currentTime callee, 0047bfe0, at its original Float32 consumption boundary.
It is not a replay implementation. Enable it through run_stock.py with
--early-controls --native-clock-observer; existing producers leave it off.

The original bridge calls its Native at 0042008c, executes FSTP DWORD [ESP+5c]
at 00420090, then moves those stored bits into EBP and discards the argument.
The observer leaves the CALL, FSTP and entire clock wrapper unchanged. It checks
the exact 15 bridge bytes, 16 wrapper bytes and scale bits, then replaces seven
bytes at 00420094. The stub saves integer registers, flags, LastError and opaque
512-byte backend FX state; records only when original [ESP+20] is 0047bfe0;
restores state; replays MOV EBP,[ESP+5c] and ADD ESP,4; and resumes at 0042009b.
Other Float Native calls execute the same original effects without clock records.
Plan version 2 requests this mutation; version 1 never reads the appended field.

The [renewed source-bound bytes and seven interior xref observations](../../../local/verify/issue53-career-journeys-20261102/subtree-removal-rea.json)
bind the exact older executable SHA256 7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde.
The clock-caller evidence id is ev_6721da7e7e82987dce712bac653710fedffbc02d384cdc48078a43afec1c55bf.
All seven analyzed incoming-reference sets are empty; this is static reference
coverage, not proof of every possible computed target. The accompanying lease,
finish and postcheck records establish the bounded closed query sequence.

Format 9 retains actual value_f32_bits, callee, original consumer address,
LastError, opaque FX bytes and one shared atomic phase per clock observation.
API call counters retain their original meaning: phases equal twice API calls
plus Native observations. There is no fabricated Native entry or phase/lane
renumbering. The existing admission gate, bounded drain, ring and writer cover
both event kinds. The compiler requires exact callee/store/installation binding,
finite clock values, complete ordinals, mixed lane reads, loss-free accounting
and final quiescence. Its distinct compiled format refuses API-only replay.

The [owned default-backend controls](../../../local/verify/issue53-native-clock-stored-abi-v2-20261102/result.json)
exercise 384 cases: six original FSTP32 values, four rounding modes, four flag
patterns, both remaining FPU stack states and clock/non-clock callees. Independent
raw comparison confirms every baseline/repeat/observed byte, scalar CW/SW,
register, flag, LastError and canary matches. Exactly 192 records match the
original stored bits; non-clock calls emit none. Removing FX restoration changes
all 384 cases. All six snapshot arrays are retained losslessly as gzip, with
encoded and decoded identities in the
[bound audit](../../../local/verify/issue53-reference-provenance-v2-20261102/native-clock-stored-abi-summary.json).
The same tests first exceeded the unchanged 8 MiB cap; that failed attempt remains
retained. Compression changes representation, not case coverage or assertions.

The earlier format-8 experiment reached normal MainMenu, name dialog and cancel,
but its compiler refused all 486 return snapshots as empty ST0 under architectural
FXSAVE interpretation. That attempt also failed outer prepaid hashing. Its
[result](../../../local/verify/parity-goal-20261102-092437/factory-fourth/issue53-stock-native-clock-v1/result.json)
remains unqualified. Format 8 keeps its original refusal: no fake 80-bit decoder
or retrospective scalar projection is introduced. The format-9 producer instead
reads the original stored integer bits, with no added FPU arithmetic.

Prepaid finalization previously hashed new-child payloads and discarded those
hashes. It now retains every initial and final pre-existing file hash and all
membership/metadata/xattr/link assertions, while scanning every child entry for
type, identity, attributes and both caps without that unused payload sweep.
The five-second limit and ledger policy are unchanged. The
[causal controls](../../../local/verify/issue53-prepaid-hash-control-v2-20261102/result.json)
include a genuine old-source deadline failure, 30 passing adversarial controls,
an old-byte mutation with unchanged metadata witness, and final child overflow.

Jev source reviews escalated at low confidence; both results are retained.
Independent root review inspected the revised stack offsets, original instruction
replay, schema and decoded raw ABI evidence. This resolves the bounded source
review only. The first format-9 normal Stock attempt reached MainMenu, name
and cancel, and observed 748 actual stored clock values, but did not produce a
complete stop receipt. Its retained result is
`local/verify/parity-goal-20261102-092437/factory-fx-mutant/issue53-stock-native-clock-stored-v2/result.json`.
Artifact finalization passed; the container refused after the unchanged five-second
stop deadline. A periodic flush still owned the writer, and rows after its last
status do not qualify a closed interval.

The production API writer now buffers output and commits batches of at most 128
records. It retains all selected slots until successful `fflush`; failed commits
restore their pending state. This preserves retry ownership, not transactional
file writes: partially appended or duplicated output remains unqualified. Both
container and host now require one exact nonnegative stop acknowledgement before
accepting a record interval, even if the tape otherwise appears complete.

The admitted real PE32 writer controls retain all 2000 original rows under a
controlled 3ms flush cost: new writer 69ms, immutable old writer 7456ms (expected
red), failed flush then successful stop 11ms. All raw fields except process thread
identity match across variants. The bound audit is
`local/verify/issue53-reference-provenance-v2-20261102/control-writer-qualified-audit-v3.json`
(SHA256 73a590e2c5fa245c5b704a11d0e9ee0a4e41c5c8049df1a1ff2a64418b0543d2).
The same run rechecks 384 complete ABI snapshots and the missing-FX restoration
negative control. A prior case reused an append-only tape name and was rejected;
a later corrected admission exceeded the unchanged global allowance. Both remain
retained. Jev escalated the final writer review; independent root source and raw
control review supports a new bounded actual Stock attempt, which remains pending.

Stable thread
lifetimes, scheduling, input/RNG replay, same-side repeats, paired Port replay and
complete D5 acceptance remain unverified. Exception/unwind handling is also
unqualified; the deliberate legacy-layout AV control stalled in the backend and
is retained separately, not used as a passing exception control.
