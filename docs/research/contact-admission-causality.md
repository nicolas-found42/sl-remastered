# Contact admission: narrowed failing case and original-code experiment

The object sleep failure remains unresolved. A new observation-only archived-baseline run preserves the exact failed sleep assertion and final telemetry. It identifies a small concrete admission event: at step 898, pass 0, the Port admits fresh face 1063 at separation `0.0477150157`, then admits face 996 on pass 1. Stock's new-ground admission uses a separate `0.02` band and sampled time priority, while this Port path uses retained-contact geometry and a separation sort. An original-code geometry helper now measures the same two fresh distances exactly, discharging that narrow numeric uncertainty. Neither replacing the sort's tie order nor adding the `0.02` eligibility band alone makes the object settle. No production correction is justified by these experiments.

This note follows [physics-gate-semantics.md](physics-gate-semantics.md) and [workflow-physics-baseline.md](workflow-physics-baseline.md). It does not reopen the refuted static-predicted-bit explanation. `FUN_0049b0b0` sets and copies that bit outside the static/dynamic branch (`local/census/all.c:114674-114683`).

## Retained experiment and unchanged red signal

Owned artifact root: `.scratch/contact-admission-causality/`. `observe.py` compiles copies of exact baseline `ceaa48df09ed157e6517ce131236b9101c4721a5` test, multi-root step, and car-world collector, linking the previously isolated baseline archive. It adds observation only. `build-command.json`, `build.log`, generated source, `observer.log`, and `result.json` retain the exact command, inputs, output and source hashes. The original test's assertions remain active and unchanged; `audit.json` deterministically compares all 41 assertion texts against the archived test. The observer records the list before admission, face identities/cache features, current/predicted poses, admission eligibility, and actual direct-corrector return.

One command, from the repository root:

```sh
python3 .scratch/contact-admission-causality/observe.py
```

The compile uses the baseline's `-std=c++20 -O3 -DNDEBUG -UNDEBUG -arch arm64 -Wall -Wextra -Wshorten-64-to-32 -ffp-contract=off`; one compiler producer writes only this owned directory. The executable runs from the archived source with dummy SDL audio/video and `SLRR_PROBE_BACKGROUND=1`. It returns `-6` at the sleep assertion and prints the identical baseline values:

```text
box: lowestY=0.283 finalY=0.293 vel=0.124 pairs=6
box rest gate: omegaSq=1.53189 scaledTorqueSq=0 scaledActivitySq=17.3394 flags=0x9
box support: lastForce=(-21.2912,142.143,71.9434) omega=(-1.14787,-0.000994247,-0.462917) activity=(-1.95864,-83.1387,4.45838)
```

The actual Stock sleep gate still rejects these angular/activity values (`local/census/all.c:120915-120951`; `src/physics_body.cpp:572-607`). Passing the linear-speed assertion does not imply sleep.

Discovery first attempted the explicit narrow `src/physics` root, which does not exist, and failed. A bounded query over explicit `src` (`--max-requests 4 --max-output-bytes 22000 --concurrency 4`, excluding render/audio) completed with a request-limit notice and only unrelated camera reading leads. It supplied no physics excerpts. The exact paths, symbols and source ranges below were therefore read directly; discovery was incomplete, not evidence of absence.

## The observed event

At step 898, pass 0, the collector actually queries faces `996, 997, 999, 1060, 1062, 1063`, in that order. `999` and `1062` are dropped by geometry before the pending list. This distinction matters: the four offered contacts are not the complete geometric candidate universe for an original-code replay.

| Face | Existed before gather | Resting before correction | Separation | Allowed by Port pending selection | Actual correction |
| --- | --- | --- | ---: | --- | --- |
| 996 | no | no | 0.0497968271 | no | unprocessed |
| 997 | yes | yes | 0.0281304121 | yes | unprocessed |
| 1060 | yes | yes | 0.0239993036 | yes | unprocessed |
| 1063 | no | no | 0.0477150157 | yes | return 2; promoted to resting |

On pass 1, face 996 is offered at `0.0497571714`, selected, corrected with return `1`, and promoted to resting. On pass 2 it is dropped; the other three records remain and are unprocessed. All four offered normals are `(0,1,0)`. This is observed Port scheduling, not a measured Stock/Port difference for the same inputs. See tagged `[contact-candidate]`, `[contact-face]`, `[contact-admission]`, `[contact-corrected]`, and `[contact-body]` rows in `observer.log`.

