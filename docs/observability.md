# Investigating retained failures

This is the 2026-11-04 observability audit and retrieval runbook. It covers the
native game and the developer/capture/release boundaries, independently of the
paused/full-port acceptance state. It does not qualify gameplay or a release.
The retained audit receipts are in `local/verify/observability-20261104-v1/`;
`docs/evidence/observability-20261104.json` identifies the final validation.

## Coverage and visibility

Inspected means the named boundary, instrumentation and retrieval path were
read, not that every function or gameplay variant was exercised. **Code** means
instrumentation exists; **observed** means this pass executed it; **retained**
means historical bytes were actually accessible. A failure report is evidence
of that report, not independent proof of its stated cause.

The main flow is player/probe startup → per-user paths and recorder → SDL
input/audio/rendering → VM/scheduler/native registry → world/resources/physics
→ frame presentation and save I/O → shutdown. Developer commands additionally
cross Python/guardian process groups, the shared pool and retention SQLite;
Stock crosses the focus observer, Docker daemon, isolated Wine guest and proxy
recorders. Packaging crosses codesign/notarytool/GitHub. Their identifiers and
clocks are separate; no shared distributed trace is currently established.

| Area / critical flow | Audit status and source | What is recorded / correlation / retrieval | Visibility and limits |
| --- | --- | --- | --- |
| Launch, settings, per-user storage, shutdown | Inspected: `src/game_main.cpp`, `game_application.cpp`, `game_application_paths.cpp` | `meta.txt`: UTC start, PID/start identity, executable hash, build type, SDL/OS, options identity; `result.txt`: exit/seconds/frames/reason. Run directory scopes IDs. Q1/Q2 below. | Code + historical native records; installed launch not exercised. Path preparation can fail before a recorder exists. Launch-observed source manifest is not a build attestation. |
| Native logger, stdout/stderr, crash/stall context | Inspected: `src/run_log.{hpp,cpp}`, `tools/diagnose.py` | Persistent ranked events, counters, gaps, snapshots, two flight checkpoints; crash handler and process identity checks. New `telemetry.json` quantifies recorder loss. Q1/Q2/Q3. | Native fixture observed including killed child, actual write refusal and bounded output. Signals/buffering/storage can lose the latest evidence; SIGKILL has no terminal result. This host exposed110 startup-probe `.ips` filenames (contents not inspected); availability/retention is host-dependent. |
| VM, script scheduling, monitor wait/wake, native dispatch | Inspected: `src/vm.cpp`, `natives/registry.cpp`, `natives/lang_system.cpp` | `vmthread`/`monitor` notes, native-call ring, failure trace; application `threads.txt` snapshot and frame. Q2/Q3. | Code + older native logs accessible. No scheduler race replay in this pass. Thread names are not stable incarnations; cross-thread ordering remains approximate. |
| Assets, pack lookup, asynchronous sound/font loading, caches | Inspected: `src/pack_registry.cpp`, `resource_precache.cpp`, `resource_texture_cache.cpp`, `natives/game_type.cpp` | Pack-slot map, resource IDs, queue flag transitions, cache notes, `load1` attempt/stage notes for the notification path. Q2/Q3. | Code + historical logs. No universal load-attempt ID joins all async callbacks; an owner disappearing before completion can leave only an error. `load1` is not every loader. |
| World creation, cars, parts, assembly and removal | Inspected: `src/game_creation.cpp`, `world_model.cpp`, `natives/car_part.cpp`, `natives/game_type.cpp` | World snapshots, object IDs/resource IDs, bounded `asm1`/`asmr` attempts and reasons. Q2/Q3. | Code + retained run records. IDs are run-local; a reused pointer is not an identity. Snapshot history cannot recover every unlogged mutation. |
| Physics, contact ordering, wheel/vehicle dynamics | Inspected: `src/car_world_step.cpp`, `run_log.hpp`; current physics evidence reports | `cartrace`: tick/frame/monotonic time, pre/post motion, controls, wheel/contact diagnostics and anomaly tags. `tools/cartrace_check.py` and `tools/parity/trace_inspect.py --help`. | Historical bound physics reports accessible; this pass preserves existing assertions and does not diagnose/fix settling. Last 512 steps / first six anomaly dumps; optional full stream can grow. Helper parity is not retail-scene parity. |
| Roads, traffic, pedestrians, racing/scripted progression | Inspected host boundary: `src/road_network.cpp`, `ground_runtime.cpp`, `natives/ground_road.cpp`, application ground snapshots | Host road/traffic counts, ground state, last GroundRef native and VM failure class; Q2/Q3. | Code only for the complete workflow. Individual shipped race/AI script branches remain **uninspected** in this audit; no retained complete Career/race diagnostic reconstruction is claimed. |
| Render, cameras, scene transitions, display changes | Inspected: `src/scene_raster3d.cpp`, `camera_frame.cpp`, application capture/present paths | Render/drop notes, display dimensions, frame-bound screenshots, camera/body IDs. Q2/Q3; bound frame review. | Code + historical records. No new visible/GPU run. A screenshot alone cannot establish input consumption or explain a black frame. |
| Physical/scripted input, menus, dealer review controller | Inspected: application input/readiness path, `tools/parity/stock/{career_flow,state_flow}.py` and retained review result | Frame hashes, request IDs/lifetimes, expiry/consumption, input labels; native input tape and wait notes. Q4. | Actual historical request retrieved: expired with no consumed response. This supports a missed review window, not the cause of reviewer latency or a successful purchase. |
| Career/config save open/write/close/read/retry | Inspected: `src/file_state.{hpp,cpp}`, `game_file.cpp`, `natives/file.cpp` | New numeric `op1` begin/end, outcome, parent, FileState lifetime scope, handle, close bytes. Q2. | Observed real missing read → write/close → successful readback, plus preserved existing file tests. Save names/content absent from new events. Successful close is syscall completion, not power-loss durability. |
| Audio, music, sound voices | Inspected: `src/audio.cpp`, `music.cpp`, `natives/sfx.cpp`, application audio boundaries | Voice/drop counters, selected device/lifecycle notes, mix meter; specialized captures retain private PCM and audio-evidence results. Q2/Q3 and `tools/parity/audio_evidence.py`. | Code; historical artifacts accessible via their bindings. No new device/PCM playback. Short underruns and every decoder/queue transition lack a persistent attempt history. |
| Video/intro playback | Inspected: `src/video.cpp`, `video_player.cpp`, application startup/video loop | Open/close/loop/audio-tail notes and decoder errors, joined by run/frame. Q2/Q3. | Code, not newly observed video playback. Underlying codec failures may have only a bubbled-up text error; no universal decoder operation trace. |
| Native isolated capture and replay | Inspected: `tools/parity/run_native.py`, `tools/replay.py`, native-run-record guide | Metadata, exact script/save manifests, executable/content identity, observer/focus/audio reports and result. Q1/Q3; `tools/parity/summarize_native_runs.py --help`. | Retained native run bytes accessible. Replay timing/randomness and full paired Journeys remain separate qualifications. No new game capture in this pass. |
| Stock/Wine captures, clock/input/API tapes | Inspected adapter/record boundaries: `tools/parity/run_stock.py`, Stock shutdown/durable/input guides | `config.json`, worker/outer results, container/image identities, observer receipts, proxy JSONL, sequence/lane/call IDs, drop/gap counts and shutdown reports. Q1/Q4; strict tape/transport inspectors. | Retained dealer review retrieved. Owned-console qualification is not actual Stock parity. No new REA, Docker, Wine or device interception. Guest/host/simulation clocks remain distinct. |
| Pool admission, pressure, process guardian, cleanup | Inspected: `tools/parity/resource_pool.py`, `iteration.md`, experiment runner | Lease request/wait, pressure/artifact samples, stop reason/group cleanup in owning result; new console stage journal supplies UTC/attempt/stage boundaries. Q5. | Console tests observed. Live daemon orphan reconciliation remains incomplete; protocol 2 stays opt-in and unactivated. An empty process/container lookup cannot resolve uncertain creation. |
| Retention, compaction and preservation | Inspected: `tools/parity/storage.py`, `prepaid_budget.py`, storage guide | SQLite reservations, preservation manifests/audits, append journals; `dev.py capacity`, bound evidence hashes. | Ledger queried: 5 MiB new reservation capacity before this pass; a 1 MiB audit root is admitted without changing allowance/reserve. No evidence deleted or compacted. Accounting is sampled, not write confinement. |
| Evidence maps, proof evaluation, comparisons | Inspected: `tools/parity/evidence_index.py`, command guide, requirement map | Explicit attempt order, file/hash bindings, typed JSON assertions, declared review/outcome. Q6. | Existing metadata accessible; final index check is reported separately. A declared acceptance or current file hash is not independent proof of causality. |
| Configure/build/CTest, Python checks, public/private CI | Inspected: CMake, `tools/check.sh`, `.github/workflows/check.yml`, dev lanes | Local command output, CTest LastTest, CI job logs when available; console experiment envelopes can retain bounds and chronology. Q5/Q7. | New local checks observed. GitHub history was queried: the latest published baseline run failed configure on missing FFmpeg headers; later native/tooling steps were skipped. The API reports zero private runners. FFmpeg is now in the Brewfile; its JPEG entry now names the already provisioned libjpeg-turbo implementation identified by CMake. No native packages were installed/upgraded. This revision has no remote run; provider retention settings remain unknown. The private job was skipped by its workflow condition; zero visible runner registrations is a separate observation. Direct unwrapped console output has no guaranteed history. |
| Packaging/signing/notarization/publication | Inspected: `tools/release/{stage,bundle,distribute}.py` and release guide | Hashed candidate manifests, durable submission intents/receipts, submission ID and bounded command outcomes; separate wait/reconcile avoids duplicate submission. Query a known receipt with Q1; use release CLI read/reconcile commands only as authorized. | Code inspected; no accepted release candidate or live signing/notary credentials exercised. Server-side history/access is **inaccessible** here. No deployment or publication changes. |
| Ghidra/decompile/census utilities and older experiments | Inventory inspected; individual historical utility internals **uninspected** | Existing stdout/files and explicit experiment results where retained; wrap future bounded console experiments with Q5. | These tools are heterogeneous. No assertion of complete past command history or full instrumentation coverage. |
| External model/REA tooling | Repository integration/evidence receipts inspected; account service telemetry **inaccessible** | Retained request/result receipts and source bindings where supplied; provider errors require separately accessible account data. | Account billing, service traces and removed sessions cannot be recovered from this repository. No credentials are logged by new signals. |
| Hosted application APIs / database service / remote metrics backend | **Not applicable** to the inspected native application architecture | Native files/counters are the evidence store. | No new telemetry service is required. External release and research services remain the separate boundaries above. |

