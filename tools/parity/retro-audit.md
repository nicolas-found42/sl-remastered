# Six environment improvements: completion audit

All six retrospective improvements are implemented and validated locally.
This audit does not complete or resume the earlier eight-deliverable game-parity
goal. The existing full integration gate is still red at object_world_step_unit.

## Evidence locations

Commands and interfaces: [agent-tools.md](agent-tools.md). The source manifest
[session-evidence.json](session-evidence.json) produces the current/history view
through `dev.py evidence-index`; it binds existing evidence without rewriting it.

Validation receipts are retained in the OS temporary directory
`sl-retro-gate-k7zn5y2g` (created November 2): `final-validations/summary.json`,
per-suite logs, `actionlint.log`, `ci-configure-without-private-resources.log`,
`current-evidence.md`, raw `focus.jsonl` and `.stdout`, review request/results and
preservation audit/resolution. Temporary storage may be cleared by the OS; the
commands/tests remain reproducible from the repository. The actual migrated
experiment is separately retained in `sl-retro-real-experiment-33j0syvq`.

## Requirement-by-requirement result

| Improvement | Implementation | Effectiveness evidence |
| --- | --- | --- |
| Propagate experiment failures | `experiment_result.py`, `dev.py experiment`, existing ResourcePool/Lease supervision | Eight actual-child controls: success, unexpected exit, signal, expected negative control, timeout, output overflow, final-report growth refusal and prior-output preservation. Actual current object_world_step_test builds successfully then aborts (-6); wrapper returns 1 and leaves the assertion stage missing, replacing the former success-looking wrapper behavior. |
| Automatic guardrails | Installed `.githooks/pre-commit`, `tools/guardrails.py`, macOS workflow and private dispatch lane | Four controls use actual clang-format/clang-tidy and an isolated Git staging index. Bad staged Python remains rejected despite good working bytes; implicit narrowing rejects and explicit cast passes. Actionlint passes. A fresh source-only checkout copy without private resources configures successfully. Equivalent portable checks pass locally. |
| Read-only capacity | `capacity.py`, `dev.py capacity` | Six controls cover exact bytes, persistent charge after payload removal, recreated identity, alias, incomplete scan and over-budget refusal. Actual ledger reports 68,266,491,904 charged / 68,719,476,736 allowed; exact subtraction gives 452,984,832 bytes. Ledger hash/device/inode/size/mtime/ctime remain unchanged across observation. Actual retained parent usage is 53,034,761 bytes. No admission or Stock prepaid support is invented. |
| Current evidence index | `evidence_index.py`, developer command, session manifest | Five controls cover current/history ordering, missing/changed files, mismatched observations, duplicate attempts and nested bool/int confusion. Actual manifest includes all eight historical audit rows, older/repaired clock attempts, input stage3 and failed full gate. Pending/escalated review and scope limitations remain visible; no automatic deliverable completion. |
| Jev packet construction | `jev_packet.py`, `dev.py jev-packet` | Five controls cover exact source inclusion, missing binding, changed hash, wrong quote/range and oversize refusal. An actual retained CTest excerpt generated a Jev request that verified the narrow failed-gate claim. Adding a missing prior-failure binding refused before submission. Semantic subclaim coverage remains a reviewer/Jev judgment. |
| Phase-aware inspection | `trace_inspect.py`, developer command | Five controls cover exact differences, ambiguous/missing/reversed/malformed rows, replacement during reading and explicit row disambiguation. Actual retained trace shows cache header [0,0] → [107592,4] at lines 14/20. A separate retained contact trace shows state 6 → 7 at lines 50/60 while body/face/feature fields remain equal. No live cache lookup is executed. |

## Checks and review

The final focused run passes **54 tests**: guardrails 4, experiments 8, capacity 6,
packets 5, evidence index 5, trace inspection 5 and existing developer suite 21.
The existing resource-pool suite separately passes **49 tests**, including its
cleanup boundary controls. Those controls retain the pool's documented process
boundary limitations; this task does not claim stronger OS containment.

The fresh full `tools/check.sh` attempt exits 8 with **126/127 CTests** passing.
The failure is the preexisting `speed < 0.3f` assertion with speed 0.467; compare
the retained prior `parent-full-v2-audit.json`. No physics or assertion changes
were made for this goal. The separately executed tooling suites prove their own
results, not a green uninterrupted full gate.

The full Jev review verified six bounded implementation claims but escalated:
safe-to-apply 0.50, composite 0.72425, with low test-gap/trace confidence. It is
preserved unchanged, not treated as automatic approval. Direct source review and
actual adversarial controls resolve the stated uncertainty without weakening
thresholds. That review found a concrete nested bool/int equality bug in index
assertions; canonical JSON comparison and negative/positive tests fix it.
The changed-code review also escalated (safe 0.53, composite 0.87575, low blast
radius confidence), with no concrete defect reported. The parent review confirms
the change is confined to JSON assertion comparison; existing top-level/hash/
ordering checks still pass. No Jev score was retried unchanged for acceptance.

Preservation checks compared 124 listed prior files. The only apparent mismatch
was the workflow plan: its prefix exactly matches the earlier recorded hash and
its entire suffix matches the logged pre-retro append. The resolution is retained
beside the original failed audit. Intentional edits to prior files are limited to
developer-command wiring, check-suite wiring and the README navigation pointer.
New tools/tests/docs/workflow files and the local hooksPath setting are additive.

## Practical limits

The workflow has not run remotely: this session did not commit or push. The
private gate requires an appropriately provisioned labeled runner; no private
assets were uploaded or downloaded. Local syntax/configuration/command validation
is the evidence here, not a claim of a GitHub Actions run.

Hash bindings and explicit declarations establish provenance, not truth of every
claim. Capacity is sampled and does not grant admission. Console experiments
retain existing supervision limits and do not replace native/Stock adapters.
These limits are explicit in interfaces and documentation and do not prevent
the six requested environment improvements from functioning as verified above.
