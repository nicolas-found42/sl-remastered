# Rapid/cold Run benchmark, 2026-11-01

The final rapid route reduces repeated Free Ride setup. Median total duration is
6.147 seconds rapid versus 19.410 seconds cold, five alternating Runs per route.
The 13.262-second difference exceeds either route's observed variation. Rapid
uses the stock splash cancel transition and actual Main Menu readiness with zero
navigation offsets. The normal cold route retains its legacy timed menu click.
Both routes complete the same two-second throttle workload and both screenshots.
This establishes ticket #3's controlled setup comparison, not full-game Parity.

## Executable and retained evidence

Private evidence is retained at
`/Users/nicolas/Documents/github/sl-remastered/local/issue-1/ticket-3-final-evidence/`.
`samples/` contains ten complete records with exact scripts, commands, identities,
initial saves, logs, screenshots and full Cartrace. `repeat.py NEW-BATCH` repeats
the bundled scenarios without custom timing or extra screenshot delays.
`report.py BATCH` regenerates observed player controls, motion and diagnostics.
The earlier `ticket-3-evidence/` batches remain unchanged and are superseded here.

The measured executable is an exact copy of the original checkout's integrated
Release arm64 `build/startup_probe` at integration `bcefc329`. `CMakeCache.txt`
retains its build configuration. Executable SHA-256 is
`1a9a671043d645a96e3bd22e97f1f02bf818dbd1f2b5647f354e275f61363479`.
Game-data manifest SHA-256 is
`5c4fb9d07be180f743c2e07bea01ab7fd70e9b328e09e98bf4ab45c7ce21bcfc`.
The Installed Game is the original checkout's clean `.scratch/2005-extract/`
installation. The source fixture is `local/rapid-testing/baseline-data/save`,
entered through normal Free Ride. This does not verify a later-state Career load.
The Reference Folder is the original checkout's `ref/`.
Stock Executable MD5 remains `e686099e7233bb7d7c461b1b35c5064c`.

All Runs use the same executable, fixture, 640×480 visible window,
`SDL_AUDIODRIVER=dummy`, `SLRR_CAR_TRACE=1`, `SLRR_FORCE_SEED=555`, and wrapper
seed/car-creation traces. Forced Seed chooses the same Car, but does not make
frame progression or replay deterministic. No stock Script or Native behavior
was changed for this measurement. No Native status is promoted.

`preservation-before.json` and `preservation-after.json` contain equal full hash
maps for all 7,859 Installed Game files and both source fixture files. Each Run
also reports its source-save map unchanged. Separate guarded writable data roots
and run-local screenshot paths protect the canonical sources. Proprietary data
and saves remain ignored local evidence.

The existing summary CLI reproduces every sample, identity check and statistic:

```sh
python3 -I tools/parity/summarize_native_runs.py \
  /Users/nicolas/Documents/github/sl-remastered/local/issue-1/ticket-3-final-evidence/samples/??-*
```

Its output is retained as `samples/timings.json`; `samples/report.json` records
actual work. The summary reports zero failed Runs and equal identities.

## Samples and clock origins

Durations below are seconds. Preparation includes the game-data manifest and
save copy, excluding build time. Menu and driving begin at `probeMain`, after
recorder preparation, and mark the first actual readiness frame. They exclude
recorder startup overhead. For example, the first rapid menu is process 0.627,
loop 0.175, and recorder log +0.699 seconds. Total includes wrapper preparation,
the child process and source-save verification. These are normal cold startup
routes, not measurements of operating-system cold caches.

| Run | Preparation | Menu | Driving | Total |
|---|---:|---:|---:|---:|
| 01-rapid | 0.427 | 0.627 | 3.046 | 6.234 |
| 01-cold | 0.432 | 3.452 | 16.312 | 19.354 |
| 02-rapid | 0.432 | 0.582 | 3.016 | 6.050 |
| 02-cold | 0.435 | 3.460 | 16.353 | 19.410 |
| 03-rapid | 0.435 | 0.583 | 3.027 | 6.170 |
| 03-cold | 0.430 | 3.468 | 16.390 | 19.436 |
| 04-rapid | 0.428 | 0.616 | 3.032 | 6.074 |
| 04-cold | 0.436 | 3.452 | 16.324 | 19.378 |
| 05-rapid | 0.438 | 0.607 | 3.070 | 6.147 |
| 05-cold | 0.435 | 3.478 | 16.373 | 19.445 |

