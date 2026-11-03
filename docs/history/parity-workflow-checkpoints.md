# Deterministic Parity workflow implementation

Status: active implementation. This plan preserves the complete requested scope;
an implemented evaluator or passing module test does not complete the workflow.

## Approved contract

User-approved exception (2026-11-02 continuation): when asked for the missing
authentic disc CUE/TOC/image package needed for CD metadata/audio validation, the
user answered "no, ignore it". Stop pursuing that item. Retain the synthetic CD
metadata disclosure; it is not authentic CD evidence. Normal intro/video and
file-audio decode/render/mix remain required. All other deliverable requirements
and actual-evidence acceptance remain in force. Historical CD pending entries
below describe the earlier scope and are superseded by this explicit exception.

Parity covers observable Stock behavior, including consistent quirks, across the
authoritative Installed Game. Explicitly approved deviations remain separate.
Replace the six broad Rungs with a dependency graph of small behavior contracts.
Each family accounts for success, refusal/cancellation, repetition, interruption,
cleanup and persistence where applicable. Inventory all shipped content; never
promote a whole family from one passing example. Begin exact; numerical tolerance
needs field-specific evidence and cannot excuse discrete branch differences.

First reproduce Stock's observed schedule; do not substitute an invented fixed
rate. First whole-program proof is normal boot, Main Menu and dialog open/cancel.
Recorded test-only controls are allowed in private copies/processes. Never modify
ref/. Missing or unreliable observations are inconclusive, not passing evidence.
Prove the comparator detects deliberately corrupted inputs and records.

The user must retain foreground focus, keyboard/mouse control, responsiveness and
their normal audible audio. Hidden backing windows are allowed. Game video/audio
decode, render, mix and timing remain exercised, with frames and PCM retained
privately. Never mute the host or bypass game processing to obtain silence.

## Deliverables and completion evidence

1. Versioned proof records and strict evaluator: provenance, stimulus, observations,
   same-side repeatability, paired comparison, first divergence, explicit coverage,
   meaningful mutation tests. Real paired capture must exercise this evaluator.
2. Content manifest and proof graph: all authoritative content accounted for;
   branch coverage and pending evidence remain visible. Stock results must support
   expected assertions. Research candidates are not verified proof rows.
3. Native invisible complete pipeline: intro/video, coherent scripted device state,
   real mixed PCM capture without physical output, frames, ordinary saves and exit.
   Independently observe no foreground activation and source preservation.
4. Stock isolated complete pipeline: private virtual display/audio/input, game/save
   copies, Wine prefix, teardown; verify intro/menu/dialog/video/audio and artifacts.
5. Deterministic controls: observe and replay actual Stock time/input/RNG/scheduler
   boundaries; corresponding Port controls; first prove each side repeats before
   cross-comparison. Account for reseeding, multiple clocks/streams and workers.
6. Shared bounded execution scheduler: CPU/memory/renderer budgets across builds,
   CTest, probes and Wine; unique writable state, deadlines, process-tree cleanup,
   no competing build writers, resource locks, measured throughput and responsiveness.
7. Developer entry point: targeted feedback, corpus checks, paired proofs, coverage,
   failure reproduction, performance lane and complete integration gate.
8. Validation: deliberate corruptions, concurrency/isolation failures, no-focus and
   silent-but-nonempty PCM proofs, real Stock/Port boot/menu/dialog comparison,
   full tools/check.sh, independent review and Jev gate against actual evidence.

## Sequence and dependencies

Start proof evaluation and mutation tests while researching native/Stock capture.
Then qualify complete silent/offscreen execution individually. Add observed
time/input/RNG tapes, reproduce each side, compare pairs and diagnose first
divergence. Introduce scheduler concurrency only for qualified execution classes;
CPU-only test parallelism may be qualified independently. Expand the catalog from
decoded Stock branches and corpus inventories; preserve one cold path plus
validated ordinary-load fixtures for faster iterations. Add developer commands
and the complete integration gate. Audit every deliverable above before declaring
completion; unfinished game contracts must remain explicit.

## Current evidence and open work

Research is in docs/research/. CPU-only sweep retained at
local/verify/headless-capacity-503sad4q/: 315 passing executions, not a whole-game
capacity result. Host has 18 CPU cores, 48 GiB RAM and 20 GPU cores. Docker reports
18 vCPUs and 7.75 GiB RAM. New bounded virtual-GL experiments are retained at
local/verify/stock-headless-20261101/. All execution limits need representative
measurement; never infer game-instance count from core count.

At the start of implementation the background probe bypassed intro and used
dummy audio in menu checks. Native capture now preserves normal intros and
captures the private final mix; see the checkpoints below. Deterministic paired
execution remains unverified.
No commit or push is authorized.

## Earlier implementation checkpoint (2026-11-01)

Historical findings below are superseded where the later checkpoint says so.

- `proof.py` and `proof-format.md`: strict contract inputs/Stock identity,
  run-local hashed tapes, required observation boundaries and consumed-stimulus
  digest, repeatability before comparison, first divergence and evidence-backed
  float-only tolerances. Twelve mutation tests pass. Capability artifacts still
  need final observer-specific schemas/validation; real paired tapes are pending.
- `AudioOutput::configurePrivateCapture`: process-private SDL disk final mix;
  `private_audio_unit` verifies actual mixed sources and independent pause. Twelve
  additional executions with four concurrent workers passed, each with its own
  output. No host volume/device setting was changed.
- `Render2D::forOffscreen`: owned SDL GPU target, resize and coordinate mapping.
  Three resized frame/pixel checks pass under ASan/UBSan. Background native
  probes now use this destination and private audio. Runner startup movies are
  normal by default; `--skip-intro` is explicit and recorded as a bypass.
- Actual normal intro -> Main Menu capture passed in
  `local/verify/parity-workflow-implementation/full-intro-5513rbq2/`.
  Activision and Invictus completed; absent StreetLegal.avi followed the existing
  absent-file branch. PCM: 1,296,960 stereo frames at 48 kHz, 2,331,621 nonzero
  samples. Passive observer found no owned foreground activation and allowed
  unrelated user-app switching. This is a native functional proof, not Parity.
- Passive observer/classifier prototype is retained under
  `local/verify/native-focus-observer/` (not yet promoted into the workflow).
  Its notification/poll coverage limits remain explicit. Actual menu-only
  background smoke passed for flag/environment modes under observation.
- Full gate attempted: 123/125 CTests passed; `car_runtime_unit` suspension
  assertion and `object_world_step_unit` sleep assertion failed. No physics
  source was changed in this slice; baseline attribution still needs evidence.
  Gate log: `local/verify/parity-workflow-implementation/check.log`.
- Exact archived HEAD `ceaa48df09ed157e6517ce131236b9101c4721a5`
  independently reproduced both physics assertions twice and via CTest, with
  matching telemetry and 116 matching target compiler commands. These failures
  do not require the workflow changes. Evidence is described in
  `docs/research/workflow-physics-baseline.md`; repairs still remain required.
- Normal boot -> player-name dialog -> actual Cancel button -> Main Menu was
  observed in `local/verify/parity-workflow-implementation/boot-cancel-_89b93yc/`.
  An earlier Escape-key attempt left the dialog open; it is not recorded as a
  successful cancellation or assumed to be a Port defect without Stock evidence.
- Post-CTest checks were run separately under the passive observer. They caught
  a new `--no-run-log` capture regression; no-log probes now retain audio in a
  unique folder under the already-validated writable data root. Remaining checks
  then passed, including corpus comparisons. Their inherited `check: ok` line
  is only the post-CTest phase, not a passing full integration gate.
- Corrupt Ninja dependency metadata caused repeated 657-edge rebuilds. Both logs
  were backed up; only the independently diagnosed invalid record was removed,
  followed by one rebuild. Two consecutive full-tree dry-runs now report
  warning-free `no work to do`. Backup/hashes and checks are under
  `local/verify/parity-workflow-implementation/ninja-recovery/`.
- Stock invisible intro/video/PCM capture works in a private Linux Wine image,
  but boot-to-menu remains red after intros. Diagnostic repro and thread stacks
  are under `local/verify/stock-post-intro/`; current observation is a Wine
  graphics resource-map/command-stream wait. Experiments retain full processing.