The tagged face rows show `996` and `1063` were genuinely absent before this gather, not merely existing rows whose feature search reset them. Their cached features after evaluation are `(body 19, triangle 3)` and `(body 20, triangle 3)`; the retained support rows use `(6,0)` and `(1,0)`. Those are Port feature indexes and must not be inserted as Stock pointers.

The Port's source path is explicit: collector creation at `src/car_world_step.cpp:1075-1101`, followed by the same `updateGroundFacePair` call for new and existing rows at `1130-1150`; that helper runs the retained `004a2d10` search (`src/physics_contact_geometry.cpp:346-483`). Pending scheduling then sorts non-resting records by separation and selects one per dynamic root (`src/physics_multi_root_step.cpp:443-486`). The `allowed` field alone would not prove application, hence the after-correction observer.

## Exact Stock distinctions

**The new-ground band is 0.02, separate from the retained 0.05 band.** `FUN_0049ec00` initializes fraction `local_c=1`, initially searches candidates at predicted poses, and subtracts the body's core radius from the query result (`local/census/all.c:117315-117316`, `117360-117383`). If the initial positive minimum is at least `_DAT_005f1070`, the zero-search-count path jumps to the final admission/cleanup label; it calls `FUN_0049b830` only when the selected separation is at most that constant (`117383-117392`, `117493-117495`). A raw Stock byte read confirms `005f1070 = 0x3ca3d70a`, float32 `0.02`:

```sh
i686-w64-mingw32-objdump -s --start-address=0x5f1068 --stop-address=0x5f1078 ref/raw/StreetLegal_Redline.bak.exe
```

The retained path's distance/drop band is different (`FUN_004a2d10`, `all.c:119830-119848`). Thus the successful Port fresh records near `0.048` are beyond Stock's new-ground band **with the same two fresh distances now measured by the original core query below**. Original retained cache/list state is still unqualified; the measurement does not establish a full `0049ec00` replay.

**Priority is a sampled fraction, not separation.** Ground creation keeps the candidate list together, bisects the fraction, and reselects candidates as the sample changes (`all.c:117560-117610`); it passes `local_c` to `FUN_0049b830`, separately from candidate separation (`117493-117495`). The body/body new-contact path likewise bisects `local_25c` and passes that fraction (`116217-116283`, `116393-116394`). The interpolator is original `FUN_004a8410` (`123395-123418`). Changing one sort key without this upstream search cannot reproduce that input.

**Admission has sequential destructive lifecycle.** `FUN_0049b830` resolves the eligible roots, rejects a later priority if body+`0x210` already holds a smaller one, and for an equal priority may reject using `(current velocity B - current velocity A) dot (old world point - proposed world point)` (`all.c:115070-115104`, `115145-115161`). On replacement it clears old body/root marks and deletes the old contact (`115105-115140`, `115162-115197`). The subsequent duplicate check against `world+0x118` occurs after those replacement actions (`115198-115211`). It stores fraction at contact+`0xac`, world point at +`0x80`, and root fraction/owner at body+`0x210`/+`0x214` (`115219-115222`, `115245-115259`). Separation is independently initialized at contact+`0x8c` by `FUN_004922e0` (`108110`). A global separation sort is insufficient to express these interactions.

**Gathering and admission are different stages.** The ordinary body/face branch of `FUN_0049f4d0` appends candidate records to the shared list (`all.c:117647-117697`). Its caller `FUN_004a0aa0` visits the body/face broadphase list (`118568-118590`). `FUN_004a5190` solves rows, updates retained contacts, conditionally sweeps them, then performs pending discovery and state-6 correction (`121076-121147`). `FUN_004a35d0` consumes state 6, clears marks, and writes state 7 regardless of the direct-corrector result (`120049-120120`). A new collector pair's existence is not proof Stock would already hold a retained contact for it.

## One-variable eligibility experiment: still red

`observe-fresh-band.py` adds only this pending-eligibility condition to the archived step copy, keeping the same full test and observer:

```cpp
if (!contacts[i].resting && contacts[i].separation > .02f) continue;
```