Median and full observed range:

| Route | Preparation | Menu | Driving | Total |
|---|---:|---:|---:|---:|
| rapid | 0.432 (0.427–0.438) | 0.607 (0.582–0.627) | 3.032 (3.016–3.070) | 6.147 (6.050–6.234) |
| cold | 0.435 (0.430–0.436) | 3.460 (3.452–3.478) | 16.353 (16.312–16.390) | 19.410 (19.354–19.445) |

The rapid Main Menu wait completes at loop 0.167–0.175 seconds. Escape follows
observed splash hotkey readiness, then a queued normal click follows menu
readiness. Cold still waits for its 10/13/13.5-second menu schedule. Removing
those waits explains the elapsed-time reduction. Preparation is essentially
unchanged. Actual City loading and rendering remain in both routes.

The host has 18 logical CPUs. Builds and other native Runs stopped before the
batch. Load averages were 2.81/10.00/9.66 before and 3.59/7.81/8.82 after.
WindowServer and desktop activity remained, so this is not an isolated host.
`machine-before.json` and `machine-after.json` retain process evidence. Alternating
routes limits drift. A separate unreported same-binary `profile-cold/` Run has
363 of 685 main-thread samples in `renderActiveViewport`, including 286 beneath
`CameraFrame::draw`. `profile-sample.txt` records the remaining rendering cost.
No rendering throughput gain is claimed.

## Actual driving outcomes and remaining Gaps

All ten Runs exit zero without timeouts and contain both 640×480 driving BMPs.
The player Car is `java.game.cars.Emer_Nonus_Street_GT`, resource `0x001b0006`,
starting at `[-278.475, 10.536, 1033.002]` with controller source 1. Throttle reaches
1.0 in every Run. Observed Up holds are 1.929–2.038 seconds for the same two-second
logical schedule. Each trace contains 122–128 player physics rows, reaches
8.09–8.24 m/s and ends 6.83–7.72 metres from its initial position. Final gear is
1 or 2 on both routes. These are comparable usable driving outcomes, not
identical trajectories or deterministic replay.

The unchanged bundled workload is ready screenshot at +0.0, Up down at +0.1,
Up up at +2.1, throttle screenshot at +2.2, and quit at +2.3 logical seconds.
The fixed pending-screenshot handling completes both captures before exit.
There are no custom capture delays or extra sleeps in these scenarios.

`cartrace_check.py` originally passed all ten traces for speed steps and falling.
Correction (ticket #5): its supposed tag check read `tags`, while production
emits `anom`; that pass did not validate anomaly tags. The corrected checker
rejects these traces for their recorded `iteration_cap` anomalies. Separately counted `iteration_cap` anomalies occur 5–8 times per Run and
are not rejected by that checker. Logs retain incomplete spatial factory
collision coverage, unsupported special body pairs and mesh directives,
tire-contact rejection, and incomplete HUD damage diagnostics. These are Gaps.
The inspected first-pair throttle images show the same Car, road and HUD speed
29 in gear 2, with a close rear-side camera and small viewpoint differences.
Camera framing, collision fidelity and HUD completeness remain unresolved.
Physical controls and real audio are untested by these dummy-audio scripted Runs.
No Stock Executable image comparison or full-game Parity claim follows.

## Verification and acceptance boundary

The wrapper's four existing tests and the existing summary CLI test pass after
collection. The original integrated full gate completes CTest 119/120, with the
known `object_world_step_unit` physics failure. The post-CTest remainder passes,
including path/symlink guards, pending screenshot behavior, replay, Python,
corpus and Native evidence audits. Parent gate logs are retained at
`/tmp/sl-issue-1/integration-instant-check.log` and
`/tmp/sl-issue-1/check-instant-after-ctest.log`.

The repeated setup reduction, removal of blind rapid menu delays, bundled
capture completion, equivalent controlled driving outcomes and source
preservation are demonstrated for this binary. The normal cold route remains
available and is exercised here. Earlier Career transitions, physical platform
behavior and the recorded physics/camera/HUD Gaps remain outside this benchmark's
acceptance claim. The full project gate is not reported as wholly green.
