# Rapid/cold Run benchmark, 2026-11-01

The measured rapid route reduces repeated setup by removing the legacy Main Menu
dwell. Median total duration is 10.199 seconds rapid versus 20.509 seconds cold,
five alternating Runs per route. Preparation and menu readiness are similar.
This measures the executable identified below. Ticket #3 acceptance remains
pending assessment of the owner's remaining menu-navigation latency request and
the separate direct-native save-symlink repair. Neither is fixed by this report.

## Measurement and artifacts

Private evidence is retained at
`/Users/nicolas/Documents/github/sl-remastered/local/issue-1/ticket-3-evidence/`.
`final-samples/` contains ten complete records, both screenshots in every record,
exact scripts, commands, identities, copied saves, logs and full Cartrace.
`repeat-capture.py final-samples-NEW` repeats the batch. `report.py BATCH` regenerates
player motion, controls and diagnostic summaries. These scripts are retained
locally beside their inputs; proprietary data is not committed.

The tracked timing report is independently repeatable without launching a game:

```sh
python3 -I tools/parity/summarize_native_runs.py \
  /Users/nicolas/Documents/github/sl-remastered/local/issue-1/ticket-3-evidence/final-samples/??-*
```

`timings.json` contains every timing sample and checked identity.
`report.json` contains observed work. `preservation.json` and
`game-data-after.json` record post-batch preservation checks.
`startup_probe` is the exact retained Release arm64 executable, built from
integration `fb99db4` plus this passive milestone instrumentation.
`measured-startup_probe.cpp`, `CMakeCache.txt` and `build.log` retain the build context.

Executable SHA-256: `f23352b85881aeb9ba95e779a41a02d224dab4e51ea8ee58769c32b4894649d9`.

Game-data manifest SHA-256: `5c4fb9d07be180f743c2e07bea01ab7fd70e9b328e09e98bf4ab45c7ce21bcfc`.

The Installed Game is the original checkout's clean `.scratch/2005-extract/`
installation. The source save is `local/rapid-testing/baseline-data/save`.
All ten source-save hash maps match, including the batch's before/after map.
The complete prelaunch and post-batch game-data file hash maps match.
The Reference Folder is explicitly the original checkout's `ref/`.
Stock Executable MD5 remains `e686099e7233bb7d7c461b1b35c5064c`.
All writes use separate data/output roots through the guarded Python Run interface.
No source save, Installed Game, Reference Folder or Steam file was changed.

Every Run uses the same executable, fixture, actual 640×480 visible window,
`SDL_AUDIODRIVER=dummy`, `SLRR_CAR_TRACE=1`, `SLRR_FORCE_SEED=555`, and the wrapper's
seed/car-creation traces. Forced Seed selects the same Car; it does not make
frame progression or replay deterministic. Stock Scripts remain unchanged.
No Steam-derived behavior was introduced and no Native status is promoted.

## Samples

All durations below are seconds. Preparation includes the data manifest and save
copy, excluding build time. Menu and driving start at `probeMain`, after native
recorder preparation, and report the first actually observed readiness frame.
The corresponding native log timestamps include about 0.07 seconds of recorder
startup in the inspected pair. Total includes wrapper preparation, the entire
child process and source-save verification. See `native-run-record.md` for clock
origins and fields; these observations do not measure operating-system cold caches.

| Run | Preparation | Menu | Driving | Total |
|---|---:|---:|---:|---:|
| 01-rapid | 0.441 | 3.486 | 6.003 | 10.034 |
| 01-cold | 0.436 | 3.463 | 16.340 | 20.412 |
| 02-rapid | 0.487 | 3.487 | 6.038 | 10.216 |
| 02-cold | 0.445 | 3.463 | 16.331 | 20.446 |
| 03-rapid | 0.463 | 3.484 | 6.059 | 10.199 |
| 03-cold | 0.443 | 3.493 | 16.429 | 20.514 |
| 04-rapid | 0.481 | 3.509 | 6.087 | 10.263 |
| 04-cold | 0.461 | 3.479 | 16.370 | 20.530 |
| 05-rapid | 0.443 | 3.474 | 6.044 | 10.162 |
| 05-cold | 0.436 | 3.500 | 16.393 | 20.509 |

Median and full observed range:

