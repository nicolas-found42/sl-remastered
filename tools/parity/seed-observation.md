# Candidate seed observation points

These are instrumentation targets, not recorded seeds. No stock binary needs to be changed on disk. A debugger or tracer can perturb scheduling and timing, so preserve its logs and compare instrumented with uninstrumented runs before drawing behavioral conclusions.

The verified stock executable's cached decompile `local/census/DecompileAt-0047c7d0.log` identifies `Math.randomize` at 0047c7d0. At 0047c7da the code pushes EAX; at 0047c7db it calls 005d73fb, the seed initializer. At a breakpoint **before** executing the call at 0047c7db, the 32-bit seed argument is the word at ESP and is also in EAX. Record the executable identity, breakpoint address, thread identity, seed, and call order. This is a runtime observation point, not a tested Wine debugger recipe: existing proxies do not intercept this internal call and the previous Wine debugger attach failed (see `docs/wine-comparison.md`). A software breakpoint may temporarily alter process memory. Do not write to `ref/` or the executable on disk.

The native `mathRandomize` in `src/natives/random.cpp` stores the seed in `arena->randomState->state` on lines 16 or 18 and reaches line 19 after either branch. With a Debug build, a debugger breakpoint at line 19 can inspect that state, `ms`, `truncated`, the thread, and the call order. The result is the actual seed assigned, not a seed inferred from time. Source line numbers may change, so check the build's source before using the breakpoint. Each VM thread has its own random state; this observation does not establish which calls choose the car.

On 2026-10-28, an isolated `local/build-parity-trace` Debug build of `startup_probe` succeeded. An LLDB batch attempt resolved that breakpoint, but `run` did not return within 15 seconds; the stopped process group was killed. `local/verify/parity-seed-probe/lldb.log` and `result.txt` record the attempt. No seed was captured and the cause of the stall is unknown. Do not present this as working automation.

The stock selected car was read from the on-screen `Player riding a ...` text in screenshots, not from a recorded random seed; different stock runs produced different cars (`docs/stock-flow.md`). Capture the back buffer and record the visible label and source filename when legible. The native Free Ride run currently fails before a driveable car is shown (`docs/continuation.md`), and the existing run log examined did not identify the chosen model. Mapping its selected resource to a car name needs separate tracing and validation. A seed, even when observed, is insufficient without random-call order, thread identity, state, timing and inputs.

## Native opt-in trace

Set `SLRR_TRACE_RANDOM_SEED=1` when running the native probe to print each successful `Math.randomize` call's already-read clock value and assigned seed to standard error. The trace does not read the clock again or advance the generator. It does not identify VM threads, observe other random calls, or identify the selected car. The output can affect timing, including subsequent clock-derived seeds. Leave the variable unset for normal runs.

An actual native Debug build run on 2026-10-28 reached the Free Ride click and printed `elapsed_ms=558.98016700000005 seed=558`; it subsequently exited 1 at the known unimplemented car wheel joints, scene objects and world registration frontier. The complete output and exit status are in `local/verify/parity-seed-probe/native-flow.{stdout,stderr,exit}`. This is one observation, not a repeatability or stock comparison result.

## Native car creation requests

`SLRR_TRACE_CAR_CREATION=1` prints each car creation request's resource ID, script class and configuration path to standard error. It records the request before the creation attempt; it does not establish that a car was successfully created, registered, or assigned to the player. The default is silent. Trace output may affect timing.

The following command ran the existing timed Free Ride click against the separate Debug build. Run it from the project directory, and choose a fresh log filename to preserve earlier evidence:

```sh
SLRR_TRACE_RANDOM_SEED=1 SLRR_TRACE_CAR_CREATION=1 \
  local/build-parity-trace/startup_probe --init --renderer --loop --visible \
  --script tools/scripts/free-ride-click.txt --data-root local/stock-data \
  '.scratch/2005-extract/Program Files/Street Legal Racing - Redline' \
  > local/verify/parity-seed-probe/native-car-flow.stdout \
  2> local/verify/parity-seed-probe/native-car-flow.stderr
```

The observed request was resource `0x001a0109`, class `java.game.cars.MC_GTB`, and config `cars\racers\MC_data\scripts\_main_GTB.cfg`; its seed trace was `584`. This run exited 1 at the unfinished car wheel joints, scene objects and world registration step. The output and exit status are in `local/verify/parity-seed-probe/native-car-flow.{stdout,stderr,exit}`. The script also saves a hover screenshot at its hard-coded `.scratch/shots/fr-hover.bmp` path, overwriting that file. This run used the shared native save directory; use a separate prepared data root when preserving initial state matters.

The separate Debug build's `car_runtime_test` passed with the trace enabled and printed its test car request; it also passed with the variable unset and emitted no car trace. The default run's output is retained in `local/verify/parity-seed-probe/car-test-default.{stdout,stderr}`. The enabled test output appeared in the terminal but was not saved to a file. Neither test nor the single application run demonstrates repeatability or stock parity.
