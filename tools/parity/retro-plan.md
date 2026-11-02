# Agent environment improvements

This work implements the six recommendations from the November 2 retrospective.
The prior eight-deliverable parity goal is not resumed by this work. Preserve the
dirty checkout, all retained evidence and resource/headless/private-audio limits.

Completion requires working integrations and observed effectiveness, not merely
new utilities or unit tests. All six are now implemented and locally validated;
see [retro-audit.md](retro-audit.md) for the final requirement/evidence audit.
Historical checkpoints below retain earlier intermediate states and limitations.

1. Experiment results: propagate compile/child/assertion/cleanup failures through
   the existing resource-pool infrastructure. Exercise real failing children and
   migrate a current experiment path without editing frozen historical attempts.
2. Automatic guardrails: wire existing formatting/lint configurations and checks
   into a fast local hook and macOS CI. Validate intentional violations and clean
   controls; keep private resource checks on an appropriately provisioned runner.
3. Capacity reporting: expose read-only ledger allowance, actual parent usage,
   adapter compatibility and admissible limits through dev.py. Validate against
   the real ledger without charging, releasing or changing reservations.
4. Evidence index: generate current deliverable status from immutable attempt
   records with source hashes, result and review status. Detect stale/missing
   evidence and preserve history; demonstrate on the retained session artifacts.
5. Jev packets: bind each claim to hashed artifacts and exact excerpts; reject
   missing evidence and truncation before submission. Reproduce the retrospective
   packet omission and validate a complete packet; retain Jev escalation policy.
6. Trace inspection: provide read-only named-phase cache/contact comparisons.
   Demonstrate the actual before/after-discovery four-entry cache transition and
   reject ambiguous phases or malformed records.

Final audit must cite current commands/results for each item and obtain a Jev
review of the implementation and evidence. A red full integration gate stays red;
separate existing failures from regressions introduced by this work.

## Implementation checkpoint

Review checkpoint: final Jev packet/result retained in OS temporary evidence
directory `sl-retro-gate-k7zn5y2g`: all six bounded claims verified but gate
escalated (safe0.50/composite0.72425; traceconfidence0.20, testgapconfidence0.24).
Parent review found nested Python bool/int comparison in index assertions;
canonical JSON comparison and a new nested negative/positive control fix it.
Five index and five trace controls now pass. This is a changed-patch follow-up,
not a retry of the unchanged Jev gate. Final requirement audit remains.
Current real object_world_step_test migrated through dev.py experiment in
`sl-retro-real-experiment-33j0syvq`: build0, child-6, wrapper1, final assertion
stage missing, execution_passedfalse. Frozen historical binaries were not rerun
because they embed old writable diagnostic paths.

Latest integration attempt: OS temporary directory `sl-retro-gate-k7zn5y2g`,
observer session45497 terminal exit8, full CTest126/127 with existing
object_world_step_unit speed0.467 failure. Separate seven-suite tooling run
session80438 retains per-suite logs in the same directory. The full gate remains
red; no assertion changes. `actionlint` now passes after shellcheck/export and
custom-runner label fixes. The index now includes eight historical audit rows
plus newer records, and `agent-tools.md` documents all six commands/integrations.
Actual contact observation at v7 first-0.jsonl lines50/60 has state6→7 with all
five selected identity fields unchanged; no full lifecycle claim.

Continuation: experiment runner now retains supervision failures and scans full
pending report bytes before publishing its terminal report. Eight actual-child
controls cover ordinary/expected failure, signals, timeout, output overflow,
final-report budget growth and existing-output preservation. Hook integration
now tested through `git hook run pre-commit` in an isolated temporary repository:
bad staged Python with good working bytes refused; actual narrowing refused;
explicit cast accepted. Four guardrail tests pass. Initial test inspected stdout
only while Git relayed hook diagnostics on stderr; corrected to inspect both.
These supersede narrower counts below; final independent review remains pending.

- `experiment_result.py` and `dev.py experiment`: four actual child controls pass;
  timeout/cleanup/final-budget reporting and historical-case migration remain.
- `capacity.py` and `dev.py capacity`: six controls pass; actual ledger bytes and
  identity/mtime/ctime unchanged. Exact SQLite arithmetic independently confirms
  452984832 bytes = 432 MiB remaining. An earlier Jev low-confidence contradiction
  is retained in the conversation, not retried; deterministic arithmetic resolves
  the number. Concurrency/replacement review and documentation remain.
- `jev_packet.py` and `dev.py jev-packet`: five controls pass; a packet built from
  actual retained CTest output was accepted by Jev for the narrow red-gate claim.
  Missing declared prior-failure evidence refused. Semantic coverage still needs
  Jev/reviewer judgment. Broader malformed-input controls and docs remain.
- `trace_inspect.py`: first actual observation locates cache header words 11/12
  at lines 14 and 20 of full-loop-a54eaebd16/work/first-0.jsonl, changing from
  [0,0] to [107592,4]. Tests, developer wiring and contact-identity demonstration
  remain; this observation is not full cache-lifecycle qualification.
- `.githooks/pre-commit` installed via local core.hooksPath; formatter/Python
  syntax checks inspect staged bytes, and clang-tidy requires matching working
  bytes plus compilation database. Three actual formatter/linter controls pass,
  including narrowing rejection and explicit-cast acceptance. The installed hook
  ran with an empty index. End-to-end staged-file controls remain to be tested.
- `.github/workflows/check.yml` adds macOS portable checks and explicit dispatch
  to a provisioned private-resource runner. No remote workflow has executed;
  dependency/setup review and equivalent local workflow validation remain.
- `evidence_index.py` / `dev.py evidence-index` now validate hashed records and
  exact JSON assertions and render current/history views. Four controls pass.
  `session-evidence.json` exercises prior/repaired clock audits and the red full
  gate; other deliverables are explicitly unindexed, not assumed absent. Broader
  actual evidence population and source/review binding review remain.
- Trace inspector now has three passing controls and developer/CI wiring. Actual
  cache transition was read from retained evidence; contact identity examples,
  frozen-source stability and malformed-selector hardening remain.
- Existing developer suite passes 21 tests after the first three lane additions.
  No new complete integration gate or final six-item audit has run.