- All twelve Jev capabilities were exercised in this goal: screen, verify, find,
  rerank, classify, decide, compare, extract, audit, review, gate and noul. They
  supplied research selection, reconciliation, identity/transcript audits,
  design-risk judgments and code/evidence checks. Uncertain classifications were
  not promoted to facts. The checkpoint gate did not approve readiness
  (`safe_to_apply: 0.14`, composite 0.5205); evidence/scope gaps remain open. Its
  result is retained in `local/verify/parity-workflow-implementation/jev-checkpoint.json`.

## Later implementation checkpoint (2026-11-01)

- Native observer is now tracked in `focus_observer.m`/`focus_evidence.py` and
  integrated into background `run_native.py`. Kernel PID/birth ownership,
  bounded lifecycle, unknown ancestry and record gaps are checked. Seven actual
  lifecycle tests and sixteen synthetic classifier cases pass. Observation
  remains notification/poll based, not an immutable foreground-denial boundary.
- Real native normal-intro/dialog run passed in
  `local/verify/native-auto-observer-20261101/`: no owned activation observed,
  unchanged executable/save, 1,348,800 captured stereo frames at 48 kHz and
  2,452,581 nonzero samples. Actual device format is retained by the audio producer.
- `proof.py` now validates raw focus ownership and actual PCM against their
  artifacts. Fifteen evaluator tests and six audio inspector tests pass. Real
  deterministic paired tapes remain pending; these tests do not supply them.
- Working Stock route is native ARM64 Linux Hangover 11.16 with Windows-code
  emulation, private Xvfb, Pulse null sink/monitor and coherent XTEST events.
  `local/verify/stock-arm64-xtest-final-20261101/` passed normal intros, observed
  menu -> New Career -> Cancel -> menu, exactly two actions, native press/release
  observations, nonzero PCM and source preservation. The older qemu/Wine stall
  is retained as a failed backend. The synthetic CD shim and forced scoped exit
  remain disclosed limits. Adapter remains local pending independent promotion.
- Local Stock record-only API hook now retains clock/input consumer calls plus
  initial clock globals. Alignment/FX negative controls and output-pointer guards
  pass. `local/verify/stock-control-tape/run-aligned-guarded/` retains 86,788
  completed call pairs, zero reported gaps/drops/pending/active calls and the
  actual successful intro/dialog path. Early startup, RNG, DirectInput controls,
  worker roles, exceptions/unwind and replay remain unqualified.
- Rung vocabulary now describes small measurable contracts and separate Journeys.
  `catalog.json` contains 36 candidates, 116 named variants and six Journeys.
  Twelve exact script hashes and 34 class/tree anchors were checked against the
  Installed Game. `catalog.py` retained all 7,859 Installed Game files and all
  35,854 Reference Folder files without exclusions at
  `local/verify/parity-catalog-20261101/`. Six graph/inventory mutation tests pass.
  This is not exhaustive semantic coverage, branch discovery or a Parity Proof.
- Shared cooperative admission in `resource_pool.py` now coordinates native
  Runs and full-gate configure/build/CTest phases. Seven process-level tests
  cover contention, atomic requests, deadlines, rollback, crash release and
  inherited root leases. Two concurrent background smoke invocations (four
  actual native Runs) passed through the budget; retained under
  `local/verify/parity-workflow-implementation/admitted-native-repeat/`.
  Estimates, not hard quotas: 12 CPU permits, 24 GiB host, 5 GiB VM, two renderer
  slots, one Stock slot and one build writer. Stock integration, pressure/output
  feedback, fairness, complete descendant accounting and broader capacity sweeps
  remain open. See `iteration.md` for units and limitations.
- Latest full gate: 125/126 CTests passed. Suspension fixture now explicitly wakes
  its Car, satisfying the actual Stock solver precondition while preserving all
  assertions. `object_world_step_unit` remains red with the same independently
  reproduced baseline angular/activity sleep failure. Full gate log:
  `local/verify/parity-workflow-implementation/check-latest.log`. Pending-contact
  research has not justified a production physics correction or assertion waiver.
  A bounded original-code geometry query at the failing step matched selected
  Port candidate distances exactly; it does not execute the complete Stock ground
  list/cache/admission path. The isolated fresh-contact distance-band change
  still failed and was not applied. See `docs/research/contact-admission-causality.md`.
- Post-CTest phases were then executed separately from their exact retained
  script and passed, including all new Python tests and corpus comparisons.
  Log: `local/verify/parity-workflow-implementation/post-ctest-latest.log`.
  Its final `check: ok` remains a phase result, not a green complete gate.
- Jev verified the new inventory figures (support 0.99); full-gate and concurrent
  smoke summaries remained low-confidence review judgments despite the retained
  raw results. The incremental resource module review escalated without a
  concrete defect (`safe_to_apply: 0.27`, correctness confidence 0.04,
  composite 0.58325). This is not readiness approval. Its complete advisory
  result is retained in `local/verify/parity-workflow-implementation/jev-iteration-checkpoint.json`.

## Tracked Stock capture checkpoint (2026-11-01)

- `run_stock.py` and `stock/` now provide the actual isolated ARM64 Stock capture
  from tracked tools, reusing the existing proxies with native mouse-transition
  and validated client-origin observations. Exact Stock SHA-256, inspected image
  ID, frozen source/compiler output, source/disc/texture audits and private data
  identity are retained per attempt. The combined Dockerfile is supplied but
  these runs used the existing inspected image; a fresh build is not claimed.
- The adapter joins the global pool, reserving six CPU permits, 3 GiB host
  estimate, 3 GiB VM, one renderer and the Stock slot. Actual Docker isolation is
  inspected: private mounts only, no host devices/sockets/network, dropped
  capabilities, no new privileges, four guest CPUs, 3 GiB and 256 processes.
  The observer covers the worker and observed host descendants; game display,
  input and final-mix Pulse monitor remain inside the private container.
- Three normal-flow captures passed under
  `local/verify/tracked-stock-adapter-{first,second,final}/`, with real native
  UP/DOWN/UP/DOWN/UP, exactly New Career/Cancel, menu/dialog/menu observations,
  nonzero PCM, source preservation and no owned activation observed. These are
  functional captures; they are not deterministic repeats or paired Parity.
- `tracked-stock-adapter-timeout/` rejects a one-second deadline with a retained
  incomplete/nonzero result, preserved sources and removed container.
  `tracked-stock-adapter-interrupted/` kills the observed worker during game
  execution; the parent rejects the missing verdict, independently audits the
  source and removes the container. The observer also detects its orphaned
  Docker client. Simultaneous supervisor/worker/daemon failures and complete
  descendant lease accounting remain open. See `stock/README.md`.
- Three counterfactual adapter tests reject changed isolation, device/socket
  exposure, reordered/missing input/UI evidence and invalid executable identity.
  The full gate again passed 125/126 CTests with the same object sleep assertion.
  Post-CTest phases, including new Python checks and corpus comparisons, passed
  separately. Logs: `check-stock-adapter.log` and `post-ctest-stock-adapter.log`
  under `local/verify/parity-workflow-implementation/`. No whole green gate claim.
- `docs/research/stock-replay-boundaries.md` establishes that the old API tape's
  separate entry/return ordinals cannot recover total interleaving (320 calls
  have unequal ordinals). A local shared-phase extension passed ABI 640 cases,
  six guards, FX-restoration negative controls and 128 forced two-thread overlap
  rounds. Old sources/captures remain immutable. This is not yet a fresh Stock
  record, OS scheduler trace, stable lane binding or replay. Its Jev review
  remains unaccepted (`safe_to_apply: 0.44`); parent source inspection found the
  changes limited to the shared counter and schema, but promotion remains open.
- `docs/research/stock-early-capture.md` identifies the actual PE entry as a
  candidate earlier interception point. Main-image TLS/delay imports are absent;
  the entry precedes engine clock and worker initialization. PE32 debugger/context
  and remote translated-code invalidation must be qualified on exact Hangover
  before using that design in Stock. Eight observed globals cannot substitute for
  a complete restorable checkpoint. Helper qualification is the next experiment.
