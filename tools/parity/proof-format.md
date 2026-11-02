# Bounded proof records (schema 1)

Run `python3 -I tools/parity/proof.py CONTRACT.json RUN.json RUN.json ...`.
Exit 0 means `matched` for a paired comparison, or `repeatable` when explicitly
using `--mode repeatability`. Every other result exits 1. A matched result covers
only this contract and these observations; it does not promote an entire family.

The contract declares `schema: 1`, a nonempty `id` and Stock `authority` reference,
`stock_executable` SHA-256, and `inputs` containing SHA-256 identities for `game`,
`fixture`, and `scenario`. `boundaries` is an ordered, nonempty array of unique
observation identifiers ending with `terminal_boundary`. `observations` names
the exact state fields required at every boundary. `stimulus_sha256` hashes the
canonical encoding of the ordered array of `{boundary, stimulus}` entries.
Canonical encoding is `proof.encoded`; it retains JSON types and float64 bits.
Stimulus must include the consumed time/input/RNG controls specified by that
contract, not merely commands the operating system was asked to deliver.

`capabilities` names mandatory capture capabilities, including `focus_preserved`
and `private_audio`. Additional required capabilities should name full intro,
video, mixed audio, source-preservation and control-coverage requirements as the
corresponding observers are implemented. Do not remove a required capability to
get a partial producer through evaluation.

Every run manifest has a unique UUID `run_id`, `schema: 1`, `contract` id,
`implementation` (`stock` or `port`), and `identity` containing the four hashes
`executable`, `game`, `fixture`, `scenario`, plus a nonempty `environment` object.
It explicitly records `complete: true`, `dropped_records: 0`, `exit_code: 0`,
`timed_out: false` and `source_preserved: true`. `coverage` equals the required
observation names. `tape` is `{path, sha256}` for run-local JSONL transitions.
Each required capability has `{verified: true, artifact: {path, sha256}}`.
Paths must be relative, contained inside the Run directory and not symlinks.
`execution_owner` is the observer's kernel-recorded root identity, including
`pid`, `pgid`, `start_sec`, `start_usec`, `exe` and `read_ok`. Each compared Run
must have a distinct execution identity, not merely a newly assigned UUID.

`focus_preserved` refers to raw schema-1 observer JSONL, not a boolean summary.
The evaluator checks ordered records, lifecycle, ownership, polling gaps and
activation results, and binds the root to `execution_owner`. User-app changes
are permitted; owned activation, unknown ancestry, timeout or orphaned children
cannot pass. These are bounded observations, not a guarantee of every possible
activation between samples.

`private_audio` refers to a schema-1 capture summary with `complete: true`,
`physical_output: false`, `encoding`, `channels`, `rate`, `container`, `frames`,
`nonzero_samples`, plus run-local `path` and `sha256` of the actual PCM. Supported
routes are `sdl-disk`/`physical_final_mix` and
`pulse-null-monitor`/`sink_monitor`. The evaluator inspects the captured bytes
and checks their hash, format and counts independently. Optional contract field
`minimum_audio_nonzero_samples` prevents silence from satisfying a scenario that
must produce sound. No sound is played during inspection. Native device format
comes from `audio-mix.raw.spec.jsonl`, retained when outputs are opened.

Tape rows contain contiguous integer `sequence` starting at zero, `boundary`, a
nonempty `stimulus` object and `observations` with exactly the required fields.
Missing records, extra/missing/reordered boundaries, different contract inputs,
changed stimulus, wrong Stock executable, mismatched hashes and unavailable
capabilities are inconclusive. Duplicate JSON keys and nonfinite values are
rejected. IDs, booleans, integers, strings, arrays and objects compare exactly.

For paired evaluation supply at least two independent records per side. Within
each side executable/environment and all observations must match exactly before
cross-side comparison. Reusing one run id or path is rejected. The first paired
state difference is reported with its boundary and field. No raw pointer/memory
serialization is appropriate across x86 and arm64; producers emit named semantic
fields with documented stable identities.

Optional `tolerances` maps an observation name to `{kind: "numeric", absolute,
relative, evidence}`. Both tolerances must be finite and nonnegative; evidence is
mandatory. Only scalar floating-point values receive tolerance; discrete integer
or boolean fields do not. Exact comparison remains the default. A tolerance
cannot repair repeatability failures or differing input schedules.

## Trust and implementation boundary

The evaluator checks structural consistency, content hashes and declared
coverage. A manifest is not independent proof that an observer behaved honestly.
Real capture producers and their observers must be tested against known failures,
including foreground theft, accidentally discarded PCM and missing input samples.
Focus and audio artifacts have the validated schemas above. Other capability
names are explicitly unsupported until their observers are implemented and
verified. These schemas do not authenticate dishonest producers; source and
runtime guard validation remain required. Hashing a fabricated assertion cannot
turn it into runtime evidence.

`test_proof.py` uses synthetic fixtures to test evaluator acceptance/rejection.
These fixtures are never Stock/Port execution evidence. Real capture integration,
deterministic controls, observer validation, corpus/branch catalog and scheduler
remain required before the complete workflow is verified.