Retained artifacts are under `fresh-band/`. The unchanged test still aborts, now at the earlier `speed < 0.3f` assertion:

```text
box: lowestY=0.273 finalY=0.299 vel=0.469 pairs=6
box rest gate: omegaSq=10.2896 scaledTorqueSq=0 scaledActivitySq=96.8435 flags=0x9
```

This refutes the prediction that narrowing pending eligibility alone is sufficient to fix settling in this adapter. It does not refute Stock's measured `0.02` constant. The Port continues to create/update/drop every pair through its retained path, and still lacks Stock's list-level new-candidate search. Do not promote this scratch condition as the fix. The earlier exact-tie reversal probe also remained awake; its evidence remains in `.scratch/physics-gate-semantics/`.

## Original-code geometry measurement

The supervising task subsequently authorized one bounded geometry-only console experiment after the shared Stock slot was released. It completed in `1.872` seconds with exit `0`, using the qualified ARM64 Hangover image, one CPU, 512 MiB, no network/display/audio, a private cloned prefix, a 25-second outer deadline, and scoped cleanup. `original-core/result.json` records the exact Docker argv, deadline, exit, cleanup and before/after SHA-256 for Stock, helper executable, helper source and input. All hashes were preserved. Stock SHA-256 is `7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`; MD5 matches the checked Stock Executable `e686099e7233bb7d7c461b1b35c5064c`. The executable file was mounted read-only and its entry point was never run.

`build-original-core.py` exports the observed step-898/pass-0 leaf pose into exact float32 words (`original-core/input.json`, `input.h`). Unobserved state words are zeroed and explicitly outside this pose-only measurement. No prototype mass/inertia or body layout is constructed. The six grid triangles are reconstructed from the existing test loop and face indexes. The helper derives from `tools/wine/core_pair_oracle.c`: it verifies mapped byte ranges against the immutable file, manually applies the existing helper's PE HIGHLOW rebasing, and discloses replacing the original allocation/free/reallocation entry points with helper `calloc`/`free`/`realloc`. It initializes original box tables with `004aa560`, triangle tables with `004aa660`, constructs the original box with `004ac1c0` and each original two-sided triangle with `004aa740`, and calls original `004ae9c0` with null initial feature pointers. It checks box topology `8` vertices/`26` features and triangle `8` features. Geometry uses the existing oracle's x87 control word `0x007f`.

The triangle descriptor is checked against **machine instructions** in `0049dba0`: the original copies the first face descriptor, reverses its second/third vertex indexes at `49dc35`/`49dc41`, and negates the second normal at `49dc07-49dc3d` before calling `004aa740` at `49dc4e`. The helper supplies these same ordered faces and normals. It does not substitute Port topology or normals.

Actual original-query results (`original-core/original.log`); `comparison.json` additionally checks the two fresh distance words against the float32 values recovered from the Port trace:

| Face | Original distance | Float32 word | Comparison with Port offered distance |
| --- | ---: | --- | --- |
| 996 | 0.0497968271 | `3d4bf7c2` | exact fresh-distance match |
| 997 | 0.0273308475 | `3cdfe4f1` | unseeded query differs from the retained Port row |
| 999 | 0.054599762 | `3d5fa400` | beyond retained band; Port dropped |
| 1060 | 0.0239993036 | `3cc49a30` | exact retained-distance match in this query |
| 1062 | 0.054599762 | `3d5fa400` | beyond retained band; Port dropped |
| 1063 | 0.0477150157 | `3d4370d2` | exact fresh-distance match |

All six unseeded original distances exceed `0.02`. The measured fresh edge contacts therefore do not need speculative numerical explanations for their admission-band discrepancy. The original raw query returns slightly diagonal closest-core normals for several edge contacts; the Port offered/corrected normals are vertical after its ground-normal orientation. This observation concerns raw query output and does not show Stock would admit those edge records: they exceed its new-ground band here.

**Boundary:** this executes original constructors and geometry query on frozen Port poses with newly empty feature caches. It does not execute `0049ec00`, `0049b830`, the original retained-contact evaluator, force correction, full CRT/global setup, or the 900-step Stock drop. The retained face 997's changed query result illustrates why it would be wrong to relabel these as a complete retained-contact snapshot. Original admission/lifecycle causality and the final sleep expectation remain unresolved.