- Jev verified the final functional-run claim (confidence 0.96) and interruption
  claim (0.86); deadline/full-gate summaries remained review judgments (0.76/0.60).
  Its patch gate escalated on test-gap confidence 0.11 and safe-to-apply 0.21,
  with no actionable defect text. Retained at
  `local/verify/parity-workflow-implementation/stock-adapter-jev-gate.json`.
  No readiness approval or full workflow completion is claimed.

## Early main-IAT recording checkpoint (2026-11-01)

- `--record-controls` now provides the guarded shared-phase recorder from tracked
  sources. `control_interval.py` validates/normalizes formats2/3/4, retains both
  hashes during comparison, reports exact first differences and enforces finite
  parsing/output bounds. Eight synthetic controls pass, plus actual captured-tape
  bit/removal/phase mutations. All results explicitly deny process replay.
- Actual late interval:71,547complete calls/143,094shared phases with zero
  reported gaps/drops/pending. The normal UI flow, actual private PCM, preserved
  source and no-owned-activation observation still pass.
- The debugger INT3 route failed qualification on the exact backend. Primary
  runtime-source analysis and owned helpers instead qualified a minimal self-IAT
  process-attach bootstrap. Its no-SSE bodies, guarded one-page publication and
  late full-file identity gate are now used by experimental `--early-controls`.
  The executable file and instruction bytes remain unchanged by this recorder.
  Loader activity before this proxy, other DLL consumers and exceptions remain
  outside its claim; initial globals remain observations, not a checkpoint.
- First actual early attempt retained five startup tick gaps and was rejected.
  Stock instructions/decompile established their exact no-argument worker and
  playlist callers; the next attempt passed105,927calls/211,854phases with zero
  reported gaps/drops/pending, actual engine initialization, normal menu/dialog
  flow, private PCM, unchanged sources and no owned activation observed.
  Artifacts: `local/verify/tracked-stock-early-record-{first,second}/`.
  Details and limitations: `stock/control-format.md`.
- Prefix optimization learned a necessary fixture condition: merely stopped Wine
  prefixes may lack persisted registry hives/providers. A new initialized clone
  was checked and successfully restarted without repair; seed bytes remained
  unchanged. Future reuse must declare this fixture and verify copy/source state.
  Host free space is now about77GiB at reinspection, not the older146GiB snapshot.
  No old captures were modified or deleted to recover space.
- Whole-system QEMU replay was researched as an alternate fidelity route. Primary
  sources document single-vCPU TCG/icount constraints and guest-private capture;
  no VM was installed or launched. It does not solve Stock-to-Port projection.
  `docs/research/whole-vm-stock-replay.md` preserves the unrun experiment.
- Live CRT RNG research now supplies an owned post-original-TLS-getter capture
  prototype, preserving the getter and raw machine state. Its controlled matrices
  and FX fault detection pass; earlier unmeasured FLAGS failures remain unresolved.
  It has not been promoted or used in Stock. Raw source/evidence and uncertain
  Jev judgments are retained in `docs/research/stock-live-rng-capture.md`.
- Full gate again passed125/126 CTests; same baseline object sleep assertion.
  Post-CTest phases passed independently, including new interval tests and corpus
  checks. Logs: `check-early-controls.log`/`post-ctest-early-controls.log` under
  `local/verify/parity-workflow-implementation/`. No green full-gate claim.
- The new Jev gate remains unaccepted (safe-to-apply0.09; composite0.52425).
  It verified the guard-regression claim at confidence0.85, left interval/capture
  claims uncertain, and labelled the test-count summary contradicted at review
  confidence0.56. An independent deterministic audit then parsed all126 unique
  test records from the full log:125passed, one object sleep failure. That audit
  and the unchanged test/physics diff preserve the factual count without retrying
  the model judgment. Artifacts: `early-controls-jev-gate.json` and
  `early-controls-gate-counts.json`. This resolves the raw count, not readiness.

## RNG observer qualification checkpoint (2026-11-01, later session)

- The interrupted static-tail draft compiled warning-free (plain, RNG, RNG+test). An owned
  fixture mapped the real Stock bytes and ran the production `control_tape_early_bootstrap`:
  18 independent checks pass (`local/verify/stock-rng-static-tail/checks.json`): exact
  published spans, 272 accepted + 224 refused calls identical to unhooked execution in all
  registers, EFLAGS, LastError and 512 FX bytes over four seeds x four x87/MXCSR settings,
  exact guard reasons, FX-restore omission detected in every case. Four attempts are kept
  (cave off-by-one, relocated mapping, XMM harness residue were harness faults).
  Unattributed: EFLAGS-only differences in the deliberately broken negative build.
  Not qualified: real Stock TLS lazy-init/failure, thread lifetime/TID recycling,
  exceptions/unwind, concurrent callers.
- `control_interval.py` accepts format 5 with exact installation row, caller allowlist,
  LCG/EAX/EFLAGS relations, state-chain continuity and lane/block binding; 13 tests pass.
  `run_stock.py --rng-observer` freezes/compiles the observer and refuses a run with no
  validated RNG record.
- First live record `local/verify/tracked-stock-rng-record-first/` passed: 71,648 calls,
  zero gaps, one real `srand(35247)`, no draws on the boot->menu->dialog route.
  Still a record, not replay or Parity. `object_world_step_unit` remains red: no causal
  fix was found, none was applied, and no assertion was waived.

### Remaining-work checklist (maps to the eight deliverables; nothing below is claimed done)

| # | Deliverable | Done | Pending |
|---|---|---|---|
| 1 | Proof records/evaluator | strict evaluator, mutation tests | real paired capture through it |
| 2 | Content manifest/graph | inventory, 36 candidates, mutation tests | per-content/branch bindings, expected Stock results |
| 3 | Native invisible pipeline | normal intro/dialog, PCM, focus observation | none known beyond limits in README |
| 4 | Stock isolated pipeline | three functional captures, failure controls | ordinary shutdown/CD audio fidelity |
| 5 | Deterministic controls | clock/async/tick recorder, RNG record (above) | input channels (DirectInput, buffered keys, WndProc cursor), thread/sync identity, actual replay of responses, Port projections/controls, same-side repeatability, paired comparison |
| 6 | Scheduler | cooperative pool, FIFO queue/crash pruning, admission pressure and observed native/Stock execution pressure (23 tests) | preparation/build/CTest pressure reaction, output budgets, complete descendant accounting, measured throughput/responsiveness |
| 7 | Developer entry point | `dev.py` test/repro/corpus/coverage/perf/proof/gate lanes, 11 tests, in the gate | `paired` lane (needs deliverable 5 replay); perf baselines on representative workloads |
| 8 | Validation | corruption tests per module; independent review of RNG recorder/validator/dev tool (9 findings: 6 fixed with tests, 3 documented) | real Stock/Port comparison, green full `tools/check.sh`, Jev gate on final evidence, review of the remaining modules |

Replay-design input (primary sources, not a qualified mechanism): `docs/research/replay-design-primary-sources.md`.
rr serializes threads and records switch points, and its progress counter fails on ARM; this host is ARM64 emulation.

Next: integrate qualified RNG/input/identity/synchronization observations; implement
strict schedule/input/RNG replay and same-side repeats, then actual paired proof
production. Expand the catalog to per-content/branch bindings and expected Stock
results. Finish resource supervision/adaptive admission and developer commands.
Resolve the remaining full-gate failure with causal evidence, rerun the complete
gate, and audit every deliverable above before marking this goal achieved.

## Scheduler continuation checkpoint (2026-11-02)

- Shared admission now uses one FIFO waiter queue, with a kernel liveness lock
  per waiter. Later small requests cannot starve an older large request; killed
  and expired waiters do not hold the queue indefinitely. Strict FIFO deliberately
  permits head-of-line blocking, including disjoint resource requests. All clients
  must use the new implementation for this fairness property.
  Four process regressions pass; bypassing FIFO fails the starvation assertion.
  Evidence: `local/verify/parity-pool-fairness-20261102/`.
