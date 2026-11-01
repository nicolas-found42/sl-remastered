# Road contact lifecycle: issue #16, 2026-11-01

**Issue #16 remains open.** This change repairs two stock-backed lifecycle
rules and preserves the remaining solver/stop Gap. Successful application exits
are not contact acceptance. No Native status or broad Parity status is promoted.

## Reproduction and authority

The primary seam is the real native application, entered through the stock
Main Menu using `tools/parity/run_native.py`. The new
`tools/scripts/free-ride-road-contact.txt` adds four seconds at rest to the
existing acceleration/steering/braking/release workload. It does not construct
synthetic game state or alter stock Scripts. Routine input skips the splash as
soon as its Escape handler is ready and clicks Free Ride on menu readiness.
The controls script now uses normalized presentation coordinates; its old
window-coordinate click missed Free Ride on the current display and timed out.

Private records are under this checkout's `local/issue-16/`; `report.py` and
`report.json` there retain calculations and screenshot/frame/final-tick pairs.
Each recorder output retains the exact script, executable SHA-256, data
manifest, source-save snapshot/hashes, writable data root, logs, screenshots,
Cartrace and contacttrace. Proprietary material stays local.

The Installed Game is the original checkout's
`.scratch/2005-extract/Program Files/Street Legal Racing - Redline`, with data
manifest SHA-256
`5c4fb9d07be180f743c2e07bea01ab7fd70e9b328e09e98bf4ab45c7ce21bcfc`.
The source fixture is its `local/rapid-testing/baseline-data/save`, entered
through normal Free Ride rather than restored as an in-memory checkpoint.
All executed recorder Runs report `source_save_unchanged=true`. Writable roots
and outputs are distinct from both sources and the explicit Reference Folder,
including after canonicalization. Stock Executable MD5 was independently read
as `e686099e7233bb7d7c461b1b35c5064c`. Steam was not needed or substituted.

## Applied stock rules

- `FUN_0044a250`, local Stock Decompile `all.c:58958-58979`, finalizes and
  sleeps the new Car compound before returning it to Scripts. The Port now
  suspends that compound before registering it. `baseline-normalized` aborted
  mid-assembly at the unplaced position `(-0.000026,-0.267778,0.107495)` with
  `car step: no loaded ground sector contains the car`; `creation-controls`
  reaches normal City placement and driving with the same scenario/fixture.
  The pre-existing sleep skip in `FUN_004a3f90`/`FUN_004a3bc0` also means an
  unplaced sleeping Car does not require a loaded ground sector. Awake Cars
  still fail explicitly if their sector is missing. This is not a catch-all
  recovery for a moving Car that leaves supported geometry.
- `FUN_004a2d10`, `all.c:119902-119909`, rechecks current-pose core geometry
  when the ten-step retained-contact search still cannot separate the shapes.
  The former Port rule came from **new** pair creation in `FUN_0049c8f0`.
  The agreed public contact-geometry seam now exercises real point/box core
  geometry: a radius-0.5 point starts 0.2 from the face, so the retained
  separation is -0.3. The test fails before the correction (`red-test.log`).
  Stock retains the final sampled pose for publishing the world point
  (`119954-119975`); the regression separately checks 0.69970703125 at the
  last sample fraction 1/1024. The review caught and corrected an initial
  implementation that incorrectly published with the current pose.
- `contacttrace` permanently records predicted, sampled and current distances,
  radii, time and bodies when this recheck occurs. The complete four-wheel,
  eight-pass Cartrace can exceed 2 KiB, so its bounded line capacity is now
  4 KiB. Earlier truncated rows remain in their original records; they are
  not treated as complete evidence.

The earlier theory that a static body's initial predicted bit should be
cleared is rejected: stock constructor `FUN_0049b0b0`,
`all.c:114682-114690`, sets it unconditionally too. No such change was made.
Full cached-feature lifecycle, new-pair overlap latching, and simultaneous
contact fidelity remain outside the demonstrated correction.

## Application evidence

`review-road-555` and `review-road-235` execute the corrected sampled-world-
point behavior on the native renderer. Both have complete traces, no failed
steps, maximum within-step gain 0.42 m/s and root Y 10.536..10.859 after driving
readiness. They reach 10.35/10.41 m/s and cross 10/11 distinct wheel-contact
faces. Their 54/39 capped steps include awake player contacts and 6/7 capped
steps with player correction on the final recorded pass. They are not passes.

For `review-road-555`, paired captures are:

