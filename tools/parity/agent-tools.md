# Agent environment commands

Run these commands from the repository root. They extend `dev.py`; the full
integration gate remains `tools/check.sh`. Local evidence and game assets stay
local. Commands print reports to stdout unless an output directory is specified.

## Command selection

Use Python 3.11+ from the project environment (`source .venv/bin/activate` after
setup). Run `python3 -I tools/parity/dev.py --help` for lane options. Commands
below are run from the repository root; replace bracketed placeholders.

| When | Command | Done when |
| --- | --- | --- |
| New checkout, or dependency/tool policy changed | `python3 -I tools/parity/dev.py setup` then `source .venv/bin/activate` | Dependencies install and the local hook is configured. Use `setup --skip-brew` when native tools are already provisioned. |
| Start a session, or diagnose a tool/version problem | `python3 -I tools/parity/dev.py doctor` | Required tool checks pass. A missing database means configure before deep native lint. |
| New native build, or CMake/preset/dependency changes | `python3 -I tools/parity/dev.py configure` | The chosen build tree has current compilation commands. Default is Release in `build`. |
| After a behavior change | `python3 -I tools/parity/dev.py test '<test-regex>'` | The target builds and matching tests pass; an empty selection fails. Requires the selected tests' real prerequisites. |
| Inspect unstaged source checks | `python3 -I tools/parity/dev.py lint --paths <files...>` | Selected working files pass. Native files are checked in full in this mode. |
| New files or intentional whole-file style cleanup | `python3 -I tools/parity/dev.py format <files...>` | Review the working diff, then stage the intended changes. Formatting never stages files. For ordinary legacy edits, fix the reported edited range without unrelated cleanup. |
| Before a commit | Stage intended files, then `python3 -I tools/parity/dev.py lint` | Actual index bytes pass fast checks. The hook repeats this check; it does not run clang-tidy. |
| Before completing a native source/header change | `python3 -I tools/parity/dev.py lint --staged --tidy --build-dir build --translation-unit @all` | Exact compile variants and actual header dependencies pass deep analysis. Stage matching dependencies/configuration first. An explicit real includer can replace `@all`. |
| Validate an asset-free clone or the public native CI slice | `python3 -I tools/parity/dev.py portable` | All selected portable tests pass. This is a small Release slice, separate from the complete gate. |
| Diagnose a repeatable test failure | `python3 -I tools/parity/dev.py repro <exact-test-name> --runs 3` | Inspect exit codes and verdict signatures. Repeated failure remains red. |
| Run ordered Python suites independently | `python3 -I tools/parity/dev.py corpus` | Every suite succeeds in gate order. This is not a replacement for a failed complete invocation. |
| Finish implementation/review with private prerequisites available | `python3 -I tools/parity/dev.py gate` | One complete invocation succeeds. Otherwise report the failed stage, actual failures and unexecuted stages separately. |
| Check a resumed handoff or an evidence claim | `python3 -I tools/parity/dev.py evidence-index tools/parity/session-evidence.json --markdown` | All bindings/assertions validate; missing private evidence remains unverified. |

Normal sequence: read the task and preserve existing edits → diagnose/configure
as needed → edit and run focused tests → stage intended files and run fast lint →
run deep lint for native changes → run the applicable portable/corpus checks →
run the complete gate and report its actual result. Commit/push only when asked.
Use `--dry-run` before the lane name to inspect a plan without executing it.

For Debug or sanitizer diagnostics, configure the corresponding preset and use
its build directory for deep lint. `portable` always configures the Release
preset, even with a custom `--build-dir`; `--no-build` tests an already configured
and built tree. For example, after `configure --preset ninja-sanitize`:

```sh
python3 -I tools/parity/resource_pool.py --cpu 2 --host-memory 6 --build-writer 1 -- \
  cmake --build --preset sanitize --target portable-tests --parallel 2
python3 -I tools/parity/dev.py portable --build-dir build-sanitize --no-build
```

Ordinary `test`, `repro` and `perf` lanes use the canonical Release `build` tree.
Capture/Stock work must follow the specialized sections below; none of the
commands above authorizes resuming a paused product goal or widening acceptance.

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

`python3 -I tools/parity/dev.py setup` explicitly installs the Brewfile inventory,
creates `.venv`, installs pinned `requirements-dev.txt` and sets the local hook
path to `.githooks`. Activate that environment before running commands. Use
`setup --skip-brew` for Python/hook setup only. Python 3.11+ is required; hosted
CI selects 3.13. `doctor [--json] [--build-dir PATH]` reports actual tool versions,
SDK/compiler identity and compilation-database coverage without installing,
configuring or launching anything. The Brewfile is not a native dependency lock;
`tools/devtools.json` checks the chosen lint tool family.