- `Lease.run_observer` adds pressure checks between bounded execution waits for
  native/Stock focus supervisors. Low disk/memory or unavailable counters trigger
  graceful observer termination, bounded cleanup and a failed attempt. The final
  pressure sample/reasons remain in result records. Owned-console controls use the
  real focus observer, verify its child is gone, and cover both pressure types,
  lost counters, success and an outer deadline. All 23 pool tests pass; deliberately
  ignoring pressure fails with the wrong termination reason (outer timeout).
  This does not qualify abrupt-parent failure, escaped descendants or game capacity.
- The native normal-intro/menu/dialog-script functional run in
  `local/verify/native-execution-pressure-20261102/` exited zero, retained 30 healthy
  pressure samples, no owned activation observed, 1,348,800 private stereo PCM
  frames (2,452,581 nonzero samples), unchanged executable and unchanged source save.
  No deterministic-repeat or paired-Parity claim follows. The changed Stock path
  has adapter tests, not a fresh live Stock qualification in this checkpoint.
- `test_input_interval.py` is now in `tools/check.sh`. Its schema and projections
  remain proposed/offline: no recorder produces this input format yet.
- The latest complete gate still passes 125/126 CTests, failing the same object
  sleep assertion. The exact post-CTest phase passes separately. Frozen sources,
  checks, mutation, functional output and hashes are retained under
  `local/verify/parity-pool-execution-pressure-20261102/`; `post-ctest.log` is not
  a green complete-gate result. No physics source or assertion was changed.
- Both incremental Jev reviews remain unaccepted (fairness safe-to-apply 0.24;
  execution pressure 0.18). The latter labelled the 23-test claim contradicted at
  confidence 0.56. `independent-count-audit.json` deterministically verifies 23
  unique passing production test records and distinguishes the separately failing
  deliberately faulty source variant. Raw judgments remain retained; this count
  audit resolves log arithmetic only and does not approve patch readiness.

All eight deliverables remain the acceptance contract. Actual input/RNG/thread/
synchronization replay, Port consumption controls, same-side repeatability and
paired proof production remain the central next work, alongside content proof
bindings, ordinary Stock shutdown/CD audio, remaining scheduler supervision,
representative performance measurements and the causal full-gate repair.

## Owned input interception checkpoint (2026-11-02)

- Optional `CONTROL_TAPE_INPUT` now implements bounded state/acquire/buffered
  interception in the recorder owner, sharing actual sequence/return/phase/lane
  and stop/drain counters with clock/RNG calls. No extra device read or input
  injection is used. Separate owned format 6 retains raw HRESULTs, pre/post bytes,
  buffered counts, identity/incarnation diagnostics and explicit Gaps. The Stock
  adapter does not compile/enable it, and existing interval validators reject it.
- `input_bind.c` supplies outside-loader-lock binding to an already-loaded owner,
  validates function-table origin and refuses absent/unready/ABI-mismatched owners.
  Two actual owned DLL load orders each produce a six-phase nested input/clock
  sequence from the same owner. No second recorder is initialized.
- Final evidence under
  `local/verify/stock-input-observer-owned-20261102/run-cross-dll/` passes 16
  independent artifact checks. Accepted: 68 input calls plus four nested clock
  calls, 144 contiguous phases, zero gaps/drops/pending. The refusal run retains
  eight expected alias/size gaps; target transparency does not validate that tape.
  64 outer cases per run match full integer/flags/LastError/512-byte FX snapshots,
  original-target entry flags, buffers/counts and canaries. Omitting FX restoration
  fails all 64 cases; six actual-record mutations are rejected.
- The first uncontrolled fixture failed eight EFLAGS comparisons, including two
  unhooked passes. Controlling return flags alone exposed one unstable entry flag.
  Explicitly controlled caller/target flags produce the passing owned controls;
  earlier raw attempts remain retained and do not qualify actual Wine/Stock FLAGS.
  `stock/input-observer.md` describes these limits and runnable qualification tools.
- Plain and RNG builds without the input option have byte-identical `.text`,
  `.rdata` and `.data` sections to the retained prior qualified recorder source.
  Evidence: `non-input-compatibility/checks.json` beneath the input evidence root.
- The full gate remains 125/126 CTests (same object sleep assertion); separate
  post-CTest checks pass. Logs are `check.log` and `post-ctest.log` under that root.
  No physics correction or assertion change was made.
- Jev verifies the bounded audit and unchanged non-input section claims at
  confidence 0.96 and 0.98. Its eight-file patch review remains unaccepted
  (`safe_to_apply=0.19`, limiting correctness confidence 0.01), retained verbatim
  in `jev-gate.json`; no live-use or workflow-readiness approval follows.

Pending before live input use: actual COM owner/device/QI/Release identities,
configuration revisions and Stock caller guards; input translation/geometry and
WndProc observations; thread lifetime/synchronization; concurrent publication,
alias/remap and exception/unwind qualification. The research's complete owned
helper matrix is not yet covered. Then implement actual response replay, Port
consumption controls, same-side repeats and real paired proofs. These bounded
console controls do not complete deliverable 5 or any whole-program proof.

## Arithmetic, command supervision and owned COM checkpoint (2026-11-02)

- REA MCP is now connected. The matching Ghidra snapshot reused existing
  selector/admission/corrector evidence; a focused predictor dossier was added.
  Every REA call in this continuation had Jev before/after. One new pseudocode
  view was blocked at injection probability0.81 and set aside. Existing raw
  disassembly and isolated original-code probes supply the arithmetic evidence.
  Ledger, snapshot and judgments are under
  `local/verify/object-sleep-diagnosis-20261102/rea/`.
- Actual landing/late corrector and compound-prediction probes found arithmetic
  grouping differences. `physics_body.cpp` now follows the original matrix
  multiply/vector instruction order; `physics_friction.cpp` keeps direct-contact
  force and impulse/response radii separate and preserves the distinct normal
  and tangent denominator orders. New regressions went red before the changes.
  Current code matches all13 independently restored Stock corrector inputs and
  all4 predictor inputs exactly. This is not a Stock trajectory or sleep fix.
  The unchanged object test still fails. Evidence:
  `local/verify/object-sleep-diagnosis-20261102/`.
- `Lease.run` supervises CLI-wrapped console preparation/build/CTest commands:
  pressure/lost-counter refusal, default1800s deadline, capped64MiB combined
  stdout/stderr, inherited-group TERM/KILL cleanup.30 pool tests and16 developer
  tests pass; bypassed pressure/output refusal makes the controls fail. Direct
  preparation calls, artifact budgets, escaped descendants, abrupt-parent cleanup
  and representative throughput remain pending. Evidence:
  `local/verify/parity-command-supervision-20261102/`.
- Optional owned COM table observes QI/AddRef/Release without changing the old
  state-only ABI.48 lifecycle calls match raw ABI/FX snapshots,96 shared phases,
  zero gaps; actual private-page release/reuse, stale incarnation/alias refusals,
  FX/retirement omission faults and five tape corruptions are checked. All25
  independent checks pass; six non-input PE sections remain byte-identical.
  This assumes quiescent aliases with one shared reference count. Canonical
  IUnknown, tear-offs, separate vtables, real Wine/Stock creation and lifetimes,
  concurrency, thread/sync identities and Win32 channels remain unqualified.
  Evidence: `local/verify/stock-input-com-lifecycle-20261102/`.
- Complete `tools/check.sh` still stops at125/126 CTests, the same object sleep
  assertion. Its exact post-CTest statements pass separately. Logs:
  `object-sleep-diagnosis-20261102/check-grouping.log` and
  `post-ctest-grouping.log`. No assertion or threshold was waived.
- Jev patch reviews remain unaccepted: command supervisor0.06, owned COM0.10,
  arithmetic0.09. Raw judgments and independent source reviews are retained;
  no readiness or live-use approval is claimed. The eight deliverables remain
  the acceptance contract, including actual replay, corresponding Port controls,
  same-side repeats, real paired proof, per-content bindings/Stock expectations,
  ordinary Stock shutdown/CD audio and remaining supervision/validation.

## Ordinary shutdown and evidence-binding checkpoint (2026-11-02)

A new goal retains all eight deliverables without reducing scope. Starting dirty
file identities and the tracked diff are preserved under
`local/verify/parity-completion-20261102/`; no commit, push, source reset or prior
capture deletion was performed. The initial independent eight-deliverable audit
is in `audit/eight-deliverables-audit.json`. Every deliverable remains partial.