## Prioritized gaps and acceptance

The priority cell gives impact / likelihood / diagnostic value (H high, M medium,
L low). These are qualitative engineering estimates for future investigations,
not measured incident frequencies. Known failures inform the estimates; the
remaining scenarios are explicitly hypothetical or deferred.

| Priority | Scenario and diagnostic gap | Acceptance / disposition |
| --- | --- | --- |
| P1 H/H/H | Known: yesterday's record cannot be located by UTC/operation; legacy metadata/tapes have unrelated shapes. | Implemented bounded read-only Q1/Q2/Q4/Q5. Native UTC is approximate (second-resolution origin plus monotonic offsets); unknown times remain included as unknown; hashes identify bytes actually read. Native historical scan hit its entry bound and remains incomplete, explicitly. |
| P1 H/M/H | Hypothetical: logger fills disk/fails writes while the game continues; missing evidence looks like no failure. | Implemented 16 MiB native run-log cap, counted dropped lines/IO failures, bounded result context and atomic health checkpoints. Actual write refusal and saturation keep save/readback successful and report loss. Complete store failure can still prevent a new health record. |
| P1 H/M/H | Hypothetical: save open/retry/close fails yesterday; raw filenames are sensitive and attempts are indistinguishable. | Implemented typed `op1` history and query; real retry/readback and interrupted-operation controls. Unknown close/unwind is not success; no new filename/payload logging. |
| P1 H/M/H | Known experiment class: launcher dies before final report; no reliable stage timeline or UTC survives. | Implemented pre-admission `attempt.json` plus synced bounded stage boundary journal; source-runner hash and attempt ID. Test actual failed/timeout stages and query incomplete records. Whole launcher/host destruction beyond these checkpoints remains limited. |
| P1 H/H/H | Existing Stock guest copy/scan races, review expiry and uncertain daemon cleanup. | Retain original failures and source/config bindings; Q4 demonstrates expiry only. Existing controller/scanner fixes remain separate work. Do not enable unfinished guardian/protocol migration; acceptance requires independently verified owned cleanup and a complete live Journey. |
| P1 H/H/H | Known hosted CI configure failure prevented native checks and later diagnostic checks from running. | GitHub run41746502239 and its failed-step log establish missing FFmpeg headers. Added FFmpeg and aligned JPEG with CMake’s existing libjpeg-turbo dependency; local dependency/configuration checks are retained. New hosted execution remains unverified; private runner provisioning is external. |
| P2 H/M/H | Loading/audio/media callbacks, owner invalidation and restarts lack a universal operation identity. | Deferred: demonstrate an injected async completion/failure/retry with a stable owner incarnation across enqueue and completion, and prove teardown/refusal does not change behavior. Existing resource/VM notes remain useful but incomplete. |
| P2 H/M/M | Source tree at launch differs from code compiled into an executable. | Deferred: bind reproducible build inputs/compiler/options to the accepted executable and verify mismatch detection. Existing executable hash remains authoritative; never equate launch source hash with build identity. |
| P2 H/unknown/H | Race/AI/script branches and historical utility internals not fully inspected; third-party service retention unavailable. | Remain explicit coverage gaps. Inspect the specific branch/service and obtain an actual retained success/failure record before claiming diagnostic coverage. |
| P2 H/unknown/M | Native frame-thread fsync, trace stream growth, simultaneous signal/logger failure, power loss. | Existing overhead/power-loss assumptions remain unqualified. Benchmark on representative native workloads before claiming a negligible frame-time cost; exercise separate storage/host failure environments. |