`dev.py configure [--preset ninja-release|ninja-debug|ninja-sanitize]` holds shared
build admission. Release uses `build`, Debug `build/preset-debug`, and optional
full-library sanitizer diagnostics `build-sanitize`. Keep sanitizer artifacts
separate from parity/performance/release builds. Use ignored CMakeUserPresets.json
for local overrides. Formatter/tidy resolution prefers `SLRR_LLVM_PREFIX`, then
the same Homebrew LLVM installation, before PATH; it does not swap the compiler.

`dev.py lint` checks staged bytes by default. Use `--base REV` for committed
changes, or `--paths FILE...` for explicit working files. Fast checks cover AST
plus pinned Ruff error rules, ShellCheck with the actual shell dialect, and
native formatting. New native files and explicit working-path checks require
full formatting; staged/committed edits check edited ranges to avoid rewriting
unrelated historical debt. The hook uses
these fast checks without a configured build or automatic file writes.

`dev.py lint --staged --tidy --build-dir build --translation-unit @all` adds deep
analysis. A header needs explicit real includers (or `@all` to inspect configured
TUs). Exact compile commands and actual compiler dependencies are checked; staged
or HEAD analysis refuses differing tracked source/configuration/dependency bytes.
Foreign-toolchain sources need their real database, not guessed native flags.
This is not a full isolated Git-index build. CI runs fast and deep checks as
separate named steps. Diagnostics and missing tools fail explicitly.

`dev.py format FILE...` is an explicit working-file write for native/Python
formatters, never staging. It refuses conflicting partial staging or aliases.
Python formatting remains opt-in; Ruff lint applies independently. `.editorconfig`
and narrow `.gitattributes` source rules avoid normalizing retained evidence.

`dev.py portable` builds only the audited asset-free `portable-tests` target and
runs serial `portable` CTests with no-match refusal and named failed/unexecuted
stages. Its smaller build budget fits the hosted ARM64 runner without changing
the local pool capacity, disk reserve, full test order or acceptance criteria.
Hosted CI uses supported macOS 15 ARM64, pinned action revisions, Python/Ruff,
workflow/shell checks, this native slice and separate deep analysis. Local
validation does not prove a remote workflow run.
The full private-resource job remains an explicit workflow dispatch
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

## Opt-in Stock intent admission

`ResourcePool(..., protocol=2)` is an opt-in library seam. The default remains
legacy protocol 1; no live pool is migrated and no Stock capture caller enables
intents. Protocol 2 adds no permits, retention allowance or disk-floor changes.

`lease.begin_stock_creation(spec)` requires an active one-Stock reservation and
accepts `container_name` (`slrr-parity-` plus 32 hex digits), pinned `image_id`
and `configuration_sha256`. It commits the creation record and its hash/identity
binding in the pool envelope before returning. One permit set authorizes one
unretired intent. Guardians must receive and acknowledge all
`lease.descriptors`, including its owner lock, before any external creation.

Admission rereads committed state under the existing mutex. Healthy intents
retain their actual permits and allow remaining capacity to run. Unresolved
holder death, lost permits, missing/corrupt records, replaced identities or
partial commits refuse new admission, even after all kernel locks disappear.
Committed permit names also remain logically reserved throughout admission;
holder death between validation and acquisition cannot recycle those slots.
The final liveness check shares the original admission deadline. Protocol-2
wire capacities and identity words require exact integer types.
There is deliberately no retirement, reset or force-clear API yet. An empty
Docker lookup cannot resolve an uncertain asynchronous creation.

`ResourcePool.migrate_stock_intents(canonical_pool, exact_capacity)` is an
explicit reviewed migration API, never an import/admission side effect. Before
using it on a live pool, qualify terminal exact-ID cleanup and coordinate every
client's protocol rollout. It requires idle resource/waiter locks, preserves the
admission-lock inode and capacity values, archives old lock inodes, installs
versioned permits and legacy refusal directories, and atomically commits protocol
2 last. New code refuses partial/unknown state; there is no automatic migration
recovery. A frozen legacy client may still use an unmodified legacy slot during
an interrupted migration, but protocol 2 and its intent API cannot activate in
that state. Completed migration rejects cached legacy clients too.

The existing resource-pool suite exercises this seam using scratch pools only.
It does not qualify Docker lifecycle cleanup, pausing, whole-game containment or
D6 completion. Pre-v2 orphan limitations remain in effect for current captures.

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