- Console commands now run under an independent guardian with a parent lifeline
  and launch handshake. Launcher death triggers inherited-group cleanup; both
  guardian and command retain admission locks. All 33 pool tests pass. Omitting
  the lifeline or root lease inheritance fails separate actual process controls.
  Parent review caught and repaired the initial root-inheritance regression.
  Escaped groups, simultaneous guardian destruction cleanup, artifact budgets
  and representative throughput/responsiveness remain unqualified. Raw review,
  mutation attempts and frozen sources are in `guardian/`.
- `System.exit()V` now sets an arena latch and returns normally, matching Stock's
  observed flag write. The host observes it at frame/substep boundaries and runs
  its existing Config finalizer. REA MCP instruction/xref evidence and Jev
  before/after judgments are retained in `rea/`; the immutable source was not
  changed. Three focused CTests pass and two isolated omission mutants fail.
  The current dispatch-source evidence hash was refreshed with its exact prior
  source/manifest preserved; all historical evidence hashes remain unchanged.
  No matched/Parity status was promoted. See `native-exit/`.
- Actual native normal intro -> menu -> New Career -> Cancel -> Exit Game -> Yes
  exits zero without a scripted quit fallback. Its run log records the native
  exit request and completed Config finalizer. `native-ordinary-exit-first/`
  retains frames, 1,427,520 private stereo PCM frames at 48 kHz (2,581,221 nonzero
  samples), unchanged probe/source save and no owned activation observed.
- Stock `--ordinary-exit` uses a GUI-subsystem guest waiter holding the actual
  game process handle. `stock-ordinary-exit-fifth/` passes the exact initial
  UP/DOWN/UP/DOWN/UP menu/dialog sequence, then Exit/Yes, WAIT_OBJECT_0 and exit0
  before Wine teardown. Private PCM has 3,095,095 stereo frames (4,215,520 nonzero
  samples), sources remain unchanged and no owned activation was observed.
  Four earlier failed attempts remain intact. Readiness now requires a real
  initial UP; a console launcher and failed polls cannot substitute for it.
  Window evidence is retained before destruction; exit/PCM evidence follows.
  Interval recording with this lane is refused until ordinary draining is
  qualified. CD-track audio and ordinary save/reload remain open.
- An independent review rechecked 19 raw ordinary-exit properties, including PCM,
  focus ownership, source hashes, native script/log, guest process handles and
  teardown ordering. Actual-record mutations are rejected. Jev verified the
  bounded native/Stock/PCM-focus claims at confidence0.80/0.88/0.97. Neither run
  proves deterministic repeatability or paired Parity.
- Catalog output now includes one hash-bound row for each of 7,859 Installed
  Game files: 12 candidate-anchored and 7,847 explicitly unassigned. All files
  remain semantically unverified; all116 named variants remain pending with
  incomplete branch inventories. Optional `--stock-capture` binds three external
  Stock UI frontiers after checking raw producer/source/input/OCR/focus/PCM
  artifacts. It does not promote internal branches or paired proofs. All23
  catalog tests pass; the fresh actual inventory retains all35,854 Reference
  Folder files and34 tree anchors. See `catalog/actual-final/`.
- Advisory patch reviews remain unaccepted (guardian safe-to-apply0.07,
  native exit0.50, catalog0.36). Their raw results and independent source/evidence
  audits remain retained. They do not establish whole-workflow readiness.

The combined complete-gate run in this checkpoint's `check.log` passes125/126
CTests and still fails `object_world_step_unit` at the unchanged sleep assertion.
The exact post-CTest statements then pass separately under the passive observer,
with no owned activation observed (`post-ctest-focus.jsonl.stdout`/`.stderr`).
`combined-check-audit.json` independently counts all126 unique test records.
That separate phase result does not substitute for a passing complete gate.
Remaining acceptance includes actual time/input/RNG/thread/synchronization replay,
Port consumption controls, same-side repeats, real paired proofs through the
strict evaluator, complete content/branch bindings and Stock expectations,
ordinary save/reload/CD audio, remaining resource supervision and measurements,
the causal object-sleep repair, and final independent/Jev audit of all eight.

## Source preservation, saves and structural branches (2026-11-02 continuation)

- Native runs now inventory the complete source game before and after execution,
  including saves, logs and runtime files excluded from the diagnostic inventory.
  A changed source makes the adapter fail. Four adapter tests pass; a mutation
  removing the refusal fails the source-write regression. Actual normal-intro,
  dialog and ordinary-exit evidence in
  `local/verify/parity-native-source-audit-20261102/` independently preserves all
  7,859 source files (458,930,051 bytes), nonempty private PCM and no observed
  owned foreground activation. Independent source review found no concrete defect.
- Actual native New Career -> SaveProof -> Garage -> ordinary exit, followed by a
  separate fresh Load Career -> SaveProof -> Garage -> ordinary exit, is retained
  in `local/verify/parity-save-reload-20261102/`. The independent auditor checks
  full source identity, exact scripts, raw focus/audio evidence, SDAT values and
  byte-identical save lineage through creation, snapshot, load and resave. Three
  parser tests and seven actual-record mutations pass. This is a functional
  save/reload qualification, not deterministic or Stock/Port paired evidence.
- The catalog now retains hash-bound structural rows for all 1,452 shipped class
  files: 1,451 decode, containing 1,491 classes, 5,262 trees, 269,034 nodes and
  5,821 explicit relative branches (9,694 edges). The remaining Dialog.class
  retains its exact identity and CONS-kind-10 decode failure. Every runtime edge
  and semantic assertion remains pending; exception/native/dynamic/data-driven
  alternatives remain outside the structural inventory. Twenty-eight catalog
  tests pass. Actual inventory, independent count/identity/offset audit and frozen
  sources: `local/verify/parity-script-branches-20261102/`.
- Artifact budgets now bound declared private trees before, during and after
  supervised work, with entry/time limits and explicit no-follow symlink policy.
  Actual native/Stock captures and a prelaunch oversized-output refusal are in
  `local/verify/parity-artifact-budget-20261102/`. Forty-three pool tests and eight
  adapter tests pass; omission mutants fail. These controls do not establish
  escaped-descendant cleanup or representative maximum capacity.
- Fresh-ground selector research exactly reproduces the retained original-code
  proposal and fourteen numeric words for two independently restored inputs.
  Five corruptions are rejected. Local integration and deep-support/retained-cache
  experiments still fail the unchanged object sleep assertion. None was promoted
  to production physics. Raw attempts, REA evidence with Jev before/after each
  call, and audits: `local/verify/parity-selector-continuation-20261102/`.

Incremental Jev results remain retained and do not approve whole-workflow
completion. Actual Stock save/reload, real input-lifetime qualification and
representative resource measurements are still being completed. Central open
work remains actual time/input/RNG/thread/synchronization replay, corresponding
Port consumption controls, independent same-side repeats, paired proofs through
the evaluator, Stock-backed semantic coverage, a green complete integration gate
and the final actual-evidence audit of all eight deliverables. The explicitly
approved authentic-CD exception above remains the only scope exception.

## Admission and containment continuation (2026-11-02)

- Stock ordinary save/load now has independent profile, source, PCM, focus and
  process-exit audits in `local/verify/parity-save-reload-20261102/`. The resaved
  career has two changed bytes in the serialized clock word (43240 to43320);
  `exact_resave:false` is retained. This is functional persistence evidence,
  not deterministic paired parity. The three Stock career parser tests are now
  included in `tools/check.sh`. Jev source review remains unaccepted; its
  low-confidence exit-order objection has a separate deterministic source/raw
  marker audit, with command-order inference explicitly distinguished from a
  runtime command trace.
- Original admission executes13 calls across5 minimal fixtures, including
  deletion of a first root's contact before a second-root rejection and the
  asymmetric second-root tie check. A local sequential Port prototype matches
  all13 semantic states with an explicit contact-identity bijection;6 separately
  compiled mutations fail, and independent review checks61 artifact facts.
  Allocation accounting, attachment branches and full gameplay are not thereby
  qualified. Evidence: `parity-contact-admission-continuation-20261102`.