| Route | Preparation | Menu | Driving | Total |
|---|---:|---:|---:|---:|
| rapid | 0.463 (0.441–0.487) | 3.486 (3.474–3.509) | 6.044 (6.003–6.087) | 10.199 (10.034–10.263) |
| cold | 0.443 (0.436–0.461) | 3.479 (3.463–3.500) | 16.370 (16.331–16.429) | 20.509 (20.412–20.530) |

The 10.310-second total median difference exceeds the 0.229-second rapid and
0.118-second cold total ranges. The improvement comes from omitted menu dwell,
not faster preparation, startup or physics. Actual menu readiness still takes
about 3.06 loop seconds. This remaining latency is explicit and under assessment.

The machine has 18 logical CPUs. Builds stopped before measurement. Desktop
activity and a few small Python checks were present; this was not an isolated
benchmark host. The routes alternate to limit drift. `machine-during-samples.json`
retains load/process evidence. The unreported `profile-cold/` Run and
`profile-sample.txt` show 593 of 610 main-thread samples in `renderActiveViewport`,
476 beneath `CameraFrame::draw`; `profile-top.txt` reports 63.3% process CPU.
Software rendering dominates the profiled driving frame. The cold route's
fixed menu schedule is the removable wait. No render throughput gain is claimed.

## Actual work and limits

All ten final Runs exit zero without process timeouts and have both driving
screenshots. The player Car is `java.game.cars.Emer_Nonus_Street_GT`, resource
`0x001b0006`, starting at `[-278.475, 10.536, 1033.002]`, with controller source 1.
Throttle reaches 1.0 in all samples. Input keydown precedes keyup, with observed
holds 1.925–2.016 seconds for a two-second logical schedule. Player traces have
164–170 physics rows; all Cars move roughly 13 metres and reach 8.21–8.49 m/s.
Final gear is 1 except `01-rapid`, which reaches gear 2. Trajectories and gearbox
state are therefore comparable driving outcomes, not identical replays.

The post-readiness workload is identical: ready screenshot at +0.0, Up down at
+0.1, Up up at +2.1, throttle screenshot at +2.2, quit at +3.3 logical seconds.
Only the route to driving differs. The cold route keeps timed 10/13/13.5-second
menu entry; rapid uses actual readiness and the existing normal menu click.
Neither route uses a VM snapshot or manufactures scene state. The copied save
is entered through Free Ride, not a verified later-state Career load.

The preceding `samples/` batch also contains five alternating Runs per side
using the unchanged bundled scenarios. It exposed an existing pending-shot/quit
issue: a slow frame can make both due together and quit before saving the final
shot. That batch is retained as exploratory evidence. Final custom scenarios
change only quit to one second later on both routes, allowing the screenshot to
render. This does not repair or certify bundled screenshot scheduling.

`cartrace_check.py` passes all ten final traces for its existing speed-step,
falling and tag checks. Separately counted `iteration_cap` anomalies occur 6–9
times per Run and are not rejected by that checker. Logs still report incomplete
spatial factory collision coverage, unsupported special body pairs and mesh
features, tire-contact rejection and HUD damage diagnostics. Those remain Gaps.
The inspected paired images show the Car and road with changing speed/gear HUD,
but the camera frames the Car closely from the side. No collision, camera,
physical-input, audio, whole-game, stock image or Career Parity claim follows.
Rapid Runs omit cold dwell and earlier Career transitions. Normal cold startup
remains available and is separately exercised here.

## Checks and acceptance boundary

The supplementary recorder timing test fails before the change and passes after
it. At the real native Run seam, `red-menu/` reaches menu but has no standalone
timestamp; `green-menu/` records menu at 3.483 seconds and correctly leaves driving
unobserved. Python recorder, save-snapshot, data-manifest, summary, replay,
Native audit, native path-refusal, Run-log failure and screenshot safety checks pass. The summary test independently
checks all samples plus known medians/ranges through its CLI.

With local read-only data links supplied, CTest passes 119/120, matching the
integration baseline. `object_world_step_unit` remains the known failure.
`ctest.log` preserves the initial no-data worktree result and
`ctest-with-data.log` preserves the representative rerun. The full evidence audit
still depends on ignored census artifacts in the original checkout; this ticket
does not weaken that audit or copy proprietary census data into version control.

The measurement, artifacts and narrow controlled-driving comparison are
established for this binary. Final ticket acceptance must also account for the
remaining startup/menu latency request and direct-native symlink fix. Any later
navigation implementation needs a new comparable batch on its own retained
executable. This report does not close those gaps by relabeling the measured Runs.
