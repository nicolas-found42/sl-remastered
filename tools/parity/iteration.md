# Private iteration and shared admission

The host has 18 CPU cores and 48 GiB RAM. At the user's explicit request on
2026-11-02, Docker was configured for 24 GiB RAM; its restarted engine reports
23.43 GiB usable. The 18 vCPUs are unchanged. Those figures do not establish a
game-instance limit.
CPU-only test sweeps and actual native/Stock results are described in
`docs/research/iteration-efficiency.md` and the historical checkpoints in
`docs/history/parity-workflow-checkpoints.md`. Current requirements are in
[the full Port specification](../../docs/specs/full-port-1.0.0.md).

`run_native.py` automatically joins the shared `resource_pool.py` budget before
preparation and execution. Metadata retains the request and admission wait.
Initial native reservation: 2 CPU permits, 2 GiB estimated host memory and one
renderer slot. Two renderer slots admit at most two native Runs together.
The full check uses the same pool for configure/build/CTest; build writers are
exclusive and compilation is bounded at six jobs. Suite ordering is preserved
until its shared-state audit supports parallel execution.

For other cooperating commands, from the repository root:

```sh
python3 -I tools/parity/resource_pool.py \
  --cpu 6 --host-memory 12 --build-writer 1 -- \
  cmake --build build --target startup_probe --parallel 6
```

Memory permits represent 512 MiB each. The provisional pool contains 12 CPU
permits, 32 GiB host memory, 20 GiB Docker memory, two renderer slots, one Stock
slot and one build-writer slot. `run_stock.py` reserves six CPU permits,
23 GiB host memory (20 GiB guest plus 3 GiB capture overhead), 20 GiB VM memory,
one renderer and the Stock slot. Docker supplies a 20 GiB hard guest memory
limit. The existing 3 GiB temporary-prefix cap, 64 GiB capture-retention allowance,
and 10 GiB disk reserve are unchanged.

Pool capacities were migrated only after every resource and waiter lock was
observed idle. Earlier evidence retains its original 3 GiB guest limit; historical
isolation validation requires that original limit explicitly. The migration and
actual Docker memory observation are retained in
`local/verify/issue53-prefix-exec-20261102/`. Older local research scripts do not
automatically join this budget. No memory estimate or game concurrency maximum
has yet been qualified by complete Journeys.

The default pool is `~/Library/Caches/sl-remastered/parity-resource-pool-v1`,
shared across checkouts/worktrees on this Mac; `SLRR_RESOURCE_POOL` selects
another location for an independently isolated experiment. A different pool
does not coordinate with the default. All participants must agree on capacities;
changing a live pool's configuration is rejected. Requests acquire all resources
or release everything and wait, with a bounded admission deadline. Live requests
enter one FIFO queue under the admission mutex: a later small request cannot
overtake a larger request waiting for enough permits. This intentionally permits
head-of-line blocking, including requests for disjoint resources. Deadlines bound
that delay; this is fairness in admission order, not a throughput guarantee.
Each queued request holds a kernel liveness lock. Timed-out requests remove their
entry; later requests prune unlocked entries left by killed waiters. All clients
must use this queue-aware implementation; older clients bypass its fairness rule.
Kernel locks
release on exit. Console guardians and specialized observer roots inherit lease
descriptors, so killing only the Python launcher does not immediately lose their
reservation; console commands are cleaned up by their independent guardian.

The opt-in protocol-2 library seam now persists Stock creation intents before
external creation can be authorized. Its committed envelope binds intent IDs,
record hashes, owner locks and reserved permit identities. Every admission
rereads the registry under the mutex; unresolved holder death or corrupt/partial
state blocks new work even if all permit FDs have closed. Healthy intents allow
remaining capacity to be used. Their committed slot names cannot be reallocated
even if a holder dies after validation; the final recheck uses the same admission
deadline. Wire capacities/identity words reject bool/float aliases of integers.
There is no retirement/force-clear operation and
no Stock caller enables this seam yet. The live default remains protocol 1,
with its existing orphan limitations. Reviewed idle migration preserves capacity
and the admission inode, archives legacy lock inodes, installs versioned permits
and legacy refusal directories, then commits protocol 2 atomically last. Partial
migration cannot activate protocol 2; frozen old clients are only guaranteed to
refuse after complete migration. See the command guide's opt-in Stock-intent
section before planning a coordinated client rollout. This is an admission
foundation; daemon creation/cancellation, exact-ID cleanup and freezer safety
still need separate qualification.

