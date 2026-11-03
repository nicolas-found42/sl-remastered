# Stock semantics behind the two workflow physics failures

The suspension failure is a test-fixture precondition error with a demonstrated one-line correction. The object sleep failure remains unresolved. Its low linear speed is insufficient for Stock's sleep gate, and none of the isolated object probes below establishes a justified production fix. Keep the existing sleep assertion.

All production files and tests were left untouched in this investigation. Read-only authority: `local/census/all.c` (Stock Decompile). Experiments compile copies under `.scratch/physics-gate-semantics/`, link the existing exact-HEAD baseline `libsl_data.a`, and run with dummy SDL drivers. No `ref/` writes, GUI, audio, shared-build writes, commits, or Stock runtime launch occurred.

## Reproducible baseline and experimental isolation

Baseline commit: `ceaa48df09ed157e6517ce131236b9101c4721a5`. Its archive, build, installed-data hashes and both failing CTest registrations are described in [workflow-physics-baseline.md](workflow-physics-baseline.md). The source root used here is `.scratch/workflow-physics-baseline-20261101T215004Z-546b1d/head-source`, with binaries and `libsl_data.a` in its sibling `head-build`.

Both baseline binaries were run twice in this investigation. The final repeat, absolute binary paths, output and subprocess return codes are in `.scratch/physics-gate-semantics/final-reproduction.json`:

```text
car_runtime_test       return -6, car_runtime_test.cpp:330, updateSuspension(u) == Active
object_world_step_test return -6, object_world_step_test.cpp:232, kBodyAsleep
```

One-command existing red loop for either exact baseline binary, from repo root:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SLRR_PROBE_BACKGROUND=1 .scratch/workflow-physics-baseline-20261101T215004Z-546b1d/head-build/car_runtime_test
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SLRR_PROBE_BACKGROUND=1 .scratch/workflow-physics-baseline-20261101T215004Z-546b1d/head-build/object_world_step_test
```

The pair ran in about 0.3 seconds, without user interaction. Compile commands for each scratch executable are saved as `*-build.json` beside it. They preserve C++20, arm64, `-O3 -DNDEBUG -UNDEBUG -Wall -Wextra -Wshorten-64-to-32 -ffp-contract=off`. Only one compiler process ran at a time. These reproduce the existing target flags; sanitizer flags were not introduced into this pre-existing release baseline.

Discovery began with bounded `jg` over the explicit `src` root. It returned incomplete discovery (request limit, eight locations-only files); the missing context was treated as unknown. Focused exact searches/direct reads supplied the evidence below. There is no `docs/adr/` directory in this checkout.

## Suspension: sleeping fixture, not spring/damping arithmetic

The actual failing call was observed by inserting two tagged prints into a copy of the exact HEAD test. The output in `.scratch/physics-gate-semantics/car-observe.log` is:

```text
max=0.705 rest=0.705 min=0 force=10000 radius=0.315 distanceSq=0.25
wheelFlags=c5 wheelProtoFlags=401 rootFlags=89 groundFlags=0
result=0 length=0.5 force=0 projection=0
```

`kBodyAsleep` is `0x80`, and enum value `0` is `SuspensionUpdateResult::Unsupported` (`src/physics_body.hpp:177`, `src/physics_suspension.hpp:104`). The wheel and root are asleep. `validChain()` rejects asleep bodies/ancestors before `updateSuspension()` reaches its scalar force calculation (`src/physics_suspension.cpp:54-70`, `181-187`). Thus max length, force sign and damping do not explain this first failed assertion: `0.5 < 0.705` is compressed, but the fixture never satisfies the awake precondition.

Stock's corresponding work gate requires at least one dynamic awake side. In `FUN_00491100`, `local_20`/`local_1c` record dynamic prototype sides, and the branch at `local/census/all.c:107381-107388` tests body flag `0x80` on each. The test supplies an asleep car wheel and static ground, so requiring an active force result contradicts this prerequisite. The Port's typed Unsupported result represents its explicitly bounded awake-body API; Stock need not return that same enum.

The existing `wakeCar()` recursively calls `wakeDetachedCompound()` for the complete tree (`src/car_physics.cpp:285-291`, `src/physics_compound_step.cpp:41-65`). A scratch copy of the test with **only** this insertion passes the entire test and all existing force/rest-length/radius/damping assertions:

```diff
 const auto original = live.suspension;
+wakeCar(*created.node->carInstance->carPhysics);
 const float rest = live.suspension.settings.restLength;