## Retrieval commands

Run from the repository root with the project Python environment. The new reader
is read-only. Default discovery: 20,000 entries, 24 levels, five seconds checked
between filesystem calls, 64 MiB total reads and 16 MiB per file. It skips links
and game/data/prefix/source/reference trees. Return 2 means discovery, reading or
selection was incomplete; inspect `problems`. Return 0 means the requested file
inventory was read, not that the incident evidence is complete or the game passed.
The default output bounds records/events to 100; `events_omitted` is explicit.

```sh
# Q1: locate retained operations by recorded UTC interval, including overlap.
python3 -I tools/incident.py --root local/runs \
  --since 2026-11-03T00:00:00-05:00 --until 2026-11-04T00:00:00-05:00
# Narrow the root or select a known record when discovery reports a bound.
python3 -I tools/incident.py --record /absolute/path/to/retained/run

# Q2: native numeric operation history; the record scopes process-local IDs.
python3 -I tools/incident.py --record /absolute/path/to/run --events
python3 -I tools/incident.py --record /absolute/path/to/run --operation op1:7
# Equivalent developer lane; -- separates forwarded query flags.
python3 -I tools/parity/dev.py incident -- --record /absolute/path/to/run --events

# Q3: existing rich native diagnosis (can COPY a matching OS .ips into the run).
# For immutable retained runs, suppress that optional import:
SLRR_CRASH_REPORT_DIR=/nonexistent tools/diagnose.sh /absolute/path/to/run
python3 -I tools/cartrace_check.py /absolute/path/to/run # requires retained cartrace-full.jsonl

# Q4: actual retained UI request. No new capture or input is started.
python3 -I tools/incident.py --record docs/evidence/stock-vision10-20261104.json \
  --request-id 28c72b5b7466a438d07df217be7de1c96b4252f54e6d0c7cc1ad52b62556ab37

# Q5: console stage history, including a missing final result after interruption.
python3 -I tools/incident.py --record /absolute/path/to/experiment --operation stage:0
# Future console experiments use the existing manifest/admission/budget runner:
python3 -I tools/parity/dev.py experiment manifest.json NEW_OUTPUT_DIRECTORY

# Available GitHub history (metadata is separate from raw, potentially sensitive logs).
gh run list --repo nicolas-found42/sl-remastered --limit 5 \
  --json databaseId,status,conclusion,createdAt,headSha,url
gh run view 41746502239 --repo nicolas-found42/sl-remastered --log-failed
gh api repos/nicolas-found42/sl-remastered/actions/runners --jq .total_count

# Q6: revalidate registered historical hashes and assertions, not just filenames.
python3 -I tools/parity/dev.py evidence-index tools/parity/session-evidence.json --markdown
# Q7: test the current implementation; this cannot recreate missing past history.
python3 -I tools/parity/dev.py test '^(diagnostic_operations|run_log|file_state|game_file)_unit$'
python3 -I tests/incident_test.py --probe build/diagnostic_operations_test --startup-probe build/startup_probe
```

