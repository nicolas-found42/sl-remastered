# Durable publication protocol

`durable_ring.c/.h` is the canonical C implementation. It is not connected to the
Stock recorder. Host protocol controls and PE32 compilation do not qualify Wine
mapping attachment, loader behavior, observer ABI, ordinary shutdown or replay.

The mapping header is128bytes; each slot is1040bytes, including1024payload bytes.
Only fixed-width words and bytes are shared. The local `dr_view` pointer and
caller buffers are not shared ABI. The initialized layout and nonce are immutable;
every supplied mapping/buffer must remain valid and writable, with caller-owned
nonoverlapping input/output storage. Remapping or corrupting live metadata is not
qualified.

Each reservation has one monotonic ticket. An atomic32bit tag combines that ticket
with FREE, RESERVED, WRITING, COMMITTED or READING. Tickets stop at0x1fffffff rather
than wrap. Slot reuse requires the exact prior ticket's FREE tag. A stale producer
cannot claim a new generation's RESERVED tag. A release commit publishes the
immutable kind/length/payload; the collector acquires that exact tag before copying.

The logical collector serializes `take` and `ack`. Acknowledgement checks the exact
ticket, generation and copied bytes, and requires its caller's successful real
archive write/flush result. Only then is the slot freed and the acknowledgement
frontier advanced. Archive failure is sticky and retains the slot; a later retry
cannot erase the failed attempt. Final archive flush/close failures must call
`dr_archive_failed` even after the last acknowledgement.

The live collector never skips an earlier unfinished reservation. Once an external
owner has actually established that producers and collector are stopped,
`dr_frozen_summary` exposes incomplete reservations, WRITING/READING states and an
abandoned collector lock. `dr_frozen_record` retains later committed bytes beyond a
hole. Neither API proves process death, invents a reply, clears pending work or
asserts that a capture is complete. Zero counters alone cannot establish the
larger capture contract.

The current host fixture uses four concurrent producers and a collector, real
archive writes, process death at three producer states and during collector
ownership, a real broken-pipe archive failure, exhaustion, overflow and stale
operations. Test-only hooks force the actual process-death instruction boundaries;
they are absent from ordinary builds. Positive concurrent fixtures pace their
synthetic producers outside the protocol to exercise many generations without
intentional overflow. A real recorder producer must not wait for capacity: a full
ring refuses and makes the attempt inconclusive.

Retained qualification: `local/verify/parity-ordinary-recorder-drain-20261102/`.
The `durable-step1/round2/` frozen source has11 ASan/UBSan host cases,8 compiled
omission mutants and a separate ThreadSanitizer concurrent run; the same source
compiles as PE32 using32bit Interlocked operations. Parent source review is hash
bound. `round2-audit.json` retains60 raw/source checks. The Jev gate remains
unaccepted and does not approve deployment or workflow completion.

Next: owned PE32 external collector and precreated mapping, attachment before the
first consumer, actual process-exit survival and terminal observations, then full
ABI/FX controls on the exact backend. No late private-ring migration or atomic
admission close can substitute for the missing whole-lifetime capture evidence.

`durable_archive.py` independently validates the immutable final mapping and
binary journal. It retains reservation-before-slot-claim holes, partial writes,
and acknowledgement interrupted between freeing the slot and advancing the
counter. A reused slot's remaining prior payload is checked against its archived
record; unused mapping payload bytes are not required to be zero. The journal's
unused bytes are zero-filled by the canonical collector copy routine.

Its `transport_complete` result is only a statement about these supplied bytes.
The caller must independently establish stopped writers, target identity and
capture coverage. An empty mapping from a refused target can have a complete
transport; it does not have a successful target execution. API entry/reply
semantics and thread incarnation binding remain separate pending work.

Twelve host tests pass, including three regressions found during independent
review. The retained seven actual owned mapping/journal pairs reproduce explicit
unfinished states and reject seventeen byte corruptions. Four historical
omission variants fail the original nine assertions. Evidence is in
`local/verify/parity-durable-archive-20261102/`; independent recheck is
`parity-durable-writing-diagnostic-20261102/transport-review.json` (33 checks).
The initial Jev gate remains unaccepted; its low-confidence compound count
contradiction is retained alongside the independent raw recheck.

The owned runtime's writing-worker ordinary-exit case is still incomplete:
`parity-durable-mapping-runtime-20261102/run-42f52d1643` retained one WRITING
reservation and required timeout termination. This is a failed qualification,
not permission to omit the case or enable complete Stock shutdown recording.

The [experimental call-boundary wire](durable-wire.md) adds a bounded binary
publisher and independent entry/reply reader. Generated host snapshots qualify
serialization only; actual observer and external-collector integration remain
separate work.