This is cooperative admission, not an OS CPU/RSS quota, a priority scheduler or
an adaptive memory-pressure controller. Unwrapped commands are unaccounted for.
It does not establish headless execution for an arbitrary command. The native
observer and Stock container adapter remain responsible for focus/audio capture,
execution deadlines and owned-process cleanup. Descendants that close inherited
descriptors or escape supervision need adapter-specific lifecycle validation.
Pressure feedback is now an opt-in admission gate (`Pressure`): while real free disk is
below 10 GiB or reclaimable memory below 1 GiB (defaults; `--min-free-disk-gib` and
`--min-available-memory-gib`, 0 disables), admission waits and then fails with
`ResourcePressure` at its deadline, naming the measured value. An unreadable sample refuses
admission. The CLI wrapper, `run_native.py` and `run_stock.py` enable it; the library
default is off. For execution under the focus observer, native and Stock adapters
also use `Lease.run_observer`: it checks pressure between bounded waits, ordinarily
once a second (plus sampling latency), and asks the observer to stop on low disk,
low memory or an unreadable sample. Results retain the sample count, last sample
and refusal reasons. Pressure refusal remains failure even if the child exits
cleanly during shutdown. The real observer owns child-group cleanup; Stock also
runs its independent container cleanup. The outer wait allows eight seconds for
observer cleanup before a last-resort root kill, which remains explicitly recorded.
Owned-console tests exercise actual observer cleanup, including lost counters and
an outer deadline; these are not measurements of game capacity.
CLI-wrapped console commands now use `Lease.run`: a private process group, a
default 1,800-second execution deadline (excluding admission), pressure sampling
ordinarily once a second, and a 64 MiB combined stdout/stderr limit. Override the
bounds with `--timeout SECONDS` and `--max-output-mib INTEGER`. Configure/build/CTest
commands already using this CLI inherit these checks. A pipe is drained into a
capped temporary file; output is relayed after process-group cleanup so a slow
consumer cannot block supervision. Child stderr therefore appears in the combined
stdout log. Limit refusal exits nonzero and retains at most the output cap; it
never promotes truncated output to a successful command. Pressure sampling
latency is additional to the nominal check interval.

An independent exec-based console guardian creates the command in a private process
group. The launcher holds a pipe write end that neither guardian nor command owns;
EOF requests cleanup even if the launcher is killed with SIGKILL. EOF before the GO
handshake refuses command creation. If the launcher disappears during command
creation, the guardian still owns the returned root PID and drains its group. The
guardian retains lease descriptors until cleanup; the command also inherits only
the lease descriptors, preserving admission if the guardian itself is destroyed.
Neither the command nor unrelated processes inherit the lifeline or private
configuration/status descriptors.
The guardian sends TERM and waits up to two seconds for its root, then sends KILL
to any remaining group. Its lease records
pressure samples, output counts, stop reason and whether root KILL was required.
This is a bounded group boundary, not complete descendant accounting: children
that create another session/process group escape it. Simultaneous guardian failure
is not qualified: this cooperative process boundary cannot guarantee cleanup after
the guardian itself is killed. A surviving command retains its inherited lease,
so this failure does not immediately release its reservation. Descendants that
close the descriptors remain a separate accounting gap. The dedicated native
observer and Stock adapter still
own their specialized cleanup. Direct preparation calls and visible native runs
that bypass `Lease.run` remain admission-only. The thresholds are provisional,
not measured game limits. Broader throughput sweeps, artifact-directory budgets,
guardian-failure cleanup and escaped-descendant accounting remain open work.

Optional artifact accounting is shared by `Lease.run` and `Lease.run_observer`.
Construct `ArtifactBudget([canonical_private_directory], maximum_logical_bytes)`
and pass `artifact_budget=...`, or add repeatable `--artifact-dir PATH` plus
`--max-artifact-mib N` to the console CLI. The default aggregate limit is 2048 MiB
when directories are supplied; without them no artifact budget is implied.
Directories must already exist outside `ref/`, have canonical paths, and not
overlap. Checks run before launch, ordinarily once a second during supervision,
and before accepting the final result. Execution records retain sample counts,
the last sample, refusal and reason. A refused run keeps its artifacts for
inspection and triggers the existing bounded group/observer cleanup.

