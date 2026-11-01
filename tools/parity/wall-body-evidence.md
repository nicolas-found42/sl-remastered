# Wall and Car-body contacts: issue #17

Issue #17 remains open. This patch adds missing Car/Car collision traversal,
renews dynamic contact rows when their features change, and corrects an
independently reproduced zero-fraction face-query refusal. It does not establish
complete collision Parity. Solver iteration caps and an intermittent application
refusal remain unresolved. No Native is promoted to matched.

## Authority and prerequisites

The target is the Stock Executable, MD5
`e686099e7233bb7d7c461b1b35c5064c`, independently checked in this session.
Existing local Stock Decompile and `docs/physics-evidence.md` supply the authority;
no Steam behavior was substituted. The issue #16 changes from `2c4c53c` are
reused as prerequisites, including normal Car construction/sleep, retained-contact
search, complete Cartrace rows, and background presentation. Their limitations
remain documented in [road-contact-evidence.md](road-contact-evidence.md).

- `FUN_004a1560`, local `all.c:118812-118850`, dispatches distinct loaded type-2
  bodies through `0049c8f0`. The Port previously traversed objects against Cars
  and objects against objects, but omitted Cars against Cars. Distinct registered
  Cars now offer their collision leaf pairs through the existing dynamic-pair
  dispatcher. Both-sleeping roots remain skipped. Special unsupported body flags
  retain their explicit diagnostic.
- `FUN_004a2d10` evaluates fixed cached features through `004af970`. A full rewalk
  to another feature must not carry an old resting row's accumulated impulse and
  forces into the new contact. The dynamic path now follows the ground path's
  renewal rule. This is not a claim that the full cached-feature evaluator is
  implemented or that the remaining solver lifecycle matches stock.
- `FUN_0049fa60`, `all.c:117978-118007`, performs the triangle test and calls
  `0049bda0` even when the regular crossing fraction is zero. The Port rejected
  zero before the triangle test, returning Unsupported for both inside and
  outside starts. A finite zero fraction now reaches the triangle test: an
  inside start produces contact and an outside start misses.

## Regression evidence

Private artifacts are under this worktree's `local/issue-17/`.

`car_world_step_unit` constructs real stock Emer Nonus collision geometry and
steps two approaching Cars above the ground. It checks their velocity response
and separation, moves them apart, and repeats the approach using the same
stepper. With only the new Car/Car traversal removed, the final regression fails
its velocity assertion (`car-car-final-red.log`); with the traversal it passes.
It does not infer contact from a retained-pair count: a successful solver can
create and drop the pair within one step. The existing sleeping-neighbor fixture
now propagates placement to descendants, as normal `setMatrix` does; moving only
its sleeping root had left its collision children at the origin.

`physics_contact_geometry_unit` checks that a retained point/box contact preserves
its accumulators on an unchanged feature and clears them on a vertex-to-face
transition. `dynamic-cache-red.log` retains the pre-fix assertion failure. The
corrected regression passes with AddressSanitizer and UndefinedBehaviorSanitizer
(`regression-results.json`).

`resource_world_unit` checks an in-triangle segment starting exactly on a face,
plus an outside-triangle segment with the same zero plane fraction.
`face-zero-red.log` records both former failures; `face-zero-green.log` passes.
This proves the bounded geometry correction independently. It does not yet prove
that the intermittent application refusal below had that cause.

## Application observations

All Runs enter Free Ride through normal splash/menu input using the existing
Run recorder, immutable installed stock data and a copied source save. No VM
state, Car or scene is manufactured by the application scenarios. Source-save
verification reports unchanged in every completed Run. Metadata retains the
executable hash, data manifest, exact input script and presentation settings.
The installed-data manifest hash is
`5c4fb9d07be180f743c2e07bea01ab7fd70e9b328e09e98bf4ab45c7ce21bcfc`.

| Private Run | Observed result |
|---|---|
| `wall-baseline` | Actual wall impact, speed 13.94 to 2.10 m/s at tick 399. 629 complete steps, 63 iteration caps; exits 0. |
| `wall-repeat` | Brakes before reaching the wall under concurrent-build timing. Not wall-impact evidence. |
| `wall-repeat-shifts` | Reaches wall; reverse gear never selected. 1,949 steps and 150 caps; exits 0. Not separation evidence. |
| `wall-reverse-held` | Fails at tick 1,339 with `wheel face intersection unsupported`. 67 earlier caps; exits 1. Original failure retained. |
| `wall-refusal-observe` | Diagnostic replay with the original face query selects reverse for 438 steps and moves from X -341.879 to -305.460. Later wall contacts are recorded. 2,231 steps, 102 caps; exits 0. Does not reproduce the refusal. |
| `wall-face-before-observe` | Second instrumented Run with the original zero-fraction rejection; exits 0 without a refused query. The intermittent refusal is not yet reproducible on demand. |
| `wall-final` | Combined final code: 3,148 successful steps, 188 caps, no reverse gear selected; exits 0. The strict Cartrace checker fails. Not completed impact/separation acceptance. |