## Bounded original-code oracle design and preconditions

Next experiment: run the actual Stock new-ground selector over the frozen step-898/pass-0 state and candidate list, stopping before applying forces. The initial verdict should be the proposed face, sampled fraction, separation, local points, normal, and world point; then compare those values against the Port's selected face 1063 and its recorded geometry. Only a measured difference at this boundary justifies extending the probe into the direct corrector and final sleep test.

Use the qualified ARM64 Hangover console helper route demonstrated by `local/verify/stock-rng-relocation/{proof.c,run.py,README.md}`: private mapped Stock image, immutable read-only Stock mount, no entry point/game/GUI/audio, unique container/prefix/output, at most 2 CPUs/1 GiB, deadline and unconditional scoped container cleanup. The RNG helper substitutes native TLS for its own narrow purpose; that does not establish physics globals, original heap initialization, or imported calls for a physics helper. The geometry-only helper above qualifies a smaller constructor/query path with disclosed allocator substitution; it supplies no blanket initialization proof for the whole ground path.

Two calling conventions have now been checked against original machine instructions, not guessed from decompiler signatures. Retained disassembly: `stock-49ec00.asm`, `stock-49b830.asm` in the artifact root.

- `0049ec00` is cdecl with one stack argument naming the static ground body. At `49ec09` it reads `[ebp+8]`; it returns with plain `ret`. Its argument is **not** the world.
- The ground call site at `49f3da` loads `ECX` from global `00617650`, pushes twelve stack arguments, and calls `0049b830` at `49f3e7`. `0049b830` is thiscall and returns with `ret 0x30`. The world comes from that global, independently of `0049ec00`'s ground-body argument.

Before any physics launch, establish the following from original instructions/constructors and capture them in the helper manifest:

1. Resolve the PE's actual mapping, relocations and required imports. `LoadLibraryEx(...DONT_RESOLVE_DLL_REFERENCES)` alone does not establish initialized original physics globals/CRT. Assert every invoked function's expected entry bytes and every used global/constant address after relocation.
2. Construct the box and triangle cores through verified original constructors. `004a5e80` dispatches a box to `004ac1c0` (`all.c:121816-121820`); that constructor calls the original allocator and topology builder (`125506-125544`). Treat any helper allocator substitution as a disclosed experimental boundary. Do not transplant Port feature indexes or assume its C++ object layout matches Stock.
3. Serialize and restore the exact leaf/root/current/predicted state words and step flags. Current observer rows retain leaf poses, velocities and angular velocities, but are **not yet a complete Stock-layout snapshot**: parent pointers/flags, all state words, contact-list ownership and pair-cache provenance must be recorded explicitly. The synthetic leaf is a 20 kg box with half-dimensions 0.25; derive prototype mass/inertia data through verified original initialization rather than guessed offsets.
4. Reproduce all six broadphase candidates and their order, including the two Port geometry drops. Derive each triangle from the existing test's 32-by-32 grid in `tests/object_world_step_test.cpp:60-75`; the successful four contacts alone are insufficient. Verify the original broadphase/list-membership decision, rather than assuming every existing Port pair is already a Stock retained row.
5. Initialize and verify the candidate list and pair cache. `0049ec00` calls `00497280` for every candidate (`all.c:117322-117353`); the cache lookup traverses body/face ownership (`111396` onward). The list initializer is `0049eb10` (`117189-117211`). Do not stub cache lookup to “absent” and call that a retained-state parity measurement.
6. Instrument both the `0049b830` entry and the pre-cleanup frontier `0049f3fd`, so a no-admission result is observable and bounded. Capture the selected candidate, fraction and separation before cleanup, and any real admission arguments before allocation/admission, then terminate the isolated console process. [INFERENCE] With these same six unseeded distances, the initial minimum exceeds `0.02`, so the source-control-flow reading predicts the cleanup frontier without a `0049b830` proposal; original cache seeds could change that input. This first-stage oracle executes original selection/search and intentionally does not claim original admission or force results. For actual replacement/duplicate semantics, a second stage must use a constructed original contact list, invoke `0049b830`, and snapshot marks/lists before/after; its allocator/destructor paths need separate qualification.