The scanner counts logical file lengths, including sparse and hidden files,
using no-follow directory descriptors. Root identity is fixed at construction.
Missing/replaced/unreadable trees, refused symlinks, special files, more than 100,000
entries or a scan exceeding one second refuse the observation. These are sampled
limits, not filesystem quotas or write confinement: growth can overshoot between
samples, sampling is not an atomic snapshot, filesystem syscall latency can
extend a scan, and writes outside declared trees are unaccounted for. Pressure
sampling latency also extends the nominal interval. `link_policy="count"` (CLI `--artifact-links count`) explicitly charges symlink
metadata lengths without following targets, including dangling links. It does
not establish target isolation. The native background adapter now declares its
whole private output tree with a provisional 1024 MiB default; Stock declares
its whole private output tree with a provisional 4096 MiB default (a retained
ordinary-exit tree measured 3,083,094,422 logical bytes before this change).
Both accept `--max-artifact-mib` and retain `execution_artifacts`; neither claims
these provisional bounds establish game capacity. Native `run/latest` and Wine
`dosdevices` links use the count-only policy. Stock's independently inspected
mount/device/network boundary remains the isolation check. Native visible mode
is outside this artifact supervision, as it is outside focus-supervisor capture.
Escaped processes and simultaneous guardian destruction cleanup remain open work.

# Candidate graph and corpus inventory

`catalog.json` contains 36 initial candidate Rungs, 116 named variants and six
Journeys. Twelve exact Installed Game script hashes and 34 class/tree anchors
locate the candidate seams. They do not assert that every branch has been decoded
or that expected behavior has already been observed. In particular, the graph
does not yet enumerate every Native, physics boundary, resource kind or asset.

```sh
python3 -I tools/parity/catalog.py \
  --game '.scratch/2005-extract/Program Files/Street Legal Racing - Redline' \
  --reference ref --out local/verify/NEW-UNUSED-CATALOG-OUTPUT
```

This retains complete bounded per-file hashes, including hidden files, executables,
saves and existing diagnostic files. An incomplete scan or changed authority hash
cannot pass inspection. Cyclic/unknown dependencies and nonexistent tree anchors
are rejected. Reference repositories and mod/later-release files stay comparison
material; they never replace the declared Installed Game authority.

The output is a **candidate inventory**. Hashing a file, naming an observation or
listing a variant does not prove parser coverage, behavior or Parity. Runtime
proof binding, exhaustive content-to-contract assignment, expected Stock results
and all currently unenumerated branches remain explicit work.

# Developer entry point

`tools/parity/dev.py` is the one command for day-to-day work. Builds and test runs go through
the shared pool with the same budgets as `tools/check.sh`; `--dry-run` prints the commands.

```sh
python3 -I tools/parity/dev.py test object_world        # build once, run matching CTests
python3 -I tools/parity/dev.py repro object_world_step_unit --runs 3   # is the verdict deterministic?
python3 -I tools/parity/dev.py corpus                   # every Python suite, exactly as the gate lists them
python3 -I tools/parity/dev.py coverage                 # catalog coverage; pending stays pending
python3 -I tools/parity/dev.py perf 'unit$' --runs 3    # wall-clock timing lane
python3 -I tools/parity/dev.py proof ...                # strict proof evaluator
python3 -I tools/parity/dev.py gate                     # complete integration gate
```

`repro` and `perf` exit non-zero for a red or nondeterministic result; they never convert a failure to
a pass. `coverage` counts a Rung as verified only when its `assertion_status` says so (currently 0 of 36).
`perf` is wall-clock including pool admission, not a representative game benchmark. There is no
`paired` lane yet: paired Stock/Port capture needs the observed-input and replay work in the plan.

`dev.py perf-captures DIRECTORY` audits a retained sequential/concurrent mixed
experiment and reports wall time, admission waits, completed work, actual
foreground-observer coverage, PCM, artifact accounting and responsiveness proxies.
It never launches commands supplied by the evidence manifest. Missing or changed
work, differing authoritative source bytes, incomplete pressure/heartbeat spans,
changed frozen CPU executables/producer identity, altered CPU output, empty/changed
PCM and incomplete focus records refuse the report. The experiment retains its
producer, exact commands and immutable tool/binary copies for inspection.

The measured fixture under `local/verify/parity-mixed-capacity-20261102/measured-final/`
contains one normal-intro/menu/New Career/Cancel/ordinary-exit native capture, one
isolated Stock capture and 90 CPU-only test executions in each mode. Sequential
wall time was 191.335s; bounded concurrent wall time was 75.382s. The observed
elapsed ratio 2.538 includes 48.176s of baseline Stock admission wait and ordinary
run-to-run variation; it is not a clean causal speedup or maximum-capacity result.
Concurrent roots overlapped 5.147s under the shared pool (two renderer slots,
one Stock slot, two CPU-test permits). Every declared task completed; both modes
retain nonempty private PCM and raw no-owned-activation observations. The mix's
worst supervisor heartbeat gap was 71.4ms, observer polling gap 58.6ms, and sampled
reclaimable memory stayed above 16.25GB. These timer/foreground measurements are
responsiveness proxies, not direct human keyboard/mouse latency measurements.

