# Isolated native Run evidence, 2026-11-01

Ticket #2 extends the existing readiness workflow. It changes host path handling,
not stock Scripts, Natives, or readiness rules. No Native status is promoted.
The Stock Executable remains the authority. Steam evidence does not determine
these host path rules. No Steam behavior was adopted by this change.

The initial native Run from this worktree failed in 0.29 seconds with
`Config constructor: invalid reference root`. After adding an explicit Reference
Folder, the next Run exposed a second cwd dependency: stock cursor extraction.
Both now use `--reference-root`. The launcher runs inside its disposable data
root rather than relying on the checkout as cwd. Recorder and data-root guards
reject overlap with protected roots before startup, including symlink aliases.

The local evidence lives at `/tmp/sl-issue-2/`. It contains proprietary saves,
traces, screenshots, and manifests and must remain local. The durable commands
are `tests/native_run_paths_test.py` and the existing `run_native.py` interface.
The exact application scenarios are preserved in each Run's `script.txt`.

## Identities

The source fixture is the original checkout's
`local/rapid-testing/baseline-data/save`. `verification.json` records identical
before and after SHA-256 maps for every fixture file. Each final Run also reports
`source_save_unchanged: true` and retains `initial-save/manifest.json`.

The Installed Game is the original checkout's
`.scratch/2005-extract/Program Files/Street Legal Racing - Redline`.
The Reference Folder is the original checkout's absolute `ref/`.
`ref/raw/StreetLegal_Redline.bak.exe` has MD5
`e686099e7233bb7d7c461b1b35c5064c`, matching the stock target.

The `rapid-final` executable SHA-256 is
`92be399dd53438d003f3c5c39e487a9f7a6ec94c55f42107d9d20a377e4cfa89`.
Its game-data manifest SHA-256 is
`5c4fb9d07be180f743c2e07bea01ab7fd70e9b328e09e98bf4ab45c7ce21bcfc`.
Exact command, data-manifest contents, trace settings, and script hashes are in
`metadata.json`. The fixture is copied normally. Free Ride is entered through
menu input. This does not prove a later-state Career load.

## Executed application behavior

All application Runs use the real 640×480 window, unchanged stock data,
`SDL_AUDIODRIVER=dummy`, and the existing input mechanism. Positive Runs enable
`SLRR_CAR_TRACE=1`. These checks do not establish physical audio or input support.

| Evidence directory | Observable result |
|---|---|
| `red-cwd` | Existing executable fails with invalid reference root. |
| `green-rapid` | Explicit save root works; cursor extraction still fails from isolated cwd. |
| `green-rapid-2` | Menu ready at loop 3.064 s/frame 369; driving ready at 5.587 s/frame 445; exits 0. |
| `cold` | Legacy timed click at 13 s reaches driving at 16.140 s/frame 1633; exits 0. |
| `rapid-final` | Menu ready at 3.079 s/frame 370; driving ready at 6.636 s/frame 449; exits 0. |
| `background` | `wait driving 2` without menu entry fails in 2.76 s: active state is not a Track. |
| `background-menu` | After actual menu readiness, driving wait fails boundedly without clicking Free Ride. |
| `invalid` | Unknown key fails nonzero in 3.86 s, retaining the exact scenario and diagnostic. |
| `failure` | Existing `fail error` scenario fails nonzero in 3.89 s, retaining the Run diagnosis. |
| `process-timeout` | Launcher timeout of 1 s kills the child and records `timed_out: true`. |

`run-summary.json` extracts frame and wall timestamps from the corresponding
Run logs and Cartrace. In `green-rapid-2`, the player starts stationary and moves
from `[-278.475, 10.536, 1033.002]` to `[-285.307, 10.756, 1033.082]`, ending at
8.15 m/s in gear 1. Controller source is 1 and throttle reaches 1.0. The ordered
keydown and keyup occur at log +6.284 s/frame 447 and +8.211 s/frame 464.
The scheduled hold is 2 seconds. Frame polling produces 1.927 wall seconds.