| Capture | Frame / final tick | Speed m/s |
|---|---:|---:|
| ready | 97 / 4 | 0.00 |
| resting | 132 / 214 | 0.00 |
| accelerating | 157 / 364 | 9.52 |
| steering | 161 / 388 | 10.25 |
| braking | 184 / 526 | 0.20 |
| stopping | 207 / 664 | 0.10 |
| stopped | 222 / 754 | 0.38 |

The Car remains fixed/asleep through the initial four-second rest. The
steering screenshot and its Cartrace show road, heading change and HUD speed/
gear. Braking reduces speed, but the final filename `stopped.bmp` is an input
label: the Car is **not** in sustained true sleep after release. Residual
creep/jitter must still be resolved against stock. This path covers a short
stock City road and triangle transitions, not all slopes, curbs or sector
boundaries.

An explicit cold comparison, `final-cold-555`, completed its unchanged
post-driving workload (623 complete rows, root Y 10.536..10.859, 45 caps).
It predates the sampled-world-point review correction and is not final-physics
acceptance. Subsequent user instructions prohibit visible/focus-stealing test
launches, so it was not repeated visibly. The normal visible cold route remains
available explicitly with `--cold-start --visible`; routine Runs skip intros.

## Background presentation and startup

The recorder now defaults to background presentation. `--visible` is an
explicit opt-in for desktop/display acceptance. Background mode creates a
hidden, non-focusable native SDL window, applies host-windowed presentation
without changing stock Config fields, and sets `SDL_HINT_MAC_BACKGROUND_APP`
before SDL initialization to prevent Cocoa launch activation. Both display
application sites honor the override. This retains native input/rendering;
SDL's dummy video backend was tried and explicitly refused stock keyboard
initialization, so it was not used as gameplay evidence.

Background and routine rapid Runs pass `--no-intro`, avoiding native intro
file opening/playback. The normal splash/menu Script still runs: Escape is
sent at splash readiness, followed by the Free Ride click at menu readiness
with no scripted delay. Screenshot and deliberate-error integration tests now
also use background presentation and skip native intros. This is a harness
presentation choice, not a change to the game's startup behavior or a claim
of fullscreen/physical-device coverage.

`focus-safe-555` uses executable SHA-256
`9f2ee70a638509309fadb2972c4e8bf86d43ac469341e7236088a090bf6e2620`.
It reports hidden=1, nonfocusable=1, fullscreen=0, reaches menu at 0.624 process
seconds, clicks on that same frame, and reaches driving at 3.652 seconds.
Its 742 complete driving rows reach 10.00 m/s, root Y 10.533..10.851 and 11
wheel-contact faces. All steps succeed, but there are 56 caps, 15 with awake
player contacts and 11 with a player correction in the final recorded pass.
The strict checker remains red; no cap tags or thresholds were weakened.

The same final binary also executes `focus-safe-235`: 754 complete driving
rows, 10.92 m/s maximum, Y 10.536..10.859, 54 caps (7 with awake player
contacts and 2 with final-pass player corrections), 9 wheel-contact faces,
menu ready and clicked at 0.614 seconds, driving at 3.639 seconds. A separate
AppKit monitor samples the foreground application throughout this Run:
463 samples, zero with the game foreground (`foreground-check.txt`). This
observes the requested focus behavior independently of the game log. Run
summaries reject mixed background/visible presentation identities.

Each Run has one `err` line: the wrapper-enabled stderr mirror of
`car creation requested` (`SLRR_TRACE_CAR_CREATION=1`), followed by successful
creation. It is not a refused creation. No other error event occurs in the
successful driving Runs. The early failed assembly Run and dummy-backend
refusal remain retained separately.

## Checks and remaining work

The new regression, CarWorldStepper, game creation and run-log CTests pass.
The complete build/type check succeeds. Full `tools/check.sh` reaches 122/123
passing CTests; the baseline `object_world_step_unit` still fails true sleep
at line 220 (reproduced before these changes). Initial per-entry reference
symlinks also made two containment tests fail; restoring one canonical
Reference Folder resolves those environmental failures without changing source.
The current oracle-controller tests use disposable synthetic reference trees,
so they do not create sentinels inside the actual Reference Folder.

The separate Native evidence audit also remains red on a historical SHA-256
for unchanged `tests/vm_test.cpp`. Its two decompile artifacts were copied into
local writable analysis storage for integrity checking; no historical hash was
rewritten or Native promoted. Relevant Python recorder, screenshot and error-
Run integration checks pass in background mode. See private gate logs for
checks executed after the full gate's existing stop.

The next acceptance work is the retained/cached-feature and solver lifecycle
behind the awake-player caps and post-braking creep, with comparable stock
application evidence. The existence of stock's count-20 escape alone does not
prove the Port's frequency or response correct. Issue #16 cannot be closed
from these Runs.