- A synthetic stationary penetrating-box selector witness matches face997 and
  all14 numeric words between original code and the local selector. Original
  runs repeat semantically; only the deep case repeats raw addresses exactly.
  Evidence and limits: `parity-selector-deep-20261102`. Two additional local
  phase/lifecycle experiments still fail the unchanged object test; neither is
  promoted. `parity-admission-phase-20261102` retains sources, failures and a
  new REA cached-contact predicate snapshot with Jev before/after every call.
- Scheduler testing exposed an escaped `setsid` child after root exit and a
  surviving root after guardian destruction. The root-PID-reuse race and narrow
  Darwin zombie-group signal error are repaired and tested. Kernel-backed
  containment remains under qualification; inherited-group cleanup alone is
  not complete process-tree cleanup.
- COM input instrumentation has matching real raw cases only under its explicit
  recorded Box64 `SAFEFLAGS=2` profile. The same binary under `SAFEFLAGS=1`
  reproduces18 EFLAGS differences, preserved without masks. Independent review
  found missing validator checks for recorded CW/MXCSR and nonzero COM launcher
  exit; those must be repaired before the final qualification is promoted.

No full-goal completion is claimed. Actual schedule replay and Port consumption,
same-side repeatability, real paired evaluator runs, semantic proof coverage,
escaped-descendant containment, the physics repair, complete green gate and
final eight-deliverable evidence audit remain required.

## Rotating selector and pending integration (2026-11-02)

The actual restored rotating-selector witness exposed a divisor-angle rounding
difference. Current geometry preserves the unrounded angle through sine; an
isolated compiled geometry test passes and its rounding-omission mutant fails.
The local selector matches the retained original frontier exactly (14numeric
words, plus a separate no-proposal case). The unchanged object sleep test still
fails. Evidence: `local/verify/parity-selector-rotation-20261102/compiled-check-v1/`.
This does not complete production contact scheduling or the full integration gate.

The earlier COM validator omissions are repaired: retained captures pass18 tests
(one positive,17 negative), and independent recheck rejects the four reported
CW/MXCSR/exit corruptions. Actual Stock input adapter, ordinary recorder drain
and kernel containment integration remain pending qualification. Disk pressure
stopped large experiments below the10GiB reserve; small bounded tests resumed
only after pressure recovered. Prior evidence is retained, and no scope is waived.

## Minimized diagnosis and durable capture continuation (2026-11-02)

The unchanged sleep failure now has a retained debug reproducer with eight floor
triangles and no unrelated static object. It still runs all900 steps and reaches
the identical final flags and12 numeric words in about0.12s. Removing the car
registration or another floor tile produces different failures, so those cuts
were rejected. The first contact window was captured twice with identical inputs;
selection, retained geometry and prediction remain separate hypotheses. Evidence:
`local/verify/parity-sleep-minimization-20261102/`. The original full test remains
unchanged and red; this is not a physics repair.

A bounded private-memory Wine prefix route retains exact source/copy manifests,
distinct file inodes, actual module hashes, per-generation stop/wait results and
all changed regular bytes before teardown. The same-image rotating-selector
control passed and matched its retained numeric evidence. Its qualification is
console-only, not a game/video/audio or replay qualification. Evidence:
`local/verify/parity-memory-prefix-20261102/run-94cfceb07b/`.

The external durable recorder's first owned eight-case matrix is incomplete.
The writing-worker ordinary-exit case timed out and required fallback termination;
the raw mapping preserves its WRITING reservation. Sources, prefix changes and
the failed result are retained in
`local/verify/parity-durable-mapping-runtime-20261102/run-42f52d1643/`.
The recorder must not be enabled as complete ordinary Stock capture from this
result. Further diagnosis and full observer ABI qualification remain required.

The new `stock/durable_archive.py` validates frozen mapping/journal bytes and
preserves incomplete reservations and interrupted acknowledgements. Independent
review found two lifecycle omissions; three new regressions went red before
their fixes. Twelve current unit tests pass. Seven actual retained raw pairs
and17 corruptions were rechecked independently; four historical omission mutants
fail their original assertions. `transport_complete` cannot establish target
execution or API completeness. Evidence and the unaccepted initial Jev gate:
`local/verify/parity-durable-archive-20261102/`; independent review:
`parity-durable-writing-diagnostic-20261102/transport-review.json`.

Source review also found the experimental input proxy's required dump-directory
environment missing from the Stock launcher. The container now prepares the
private directory and Windows guest path before starting runtime processes and
refuses existing capture artifacts. Three shell-only tests pass after the
original preflight failed five assertions. Evidence:
`local/verify/parity-stock-input-preflight-20261102/`. This does not replace a
fresh actual input-adapter run. Both new self-contained suites are included in
`tools/check.sh`; the full integration gate has not yet been rerun successfully.

Disk pressure continues to refuse runs below the unchanged10GiB reserve. No old
capture was removed or resource floor bypassed. Actual deterministic replay,
Port consumption, same-side repeats, paired proofs, semantic coverage, production
containment, the sleep repair, the complete green gate and the final audit of all
eight deliverables remain required.

## Prepared probes and audit boundaries (2026-11-02 continuation)

The first-contact original-selector run retained two successful executions at
`local/verify/parity-first-contact-selector-20261102/run-faab415ff3/`.
Raw inspection agrees on the selected face and fourteen proposal words. The
independent pointer/code/body-state auditor and comparator have not completed:
their admission was refused by the disk reserve. This is neither a qualified
contact-admission result nor a repair of the sleep failure.

The retained-query probe in `parity-sleep-retained-query-20261102/` is prepared
to distinguish stale contact features from prediction errors. It freezes the
debug sources and checks dependency identities before and after execution. It
has not compiled or run. Production physics is not changed by this probe.

An additional input-proxy source repair prevents a failed setup from publishing
a usable factory with missing context and checks truncated Win32 path buffers.
Its host-only setup regression fixture is prepared in
`parity-stock-input-preflight-20261102/proxy-initialization/` but has not run.
The earlier three passing container-preflight tests do not validate this new
change, and neither set substitutes for a new owned Windows ABI qualification.

The admission-graph helper and binary durable ENTRY/REPLY wire are still under
source review. Their prepared tests and expected outputs are predictions, not
observations. No Stock replay or complete ordinary recorder is enabled from
these prototypes. The failed writing-worker exit remains an unresolved runtime
failure, with its original evidence preserved.

## New full-scope goal and resumed probes (2026-11-02)

A new active goal preserves all eight deliverables and the existing authentic-CD
exception. Starting diff, dirty-file hashes and complete plan are retained at
`local/verify/parity-goal-20261102-092437/`. Before this checkpoint append, every
starting dirty file still matched its saved hash. No reset, commit, push or prior
evidence deletion occurred. REA MCP session inspection had Jev before/after; no
native session was opened. The independent eight-deliverable audit-v2 in `audit/`
records exact evidence paths and still refuses whole-goal completion.

- Host input setup now passes nine current cases and rejects the retained old
  missing-environment implementation under ASan/UBSan. This is mocked host setup,
  not Windows ABI qualification (`input-preflight/`).
- First-contact original-selector audits complete: face1 and all14 proposal words
  match twice, with body/topology/detour checks. Independent copied-raw controls
  reject five corruptions. Raw pointer repetitions differ; admission, cleanup,
  discovery and trajectory remain unproved (`first-contact-audit/`).
- Retained-query probes repeat the unchanged sleep failure twice. The original
  cached vertex-edge query preserves features[6,3] and returns zero without
  touching initialized outputs; Port's full rewalk differs. All three bodies'106
  state words match captured inputs in both original executions. An exact-witness
  local drop intervention executes in minimized and full unchanged sleep tests;
  both remain red. No production physics changed. Direct-wrapper ABI/FX negative
  controls and complete updater semantics remain open (`retained-query/`).
- Writing-worker shutdown baseline again times out with fallback exit98; the
  diagnostic-marker variant exits37 with its detach record. Reversed case order
  produces the same outcomes. Containers were removed and private prefix changes
  retained with sources preserved. Instrumentation sensitivity remains unresolved;
  neither result qualifies the original ordinary recorder. First run:
  `parity-durable-writing-diagnostic-20261102/run-92e0998737/`; reversed run:
  this goal's `durable-reversed/run-f0f41e81b7/`.