Python's `time.monotonic` uses `mach_absolute_time` here, while the native focus
observer uses `CLOCK_MONOTONIC`. The first audit refused their mixed absolute
coordinates. The accepted report compares only the three observer root-lifetime
intervals in their common C clock domain; CPU job elapsed intervals remain in
Python's clock domain. No deterministic schedule or clock projection follows.
The original incomplete-tooling attempt and the clock-domain refusal remain
retained. Frozen CPU bytes were declared before both captures and are rehashed
with the same retained producer/argv binding; this is bounded workload attribution,
not a per-call runtime execution tape. Producer source identity was audited after
the captures. Broader repeats, other game journeys, heavy builds, escaped-process
accounting and direct user-input latency qualification remain open.

The console guardian now keeps its root unreaped through the final process-group
signal (`waitid(..., WNOWAIT)`), reserving the PID/PGID against reuse. On Darwin,
signaling a zombie-only group returns EPERM. That no-op is accepted only after
confirming the exited, unreaped root and a bounded PID/PGID/state census containing
that root with no live members. Live-member, missing-root and unreadable census
cases remain failures. Cleanup failures retain `cleanup_incomplete` and the actual
error even when the root itself exited zero; they cannot become successful runs.
A new actual-child boundary regression detects premature reaping, and a separate
permission-refusal control rejects an implementation that ignores live members.
All 47 pool tests pass after the additional lost-wait-identity refusal check
(`pool-tests-owned-identity.log`). The initial zombie-group regression,
real parent command failures, helper compile/refusal attempts and corrected
controls remain retained at `local/verify/parity-descendant-boundaries-20261102/`.

The owned, self-deadlining console qualification in that root's `run-v4/` proves
both remaining boundaries with kernel PID/birth identities and independent
cleanup. An ordinary inherited-group child is drained. A child that calls setsid
and closes inherited lease descriptors survives a zero-exit root; the reservation
becomes available while that child is still alive. Destroying the guardian also
leaves its command alive; the launcher returns failure and the command's inherited
lease remains held until independent cleanup. The fixture independently cleans
all owned targets and has a kernel alarm deadline if its checker fails. Its
pass means these controls observed the gaps, not that escaped children or
simultaneous guardian destruction cleanup are qualified. Polling ancestry can
observe some such descendants but cannot prove absence of a short-lived escape;
a stronger lifecycle boundary or an explicitly qualified execution class remains
required for complete process-tree accounting.

The subsequent owned `coalition/escape.json` experiment establishes a stronger
Darwin primitive: a temporary launchd job receives a distinct resource coalition,
and its setsid child retains that membership after closing inherited descriptors
and being reparented to PID 1. PID-version-bound signaling removes that target;
wrong-coalition and wrong-version controls refuse and leave it alive. The exact
owned launchd service is then removed. This primitive is not enabled in adapters:
general membership census, independent automatic cleanup and lease handoff,
protected-workspace access, and external Docker container cleanup still require
integration and qualification. The next small watchdog probe was cancelled before
admission when free storage fell below the 10 GiB reserve. Its source is retained
in `watchdog-v1/`; it supplies no runtime success evidence.

The subsequent automatic watchdog prototype is isolated at
`local/verify/parity-descendant-boundaries-20261102/watchdog-runtime-v3/`;
`capture-v4/qualification.json` retains the complete four-case TMP-only run.
Normal root exit, launcher death, owner death and watcher death all clean the
self-deadlining setsid/closed-descriptor target, with no sampled reservation
release while either observed target remains alive. The launcher closes its own
fixture lease before triggering failure, so duplicate holder lifetime is exercised.
The subsequent independent kernel census finds all four private coalitions empty.
Watcher-death command cleanup is automatic through owner peer EOF, but its service
definition removal is an explicit independent bootout. That missing automatic
service removal, startup kill points, simultaneous holder destruction, durable
orphan accounting, protected workspace/Docker integration, and broader operational
limits remain unqualified; this is not enabled as a production backend.

The first attempted run refused its Unix socket under the artifact policy. An
explicit `--artifact-sockets count` option now charges Unix namespace metadata
without connecting or reading socket traffic; the default remains reject and
FIFOs/other special files remain refused. Actual socket no-connect, byte/entry
limits and FIFO refusal checks pass; all 49 pool tests pass in 24.493 seconds.
The entire successful private TMP tree remained under the 32 MiB logical-byte
budget. Prior socket refusal and zombie/private-identity observation failures are
retained. The final passive observer exits zero, reports no observed owned
activation and a 38.103 ms maximum poll gap. Jev's final bounded prototype gate
remains escalated (safe-to-apply .30, composite .77575; all four claims verified
but requiring review), and is not retried. Source and raw evidence need the
parent's independent review before promoting any bounded claim.