Stop with a typed missing-precondition result if any of these are unknown. The already verified early-priority rejection branch of `0049b830` can be exercised without allocation or mass fields, but it would only validate that narrow comparator rule, not measure this six-candidate event or establish the sleep cause. A helper that substitutes guessed geometry, empty caches, or guessed mass layout would produce a misleading verdict.

One geometry-only console helper/container was launched after explicit supervisor authorization and cleaned up; no game entry point, GUI or audio was launched. No production physics, assertions, installed game, or `ref/` file was modified. The original integration gate remains red. Full-gate verification belongs to the supervising task; this note claims only the retained focused experiments and source findings above.

## Independent judgment boundary

Jev verified the unchanged baseline failure, unsuccessful eligibility probe, separate priority field and original geometry distance/source-preservation claims (`jev-verification.json`; confidence 0.82–0.89 for those source/result claims). An initially ambiguous multi-pass evidence slice produced a low-confidence contradictory face/index mapping judgment; that slice was corrected to the complete first pass only. The focused recheck verifies face 1063/index 3/result 2 and face 996/index 0/unprocessed at confidence `0.98` (`jev-focused-verification.json`). Its source-control-flow reading remains a review judgment at confidence `0.46` (`supports 0.64`, `contradicts 0.34`), so this note does not treat the admission-band interpretation as independent runtime certification. The actual original core distances are measured separately; complete original admission and final sleep causality still require the listed preconditions.

## Phase 1-2 finding: the box never settles (diagnosing-bugs, 2026-11-01)

Loop: `SLRR_PROBE_BACKGROUND=1 ctest --test-dir build -R '^object_world_step_unit$'` — 0.18 s, identical
values every run, red on the exact sleep assertion. A scratch copy (`.scratch/diag-sleep/probe.cpp`, tagged
`[DEBUG-a4f2]`, original test untouched) runs `DIAG_STEPS` steps and reports the sleep step.

At 3000 steps (50 s simulated) the box **never sleeps and never converges**: it hovers at `y≈0.293`
(0.043 above its 0.25 half-extent), `vy` stays negative at every 60-step sample, contact count flips
between 1 and 6, and `omegaSq` repeatedly spikes to 4-6. So this is a persistent limit cycle at the
contact margin, not slow convergence; lengthening the test or loosening the gate cannot be the repair.

Ranked, falsifiable hypotheses (none tested yet): (1) contact drop/admit hysteresis makes the box fall,
re-admit and bounce — per-step contact count and minimum separation should alternate with `vy` sign;
(2) the corrector acts only on penetration, so a ~0.04 margin yields a hover/bounce cycle — a smaller
margin should shrink the amplitude proportionally; (3) one-contact-per-root selection produces torque —
omega spikes should coincide with steps holding only 1-2 corners. Testing (1) needs a tagged probe in
`src/car_world_step.cpp` near the object-pair update (`objectPairs_`, lines ~1060-1160); only counts are
exposed today (`debugObjectPairCount`). No production change was made.

### Phase 4 probe results (tagged `[DEBUG-a4f2]`, temporary, reverted byte-identical)

Per-pair trace over 900 steps (`.scratch/diag-sleep/pairs.log`, `pairs2.log`): the ground under the box is six
triangles (faces 996, 997, 999, 1060, 1062, 1063). 996/997/1060 stay `Updated` and `resting=1` throughout.
999/1062/1063 are updated on pass 0 and `Dropped` on pass 1 of the same step, never becoming resting. Drop
reasons across the run: **3,797 distance-band** (`predicted > radii + skin`, e.g. predicted 0.07233 vs skin
0.05, `radii=0`) versus **5** 45-degree-normal drops. So hypothesis 3's mechanism (normal-deviation drop)
is not the driver; the band drops are a consequence of the box's predicted pose tilting away from those
triangles, i.e. downstream of an earlier asymmetry, not its origin. The stable ~0.04 hover is consistent
with a skin-width rest (`skin = 0.05`, shape radius 0) and is not by itself evidence of a defect.

Still unknown: the first asymmetric event that tilts the box. Next probe: log per-step box orientation,
corner heights and which faces are `resting` for steps 0-120, then compare against the Stock contact solver
(`FUN_004a5190` row solve) with the same three faces. No production change; gate remains red.

### Phase 4b: the first asymmetric event is the landing (no source instrumentation; scratch probe only)

