# Capture storage

The November 2 inventory found approximately 261 GiB of Wine prefix files,
39 GiB of private game files, 46 GiB of frames, 5 GiB of JSON/logs and 91 GiB
of other files under `local/`. These are logical totals, not exclusive APFS
block ownership. A pair of independently initialized retained prefixes alone
contained 648 duplicate files above 1 MiB, totaling 2.07 GB.

The failure was cumulative: a 4 GiB per-run limit does not limit hundreds of
retained runs. Fresh Wine initialization repeatedly materializes DLLs, while
the older background recorder had no finite capture deadline or storage budget.
The old game-copy fallback also silently switched from APFS clones to full
copies. All historical evidence remains required; deleting failed runs is not
the retention policy.

## Prevention

`ArtifactBudget` now reserves a durable, cross-process **64 GiB aggregate
allowance for new retained capture roots**, including explicit Stock, native and
owned-input outputs outside the repository, in
`local/storage-index/retention.sqlite`. Each distinct output is charged its
declared maximum, including failed runs. Directory identity preserves charges
across renames and recreation. Reopening a root cannot reduce its charge.
Restarting Python does not reset it; independent processes cannot
overbook it. Admission refuses before execution when exhausted. Existing
historical data is not silently deleted or charged as a new run. This is a
conservative retention allowance, alongside the per-run sampled limit and
10 GiB host reserve, not a filesystem quota for unwrapped programs. Do not
delete/reset this ledger to bypass admission: archive retention deliberately
and review a policy migration if more distinct evidence is needed.

The Stock runner refuses a failed APFS clone instead of copying the entire game.
Successful drained Stock captures automatically compact duplicate payloads.
Capture budgets hold shared maintenance locks; compaction requires exclusive
access. Busy automatic maintenance is reported as deferred. Admission acquires
the lock before charging retention. These locks cover cooperative callers;
manual maintenance still requires checking for legacy or orphan writers.
Owned input qualification and visible native captures now use the same runtime
artifact supervision. The old unbounded `run_capture.sh` refuses immediately
and directs callers to the isolated, bounded `run_stock.py`.

## Lossless compaction

`storage.py` preserves file paths and contents using APFS copy-on-write clones.
It never hard-links writable captures, truncates logs, or drops screenshots.
Target ownership, mode, timestamps, ACLs and extended attributes are restored;
bytes and original metadata are checked before atomic replacement. macOS can
add its non-removable `com.apple.provenance` label to a newly created inode;
this sole permitted addition is recorded verbatim in the migration journal.
An existing attribute may never change or disappear. Inode and ctime
necessarily change and are recorded in a flushed write-ahead journal. Source
and target remain independent on later writes. Flagged, multiply linked,
aliased and protected reference/Git files are refused or excluded. There is no
full-copy fallback. A failed post-replacement journal step reports an explicitly
committed change rather than claiming that nothing changed.
Read-only directories, flagged files and unpreservable metadata are skipped and
journaled explicitly, without changing their protections. A completed scan can
therefore retain duplicates; its skipped counts must accompany any report.

First stop all writers and verify no live container mounts the captures. Then:

```sh
python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 --host-memory 2 --timeout 1900 -- \
  python3 -I tools/parity/storage.py local/verify local/wine-runs local/runs --seconds 1800
```

This indexes and reports duplicates without replacing files. Add
`--apply-stopped` to compact verified stopped trees. Keep `local/storage-index/`
and its journals. The index contains paths/hashes, not another full payload
copy. Cached canonical paths are rehashed before reuse; changed or missing
canonical files cannot overwrite different bytes. Reaching an execution or
scan bound yields incomplete evidence, never a successful full-scan claim.

Compaction is maintenance, not target execution qualification. Historical
inode observations remain historical observations; the migration journal
explains later inode identities. `du` cannot establish exclusive APFS storage
savings. Report actual filesystem free-space changes and disclose concurrent
host writes.

`test_storage.py` exercises real APFS clone/write isolation, target metadata,
stale index entries, protected paths, journal/durability failures, final scan
deadlines, persistent aggregate limits and independent-process overbooking.

## Using unused capacity in an existing reservation

The native adapter can place a new capture inside an already registered budget
root. Supply `--artifact-budget-root PATH --artifact-parent-max-mib N`; `PATH`
must have the exact registered path, device/inode and maximum, and the output
must be a new direct child. Admission reads the ledger without inserting,
reducing or increasing any reservation. Unknown, renamed, recreated, aliased,
overlapping or differently sized roots refuse. This does not reclaim charges
from deleted captures or reset the cumulative allowance.

The adapter checks the entire parent, including all older captures, against its
unchanged maximum. The new child also has its own limit, at most the existing
1 GiB native default. A maintenance lock and a per-parent writer lock exclude
cooperating maintenance and competing new captures. Previously stopped writers
must remain stopped; this is sampled accounting, not a filesystem write sandbox.
The ledger and parent are held open and checked for replacement.

Before launch and at finalization, the adapter hashes older regular files and
checks their identities, contents, ownership, mode, timestamps other than access
time, flags and extended attributes. Link targets and tree membership are also
checked. Reading files can change access times; those deltas are recorded rather
than hidden by timestamp-restoration writes. Adding the new child changes parent
directory metadata; that single transition is recorded explicitly. Later changes
outside the new child reject the attempt.

`prepaid-audit.json` retains the old manifest and parent/child samples. A second
final scan includes the result and audit writes before CLI success. Finalization
failure marks the current result failed, retains diagnostics where the owned
child still exists, and returns nonzero. No old capture is overwritten. The
ledger's historical device/inode key cannot prove that an inode was never reused
before admission; current identities and held descriptors remain explicit limits.

The native test suites accept the paired environment variables
`SLRR_NATIVE_PREPAID_ROOT` and `SLRR_NATIVE_PREPAID_MAX_MIB` for this path. They
retain uniquely named capture children. Their temporary input fixtures remain
separate from the prepaid parent. With neither variable, existing behavior is
unchanged.

Qualification is retained in
`local/verify/parity-goal-20261102-092437/prepaid-prototype/`: scratch-ledger and
fake-Lease tests, preserved earlier defects, and actual adapter/background
captures using an existing 1 GiB reservation. Both real background modes retain
private nonzero PCM and no observed owned activation; the ledger and old file
bytes remain unchanged. These are menu smoke captures with explicit intro bypass,
not full-intro, replay, Stock adapter or complete scheduler qualification.

The experimental Stock adapter also accepts these prepaid arguments with
`--ephemeral-prefix`. Its seed must pass the applicable qualification: ordinary
Career runs require a successful normal Career capture with the pinned image;
the console ABI seed is not sufficient. The seed and frozen instrumentation are
read-only. Wine runs in a private executable 3 GiB tmpfs prefix within the 20 GiB
guest limit. The adapter retains complete before/after manifests and bounded
changed-file bytes after Wine quiescence, and verifies seed, instrumentation and
older prepaid files are preserved. The new child is capped at 1 GiB. Input
observer capture is currently excluded from this route.

Actual normal-intro create/load/ordinary-exit observations are retained in
`parity-goal-20261102-092437/factory-raw-fix-fifth/issue53-stock-functional-20g-v4`
and `factory-raw-fix-sixth/issue53-stock-load-20g-v1` under `local/verify/`;
`local/verify/issue53-prefix-exec-20261102/stock-roundtrip-20g.json` independently
audits their Career persistence. These qualify that bounded functional route,
not complete D4, replay, same-side repeatability or paired parity. The existing
retention allowance, child limits, scan deadlines and disk floor remain intact.