In `rapid-final`, loading takes longer while the input wait remains blocked.
The subsequent keydown and keyup remain ordered and 2.106 wall seconds apart,
rather than collapsing at the end of loading. The Car reaches 6.15 m/s.
`cold` reaches 7.37 m/s with a 1.901-second observed hold. The frame counter advances through the waits while stock menu and loading
transitions complete. The scheduler and physics retain their existing real
clocks. No artificial disk-loading delay was injected. `cartrace_check.py` passes the final
rapid and cold traces. This check is limited to speed steps, falling, finite
values, and diagnostic tags. It does not establish complete collision Parity.

These Runs are outcome checks, not a new performance benchmark. A full build
ran concurrently with some samples, elapsed-time random seeds differed, and
no speedup is claimed from these timings.

## Limits and checks

The throttle screenshot's HUD still shows zero speed and about 1000 RPM despite
Cartrace motion. The camera renders the player Car and road, allowing observation of driving,
but frames the Car close and partly offscreen from the side. HUD response and
stock Chase Camera framing remain explicit visual Gaps. Readiness and controlled chassis
motion do not establish HUD Parity, full driving fidelity, or any Career Rung.
No stock image or frame-by-frame comparison was performed.

Supplementary checks pass: ProbeScript timing tests, Python save-snapshot and
Run-recorder tests, and native path refusals against disposable game/reference
directories, including symlink aliases. The path regression fails on the old
executable and passes on this implementation.

The initial full project gate result is recorded in `/tmp/sl-issue-2/check.log`:
118 of 120 CTests pass, matching the parent's pre-existing `native_random_unit`
and `object_world_step_unit` failures. The latest integration branch corrects
the random test's invalid expectations without changing production code.
The final gate records 119 of 120 CTests passing in
`/tmp/sl-issue-2/check-final.log`. Only `object_world_step_unit` still fails.

The remainder of the gate was attempted separately in `check-remainder.log`.
It initially stops at the Native evidence audit because ignored census artifacts
are absent from this worktree and the existing VM evidence SHA-256 is stale.
After the parent repairs that local record, `check-remainder-final.log` reports
the audit correctly refusing a symlink to the original census artifact. The
parent validates the original checkout. This ticket does not rewrite artifact
checks or copy the proprietary corpus to bypass that refusal. Direct Run-log integration checks
pass for error, SIGSEGV, abort, throw, SIGTERM, SIGKILL, and partial Runs.
The existing screenshot safety checks and ten replay tests also pass.

## Writable save boundary review correction

Incremental review found that `data/save` could point outside the writable data
root while `FileState` treated its canonical target as trusted. The public
`FileState` regression in `tests/file_state_test.cpp` reproduced deletion and
truncation of a disposable Installed Game sentinel before the fix. It now
checks refusal and preserved bytes for delete, copy, move and write-open through
both save-root and nested symlinks into disposable game/reference trees.

`tests/native_run_paths_test.py` exercises the native process boundary: unsafe
save-root and nested save-file links refuse before recorder output is created.
Contained save links still reach normal startup. Duplicate `--data-root` flags
also refuse before recorder output; previously preflight used the last value
while the main parser used the first. Diagnostic wording is not an assertion.

The isolated positive Run at `/tmp/sl-issue-2-review-positive` used the existing
bundled rapid Free Ride scenario through `run_native.py`, unchanged stock data
and the same source fixture named above. It exited 0 without timing out and
recorded `source_save_unchanged: true`. Menu readiness was observed at loop
3.066 seconds and driving readiness at 5.874 seconds; the driving-ready screenshot
and normal throttle input completed. The final throttle screenshot was not emitted
before the queued quit, so it is not claimed as evidence here. This verifies
continued normal entry with the boundary checks, and makes no new Parity or
Native-status claim.

The repeated rapid/cold comparison and remaining acceptance limits are recorded
in [rapid-benchmark-evidence.md](rapid-benchmark-evidence.md). This supersedes no
Native or Parity status from the isolated-path evidence above.