Per-step body-state probe (`DIAG_TILT`, `.scratch/diag-sleep/probe.cpp`): at step 39 the box is flat
(`up=1.0000`, lowest corner 0.0723 above ground, falling 6.5 m/s). At step 40 exactly **one** bottom corner
is inside the skin (`corners=00001000`) and the box leaves the step with omega (-11.35, 9.23, 3.03) and
`up=0.989`; it then bounces corner to corner and never reaches flat rest. The step-40 candidate list
contains one corner's pairs (faces 997 and 1060 both at separation 0.01724, i.e. one point seen through two
triangles of a quad; face 996 dropped by the 45-degree normal check).

Updated ranking: (A) closest-feature selection yields one contact point per triangle, so a flat landing
over a triangle mesh admits few, asymmetric contacts and a large single-corner spin; (B) Stock does the
same and the divergence is later (solver/corrector or sleep gate); (C) the Port's one-contact-per-root
pass selection drops simultaneous corners Stock would admit together. A and C predict a large step-40
spin in Stock only if Stock's candidate list is also single-corner; B predicts Stock's step-40 list is
also single-corner but its subsequent response damps the spin. **Not yet tested:** run the original-code
collision core (the existing `.scratch/contact-admission-causality/original-core` helper) on this exact
impact pose and compare its candidate list/feature pairs with the Port's. Until then no fix is justified.

