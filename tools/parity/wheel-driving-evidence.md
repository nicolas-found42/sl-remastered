# Issue #18: unfinished wheel driving work, 2026-11-01

This change preserves work in progress at the owner's request. Issue #18 remains
open. It is **not** a completed stock Parity claim or a passing repository gate.

## Implemented and observed

- Native Run recording defaults to a hidden, non-focusable macOS window, with
  desktop fullscreen and native intros bypassed. Visible presentation remains an
  explicit option. The crash/screenshot integration tests use the hidden route.
  These presentation changes reuse the existing issue-16 branch work.
- Wheel Cartrace records callback-time load, length and contact separately from
  the final gather, plus actual hub/bone poses and rotation angle. Trace capacity
  is 4096 bytes; the measured final-controls trace has no truncated rows.
- The stock-2005 suspension constructor `00490dc0` uses the collider half-length
  as its local anchor. The zero-anchor implementation produced a roughly 0.5 m
  difference between rendered wheel circumference and loaded ground contact.
  The corrected anchor is used by contact selection and force application.
- Default helpers advance suspension travel, steering and spin. Installed wheel,
  hub and arm slot families attach to their corresponding helper bodies. Part
  alignment uses the helper's current frame, preventing a rim from orbiting an
  offset construction frame. The spinning helper's construction transform no
  longer applies the main-body translation twice.
- Spring force, damping, raw travel lengths and radius setters now reconfigure
  live suspension settings. Wheel references can follow a chassis Part's Car
  link. `Chassis.forceUpdate` invokes the unchanged chassis `updatevariables`
  script before the previous mass refresh; the remaining stock transfer is
  explicitly incomplete.

Private evidence is preserved in the primary checkout at
`local/issue-1/ticket-18-evidence-20261101-af94/`. Original metadata retains the
original worktree paths; do not rewrite historical identities. Proprietary data,
saves, images and stock excerpts remain local. The stock executable MD5 was
independently checked as `e686099e7233bb7d7c461b1b35c5064c`.

`observe-poses-complete` records all four hub transforms staying fixed despite
changing wheel speeds and suspension lengths. `contact-height-observe` measures
256 loaded samples with a 0.4748..0.5400 m circumference/contact gap. Its single
captured wheel sample is the minimized replay. `anchor-change` removes that
systematic error: 422 loaded samples range from -0.0155 to +0.0112 m.

`final-controls` enters Free Ride normally, accelerates, steers both directions,
coasts, brakes and stops. Comparing the rendered wheel with the force contact
from the same physics phase gives 1535 loaded samples spanning -0.0157..+0.0171 m.
A final-gather contact is a newer candidate and must not be silently treated as
that force contact. The trace retains both. The original latest-contact checker
remains in the local evidence, including its stricter failing observations.
This run preceded the restoration of the real script configuration callback.

The later `force-update-observe` has one actual stock-script
`Chassis.forceUpdate` invocation but zero live wheel configurations.
`force-update-change` restores the script callback and exits zero after normal
entry and a short drive, with the source save unchanged. Its log records the
actual radius, force, damping, min/max travel and arm/hub values set by stock
scripts. These newly reached custom geometry paths remain unfinished below.

## Validation and known failures

- The wheel-runtime, Car physics and Car-runtime focused tests pass in the
  retained logs. The zero-anchor fault fails the new selection/force regression.
  The native setter regression fails when only the side record changes and
  passes when force/damping/radius affect the physical suspension. A chassis
  Part reference also exercises owning-Car resolution.
- The earlier full gate stopped at CTest: 121/123 passed. One failure was the
  deliberately red setter regression, subsequently repaired. The other was the
  previously documented `object_world_step_unit` true-sleep assertion at line
  220. See `integration-check.log` for the final attempted gate on this WIP.
- Jev review escalated: safe-to-apply 0.28, correctness confidence 0.19, composite
  0.62425. A subsequent read-only stronger-reasoner review confirmed the anchor,
  helper slot mapping/current-frame alignment and special helper mass branch,
  but identified the concrete unresolved issues below. No completion gate was
  claimed and no Native evidence status was promoted.

## Required follow-up

1. **Implement actual custom arm/hub posing.** The newly reached stock scripts
   set these values, but the natives currently retain/log them without applying
   the geometry. Propagating them into the present default-only pose function
   would instead trigger its rejection guard and freeze the helpers.
2. **Correct default helper rotation order for nonidentity base orientation.**
   Stock `0054eab0(this,A)` means `this = A * this`; `0054ebc0(this,A)` means
   `this = this * A`. In `0044fa70`, arm rotation is `Rarm` (identity without an
   arm), default hub rotation is `Ry(steer) * Rarm * base`, and bone rotation is
   `hub * Rx(spin)`. Current hub uses physical suspension `base * Ry(steer)` and
   arm uses `base`. The zero-base test car hides this discrepancy.
3. **Complete `forceUpdate` batching and reset semantics.** `00448430` checks
   `suspend_update == 0` and resets wheel fields +0x15c..+0x170 and all 17 tire
   coefficients before calling `updatevariables`. The current call lacks that
   guard/reset and the complete engine-field transfer. Stock `reload` only
   refreshes nitrous-related fields; it must not be substituted for this path.
4. **Repair the live tire coefficient record.** `CarWheelExtraState` currently
   has 16 entries, although stock accepts indices 0..16. Frame reconstruction
   also overwrites some indexed setter values with separate scalar defaults;
   preserve actual setter ordering and friction side effects with behavioral
   coverage before claiming tire numerical Parity.
5. Retain the incomplete coupled contact solve, full cached-feature edgewalk,
   strict iteration-cap observations and unrelated object-sleep failure as
   unresolved. Repeat both the actual full controls Run and required checks
   after completing the newly reached script-configured geometry.

The local stronger-review notes include the stock arm/hub-zero equations and
verified instruction addresses for continuing this work. Stock comparison
corpora are function-level evidence; there is no frame-by-frame stock driving
trace establishing whole-Car numerical Parity.

## Merge verification on combined main

The merged `startup_probe` and four targeted test executables build. Three of
four focused CTests pass. `car_runtime_unit` fails its new
`physics::updateSuspension(u) == Active` assertion on the combined main
checkpoints; this integration failure is preserved as unfinished work, not
reported as a passing regression. See `merge-build.log`, `merge-tests.log`,
`merge-background-test.log` and the `merged-smoke` record in the preserved
private evidence directory. The newer main launcher guards and both sets of
wheel/contact trace fields were retained when resolving the merge conflicts.

The merged hidden-window regression passes for both explicit background mode
and the environment override. The merged short native driving Run exits zero,
observes real menu/driving milestones and leaves its source save unchanged.
These smoke results do not clear the failing Car-runtime test or issue #18.