`run-report.json` retains calculations. `body-contact` rows identify both bodies,
ground leaf/face, cached feature indices, local contact points, normal, impulse,
resting state, correction result, tick and pass. `wheel-face-refusal` retains the
exact rejected segment and triangle, if the refusal recurs. Control telemetry
includes the shift request and driven-wheel speed, so a failed reverse transition
can be distinguished from retained wall contact. Screenshots are retained with
their frame/log records; screenshots alone are not acceptance.

The foreground monitor observed 1,009 samples with zero game activations during
the background Run. All game launches use hidden, nonfocusable native SDL
presentation with dummy audio. These Runs do not verify physical display/audio.

Only one Car is present in these Free Ride application Runs. The two-Car
regression is supplementary, not the required actual two-Car application Run.
Current `placeGroundTraffic` stores placement records without instantiating Cars
(issue #35); a normal multi-Car scene or another supported stock route remains
necessary. No synthetic application spawn is substituted for that criterion.

## Gates and remaining work

The full build succeeds; both full gates pass 122/123 CTests and stops
at the existing `object_world_step_unit` true-sleep assertion. Separate checks
after that stop retain a historical Native evidence SHA-256 mismatch for
unchanged `tests/vm_test.cpp`. The hash is not rewritten. Recorder/save isolation,
replay, screenshot, error-Run and corpus checks were also exercised; see the
private logs. The final gate is `final-full-check.log`; it still fails the same object-sleep assertion. PTX readers agree, phase-2 corpus comparison passes, and the
application binary is arm64.

Jev's initial partial-patch review escalated (`safe_to_apply=0.23`, correctness
confidence `0.49`). The final code gate also escalated (`safe_to_apply=0.05`,
correctness confidence `0.26`, composite `0.28875`). It accepted the full-gate
failure claim at confidence `0.91`, but did not confidently verify the other
claims. Its zero-fraction contradiction had only `0.18` confidence. A new
isolated before/after replay retains explicit exit codes 1/0 and the two failed
assertions in `zero-fraction-receipts.json`; a focused Jev verification judged
that receipt supported (`supports=0.67`, confidence `0.50`) but still requested
review. None of these judgments approve closure. Further evidence and independent
review are required before completion.

Next: capture the intermittent refused face query, verify any causal fix with
that same application scenario, resolve relevant solver caps without weakening
the checker or sleep assertions, and exercise real Car-body impacts in a normal
multi-Car application scene. Keep the failing and corrected Runs and update this
record only to the level demonstrated.

## Unfinished continuation preserved for merge

The later diagnosis rechecks `object_world_step_unit` deterministically in about
0.2 seconds. A local reduced one-box fixture reproduces the same true-sleep
failure. Side-by-side observation finds that the full rewalk replaces valid
cached vertex/face rows. `cachedVertexFace` now implements the bounded
004af4f0/004ad0a0 case and preserves valid local vertices/support rows in both
ground and ordinary body/body paths. The captured geometry regression fails
before this correction and passes afterward, including reversed body ordering.
Wheel/swept dispatch remains separate; remaining cached feature cases explicitly
report their full-rewalk fallback. Float rotation grouping follows 00436880.

The latest targeted checks pass `physics_cached_unit`,
`physics_contact_geometry_unit` (ASan/UBSan) and `car_world_step_unit`;
`object_world_step_unit` still fails true sleep (final speed 0.133 m/s). An earlier
experimental float grouping produced 0.027 m/s; that is not the current result
and never satisfied the sleep gate. A short application Run with the initial
vertex-cache implementation executes the changed path, exits zero and preserves
the source save, but records 211 iteration caps in 549 steps. That Run predates
the final float-grouping/body-order refinements. The latest refinements have not
yet received a fresh full project gate and full application acceptance Run.

Local experiments with feature-keyed contact multiplicity, removal of an extra
resting sweep, and their combination all still fail sleep; those collector/solver
experiments are not applied to production. The existing direct-contact oracle
was found to cover state-6 impacts only; adding state-7 comparisons was planned
but not executed. Docker was unavailable. No new Wine oracle was launched.

The user requested committing, pushing and merging this explicitly unfinished
work, then removing its worktree. Issue #17 is not closed or represented as
resolved by that delivery. The Jev escalation above remains unresolved.