- New `stock/qualify_factory.py` and `stock/check_factory.py` retain bounded owned
  factory execution. Fifth run passes both load orders and rejects six tape
  corruptions each. Independent review checks derivation identities, substitution
  guards, factory-inclusive execution verdict, guest SAFEFLAGS=2, module hashes
  and frozen producers. Raw unhooked integer/EFLAGS/full-FX controls, factory-code
  omission mutants and live Stock consumption remain pending; `qualified:false`
  is retained. Earlier export-name and seed-live refusals remain. Jev gate remains
  escalated (safe-to-apply0.22; composite0.6605), with uncertain mutation-count
  support and verified unqualified-ABI scope. No readiness claim.

Actual schedule replay, Port consumption, independent same-side repeats, paired
evaluator capture, Stock-backed semantic coverage, production containment, causal
sleep repair, green full integration gate and final independent/Jev audit of all
eight remain required. The complete gate was not rerun in this checkpoint; the
original unchanged sleep test remains directly observed red.

## Factory transparency and lifecycle diagnosis continuation (2026-11-02)

Evidence remains under `local/verify/parity-goal-20261102-092437/` unless stated
otherwise. All eight deliverables remain required and the goal remains active.

- Actual raw factory controls exposed EDX and XMM0/1 corruption by the C wrapper
  in both load orders; unhooked repeats matched all560 bytes. The experimental
  proxy now saves/restores entry and normal-return integer/flags/FX state, with
  lazy initialization inside the saved-state boundary. Current-source owned
  `factory-final-positive/` compares all560 entry and return bytes and allfive
  original arguments exactly in both load orders. Actual FX omission and EDX
  corruption variants fail the checker. These are owned resolver/target controls,
  not real Wine device construction or Stock input qualification.
- Twenty setup-refusal cases (ten modes, both load orders) now include missing
  and unready observer binding, refuse twice and call no original factory.
  Eleven recursive calls retain eight shim completions and three explicit
  overflow gaps; overflow forwards without terminating the target. Raw overflow,
  CreateDevice ABI, concurrent publication, thread lifetime and exceptions/unwind
  remain unqualified. No live Stock interception was enabled.
- Independent final review checks current/frozen proxy hashes, raw positive and
  refusal records and both full prefix postmanifests/changed-byte archives in
  `audit/factory-final-review/`. Final incremental Jev gate remains escalated
  (safe-to-apply0.07, composite0.66546); four bounded claims are verified, two
  requiring review. Its raw result and exact input are retained. No readiness
  approval or complete integration-gate claim follows.
- Large prefix-clone reservations reached the cumulative64GiB retention boundary.
  The new optional factory memory-prefix route uses the existing qualified
  read-only seed/private3GiB tmpfs route, archives changed bytes after stopped
  writers, and reserves64MiB of retained artifacts. Successful final/refusal
  captures independently retain full postmanifests and25365 changed bytes each.
  No ledger reset, allowance increase, disk-floor change or prior deletion occurred.
- Shutdown component splits initially implicated owner instrumentation, but the
  controlled identity matrix disproves a deterministic identity outcome. At the
  same `/payload/subject` path, from the identical seed manifest in fresh private
  prefixes, ordered old/new/new/old DLL runs exit98/98/37/37. Each exact identity
  therefore both passes and fails. Old/rebuilt DLLs differ only in six timestamp/
  checksum bytes; executable code/import/relocation bytes match. Initial audit
  used unordered directory discovery as launch order and was rejected; corrected
  log-bound audit and both Jev results remain in `durable-identity-matrix/`.
  The actual-matrix Jev verification remains review-confidence0.67. Two subsequent
  complete-marker repeats exit37 with successful main wait, pre-exit and detach
  markers (`durable-full-marker-repeat/`). Neither repairs or qualifies the
  intermittent baseline. Next diagnostic is a red complete-marker attempt plus
  bounded, identity-bound thread observations before fallback termination.
- General cached vertex-edge rejection leaves the minimized final state bit-exact
  to baseline and both sleep tests red. Two final-step compound predictions match
  original004a3bc0 across106 state words on both bodies in two private generations.
  An initial capture used different library geometry; its result is retained and
  excluded from the exact-baseline claim. Corrected evidence is under
  `retained-query/late-predictor-current-geometry/`.
- Focused REA MCP updater dossier/snapshot was acquired with Jev before/after each
  call and the session closed. The original full updater004a2d10 deletes the one
  reconstructed captured contact after one query, unlinks references and preserves
  all body-state words in both runs. Port retains it as Updated/non-resting with
  migrated features. `retained-query/full-updater/lifecycle-comparison.json` retains
  this real lifecycle difference and limits: omitted surrounding contacts, supplied
  zero material properties and incomplete wrapper ABI fault qualification. Earlier
  exact-witness deletion intervention still does not repair the sleep failure.
  No production physics was changed.

Actual scheduling/input/RNG replay, Port consumption, same-side repeats and paired
proofs, semantic coverage, production containment, causal sleep repair and complete
green integration/final eight-deliverable audit remain open. The full gate was not
rerun in this continuation; no existing assertion was weakened.

## Raw input, durable wire and complete-gate continuation (2026-11-02)

Evidence remains under `local/verify/parity-goal-20261102-092437/`. Independent
`audit/eight-deliverables-audit-v3.json` and its full-gate addendum retain every
deliverable's actual-evidence status. The goal remains active at full scope.

- Actual Wine factory/CreateDevice normal-return snapshots match unhooked
  repeats. The private keyboard fixture observes coherent HWND key messages,
  DirectInput state and buffered events. Earlier captures retain two unsupported
  bulk-input gaps. Format7 now retains the complete caller capacity up to sixteen
  20-byte keyboard records, including untouched tails and actual count values.
  Owned raw controls cover counts1/2/16, unsupported counts0/17/UINT_MAX and actual
  returned-count overflow. Unsupported calls forward with explicit gaps.
- Independent review found that the tape validator accepted gap-free rows with
  missing snapshots; it now rejects them. A separate final-source inspection
  found that the real Wine helper ignored its requested DLL load order. Earlier
  directories named for two orders therefore provide owner-first evidence only.
  `real-bulk16-final-current/` uses the corrected helper and actual load-order
  markers; independent `audit/bulk-load-order-final/` verifies both orders and
  rejects a changed marker. These remain owned fixtures, not Stock consumption
  or complete configuration/thread-lifetime interception.
- `durable_wire.c/.h/.py` now defines version3 entry/reply/gap records with raw
  integer/flags/FX snapshots and five arguments. Typed input metadata retains
  actual counts, device identities and validity; the full320-byte bulk record
  fits the existing1024-byte ring payload exactly. Eighteen Python controls,
  ASan/UBSan native serialization with an independent Python reader, PE32
  compilation and the focused CTest pass. Three compiled count/tail/validity
  omission variants are rejected. Evidence and frozen prior versions are in
  `durable-wire-input/` and `durable-wire-qualification/`. Actual producer,
  external identity binding and replay remain unqualified.
- `dev corpus` previously omitted all eight Python suites wrapped by the shared
  pool. It now retains their gate limits and invokes argument lists without a
  shell. Twenty-one focused tests and independent command/source review pass.
  The incremental wire/corpus Jev gate escalates (safe-to-apply0.21,
  composite0.6805625); its four bounded claims are verified, one at review
  confidence0.76. This is not completion approval.
- Fresh complete `tools/check.sh` invocation passes126/127 CTests and fails the
  unchanged object sleep assertion at line232, with flags0x9 and the same body
  telemetry. `full-gate-wire-v3/focus.jsonl.stdout` is the actual child log;
  the passive observer records no owned activation. The earlier relative-output
  observer invocation refused before starting a child and remains preserved.
  A separately retained post-CTest script stops in the native background test:
  its flag-mode capture passes with private nonempty PCM, but environment-mode
  admission exceeds the cumulative64GiB retention allowance. No full green gate
  or passing whole post-CTest phase is claimed. The ledger, limit and all prior
  evidence remain unchanged; existing-reservation reuse is being reviewed.