Issue mapping (read all 48 open issues, #1, #6-#52): every one is an acceptance-criteria slice, none reports
a defect. This failure is the concrete Gap for **#16** (stable moving-Car contact) and bears on **#20**
(compound body lifecycles) and **#17** (stale cached contact state); evidence belongs on those issues.

Downgrade of hypothesis (C): `src/physics_multi_root_step.cpp:443-486` documents that its
one-pending-contact-per-dynamic-root selection implements Stock `0049b830` (a root's pending mark is
displaced only by a smaller separation). Removing it would deviate from Stock, so it is not a defect to
"fix"; it is retained as the faithful rule. Remaining candidates: (A) closest-feature selection and
(B) a later solver/corrector/sleep-gate difference. Both need the original-core/solver comparison above.

## Correction and later bounded evidence (2026-11-02)

The preceding downgrade of hypothesis C was incorrect. Raw `0049f3b7..0049f3e7`
passes a sampled interpolation fraction separately from separation to `0049b830`;
`0049b891` compares that fraction with root+210. Admission also has sequential,
destructive replacement and asymmetric second-root equality behavior. Sorting
separations is not a verified implementation of this rule. See
`local/verify/object-sleep-diagnosis-20261102/admission-static.md` and the retained
REA dossier `ev_a81e8c8ecda0a4e6ce478e890aac0c20bafdc0052ee0ac45e2de17c70190fc1f`.
This corrects the research claim; it does not supply a causal sleep repair.

The earlier one-corner observation describes an initial candidate boundary,
not the complete step. `first-impact.jsonl` under that evidence root records
three actual corrections at step40 at distinct local points. Executing original
`00496050` on those three independently restored before states matches all999
state/contact words. Ten later support inputs exposed16 differing words in six
calls in the then-current implementation. Four captured compound prediction
inputs exposed25 differing words out of848.

Raw arithmetic instructions, independently audited and checked with original
matrix cancellation probes, identify sum grouping differences in `0054e9c0`,
`00436880`, and the direct corrector's distinct radius/response/denominator
operations. Production corrections now match all13 restored corrector cases
(4329 state/contact words plus status/counters) and all4 predictor cases
(848 state words). The original matrix and direct-contact regressions each went
red before their correction and green afterward. See `production-grouping.patch`,
`prediction/matrix-original/`, `prediction/component/production.log` and
`component-replay/production-grouped.log` under the same root.

These independent primitive inputs are not a Stock-driven trajectory. Predictor
execution declares a mapped-data bit that skips CRT destructor registration;
game entry never ran. The complete object sleep test still fails unchanged;
the complete gate remains125/126 CTests, with a separate passing post-CTest phase.
New REA predictor pseudocode was screened as blocked (injection probability0.81)
and was not used for the corrections; existing disassembly and original-code
execution are their authority. The incremental Jev gate remains unaccepted
(`safe_to_apply=0.09`); it verifies the absence of a sleep fix at confidence0.97
and leaves the positive arithmetic evidence as review judgments. All raw results
are retained without confidence rerolls.

## Original admission lifecycle witness (2026-11-02 continuation)

A new isolated helper now executes original `0049b830`, its contact constructor,
reference-list insertion, cleanup and destructor over five declared minimal
synthetic fixtures. This advances beyond the selector's earlier stop-before-
admission frontier. It does not execute Stock gameplay, geometry discovery or a
sleep trajectory. The only mapped-code replacements are the previously qualified
bounded allocation/reallocation/free entries; whole-code identity is checked
before relocation. The minimal bodies, materials and two-sentinel contact list
are explicit inputs, not a claim that the complete world was constructed.

REA MCP instruction evidence corroborates admission
`ev_7ca23381a2f953ab714ed4d9923bc260bd2f4875efa48c2b00868f31ac27759a`
and contact initialization
`ev_1dbd25ba832abcd066132eef953682ea5204b0435fde6a63ccfd99a10339b459`.
Every call, including open/close, has Jev before/after in
[`rea/ledger.json`](../../local/verify/parity-contact-admission-continuation-20261102/rea/ledger.json).
The new snapshot leaves previous snapshots untouched. A CPU4/host-memory8 lease
covered analysis and was released after close.

Two separate helper executions produce exactly equal raw records for 13 calls.
The [independent audit](../../local/verify/parity-contact-admission-continuation-20261102/independent-audit.json)
checks source/binary identities, list/reference/owner counts, separate fraction
and separation words and five rejected record corruptions. Observed transitions:

- A later fraction rejects without mutation; an earlier fraction destroys and
  replaces the existing contact. The contact retains fraction at `+ac` separately
  from its constant `0.01` separation at `+8c`.
- At equal priority, the declared positive dot-product case rejects and the
  negative case replaces the contact and world point.
- When both roots have different owners, the first owner's contact is destroyed
  before the second root rejects the new candidate for its earlier fraction.
  The failed call leaves the first root unmarked with no references and preserves
  the second owner. A global sort or an all-or-nothing admission transaction cannot
  express this observed transition.
- A duplicate feature pair rejects when no pending mark is held.
- The declared second-root tie fixture replaces its contact when the first root's
  stored fraction differs. Instruction `0049ba24` compares the first root's stored
  fraction in that second-root path; this asymmetry must not be silently repaired.

Evidence is retained at
[`run-31bcdc67b9`](../../local/verify/parity-contact-admission-continuation-20261102/run-31bcdc67b9).
The private ARM64 Hangover run declares `BOX64_DYNAREC_SAFEFLAGS=2`, uses a fresh
private clone of an already-stopped qualified prefix, one CPU/1GiB/128 processes,
no network/display/audio, read-only Stock and payload mounts, bounded outputs,
pressure checks and unconditional container removal. The complete seed and
reference hashes remain unchanged. Its peak modeled allocation is368bytes;
27 allocations and24 frees are observed at the final endpoint. Remaining owned
allocations are reclaimed by process exit, not described as whole-world cleanup.

The initial compiler invocation failed in the concurrently updated guardian
cleanup (Darwin zombie-only `killpg` EPERM); that failed wrapper and executable
remain retained. A separate checked build after the guardian repair passed.
The [Jev gate](../../local/verify/parity-contact-admission-continuation-20261102/jev-gate.json)
is unaccepted (safe-to-apply0.20, composite0.66383). It verifies the five-corruption
claim at0.97 and leaves the execution/destructive-order claims at review
confidence0.64/0.14. The raw original output and independent source review remain
the bounded evidence, not a production-physics or whole-workflow approval.

Next is a corresponding Port admission implementation and differential check of
these transitions, followed by integration with the separately sampled selector
and retained-contact lifecycle. The unchanged object sleep assertion remains
required. No threshold, assertion or production physics behavior was changed by
this witness.

### Local Port admission differential

The local `port-admission.hpp` prototype now executes the same five declared
fixtures. Its 13 after-states match the original records in contact order,
fraction/separation words, world points, feature identities, counters and all
four bodies' marks, owners, reference counts and predicted flags. Contact IDs
use an explicit bijection by first appearance; allocator counts have no Port
equivalent and are excluded, rather than manufactured. The comparison retains
both inputs and outputs under
`local/verify/parity-contact-admission-continuation-20261102/port-budget/port-first/`.

Two separate native processes produce identical control output under
ASan/UBSan. Six separately compiled mutations fail the same comparator: wrong
second-root tie, missing removal counter, stale owner, missing duplicate check,
missing dot rejection and separation substituted for fraction. This is still
an isolated prototype: attachment branches, exceptional arithmetic, complete
contact construction and world-step integration are not qualified. The fixture
sets x87 control word `0x007f`; native contraction is disabled. These cases do
not establish general floating-point equivalence.

The retained `port-jev-gate.json` remains unaccepted (safe-to-apply0.05,
composite0.733). It verifies the bounded comparison and mutation statements at
review confidence0.73/0.55 and the absence of integration at0.98. Independent
source review found no concrete defect within the declared fixture scope;
`child-independent-admission-audit.json` records 61 offline artifact checks.
The independent Jev review also remains unaccepted (safe-to-apply0.10,
composite0.53475). Fixture reset directly frees registry allocations, so the
reported free count must not be described as destructor-only evidence. The
object sleep test remains required and unchanged.

### Deep selector and phase experiments

`local/verify/parity-selector-deep-20261102/` supplies an unchanged pre-landing
control and an explicitly synthetic stationary box with both leaf/root poses at
height0.125. The original selector chooses face997, fraction1/1024 and
separation-0.125. The local selector matches all14 numeric words, including its
deep face-support fallback. Each original case ran twice in private isolated
Wine processes. Independent inspection checks topology, body bytes, detour
destinations and publication pointers in every run. Deep raw output repeats
exactly; control heap addresses differ by40bytes, while its independently
audited semantic frontier repeats. The failed raw-repeat assertion is retained.
The comparison rejects five record corruptions. Jev verifies the repetition
limits at0.91 and lack of trajectory/sleep proof at1.0; the numerical comparison
remains at review confidence0.46 and is directly checked by the comparator.

Two isolated world-step experiments under
`local/verify/parity-admission-phase-20261102/` retain the unchanged object test.
The first defers fresh selection until after retained-contact correction and
groups candidates by both body and ground. It still fails speed<0.3, at speed
0.302 with angular-speed-squared0.444669. The second also replaces dropped
cached vertex/face records with fresh candidates in the same discovery pass;
it fails at speed0.642 with angular-speed-squared2.19017. These results reject
either experimental change as a sufficient sleep repair. Neither experiment is
integrated into production. Full sequential admission, retained-cache semantics
and end-to-end physics causality remain open.

### Rotating selector precision witness (2026-11-02)

`local/verify/parity-selector-rotation-20261102/` restores two rotated Port
poses into the original selector with an explicitly empty cache. The original
runs repeat semantically. The first proposed contact previously differed in
separation/world Y: original word982673408 versus Port982673664. Instruction
0055030d stores the angle without popping ST0; FSIN consumes the unrounded
register value, while numerator calculations reload the float copy. The Port
rounded before both operations.

The production interpolation now preserves a double angle for the divisor and
uses its float copy for numerator angles. This is a bounded correction, not a
universal equivalence claim for host libm and x87 transcendental instructions.
`compiled-check-v1/` passes the full isolated geometry test; restoring divisor
rounding makes the new original-backed assertion fail. The local selector
matches all14 numeric words for the proposal and correctly produces no proposal
for the other case. Five record corruptions are rejected. The unchanged object
sleep test still fails, and production selector/admission integration remains
open. These executables use current geometry with the retained library; they
are not a new complete build or a green integration gate.

Independent source review found no concrete defect in the bounded change. Jev
verifies the compiled-test/mutant claim at0.96, but its patch gate remains
unaccepted (safe-to-apply0.33; composite0.814; blast confidence0.22). Its compound
selector/sleep claim has low confidence0.28; the exact comparison and assertion
logs remain available in `compiled-check-v1/audit.json` and adjacent raw outputs.

The `parity-admission-phase-20261102/v3-angle` local phase experiment combines
this corrected interpolation with the previously retained selector/phase sources.
It still fails the unchanged `speed < 0.3f` assertion. Its sources, exact compiler
command, binary hash and failure are retained. This rules out that combination
as a sufficient sleep repair; it does not identify the remaining cause.