For this audit's generated native examples, point Q2 at each `control-*/runs/`
child under the retained audit root. `controls-v2.json` gives the final native paths; `controls.json` retains earlier/native and console records. Audit
query gzip files preserve exact stdout; `historical-queries.json` records argv,
exit, decoded hash and incomplete status.

## Reconstructions actually supported

The retained `control-v2-hold` query contains completed save/readback operations,
then `op1:7` beginning without an end. Its last health record is a checkpoint;
there is no terminal result after the test driver sends SIGKILL. This establishes
the last retained operation and incomplete history. Only the separate driver
receipt establishes the kill; the native record alone cannot explain it.

The fresh failure challenge exhausted file descriptors during stdout-capture
setup. The original recorder left partial pipe descriptors open; the current
assertion fails against that original source and passes with rollback. The new
record reports `capture_unavailable=1`, `io_failures=1`, `first_errno=24` while
actual save/readback succeeds. The analogous occupied-log-directory negative
control establishes the startup-path fix. These controls demonstrate those
specific repairs, not all storage failure behavior.

Historical retrieval also found native run `20261101T120154Z-33549` with a failed
result and executable identity, but no new health schema; its cause is unknown.
The retained dealer request in Q4 was expired and unconsumed, with no response
file at query time. That does not identify why the reviewer missed its window.
The published CI failed-step log directly identifies missing FFmpeg headers;
local configure now passes, while a hosted run of this revision remains needed.

