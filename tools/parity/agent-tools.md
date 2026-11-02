# Agent environment commands

Run these commands from the repository root. They extend `dev.py`; the full
integration gate remains `tools/check.sh`. Local evidence and game assets stay
local. Commands print reports to stdout unless an output directory is specified.

## Experiment stages

`python3 -I tools/parity/dev.py experiment manifest.json NEW_OUTPUT_DIRECTORY`

The manifest is a JSON object containing `stages`, an ordered array of objects
with `name`, `command` (an argv array), and optional integer `expected_exit`.
Compile, execute and assert as separate named stages. Stop at the first unexpected
exit. Expected refusal controls must declare their exit before execution.

The console runner uses the existing resource pool, pressure supervision, a
60-second per-stage timeout, 1 MiB output limit per stage and an 8 MiB artifact
limit. Repository-local capture roots charge the existing retention ledger.
The standalone `experiment_result.py` accepts `--timeout`. Both console entry
points accept explicit `--artifact-links count` for recorder `latest` links;
the default rejects links. Count charges link metadata without following it and
does not prove target isolation. Native game and Stock
captures must keep using their specialized adapters for focus/audio/isolation;
this console runner does not grant those capabilities.

The new output directory contains numbered logs and `result.json`. Unexpected
exit, signal, timeout, output limit, missing stage or final artifact refusal makes
the wrapper nonzero. A final artifact refusal preserves the pending report and
publishes a failed result. `execution_passed` never proves parity. Existing output
directories refuse without modification. Sampled accounting is not a filesystem
write sandbox; commands must keep outputs within their declared tree.

## Local and CI guardrails

Install for a new checkout with `git config --local core.hooksPath .githooks`.
The hook checks the actual staged Python syntax and C/C++ formatting. C/C++ lint
also requires working bytes to match staged bytes and a CMake compilation database
at `build/compile_commands.json`; run the normal configure step first. Install
LLVM's `clang-format` and `clang-tidy` on PATH (Homebrew LLVM is detected locally).
Checks use `.clang-format` and `.clang-tidy`, promote lint warnings to errors,
and never format files automatically. Stage selected changes normally; the hook
does not stage or rewrite unrelated work.

`.github/workflows/check.yml` runs changed-source checks and portable tooling
controls on macOS. The full private-resource job is an explicit workflow dispatch
to a runner labeled `sl-private-resources` with the same private prerequisites as
the local full gate. Provision game/reference resources locally on that runner;
the workflow does not upload or fetch them. Remote execution is separate from
local validation of the workflow and its commands. `actionlint` validates YAML,
expressions and embedded shell scripts; its custom runner label is documented in
`.github/actionlint.yaml`.

## Capacity observation

`python3 -I tools/parity/dev.py capacity --parent EXISTING_REGISTERED_ROOT`

Without `--parent`, report the read-only ledger allowance and total charge.
With a parent, additionally validate its registered path/device/inode and scan
logical usage under a five-second/100,000-entry bound. Failed or incomplete scans
refuse. Report native and experimental Stock child size candidates. Stock prepaid
reuse requires the qualified ephemeral-prefix route described in `storage.md`.
`admission_granted` is always false: actual adapter checks, writer
locks, free-disk reserve and preservation checks still run at launch. Deleting
files does not reduce ledger charges. Link/socket metadata is counted without
following links or connecting sockets.

## Current evidence index

`python3 -I tools/parity/dev.py evidence-index tools/parity/session-evidence.json --markdown`

The session manifest includes the prior eight-deliverable audit plus newer clock
and full-check records. It preserves historical findings and explicitly orders
attempts per deliverable; it does not resume that prior goal. An index record
contains `id`, `deliverable`, integer `sequence`, bounded `scope`, declared
`outcome`, declared `review`, `evidence` file/hash bindings and `assertions`.
Assertions name an evidence id, JSON pointer and exact `equals` value. Each run
rehashes files and checks assertions; missing/changed evidence makes the command
nonzero and remains visible in both current and history views. Review and outcome
are declared metadata, not an independently inferred approval. Completion remains
unproven until the full requirement audit. Add a new ordered attempt instead of
rewriting historical records. Redirect stdout to a fresh path for a handoff view.

## Jev evidence packets

`python3 -I tools/parity/dev.py jev-packet manifest.json`

Manifest schema 1 has `claims` and `evidence`. Each claim has `text` and a nonempty
`requires` list of evidence ids. Every evidence binding has `path`, `sha256`,
one-based inclusive `start_line`/`end_line`, and `quote` matching those lines
exactly, including newlines. Up to 16 claims and 16 evidence items are accepted.
Sources are bounded at 8 MiB; the serialized request is bounded at 32,000
characters. Oversized requests refuse; split claims explicitly instead of losing
the tail. Unbound evidence and missing bindings refuse.

Output contains `tool`, `arguments` and provenance. Submit exactly `arguments`
to the named Jev MCP tool after the applicable content screening. The command
does not contact a provider, lower thresholds, grant approval or prove support.
Optional `request` text and a bound `diff` excerpt produce a `jev_gate` packet;
otherwise the packet targets `jev_verify`. Mechanical coverage checks only know
declared requirements: reviewers/Jev still determine whether every claim is
actually supported, including omitted subclaims.

## Trace phases

`python3 -I tools/parity/dev.py trace-inspect TRACE.jsonl --before SELECTOR_JSON --after SELECTOR_JSON --fields FIELD_JSON`

Selectors match exact typed JSON fields. For the retained physics body trace,
select `type`, `phase`, `step`, `index` and `correction`. Historical rows lacking an occurrence id can also use
`"@line":50` to bind an exact row; retain the reported source hash with that
selection. Reused raw pointer values alone are not stable contact identities.
Fields map names to
zero-based unsigned 32-bit word offsets. For example, cache header fields are
`{"cache_head":11,"cache_count":12}` for the qualified leaf fixture. The tool
returns source SHA-256, exact row numbers, selected values and changed fields.
Missing/duplicate phases, reversed order, invalid words and exceeded bounds
refuse. It reads retained files only; it never calls cache lookups that could
change LRU state. Offsets are explicit fixture knowledge, not inferred layouts.
Contact comparisons require the applicable captured words and explicit identity
fields; header observations alone do not establish contact/cache lifecycle.