- Passive observation of the intermittent owned shutdown failure retains a
  successful main event wait/pre-exit marker but no worker post-SetEvent or DLL
  detach marker. Four stable process-birth/thread snapshots show a native futex
  wait; kernel stack access was denied and no guest unwind is claimed. Four
  debug-log attempts exit normally. They do not repair or explain the red run.
  `durable-passive-repeat/` and `durable-passive-trace/` retain both outcomes.
- Physics discovery invalidated the earlier two-triangle completeness claim.
  Native candidate inventory contains faces[0,1,4,5]. Actual original root-to-leaf
  filtering queues all four in that order in both generations and preserves body
  state. Its zero return is deferred-registration status, not a candidate count.
  Original four-face selection differs from the Port's first correction, and a
  local selector reproduces the original values. Whole-test prototypes remain
  red; full scene/resource grouping and cache lifecycle remain uncertain. No
  production physics source or existing assertion was changed. The conditional
  evidence and invalidated assumptions are indexed by `retained-query/checkpoint.json`.

The user additionally requested scouts for `ref/` resources and useful upstream
GitHub changes. Two agents are inspecting those sources read-only, with isolated
checkouts for any upstream code needed. Scouting has not pulled into the dirty
main checkout or written into `ref/`. Actual replay, paired
proofs, semantic coverage, production containment, causal sleep repair and final
eight-deliverable completion gates remain required.

The completed scouts are retained in `reference-scout/findings.json` and
`upstream-scout/findings.json`. The four source folders in `ref/` are archive
snapshots with no nested Git metadata. The main reverse/compiler/Blender blobs
match their queried upstream trees. New reverse `dev` commit
`cb4060edcabde7b61007659c8f0012175b685c65` provides input/VM research, and newer
`SLRR_rev` code provides profiling methods; neither is a verified parity fix.
The clean sibling `games/street-legal` at
`33eca3c735f55e5a30c8b7b6f5b00f85e3c9a799` documents the same authoritative
executable and supplies original-code constructor/input/VM oracle recipes.
Inspection confirms its collision-world and resource callbacks still have
explicit boundaries, and its Python oracles use Unicorn. No whole-game or
hardware-fidelity claim follows. Its sleep commit logic agrees structurally
with the Port; no threshold change is justified. Selected upstream blobs are
pinned outside `ref/`; main and reference checkouts were not changed.

## Existing-reservation and durable-handoff checkpoint (2026-11-02)

Evidence in this checkpoint is under
`local/verify/parity-goal-20261102-092437/` unless a complete path is given.

- `prepaid_budget.py` now permits bounded captures as unique direct children of
  an exactly registered existing artifact root. It validates the parent path,
  device/inode, reservation amount, ledger identity and non-overlap; serializes
  cooperating writers; and checks both whole-parent and child byte caps. It
  preserves old membership, file hashes and non-access metadata. Reads can
  change atime, which is recorded rather than restored. Scan deadlines and entry
  bounds remain explicit; this is cooperative monitoring, not a kernel quota.
  The retention algorithm and cumulative 64 GiB policy were not changed.
- Actual adapter-protocol tests and both real native background smoke modes
  passed using the already reserved 1 GiB parent
  `local/verify/native-background-tests/attempt-z_x05__f/flag`. No new reservation
  was charged by these captures. Actual flag/environment smoke PCM contains
  respectively 29,760/28,800 stereo frames and 10,750 nonzero samples each.
  Both explicitly bypass intro and therefore remain menu smoke evidence, not
  replacements for the earlier complete native pipeline captures. Passive
  observers report no owned activation; their sampling limits remain explicit.
- Parent and independent audits preserved 476 overlapping per-capture old-entry
  comparisons across 160 distinct historical paths. These are not 476 distinct
  files. Root directory additions and atime effects are disclosed. The actual
  captures left the ledger at 68,258,103,296 bytes charged, unchanged from their
  pre-capture snapshots. `prepaid-prototype/actual-independent-audit.json` and
  `parent-final-resolution.json` bind the bounded findings.
- All 27 prepaid scratch/lifecycle tests pass under the actual gate's inherited
  `SLRR_PROBE_BACKGROUND=1`. The first attempt exposed a toy fixture whose
  intended visible mode was overridden by that environment; its scoped test
  setup was corrected, and both failed and corrected logs were retained. The
  exact post-CTest section of `tools/check.sh` then passed separately, including
  corpus comparisons, with no owned activation observed. Its `check: ok` is
  solely that phase. Full CTest remains 126/127 with the sleep assertion red.
- The original prepaid integration Jev gate remains escalated
  (`safe_to_apply=0.10`, composite 0.5855). Parent review resolved factual
  ambiguity without declaring approval. A later, narrower verification with
  corrected comparison wording supports the comparison count at confidence1,
  the 27-test claim at 0.76 (review), and the separate post-CTest claim at0.96.
  Both results remain in `prepaid-prototype/`; no final readiness is inferred.
- Corrected owned binary handoff
  `input-durable-handoff/run-69b1bcb630/attempt-676d0929` binds the actual suspended
  child PID, main TID and process-creation FILETIME before resume. The external
  collector durably archives the first ENTRY before releasing its held target.
  Normal cases retain three ENTRY/REPLY pairs; forced termination retains one
  ENTRY and zero REPLY; foreign-worker calls retain explicit gaps. Full initialized
  560-byte machine-state images and five arguments match the owned control
  targets. FXSAVE destinations are initialized before capture; arbitrary prior
  reserved bytes are not qualified. The independent raw decoder and 18 negative
  controls are retained. This is an owned synthetic handoff, not Stock replay or
  a qualified real-device recorder; the earlier failed attempts remain intact.
- `audit/eight-deliverables-audit-v4-addendum.json` independently rechecks all
  eight requirements against these artifacts. Actual Stock/Port replay and
  paired proofs, semantic branch coverage, complete production containment,
  causal physics repair, the full green gate and final completion review remain
  required. The goal stays active with the original scope.

The next genuine Wine input handoff attempt,
`input-durable-handoff/run-69b1bcb630/attempt-db9dcdaa`, remains incomplete.
Its state fixture archives 54 ENTRY/REPLY pairs, while its COM fixture archives
124 ENTRY and only 120 REPLY records. Independent struct-only decoding matches
all 178 ENTRY machine images (560 initialized bytes each) and arguments against
the post-restore forwarding gateway. That gateway is outside the Wine target;
it is not an observation of the target's first instruction. A two-slot starved
collector control retains the expected sticky overflow.

All four missing COM replies are genuine outer zero-Release returns after a
nested zero-Release has already retired the same device/incarnation's two
interfaces. The current stateless wire rule rejects zero-Release with zero newly
retired interfaces. The collector correctly refuses completeness. Fixing the
representation requires an explicit causal nested-retirement witness and
negative controls; it does not justify accepting missing retirement generally.
Precheck input refusals also need durable gap/incompleteness qualification before
promotion. The failed compile/preparation attempt and all subsequent raw records
are retained. Parent evidence is `stage2-parent-decode-v2.json` and
`stage2-parent-release-frontier.json`; the first decoder's incorrect vtable-index
divisor and its output remain preserved separately. Jev verifies the bounded
pair/snapshot claim at0.96 and the missing-reply explanation at0.94.

The remaining stale `system/Scripts/render/osd/Dialog.class` is not repaired by
adding constant kind10. A diagnostic 12-byte legacy class-reference layout aligns
all eleven constant chunks, where the apparent kind10 is a string length; all
eleven legacy TREE chunks still fail the current tree decoder. Current Stock's
actual kind4 jump-table/vtable/size-reader bytes establish an 8-byte record,
matching the current parser. REA could not resolve the cited loader as a Ghidra
procedure, so the query failure was retained and the conclusion instead uses
REA raw bytes, the observed error-string xref and independent PE mapping and
instruction decoding. No production parser or reference file was changed.
`dialog-constant/` and `audit/tufa-dialog-*` retain the evidence and uncertainty;
caller version admission and complete legacy semantics remain unverified.
This static investigation added one bounded 8 MiB reservation, bringing total
charged retention to68,266,491,904 bytes under the unchanged68,719,476,736-byte
policy. Earlier unchanged-ledger capture claims remain bound to their historical
snapshots. `preservation-prepaid-handoff-v4.json` finds no unexpected changes
among the initial listed files; it is not a fresh exhaustive reference inventory.
