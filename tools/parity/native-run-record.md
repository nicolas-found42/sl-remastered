# Record a native Run

Build `startup_probe` separately, then record one real application launch:

```sh
python3 -I tools/parity/run_native.py local/verify/native-attempt-unique \
  --probe build/startup_probe \
  --game '.scratch/2005-extract/Program Files/Street Legal Racing - Redline' \
  --reference-root "$PWD/ref" \
  --save local/rapid-testing/baseline-data/save
```

Use `--background` while working alongside the Run. It keeps the Cocoa window
hidden, prevents desktop fullscreen and focus activation, omits native startup
movies and leaves the host cursor alone. The normal rapid script still sends
Escape as soon as the splash hotkey is ready. Rendering, captures, stock Script
initialization and simulation continue; this route does not test visible host
display behavior. `SLRR_PROBE_BACKGROUND=1` applies the same mode to existing
direct probe invocations, overriding `--visible`. `tools/check.sh` defaults to
this mode; explicitly setting the variable to `0` retains its visible route.

The default rapid script waits for the actual Main Menu, enters Free Ride through
normal input, waits for the player's physical Car and presented camera, then
holds Up for two logical seconds. `--cold-start` retains the legacy 10/13/13.5-second
menu input and the same driving wait and throttle workload. `--script FILE` accepts
custom scenarios; screenshots must use `@run/NAME`. Neither route restores a VM
snapshot or proves a Career load. The default process timeout is 120 seconds.

Each new output contains `script.txt`, `metadata.json`, a bounded game-data
manifest, `initial-save/` with source hashes, the private writable `data/save/`,
stdout/stderr, `result.json`, and the probe's `run/` evidence (including screenshots
and Cartrace when enabled). Paths are checked for source/reference overlap,
including symlink aliases. Proprietary data and saves must remain local.

`result.json` separates `elapsed_seconds` (subprocess launch through exit) from
`timing.preparation_seconds` (wrapper entry through preparation and metadata,
including the data manifest and fixture copy) and `timing.total_seconds` (wrapper
entry through process exit, fixture verification and milestone extraction).
Build time is excluded. `menu_seconds` and `driving_seconds` are first observed
readiness from entry to the probe's main routine, at millisecond resolution;
missing observations are `null`. `milestones` also records loop time and frame.
The cold route observes these milestones without changing its script clock.
Preparation and total durations use Python's monotonic clock; the probe uses
C++'s steady clock. These origins must not be mixed to infer submillisecond gaps.

Failures and timeouts return nonzero; a zero exit alone does not prove driving.
Inspect the exact input, readiness, screenshots, Cartrace and errors. Traces can
alter timing. Metadata retains inherited `SDL_`/`SLRR_` environment and the
wrapper's trace overrides. The run recorder additionally captures application
configuration. Existing `SLRR_FORCE_SEED` fixes random seeding, not scene timing
or complete replay determinism.

For comparisons, retain one built executable, alternate at least five samples
per route with identical data/save identity, seed, display, audio and tracing.
Report each sample, median and range for all four timing fields, plus process
failures, actual player controls/motion and fixture preservation. Compare the
same post-ready workload; rapid Runs omit cold menu dwell and earlier Career
transitions. See [rapid-benchmark-evidence.md](rapid-benchmark-evidence.md) for the
measured scope and retained local replay/report commands.

`python3 -I tools/parity/summarize_native_runs.py OUTPUT...` prints samples,
failure count, medians and ranges as JSON. It rejects differing executable,
data, fixture or recorded environment identities and differing scripts within a
mode. It counts observed timings explicitly, retaining `null` when unobserved.
This summary does not judge driving outcomes or stock Parity.

`python3 -I tools/parity/test_run_native.py` checks recorder boundaries with a
small external fake process; actual application Runs remain the acceptance seam.