## Interpretation, bounds and privacy

`op1` version 1 uses integer fields: `i` attempt, `p` synchronous parent, `s`
FileState lifetime scope, `k` kind (1 read, 2 write-open, 3 close), `e` phase
(0 begin, 1 end), `h` file handle, `r` outcome, `n` bytes written by close.
Outcomes: 0 unknown/unwound, 1 successful return, 2 open unavailable (the existing
`FileOpenResult::missing`, which can also mean an OS open refusal), 3 rejected,
4 close IO error, 5 no handle. Join handles only within `(run, s)`; correlate
attempts by `(run, i)`. Parent IDs describe synchronous nesting, not a distributed
trace or a scheduler dependency. Two FileState instances may both have handle 1.

Events retain existing millisecond time/frame stamps and 151-character text
capacity. The largest numeric `op1` record fits that capacity. Failed-run tails
repeat prior events; the query deduplicates identical halves and flags conflicting
duplicates, missing halves and reversed time. It does not manufacture an end.

`telemetry.json` is replaced at start, checkpoint attempts and finish. It records
attempted log bytes, dropped lines, IO failures/first errno, dropped bounded
result-context lines and checkpoint sequence. The run log defaults to 16 MiB;
programmatic bounds clamp to 128 bytes–64 MiB. Captured failure reason/stack
context is bounded to 1,024 characters per line and 64 stack lines. Pipes keep
draining after log saturation. Ring/snapshot/optional full-trace limits remain
their existing separate policies. Partial stdout-capture setup now rolls back owned descriptors; `capture_requested`
and `capture_unavailable` distinguish a refused capture from an intentionally
uncaptured run. Actual FD exhaustion reproduced the leak before the fix and
keeps save/readback working after it. The latest health snapshot can itself be stale
or missing after complete disk failure. Health is not a liveness/cleanup receipt.

Console `attempt.json` binds UUID, UTC, PID, Python version and the runner source
hash. `stages.jsonl` contains no commands or environment: one attempt marker and
at most two boundary records for each of the CLI's 32 stages. A failed command's
`end` records its actual exit; a supervision exception records `interrupted` and
does not invent an exit. Journal write failures are counted in the final result;
they do not alter the wrapped command. The existing result/artifact acceptance
and the 8 MiB console budget still apply. Hashing the runner is not hashing the
executed program or all dependencies.

There is no automatic deletion or time-based retention introduced here. Native
run roots remain private; retained captures keep the existing aggregate ledger,
per-run limits, reserve and preservation policies. Old logs may contain local
paths, player names, input or save contents; they are not safe exports. New typed
events/health/journals add no such payload. Query output omits raw messages,
argv/environment/save contents, but its source paths are local references and
must still be reviewed before sharing. Never query or export a secret store.

## Short investigation runbook

1. Establish the affected UTC interval and run/capture/attempt ID. Start with Q1
   or a known record; preserve results and failures. Narrow incomplete searches.
2. Read the exact result, context and source hashes. Distinguish executable,
   launch-source, game-data, image and configuration identities. Check health,
   drop/gap/omission counters and missing evidence before interpreting silence.
3. Retrieve operation halves/stage boundaries/request IDs; then inspect the
   indicated original source lines and relevant thread/world/cartrace/frame
   records. Clock offsets, reused IDs and missing terminal evidence limit joins.
4. Write the supported event sequence, competing hypotheses and missing
   observations. An expired review proves expiry; it does not prove a hung game.
   An unmatched begin proves incomplete retained history, not why it stopped.
5. Reproduce safely using the specialized adapter or console experiment lane.
   Keep original evidence, bind the changed sources and executable, demonstrate
   the failure before repair and the observable acceptance after it. Update the
   requirement/evidence maps only for the scope actually established.

## Review boundary

This is a codebase-wide audit pass with implemented shared improvements, not
complete observability coverage. Shipped race/AI branches, heterogeneous older
utilities, inaccessible Apple/model-provider histories and remote retention settings, full-game timing/performance and
power-loss durability remain open. Final command outcomes and the different
fresh-pass failure challenge are retained in the audit result, including the
complete gate's failed and unexecuted stages.