```

Proof: `.scratch/physics-gate-semantics/car-wake.patch`, `car-wake-build.json`, and `final-reproduction.json` (exit `0`, `car runtime ok`). The unmodified executable remains red in the same repeat manifest. No production suspension change is indicated. Add the wake immediately before the synthetic contact measurement; retain every existing force assertion. If the fixture remains in use after this block, restore the intended sleep state explicitly after restoring its suspension/velocity fields.

Regression seam: this existing integration test already demonstrates that script setters alter the live car's suspension force, including distinct compression/rebound damping. Its complete tree must be awake before direct solver calls. The current unit tests already include explicit awake-body suspension cases (`tests/physics_suspension_test.cpp`). A test that merely asserts `wakeCar` forwarded a call would add no behavioral coverage.

## Object: Stock gate correctly rejects the observed state

At 900 steps the exact test reports:

```text
lowestY=0.283 finalY=0.293 speed=0.124 retainedPairs=6
omegaSq=1.53189 scaledTorqueSq=0 scaledActivitySq=17.3394 flags=0x9
lastForce=(-21.2912,142.143,71.9434)
omega=(-1.14787,-0.000994247,-0.462917)
activity=(-1.95864,-83.1387,4.45838)
```

`FUN_004a4ed0` checks all four quantities, after smoothing current force into activity (`local/census/all.c:120915-120951`): next linear speed squared `< 0.1`, next angular speed squared `< 0.001`, inverse-mass-squared times current torque squared `< 0.01`, and inverse-mass-squared times activity squared `< 0.01`. The numeric constants and raw-word identities are recorded in `src/physics_body.hpp:52-54`; the Port preserves these gates and current/next selection in `src/physics_body.cpp:572-607`.

Both observed angular speed and activity independently exceed the rest gates. Clearing the sleep assertion, using linear speed alone, or increasing thresholds would conceal ongoing motion/support-force oscillation. The test's printed `scaledTorqueSq=0` reads the post-commit current buffer, so it does not establish that the torque seen by the preceding commit decision was zero. Observer correction rows show nonzero pre-commit torques.

A scratch copy of `physics_friction.cpp` adds observation only at the actual direct-corrector seam for steps 895–899. Its output is `.scratch/physics-gate-semantics/object-observe.log`; build/source are retained beside it. All listed support normals are `(0,1,0)`. For step 899 the three retained contacts have separation `0.0237303`, `0.0172912`, `0.0434338`, with forces accumulating to the exact baseline `(-21.2912,142.143,71.9434)` and torque to `(-68.2248,-2.13179,-27.9432)`. Steps 896–898 admit additional impact contacts. Lateral forces in the root accumulator therefore do not prove diagonal ground normals; the corrector's friction supplies lateral force even with vertical normals.

## Ranked, falsifiable object hypotheses and probes

Ranking was sent to the supervising agent before the probes:

1. **Incorrect contact geometry/feature lifetime.** Prediction: near-final supports reveal non-face normals or a retained contact crossing a Stock feature-lifetime boundary. Vertical normals were observed, refuting the diagonal-normal subcase. The fixed vertex/face cache itself is consistent with `FUN_004af4f0`: that helper tests the point against the face's side planes and plane distance, not every body-vertex adjacent plane (`all.c:127406-127458`). Removing the retained cache based only on a physically preferable lowest vertex is not justified.
2. **Incorrect retained/new contact scheduling or admission.** Prediction: replaying the same candidate set through Stock's selection/lifecycle yields a different corrected record, priority or number of retained supports. This remains open, with the concrete source discrepancy below. Reversing only exact separation ties changes the trajectory but does not satisfy the sleep gate.
3. **Unproved fixture expectation/timing.** Prediction: only time, placement or floor tessellation causes the red state. Scratch minimization shows that more time and several placement/grid changes remain red, so none is a sufficient correction. No Stock capture establishes that this synthetic box must sleep at exactly 15 seconds.
4. **Incorrect sleep gate translation.** Prediction: equal current/next inputs satisfy Stock's four gates but Port rejects them. The observed angular/activity values fail both implementations' documented gates. Do not change the gate based on this repro.

`object-variants.json` records one-variable probes. The observer executable deliberately returns `42` for awake and `0` for asleep before downstream assertions, to inspect candidates; that early exit is confined to scratch and is not a proposed test edit.

| Probe | Final angular speed squared | Final scaled activity squared | Result |
| --- | ---: | ---: | --- |
| Original 900 steps | 1.53189 | 17.3394 | awake |
| 9000 steps | 1.55933 | 12.0451 | awake |
| z=0.25 | 1.70773 | 8.24839 | awake |
| x=2.25, z=0.25 | 0.675601 | 25.3787 | awake |
| Start y=0.28 | 3.05936 | 13.7567 | awake |
| Grid step=2 m | 2.64008 | 0.48951 | awake; below plane |
| Grid step=0.5 m | 0.236446 | 8.08622 | awake |

Changing the material table's friction alone had identical output because `createGroundRuntime({0,0,0}, 1.0f, 0)` still supplies the actual ground prototype friction. This probe is inconclusive about friction, not evidence of friction independence.

## Correcting the prior static predicted-bit explanation

`local/issue-1/ticket-1-evidence/README.md` and decision row timestamp `2026-11-01T20:00:00Z` infer that static Stock bodies retain zero prediction bits because state helper `FUN_004a63e0` clears them and the static predictor returns early. That omits the subsequent complete body initializer.

`FUN_0049b0b0` explicitly performs, outside the dynamic/static flag branch (`all.c:114674-114683`):

```c
*(uint *)(param_2 + 0xa0) = *(uint *)(param_2 + 0xa0) | 1;
puVar7 = (undefined4 *)(param_2 + 100);
puVar8 = (undefined4 *)(param_2 + 0x138);
for (iVar6 = 0x35; iVar6 != 0; iVar6 = iVar6 + -1) {
  *puVar8 = *puVar7;
  puVar7 = puVar7 + 1;
  puVar8 = puVar8 + 1;
}
```

Body+`0xa0` is state0+`0x3c`; the state record is `0xd4` bytes = `0x35` words. The full initializer sets and copies the bit, matching Port `createBody()`. The zeroing helper alone cannot establish the earlier conclusion. This finding does not rewrite the historical decision row; a correcting row is appropriate if that earlier claim is relied on again.

Two scratch experiments—clearing both ground state bits in the fixture, and compiling a complete `physics_body.cpp` override that prevents every static `createBody()` from setting them—still produce exactly the original failed telemetry. See `object-static-bit.log`, `object-body-static-bit.log`, and the repeat manifest. Do not apply this proposed production change.

## Concrete open mismatch: new-contact admission priority and lifecycle

The Port pending-contact block sorts by `DirectContact::separation` and keeps the first non-resting record for each dynamic root (`src/physics_multi_root_step.cpp:443-486`). Its comment identifies `FUN_0049b830 param_12` as core distance minus radii. The Stock callers show that this identification is incomplete:

- `FUN_0049c8f0` initializes `local_25c=1`, bisects it against overlap with `local_260`, samples current/predicted poses at that fraction, and passes `local_25c` to `FUN_0049b830` (`all.c:116217-116283`, `116393-116394`). Separation is the distinct `local_270` argument.
- Ground creation `FUN_0049ec00` initializes `local_c=1`, uses a shared candidate-list minimum and ten-step time search, reselects the candidate across the list as the sample changes, and passes `local_c` as that priority (`all.c:117315-117316`, `117360-117383`, `117493-117495`, `117560-117622`). Separation is the distinct candidate `puVar15[10]` argument.
- `FUN_0049b830` compares existing body+`0x210` with `param_12`, stores `param_12` separately at contact+`0xac`, and on equal priority may compare old/new world points against relative current velocity (`all.c:115087-115104`, `115220`, `115245-115259`). Contact separation is stored by `FUN_004922e0` at contact+`0x8c`, not +`0xac`.

This is a source-backed difference between priority and separation, **not proof that implementing it alone fixes the object's sleep**. Stock's initial ground candidate selection itself uses minimum separation, so merely reversing the sort or substituting a guessed time does not reproduce the full algorithm. The experiment that reverses only exact separation ties ends with `omegaSq=2.85899`, `scaledActivitySq=9.8905` and still fails; source and repeat output are retained as `multi-pending-last.cpp` and `object-pending-last.log`.

Recommended next seam: record the candidate's sampled fraction/world point and whether the record was retained at step start, then compare a minimized real candidate set against `FUN_0049ec00`/`FUN_0049b830`/`FUN_004a35d0`. Preserve Stock's distinction between updating existing `world+0x118` records (`FUN_004a2d10`) and discovering new candidates only in the pending stage (`FUN_004a2540`); the Port currently gathers all candidates before each retained sweep. A production correction needs that exact causal comparison and an object test that turns green without changing its sleep assertion. Full Stock runtime experimentation was not authorized for this subtask and was not launched.

## Verification boundary

Jev verified the exact one-line suspension red/green claim at confidence `0.96` (`supports=0.97`). Other factual claims were submitted with direct local source/trace evidence; it returned verified labels but flagged several for review (confidence `0.42–0.70`). Those review flags remain visible here rather than being treated as automatic parity certification. In particular, the open admission mismatch needs independent review/runtime confirmation before choosing a production fix. The original object loop remains red; neither this note nor the scratch probes claim completion of the full integration gate.
