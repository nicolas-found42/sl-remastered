# Continuation record

## Current state, 2026-11-02 (issue #53 gate: physics sleep root-caused)

Active goal: deliver Full Port 1.0.0 (issue #53). The full gate
`tools/check.sh` is RED at exactly one CTest, `object_world_step_unit`; the
other 126 pass. See [requirement-map.md](requirement-map.md) for the
requirement-to-evidence map and D1–D8 status.

Physics root-cause this session (provenance-corrected, stock `.bak.exe`):
- The direct-corrector x87 matrix-row groupings are STOCK-CORRECT; the
  original-corrector audit's "force radius differs from generic
  matrixTransformRows" claim is REFUTED (both are (2+1)+0/(2+0)+1/(2+0)+1).
  Evidence: `local/verify/object-sleep-diagnosis-20261102/grouping-verification-bak-exe.md`.
- Compound step without contacts and per-triangle contact geometry are both
  exact Port-vs-Stock (existing oracles).
- The box-drop failure is a CONTACT-LIFECYCLE/DISCOVERY divergence: the box
  churns (manifold 1..10 contacts, corners migrate, mostly impact/state-6 not
  resting/state-7), omega²≈2.8, never reaches true sleep.
  Evidence: `local/verify/issue53-box-manifold-20261102/FINDING.md` + `contacts.jsonl`.
- Delegated: does Stock transition a persistent slow box-ground contact to
  RESTING and sleep? → `local/verify/issue53-contact-lifecycle-20261102/REPORT.md`.
  Do NOT weaken the settling assertions; repair the actual lifecycle/discovery
  divergence, then re-run the unchanged test and the full gate.
- **2026-11-02 FIX APPLIED** (src/car_world_step.cpp): Stock admits a NEW ground
  row only within core separation 0.02 (const 005f1070); the Port wrongly used
  the retained-row 0.05 tolerance at creation. Gating new-pair admission at
  0.02 cut box rotation ~6.7x — the test now PASSES the speed gate (vel
  0.467->0.183, omegaSq 2.76->0.41) and fails ONLY the true-sleep assert. No
  ctest regression (7 failures identical to baseline; car_world_step_unit
  passes). The box still micro-rocks on a corner (omegaSq 0.41 vs the 0.001
  rest gate, scaledActivitySq 16.6 vs 0.01) and never reaches kBodyAsleep.
  Residual contact-stability churn is the remaining blocker. Decision logged in
  docs/decisions.tsv; state in docs/requirement-map.md (REQ-GATE-PHYSICS-SLEEP).
- Reference provenance (Build606 vs Build940, VERSIONINFO 2.2.1/2.3.1) is still
  unresolved and blocks content reconciliation (REQ-PROVENANCE-VERSION).

Resume: read [requirement-map.md](requirement-map.md), then the two evidence
notes above, then the delegated REPORT when it lands.

## Current state, 2026-10-30 (Free Ride frame frontier)

Run `local/runs/20261030T232757Z-63125` used seed 235 and the existing timed
Free Ride input. It created a car and loaded several objects, then stopped at
`camera traversal: mounted type-1 2509802 () load unsupported: object
construction unsupported: mesh body`. Ground candidates still cannot be
drawn; object construction does not establish visible city rendering. The run
stopped before proving the parked host callback's later resumption.

GUI runs `local/runs/20261030T210013Z-38795` and `20261030T210917Z-40441`
displayed the main menu but did not select Free Ride. SDL events were recorded,
but automated clicks did not land at the displayed target; the computer-use
coordinate mismatch was reported. Short mouse transitions are now buffered.
The earlier traversal run `20261030T201238Z-31804` used unsupported shortcuts
and does not establish that the script frontier cleared.

- `RenderRef.setMatrix` (0x00481240 + 0x004810b0): stock FUN_0048bf50 writes
  the Ypr_toMatrix (0x0054ecd0) basis transposed into the 4x4 bone-local
  matrix with position*10 (0x0054f4c0). `SceneRenderInstance::setBoneMatrix`
  implements the single-bone overload (precision mirrors Vector3.rotate).
  The four-argument overload remains unsupported: bone parentage is not modeled.
- `Input.getAxis(II)F` (0x0047c990 -> FUN_00557430): unavailable keyboard
  samples and unsupported devices are reported, not replaced with zero.
- Material cache: `particle` and `skidmark` directives parse into
  `ResourceMaterialResult`; the type-14 cache accepts general render
  materials (particles.rpk flags 256), not just fonts. Tests:
  `resource_material_test`.
- TGA decode: `resourceTextureLoad` now reads TGA types 2/10 (24/32 bpp) and
  3 (8-bit grayscale); asphalt.tga cached live. Test: `resource_texture_test`.
- `System.isLoadingReset()V` / `System.isLoading()I`: stock uses a 32-sample
  loader processing history. The host models that history for its pack-load
  queue; other resource-loader queues remain a coverage limitation.
- `Object.wait()V`: the cursor and animation dispatch now use scheduler-owned
  host tasks and resumable state, so a top-level callback can park without
  replaying earlier side effects. Nested synchronous native callbacks still
  cannot suspend and report an explicit error. The live run reached a parked
  host callback but did not establish its later resumption.
- Camera traversal loads only supported mounted type-1 payloads and rejects
  unsupported payloads instead of marking them loaded without their contents.
  Visible mounted polygons and some cleanup paths remain unsupported.

## Current state, 2026-10-30 (later)

The blocker moved again: Camera.create plane-test mode 2 is implemented and the
frontier is now `RenderRef.setMatrix: nonzero rotation unsupported` in
`java.game.Navigator` line 123 (via line 108, `Track` line 1274). The
implementation is stock `FUN_00510fc0` (all.c:197966): stock stores the
plane-test mode unconditionally at camera payload +0x2c (`FUN_004861e0`,
all.c:99327) and treats every non-zero mode identically — no dilation, root
bounds reset to {mode 0, extents 100} on the camera instance node, forced
frustum checks, forced re-walk. The host now mirrors that:
`cameraCreate` stores the mode (no rejection), `CameraFrame::draw` resets the
instance bounds when the mode is non-zero, and `traverseCameraCandidates`
forces frustum checks and root-dirty for non-zero modes. Mode 0's per-frame
plane-distance dilation (2.0 per frame, float at VA 0x5f0ab8) is not modeled
(the host rebuilds the planes each call); that only widens streaming tests and
is recorded as a bounded divergence. Live run `local/runs/20261030T192759Z-26885`
(seed 235) passes the map camera creation and stops at the setMatrix rotation
arm. Focused tests: `camera_traversal_test` (mode-2 forced-frustum case) and
`native_camera_test` (create stores mode 2, frame draw resets bounds).

## Current state, 2026-10-30

The player still cannot drive Free Ride. The blocker moved from car activate to `Camera.create: plane-test mode unsupported`. Player `osd` queues `activate` on the controllable car; stock `FUN_00448d70` (all.c:57388) then clears car +0x1edc, finds the first occupied seat, and stores the player device's message 0x15 reply there. The reply is player +0x242c, a debug field written only by console AI commands (`FUN_0047b220` case 0x15; writers at all.c:39537 and all.c:90422), so a fresh run stores 0; stock never fails this step. The host activate tail now implements that (store the seated player's `PlayerInstance::debugAiValue`, default 0) and car `getInfo(0x15)` returns the stored word (stock car getInfo case 0x15, all.c:68209). Focused test: `game_type_native_unit` (occupied seat stores the player value, unlinked seat stores 0, getInfo reads it back). Live runs `local/runs/20261029T165126Z-74971` (seed 235, scripted) and `local/runs/20261030T183518Z-23047` (interactive GUI click, no seed) both pass car activation and exit 1 at the new frontier.

New frontier: `Camera.create: plane-test mode unsupported` in `java.render.Camera` (tree 10 line 23) via `java.game.Navigator` line 105, `Track` line 1274. The host rejects `Camera.create` arg 9 != 0 (`src/natives/viewport.cpp:110`); the Navigator map camera passes mode 2. Stock `FUN_004861e0` (all.c:99280) feeds the mode into the camera construction (`FUN_00539500`) instead of rejecting it. The mode-2 plane-test root updates are a separate open piece (`src/camera_traversal.cpp:95`). Ground/traffic/pedestrians/water/fog all load (traffic=817), the car sits on track at pos=-278.5,9.8,1033.0.

Pre-existing: `game_type_native_unit` lines 465-562 (camera-follow fixture) fail in the imported tree; they predate this unit (no source modifications existed in the working tree before it) and are unrelated to the activate path.

## Current state, 2026-10-29

The player cannot yet drive Free Ride. The latest isolated run is `local/verify/slrr-skill/20261029T232818Z-v5s_k2vz/`. Player `osd` queues `activate` with the player instance, the camera id, and the render-instance id. That command links the three ids onto the occupied seat, calls player `getInfo(0x1a)`, and stores the seat camera mode from stock `FUN_00448e60`. The mode word on the car is 0 when no seat has a camera id, or when that count equals the player-id count. It is 1 when the camera-id count is nonzero and the two counts differ. On the live path the mode is 0, so `FUN_00449680` calls `FUN_0048ce30` and stores the driver `ext_camera` look, move, and zoom on the seat camera. The missing driver skeleton is recorded as a visual gap, the same note as player car activation. Empty seat link slots then have their cached ids cleared. The command stops where stock would create cockpit text and the gauge. The cause is `GameRef.queueEvent: car activate gauges unsupported`. The only screenshot is the main menu, taken before the click. The run exits at 14.7 seconds, before the post-click shot, so the city is not shown. The car is at pos=-278.518, 9.800, 1033.002 with speed 0. `cleanup.json` has `child_stopped`, `scratch_removed`, and `evidence_retained` true. Seed 235 and a fresh disposable save were used. Scripted input does not prove physical keyboard delivery. The id `-65456` is the type-3 render node `s_renderref_camera` (`0xffff0040`). Stock looks that id up as type 1, so `player.osd` stays unset. The queued command carries the player instance, the camera id, and the render-instance id. A focused test applies the camera follow force while a second dynamic body, marked as a parent, stays registered and unmoved. The live frame dies in the animation callback on the unimplemented cockpit gauges, so the following physics step is not reached.

The native camera has a shared physics prototype, a type-2 body node, a type-12 camera resource, and a type-3 render node linked to the body. `GameRef.create` creates and owns these resources. The camera handles the initial `dist`, `smooth`, `force`, `torque`, `roll`, and `render` commands. The ground retains the most recent fog packet, which the camera copies. The existing renderer implements bounded vertex-linear range fog with software pixel tests for transparency and clipping. Stock device fog mode selection, depth, perspective, lighting and visual parity are not verified. The car now exposes its existing compound body through a `car_root` physics resource. The player camera command returns success, and the armed follow step applies the camera force while that car root stays registered. Collision and driving input remain unconnected. Player `viewport` and `camera` commands retain links. The camera command returns success. Player `osd` queues car `activate`. Activate links the three ids, stores the seat camera mode, stores the seat camera pose, clears empty seat link slots, and stops on the unimplemented cockpit gauges.

The VM dispatches native constructors and implements `Ypr(Vector3)` using stock `00482610` and `00551d20`. Focused tests cover constructor chaining and camera resource ownership. Constructor overload selection and native argument conversion differ for some values. `GameRef.isScripted` still needs a native-only resource path. The skeleton test working directory is corrected. Focused physics and observability tests pass. The latest full project check is recorded in `local/verify/camera-observability-check.log`.

The stock car seat model now retains separate player, camera, driver render and OSD links; selection and teardown pass focused tests. The real driver uses `humans/drivers/vin.skl` and a skinned SCX model; skeletal animation is not implemented. Car `activate` links the player, camera, and render instance, stores the seat camera mode and the seat camera pose, clears empty seat link slots, and stops on the unimplemented cockpit gauges. Seat follow is already applied, and the armed camera step runs once a dynamic body is registered. Ground subscriber delivery in the live journey remains open. Initial no-camera activation links the player, seat, render instance and collision subscription; driver animation is a deliberate visual parity gap. Standalone camera follow forces pass focused tests. The live physics callback uses the armed follow step once a dynamic body is registered, and it still uses the empty step before that. A no-contact camera step and a multi-root prediction/commit coordinator also pass focused tests. The coordinator stays out of this world. A bounded player-axis snapshot also passes; it is not connected to car forces. Wheel constraints, road contacts, compound stepping and frame scheduling are required for driving. The current ground body has no collision geometry. Stock consumes city `PHYS` indexed triangles for wheel and body contacts; `.trc` files are traffic graphs. The extracted city2 node `0x98d8` face 4 lies under the recorded car spawn. The bounded decoder and directed segment-to-face query pass tests against that extracted triangle. The steering attachment rotation also passes focused tests. Focused tests load the real city2 factory and passive spawn ancestors parent-first, retaining PHYS and POLY; the live collision traversal does not yet select them. The existing ground factory has been extracted into a reusable loader and passed focused tests and a fresh native run. The city2 ground parameters now parse with their omitted road path, as stock permits. EXTP bounds expansion is connected and a bounded candidate query finds loaded city2 faces, but always reports incomplete world coverage. The spawn leaf inherits the original city sector; face material 6 resolves to `GBegin1` in `city.scm`, not `Cross` in city2. Focused tests verify those links. Live collision integration remains open. Fix the first wrong state without a no-op, rerun the isolated Free Ride journey, and inspect the city and response to driving input when reachable. The full camera follow simulation, ground collision, car physics, HUD, pause, sound, career and stock behavior comparisons remain open. The detailed backlog is `docs/parity-checklist.tsv`; the player journey is `.agents/skills/verify-slrr/features/free-ride.md`.

The work order is to reproduce the first player-visible failure, trace its cause against the stock executable, make one bounded change, run focused checks and the real app journey, then update this record. After Free Ride becomes controllable, compare behavior with the stock Wine run and continue through the parity checklist. Use `docs/decisions.tsv` for the append-only decision trail. Do not commit or push unless asked. Run `python3 -I tools/ref_guard.py verify` before reporting a unit complete. Historical checkpoints are in `docs/STATUS.md`.

Loading stall and camera cleanup (verified 2026-10-31, seed 235 scripted Free Ride): the loading dialog never dismissed because `Thread.sleep(F)` was treated as seconds. Stock `0047c650` sleeps on a clock of `QueryPerformanceCounter` seconds times `1000.0` (float at `005f0910`), so the argument is milliseconds; the `LoadingScreen` flasher sleeps literal 300 and `HotkeyWatcher` 50. `VmScheduler::sleep` now divides by 1000 and stores a double wake time. After the fix the flasher sees `System.isLoading` go true then false and wakes `host:handleEvent` (run `20261031T032325Z-97896`). The next failure was `FUN_00511e60` hidden-node cleanup rejecting nodes that already had a type-1 camera record; stock destroys that record and never touches candidate records, so `CameraType1Records::removeRecord` now does the same and the candidate guard is gone. The run now fails one step later, in `simulation step: camera follow: registered car physics cannot be advanced by camera-only step` (run `20261031T032901Z-3040`): the per-frame car step is the next unit. Observability added for this: `threads.txt`, `world.txt` and `gaps.txt` in each run directory (refreshed every heartbeat and on loop exit, copied into `crash.txt`), `monitor wait/wake/lost notify` notes, `System.isLoading` edge notes and a `stall:` note when a host task stays parked for 5 s. Open problems recorded by this session: `ref/analysis/*` holds four files that `ref_guard` reports as added (dated before this work), and ctest has four failing targets (`resource_material_unit`, `native_render_change_vm_unit`, `physics_multi_root_step_unit`, `object_runtime_unit`) whose sources were last edited between 18:28 and 20:32 on 2026-10-30 and were not touched by this session.

Test triage: the multi-root fixture now exercises rollback with a second failing camera callback instead of relying on a prediction flag that the stock force pass `004a3f90` clears; it checks the first step's velocity rather than position for simple Euler integration. Material fixture permits the textureless material accepted by stock `00549720`; light texture IDs accept signed decimal as scanned by `00548370`. Synthetic object-runtime fixtures bind materials before LOD updates and guard failed render lookups. Build and tests have not been run for these changes; the main integration owner must run the requested build and full ctest. The reported four added `ref/analysis/` files are not in `tools/ref.manifest`; their names and contents identify Ghidra/Kuna analysis output of the `.bak` executable, but their author is not established. Recommend moving them outside `ref/` only with user approval; no reference files or manifest were modified.

Driving integration remains incomplete (run `local/runs/20261031T034730Z-5740`, seed 235): the probe exits at 15.45 s with `camera follow: registered car physics cannot be advanced by camera-only step`, before the scripted accelerator hold. The physical-root diagnostic now reports `(-0.000,-0.261,0.180)` at rest, whereas the earlier cached script position was `(-278.518,9.800,1033.002)`; `GameRef.setPos`/`setMatrix` currently update the cache rather than the physics compound. Bounded multi-root, compound ground-contact, retained tire, dynamic-pair and drivetrain/damage units were extended; they are not connected to the live world step. The stock sector manager at ground instance +0xb4 (00463220, 0048ae40, 004b4250) and its ordered neighbor lifecycle remain missing; no collision clearance is claimed. Failure sound routing and named configuration overrides are also unbound. Focused `ground_traffic_unit`, `car_runtime_unit`, `car_activate_unit`, `game_type_native_unit` and `physics_multi_root_step_unit` passed.

Sector collision investigation: stock 004a2010 skips child nodes marked 0x200000 before dispatch; passive candidate traversal now does likewise, with a real-city fixture regression. This is not the sector-manager lifecycle: 004a2540 queued world-node links, 004b3c40 records and 004b4250 swept-endpoint neighbor updates are still absent, as is the mixed type-1/type-2 collision descent. Candidate completeness remains false; no live contact coverage is claimed. Tests were not run.

### Drive journey status (run `local/runs/20261031T05*`, seed 235)

Car step is live (`CarWorldStepper`, `GameWorld::stepRegisteredCars`): controls sampled, drivetrain and tire steps run, ground pairs are retained across frames and de-duplicated across overlapping city sectors, sectors within 6 m of the car are queried. Two blockers remain before the car drives correctly:

1. **Spawn height (fixed).** `City.enter` places the player car at `posStart` (-278.5, 9.8, 1033.0), and the ground there is one layer at y=9.812. Wheel centres hang 0.47 m under the root (the suspension probe is a vertical segment of +/-0.5 m through the wheel body), so a rear wheel is accepted only once the root is about 0.37 m above ground, and the chassis mesh stops the car at about 0.31 m. The port updated every suspension (`00491100`) before the first collision pass, which extends a never-touched suspension to its maximum length (0.705) and makes the selection test (`00491d50`: `length - dist <= aux`) fail by 0.005 m. Stock `004a5190` runs the constraint update only while the world is unsettled (the flag at world `+0x1ec` is cleared by joints and by `004a2d10` when contacts are near), so a suspension that has never held a contact keeps its built length (0.605). `stepMultiRootWorld` now skips the update for a suspension with no contact body. Result: all four wheels hold a contact from the first step and the car settles at root = ground + 0.6 m without any lift (seed 235 run `20261031T055226Z-38193`). A 0.3 m spawn lift gave the same settling, a 0.15 m lift did not. This rule is inferred from the pass structure, not read from a stock run; check it against the stock Wine capture if a numeric reference appears.
2. **Stacked contact impulses (open, cause not found).** A head-on hit at 18 m/s into a wall (material 0x8, normal 0.98,0,-0.18, ahead of the car at x~-345) produces 6 simultaneous face contacts from the front parts and the speed goes 18 -> 95 m/s in one step. The earlier note here that stock solves these in `004a3900`'s matrix was wrong for this path: stock `00496050` (the direct type-6 correction, vfunc 0x10 run by `004a35d0`) reads point velocity through `004a73c0` from `state[current ^ 1]` and applies impulses through `004a69e0` to `state[current]`, which is what `correctDirectContact` does, so six contacts in one pass stack in stock's formula too. What differs is the contact lifecycle. In stock `004a35d0` corrects only objects in state 6 and sets them to 7; the only writers of state 6 found are the constructors (`00492190`, `004922e0`), and contact objects expire after 10 steps in `004b4250`. The port instead keeps one pair per (body, face) and corrects every pair on every pass. Stock also has no 0.021 position shift in `00496050`; the port's shift comes from elsewhere and its source is not confirmed. Next: read how `0049f4d0`/`004a2d10`/`004b4250` create and re-arm type-6 objects (and whether several parts of one compound share one object per ground sector) before changing the loop.

The run log carries `car live wheel N`, `car wheel selection rejected`, `car contact iteration` and `car step discontinuity` notes for both.

Step trace: `docs/run-log.md` section "Car step trace" lists the `cartrace*.jsonl` files and fields. In run `20261031T053116Z-33067` the speed jump at tick 674 is one step: 8 contacts in the first contact pass share the wall normal (0.98, 0, -0.18) and the largest impulse is 23140; the second pass has none. Wheel 2 has a face hit but never a held contact in 1461 steps (`wi` stays 0, `s` stays `R`).

Render/physics ground alignment is **not** established. A probe that applied the ground draw matrix to the `positionScale = 10` vertices produced bounds about 10x the car's world coordinates (x -3755..-1915, y 80..188, z 8572..12096); dividing them by 10 afterwards is not a rendered-world transform. The only verified ground height is the physics layer (y=9.812 at the spawn). Before trusting `positionScale = 10` for ground in `src/scene_raster3d.cpp`, check `drawMatrix()`'s scale and compare an intersecting rendered triangle at an actual world point.

## Contact selection per root (stock 0049b830)

Stock keeps one pending contact per dynamic root body in a pass (0049b830, called from 0049c8f0; marks `0x400` and `+0x210/+0x214` on the root). A new contact is rejected when the pending one has a smaller `param_12`, which the caller passes as `local_270`: the core distance minus both radii, i.e. the contact separation. (The step-time fraction `local_25c` is a different argument, `param_11`, only stored in the record.) Otherwise the pending contact is deleted and the new one replaces it; an exact tie goes to a position/normal dot test that is not ported (collector order wins instead). `MultiRootWork::onePendingContactPerRoot` (set by the live car step) applies this: per pass only the smallest-separation contact per dynamic root is corrected; static roots hold no mark. Result: the wall hit no longer jumps speed (max 18.25 m/s over 1430 steps, run `20261031T061708Z-42722` with an earlier time-of-impact key; re-verified below with the separation key). Test: `tests/physics_multi_root_step_test.cpp` (three ground contacts, only the smallest separation is corrected with the flag).

## Ground-face contact normal and partial collision cores

Two stock-faithful fixes unblocked the drive after the one-per-root contact rule:

1. `updateGroundFacePair` now drops a pair whose closest-core normal deviates from the stored PHYS face normal by more than 45 degrees, and otherwise overrides the contact normal with the face normal. A dim-0 mesh part at rel (0,-0.10,0.25) produced a -3.629 separation contact with a -z normal on the flat road; with the separation-keyed one-per-root pick it suppressed every real contact each pass (zero impulses, car fell through). Ground-face contacts in stock are constructed from the face, so the deviation marks a failed core query.
2. `meshCore` keeps the well-formed feature prefix when the 004ac150 reservation fills instead of failing the load. Stock 004ab8d0 stops at the reservation and returns failure; 004a5e80 ignores the return and the shape's built flag is already set, so the object loads with a partial core. The port used to abort, which killed the frame render when a mounted object's mesh (type-1 2498064) exceeded the capacity.

Result: run `20261031T070852Z-53012` (seed 235) ends status=ok in 41 s, 1401 steps, max speed 18.36 m/s, no step raises speed by more than 5 m/s; the car settles onto the wheels at root 10.374, drives 16.9 m/s, and stops at the wall. Screenshots (`local/runs/layers/drive_a/b/c.png`): the city (trees, houses, road furniture, minimap) renders; the car is still an untextured white shell without wheels; the follow camera is a high top-down view that ends inside the wall texture after the stop.

Remaining rung-1 render items: car paint and wheels (per-part render instances), chase camera framing, camera clipping into the wall, speedometer/gear HUD.


## 0049fa60 coplanar-graze clip

`queryGamePhysicsFace` now ports the near-coplanar branch of stock 0049fa60 (both segment ends within 0.02 of the face plane) instead of returning Unsupported. The port is literal: the edge value pairs each delta component with the other axis of the edge (a 2D cross scaled by the signed projected area), the f0 >= 0 branch tracks the local_44 maximum and the f0 < 0 branch tracks the local_40 minimum (missing when f1 < 0), the loop breaks when local_40 <= local_44, and a surviving hit reports local_44 with the mirrored point formula start + (start - end) * fraction. On normally-wound faces the branch mostly misses; tests in `tests/resource_world_test.cpp` pin the literal semantics including the one configuration that hits (start on the interior side of every edge, leaving through one: fraction 0.6845 on the spawn face). This removed the 'wheel face intersection unsupported' abort when the car slid along the boundary wall (run 071726Z failed there; 073522Z passes).

Also: scripted screenshot saves now print to stdout so play.sh runs no longer flood run.log's err stream (162 mislabeled lines per run).


## Car parts render and ResourceRef.destroy

`SceneRaster3D` now draws every installed car part: the chassis at the car root body (fallback when the chassis node has no instance) plus the `attachedParts` tree, each part at its `partPhysics` root body pose with its own type-14 material, mesh, and texture. Wheels render (chrome rims visible, 55 pieces). The blocker was `ResourceRef.destroy`: the port queue-destroyed the node, and the Free Ride car assembly drops its temporary GameRefs after install, so all 52 installed parts were destroyed before the first frame (raster: live=0 dead=52). Stock 0047d1e0 traces to 0048a8d0 -> 00427620, which posts a render-refresh record (0x1c, vtable 005f09b4 — the render command queue record also used by FUN_00427210) and never destroys the instance; the port now detaches the binding only. Debug raster instrumentation removed; the parts count note stays.

Next render items: chase camera (current view is a close top-down), paint texture check, camera-wall clipping, HUD speedometer/gear.

## Open: spawn settle with the full part compound (drive regression)

With parts rendering (see above), the spawn overlap deepens: the compound offers ~36 ground contacts (a dim-0 part at rel (-0.00,-0.10,0.25) reaches separation -3.629) versus 16 before, and the 0.021 positional shift launches the car past the wheel-catch window (root 10.72; wheel anchor distance 0.9 against the stored 0.597 ratchet from 00491d50's closer test). Removing the shift (impulse-only, per 00496050) sinks the compound through the road instead. The creation drop (0049c8f0) with no shift free-falls through because the port's free fall creeps ~0.005 m/step with velocity clamped at one gravity step (the prediction/commit path discards the car prediction each step) — the wheels catch at ticks 2-6 with 9.5-16.3 kN but cannot carry the settle before the velocity revert stalls the fall.

Paint (unverified, tint likely unapplied): the body samples `Nonus_chassis.dds` (average RGB 150,147,148 -- light grey) and renders white-ish, consistent with the un-tinted template; the nine Emer Nonus factory colors (`cars/textures/factory_colors/Nonus/`) are all dark or saturated, so IF stock tints the body per colorIndex a white body is wrong. UNVERIFIED: the run's actual colorIndex, whether stock multiplies the chassis texture by the factory color or replaces material slots, and lighting effects. Fix path: log the resolved chassis texture id and colorIndex at car creation, read the stock tint mechanism (around 00449680), then apply it in the raster chassis material.

Camera state: the follow binds and steps (1 device, 1 bound per step), the seat pose applies from the driver ext_camera (mode 0), and the current top-down view needs FUN_0048ce30's command mapping resolved: mode 0 posts a param_3-branch command (empty-format FUN_00551220 -> FUN_00426800) plus a move command whose 7-float argument order (offset3, distance, rotation3) and driver-record layout (00443d40: numbers 2..11 at record +0x64..+0x88 with number 11 scaled by car+0x1278 and number 1 at +0x8c scaled by car+0x127c) must be mapped before changing seatCameraPoseFromDriver. A look-gating experiment was reverted for lack of that evidence.

## Root-flag status (corrected)

Established: `FUN_0049b0b0` (the generic body-creation helper every `FUN_0049b230` body goes through) sets body flag `0x100` only when the shape word `+0x9c` has bit `0x40`.
NOT established: which creation path builds stock's LIVE aggregate car root. The type-7 call at `all.c:44761` sits in a config-parser context, so neither that shape's flags nor a chassis-root hierarchy are proven for the live root. Earlier wording in this file that called the NoRotation question "resolved" was premature and is withdrawn (a correcting `decisions.tsv` row records the same).
Gate: no root refactor (mass, NoRotation, or replacing the synthetic root with the chassis part root) may land before a LIVE stock trace via REA — a memory scan of the live root body (flags, invMass, pose trajectory) during the spawn settle in the running stock exe, or an REA breakpoint on the creation path that actually builds it.

FUN_00449680 scope check: its body (all.c 57783-58032) contains no FUN_0049ab60 / FUN_0049b230 / FUN_0048a1e0 calls -- it is the seat/camera/OSD setup path (FUN_0048ce30, FUN_00448f20, gauges, bone setup), NOT the compound/body constructor. The stock chassis part bodies are built elsewhere (the car resource loader path), so even the car-constructor function does not build the root through FUN_0049ab60; the live trace stands as the only route. NEGATIVE EVIDENCE (REA read of 0x0060e0f8, 107 x 8 bytes): the stock car cfg directive table contains NO handler 0x00437070 -- the 107 entries match the port's kCarDirectiveTable exactly (click->0x46b950 at entry 0, mass->0x443f50 at entry 61, ext_camera->0x443d40 at entry 70, etc.). FUN_00437070 is therefore NOT a car cfg directive: the type-7 root body it creates at object +0x168 belongs to some other system (a scene-object/other-resource constructor with a "%s %f %f %f" parser), and the earlier idea that stock's aggregate car root is that shape is closed. Stock's LIVE aggregate car root remains completely untraced -- the only path is a live stock run under REA (launch the Wine stock exe with the REA session, memory-scan the live car root during the spawn settle: flags, invMass, pose).


## Camera command mapping (cfg numbers -> follow force)

The Emer Nonus `_main` cfg (lines 71-73): `controller 0 1 2 3 4 5 6`, then `ext_camera 120 0,1.8,0 0.0,1.0,-2.0 0,0.0,0.0 5`. Stock FUN_0048ce30's mode-0 caller (FUN_00449680) formats ONE `move <id> <v?>...` command with the driver record fields; the port maps v[1..3]=moveOffset, v[4..6]=lookOffset, v[7..9]=moveRotation, v[10]*distanceScale=moveDistance, v[0]*fovScale=zoom. For this cfg the computed pose is moveOffset (0,1.8,0), lookOffset (0,1.0,-2.0), moveRotation (0,0,0), moveDistance 5*distanceScale, zoom 120*fovScale. The current top-down view is consistent with moveOffset (0,1.8,0) (a point 1.8 ABOVE the car) being the spring target: the follow pulls the camera to that point and the look torque aims it at the car, i.e. straight down. The cfg's `ext_camera` numbers are stock-authored, so if stock's chase view uses this same record the port's FORCE application must differ (e.g. stock treats the 1.8 offset as a rotation-space offset, or the moveDistance semantics invert the direction); alternatively stock's seat camera in Free Ride is mode 1 (the FUN_00449680 else-branch: moveRotation (0,-(r*0.1+1),-(r+scale)), moveDistance 0.01, lookOffset (0,0,-20)) and the port's mode selection is wrong. Stock FUN_00448e60 (mode condition, read): mode=1 ONLY when cameraIds != 0 AND cameraIds != playerIds; otherwise mode=0. The port's portSeatMode mirrors that count, and the live activation logged mode=0 with cameraCached=0xffff00ad -- so either playerIds != 0 and equals cameraIds (mode 0 is then correct, and the top-down view comes from the mode-0 pose itself), or the port's counts differ from stock's seat iteration (stock strides 0x12a ints = 0x4a8 bytes from param_2+0x138, player id at -4 ints, camera id at 0). RESULT (live log): seats=1 players=1 cameras=1 -- both counts equal and nonzero, so FUN_00448e60 sets mode 0, and the port's mode 0 IS stock-correct. The top-down view therefore comes from the follow force applying the mode-0 pose: moveOffset (0,1.8,0) puts the spring target 1.8 m directly ABOVE the car and the look torque aims down. Stock's mode-0 move command carries the driver-record offset semantics (FUN_0048ce30's 7 floats: offset3 + distance + rotation3 from the record at +0x88/+0x7c/+0x8c, NOT the raw ext_camera v[1..10] the port assumes) -- the port's extCamera field mapping is the remaining divergence. READ BLOCKER: FUN_0048ce30's 7 floats come from three driver-record fields that the decompile types inconsistently -- `param_4` (offset vec) is read as a POINTER stored at record+0x88, `param_6` (rotation vec) as a pointer at +0x8c, `param_5` (distance) from +0x7c, target id from +0x78; but the ext_camera parser FUN_00443d40 writes SCALARS into +0x88 (number11 x car+0x1278) and +0x8c (number1 x car+0x127c). Either the mode-0 record FUN_00449680 reads is a DIFFERENT record than the ext_camera cfg record, or the decompiler's scalar/pointer typing is wrong. Settle this from the LIVE stock state (REA: the driver record bytes at car+0x1784+idx*0x90 and the command strings FUN_0048ce30 formats) before changing seatCameraPoseFromDriver -- the same live-trace session that settles the root can capture both.


## Paint: the `colors` data path (finding)

The `colors` cfg directive (`0046d330`) parses `count` followed by count numbers into the BODY record's +0x60 vector (the port stores it as `CarBodyRecord::colors`). Stock's `colorscheme` command handler (FUN_00475ec0, the vehicle command parser, `local/census/DecompileAt-00475ec0.log`) reads the record's color list and calls `FUN_0048c8c0(<color at +0x60[index]>)` for each body whose color count is > 0, with `index = colorScheme % count`. `FUN_0048c8c0` resolves the color number as a RESOURCE and stores it into the resource's `+0xcc` field -- i.e. the color scheme selects a RESOURCE ID per body (a paint resource, not a texture multiplier). The port: `CarBodyRecord::colors` is parsed but has NO consumer (grep: only the parser writes it), and `colorIndex` (set by the `colorscheme` command) is stored on the car instance with no renderer consumer either. That matches the observed white/unpainted body: stock binds a per-body paint RESOURCE through `FUN_0048c8c0`; the port never applies it. PORT-SIDE STATE: `SceneRenderInstance::setInstanceColor` exists with the correct stock semantics (instance +0xcc, initial 0xffffffff = white), and object_runtime applies it for OBJECT instances (src/object_runtime.cpp:232). What is missing: (a) any consumer of `instanceColor()` -- grep shows NO reader, so even object colors never reach the raster/material; (b) the car path never calls it: the `colorscheme` command stores `colorIndex` and nothing computes the per-body color resource or calls setInstanceColor for the car parts. CALLER EVIDENCE (REA procedure_callers on 0x0048c8c0): 6 resolved callers -- 0x449680, 0x4661b0, 0x46f890, 0x46f9e0, 0x475ec0 (the colorscheme command parser), 0x480310; coverage partial (14 call refs into the body lack resolved sources). The setter is used by several paths (object/vehicle color assignment), consistent with a shared instance-color field. The +0xcc CONSUMER found: FUN_0048aa70 (all.c 102068-102095) reads the render resource's +0xcc and treats it as a RESOURCE (object with +0x50) copied into a caller-provided reference holder -- i.e. stock's colorscheme swaps/references a COLOR RESOURCE, not a tint multiply. So the paint is a per-body color resource assigned through the render instance, and the port must (a) resolve the selected color id to a resource and (b) make the body material use it (the same shape as the port's SceneRenderInstance::instanceColor_ but holding a RESOURCE reference, not a raw 0xRRGGBB). The float reads at 120769/122277/37184 are unrelated struct fields. NEXT STEPS (paint, independent of the settle): (1) identify the render-instance +0xcc draw consumer by reading the type-12/14 material/draw path (0051xx renderer functions) for a read of the same offset inside a render-instance-typed object; (2) wire instanceColor() into the raster/material color multiply with the stock semantics (0xffffffff = white); (3) implement the `colorscheme` per-body selection (color = body.colors[colorIndex % count] when count > 0) and call setInstanceColor on each body's render instance. OBSERVED: the live Free Ride run never issues the `colorscheme` command (the port logs zero of them; the command string exists only inside the stock exe and the packed scripts, not in the unpacked Scripts tree) -- so the white body in Free Ride may come from the DEFAULT body color path (body record colors with no scheme command, or the render material\'s own texture), not from colorscheme. Verify which path Free Ride uses before wiring the scheme: check the Vehicle.load/<init> trees in cars.rpk (the packed classes) for `colorscheme`/`paint` commands. | STOCK `paint` COMMAND TRACED: FUN_00475ec0 matches "paint", parses `%s %f %f %d %d %f`, resolves the first token via FUN_004681d0, then calls FUN_0048db00(resource, f, f, 0, f); FUN_0048db00 builds a 0x24-byte link record (vtable 005f2bfc) holding the resource's +0x50 id and splices it into the resource's +0x48 list (FUN_004fd650) -- a subscription/link update, NOT a material write. The PORT has no `paint` branch: for the car device an unknown command returns monostate silently. PORT OBSERVATION (run after adding a one-time "car command unrecognized" note): the Free Ride flow issues ZERO unrecognized car commands in the first 30 s -- so `paint` is NOT sent during Free Ride and the white racer body is not a dropped `paint` either. The body material comes from the loaded body render resources as-is. REMAINING PAINT QUESTION narrows to: does stock's racer body material itself differ (a paint texture chosen at car creation from the descriptor's colorIndex), which is a load-time texture selection, not a runtime command. NEXT: read the savegame descriptor -> car creation path (GetVehicleDescriptor colorIndex consumer, 00449680/00467ff0-era) for a texture/material selection, and compare the port's body material texture against the stock shot for the SAME car (the Emer Nonus Street GT -- the wine capture is a different car, so a new stock capture or the stock material data is required).


## HUD gauges (speedometer / gear): port state

The car cfg carries the cockpit gauges (`cockpit_speed -0.385 0.160 -0.460 0 0 0 0x00020037 2.2 -0.0084 -2.2 2.2` and `cockpit_rpm ... 0x00020037 2.2 -0.0004 -2.2 2.2` in the Emer Nonus _main). The port PARSES them (`CarConfig::cockpitSpeed/cockpitRpm`, handler 0x00444b60/0x00444cb0) but NOTHING consumes them: grep finds no reader of cockpitSpeed/cockpitRpm outside car_config.cpp, and the activation path only CHECKS that the ten cockpit seat links are non-zero (`car activate gauges unsupported`) without creating or drawing any gauge. Stock creates the gauges at activation (FUN_00448d70-era, `cockpit_rpm`/`cockpit_speed` become OSD elements bound to the seat osd device). The gear indicator in the stock shots is part of the OSD (`osd_gauge`/gear text resource). ADDITIONAL FACTS: the Emer Nonus _main cfg has NO `osd_*` lines at all (only cockpit_speed/cockpit_rpm), so the stock HUD for this car comes from the OSD RESOURCE bound through the `osd` player command (the seat osd id) plus the script-side render.osd classes -- not from cfg OSD elements. The port has no render.osd native surface (registry check: no OSD entries) and no consumer of the seat osd link. So the HUD works needs: (a) the `osd` command's `activate <player> <camera> <render>` to route into a script-side OSD (the player osd resource the command already resolves and links), (b) native coverage for the render.osd classes that resource uses, (c) a draw path for the OSD render instances. None of these exist; the HUD item is a small subsystem, not a tweak. INVENTORY DONE: render/Osd.class has 73 methods and the osd package 11 classes (Button, Gadget, Label, Menu, MultiChoice, Slider, StringInput, Style, Dialog + subdir). The methods used by the HUD (createRectangle/text/createText/createObject/createIcon + gadget factories) are pure script-level calls into render natives (render.Text/Rectangle/Group/Viewport). The port's renderer already draws text/rectangles for the frontend (the caption "Player riding ..." renders), so the HUD is mostly about the OSD RESOURCE's script running and its render objects being attached to the player's viewport -- not new natives. FIRST HUD DATUM (live): the `osd` command resolves id 0xffff0050 with status=found but the node has NO packNode (no name) -- the OSD resource is a native/VM-backed object, not a packed resource. The instrumentation first crashed on packNode->name (fixed with a null guard -- a real bug found by the new log). NODE IDENTIFIED: world dump shows `'Ljava.render.Osd;' type 1 instance 0xffff0050 flags 0x231 (own sector root)` -- the player OSD is a java.render.Osd game resource (type 1) created at startup with a script receiver class java.render.Osd. Its script runs (the Osd.class methods above); the HUD work is therefore: confirm the Osd script instance is loaded and initialized (the world dump shows the node exists), then verify the render objects its methods create (createRectangle/createText/createObject on the player viewport) are submitted to the port's draw list, and that the viewport passed by `activate` matches the camera the world renders through. RESOLVED (live): the `osd` block's lookup returns Found with worldNode = 0x75f9915900 and the world's findInstance returns the SAME node: type=1, loaded=1, script class java.render.Osd. The OSD node IS loaded and has its script class; the earlier null was my first instrumentation gating on a different (camera-control) block. CORRECTED AND TRACED: `createObject` (tree 96) calls the overload tree 98, which calls tree 99; tree 99's actual calls are: Vector.lastElement, a `new java.render.RectangleTemplate`-style construction, GameType natives (the registerCallback/setEventMask family), RenderRef.setMatrix(Vector3,Ypr) and Vector.addElement -- i.e. the OSD's object factory builds a RectangleTemplate-like render object through GameType natives that the PORT ALREADY HAS (registerCallback/setEventMask/setMatrix are registered; RenderRef.setMatrix exists) plus java.util.Vector (script-side). So the OSD factories are NOT missing natives: the objects are created script-side and registered via render-ref natives the port implements. The blocker is therefore (a) the OSD script's methods never running (no OSD activation/show call) and (b) whatever the resulting render refs need to draw (the OSD object vector / viewport attachment). FOUND THE CALL: `Player.showOsd()V` (Player.class tree 7, line 59) is exactly `this.osd.show()` followed by a `GameType.EVENT_COMMAND` "osd <id>" command to the player -- i.e. the OSD is SHOWN BY THE PLAYER SCRIPT (not by the native `activate` command, which only links the ids). The port's player script path: `showOsd` is a Player method; check whether the port ever invokes it (grep of the port for showOsd -> none; the script drives it via Racer/Track or the frontend). Also the OSD's own `show()V` (tree 79) forwards to `show(I)V` (tree 81) which walks `init`, `resetSelection`, `controlMode`, gadget list -- all java, using the natives the port has. CALLER CHAIN COMPLETE: `Track.enableOsd(I)V` (Track.class tree 80, line 1772) calls `player.showOsd()` -- Track drives the OSD (also updating osdCounter/nav). So the HUD gate is: does the port's script flow execute `Track.enableOsd`? (grep of the port for enableOsd -> none; Track is a script class, so this depends on the VM dispatching the script method, which the port does for other script calls -- the question is which script path calls enableOsd, likely the city entry / track setup). RESULT (live): the whole chain DOES run -- Track.enableOsd(I)V (1), Player.showOsd()V (1), Osd.show()V (8), Osd.show(I)V (8), plus the loading-screen OSD. So the OSD script executes; the gap is downstream: the gadgets' RenderRef/rectangle objects it creates must be attached to the player's viewport and drawn. The viewport render-attachment path exists (ViewportRenderAttachments, Camera.create prepares/activates it, CameraFrame::draw iterates `active()`), but the OSD path creates its objects as RenderRefs -- check whether OSD RenderRefs are added to the SAME attachment list or to a separate OSD layer (stock's Osd.show adds to its own group vectors; the `osd` command's activate triple (player, camera, render) suggests the OSD render instance is the PLAYER's render attachment, not a camera). RESULT (provisional, WITHDRAWN): the one-shot inventory fired at +00.351 f0, BEFORE the player OSD activation (+14.45 s) -- it measured the pre-activation state, so it establishes nothing about Free Ride's attachments. The "never attached" claim in decisions.tsv is withdrawn (correcting row appended). POST-ACTIVATION MEASUREMENT (run, +14.439 f1346, right after activate): the two active viewports (0xffff036c, 0xffff0041) each carry exactly one type-3 camera attachment (0xffff0368, 0xffff0364). Still no OSD render objects in the viewport lists -- now this is measured AFTER activation, so the finding stands: the HUD objects are not attached to any viewport. The OSD's drawing surface is therefore the player's render instance path (the activate triple), not the camera viewport list. PLAYER RENDER INSTANCE (live): id=0xffff040e, node found, type=3 (render instance), loaded. So the activate triple's render id resolves to a live type-3 render instance. The OSD script's created RenderRefs would attach TO that instance's scene render instance (SceneRenderInstance) if the script used it; the OSD's `createObject` builds RenderRefs through GameType natives and pushes them into its own `object` vector, and `Osd.show(I)` iterates `object` -- so the HUD's draw surface is the OSD's own object vector rendered via the OSD's viewport (the `vp` field in tree 82). ALL-VIEWPORT MEASUREMENT (post-activation, every node with a viewport): three viewports carry attachments -- 0xffff036c->0xffff0368 (type 3), 0xffff0041->0xffff0364 (type 3), 0xffff0065->0xffff0063 (type 3). ALL are camera render instances; no viewport anywhere carries an OSD render object. So the OSD's RenderRefs are not attached to ANY viewport, active or not. Two possibilities remain: (a) the OSD's objects live only in its script-side `object` vector and are drawn by the OSD's own draw call (a Viewport/Group draw native the port may not dispatch), or (b) the OSD's createObject path failed/never ran (its GameType natives are registered but the call path within Osd.show(I) may not reach them). ANSWERED (live counts): the OSD factories DO run -- createText 744, createRectangle 33, show 16, hide 12 (createObject does not appear; the HUD uses createText/createRectangle paths). So the OSD script creates its text/rectangle render objects. Since NO viewport carries them (all three viewports: camera type-3 only), the objects must be drawn through the OSD's own draw call -- a render.Group/Viewport draw native the port never dispatches (the loadcall filter would show it if it were a script call; more likely the OSD's show(I) adds the objects to a group whose DRAW happens in native code during the viewport render). PORT NATIVE MAP: `java.render.Text` natives exist (create/update/changeColor/changeAlign + setDefaultColor), and the port draws frontend text; there is NO `java.render.Rectangle` or `java.render.Group` native family registered at all, and no generic "render object" native. The OSD's createRectangle (33 calls) and its `object` vector therefore construct java objects whose DRAW never happens host-side. CORRECTED (per review, same trap as before): java.render.Rectangle has NO natives at all -- its methods (ctor, changeTexture, finalize, setPos, setOri) are PURE JAVA: the ctor builds a RenderRef ('ins') from a RectangleTemplate and a root ResourceRef; setPos/setOri call RenderRef.setMatrix (a native the port HAS). So Rectangle needs NO native registration: its objects are RenderRefs manipulated through natives already present. LIKEWISE java.render.Group (check similarly before assuming). What is genuinely missing is the DRAW: the OSD's RenderRefs (instances) must be drawn by the renderer when they are attached to a viewport; the port's only type-3 camera attachments mean the OSD's RenderRefs are never attached -- so the missing step is the OSD's VIEWPORT/attachment binding (Osd.vp) or a generic render-object draw for OSD RenderRefs. Osd(Viewport) CONSTRUCTOR TRACED (tree 64): it calls createNativeInstance() (GameType native 00481a70 -- registered as gameTypeCreateNativeInstance), setFlags(WORLDTREEROOT) (GameRef.setFlags 00486c80 -- registered), then reads vp.getWidth/getHeight/getAspect (Viewport natives -- registered) to set vpWidth/vpHeight/vpAspect. All natives the OSD constructor needs are present. So the OSD construction path is complete native-wise; the question is which Viewport object it receives: the constructor takes a java.render.Viewport argument -- the caller passes it (the Player/Osd creation script). FACT: `new Osd(` does not appear in any script class (grep of all `java.render.Osd` refs: only Input, Hotkey, Frontend, TextBox/StringInput/dialogs use it) -- the OSD instances are created NATIVELY (stock: the HUD OSD is built by the engine's viewport/player setup, the scripts only call methods on the Osd reference they get). So the OSD(vp) constructor runs inside the engine -- the port must create the player's OSD natively with the player viewport. NEXT: find stock's OSD creation call (REA: xrefs to the Osd class's construction / the 0xffff0050 instance's creation path -- the log shows the osd node is created before +14.4 s) and mirror it; then the OSD's RenderRefs (created with setPos/setOri through RenderRef.setMatrix) need to be DRAWN -- the port's renderer must visit RenderRef-attached objects for the OSD viewport. The remaining HUD work is precisely: (1) the OSD's Viewport argument, (2) the renderer's draw of RenderRef objects attached to that viewport. Stock's OSD draws through its own render path (Osd.show's gadget list + the `osd`/activate triple linking the player's RENDER instance id, not the camera viewport), so the next step is that player-render-instance path: the osd command's activate triple is (player.instanceId, player.camera, player.renderInstance) -- the player's render instance is the OSD drawing surface. Instrument: log the player.renderInstance node (type/loaded/attachments) and whether the OSD script's created RenderRefs land on it. The loadcall log shows only the LOADING-SCREEN Osd methods executing (LoadingScreen.show/hide, SimpleLoadingDialog/dialog show/hide) -- the player OSD's own script methods never run because there is no OSD instance ACTIVE: the seat osd link is set, but nothing calls the Osd class's show/activate path (stock's OSD object is driven by the player/viewport update, likely via the `activate` command's osd binding and the game loop's event). NEXT (HUD, concrete): (a) add the render natives the OSD needs (createObject/createRectangle/createText/createIcon factories), (b) find and call the stock OSD activation path (the player update that shows the seat osd), (c) verify the gadgets draw on the player viewport. This is the HUD subsystem's real boundary.

## Free Ride state after the anchor/camera fixes (2026-10-31T11:2xZ)

DRIVE ROOT CAUSE (fixed; mechanism corrected): the port stored a spurious
+0.5 m wheel-local Y offset in `SuspensionUpdate::anchor`
(`{0, friction[0] * 0.5f, 0}`, `friction[0] = 1`). Both the anchor and
`candidate.pointA` are wheel-local (`game_spatial_payload.cpp:417` rotates the
contact point into the wheel frame), so the subtraction was frame-consistent --
the defect was the offset itself: it inflated the hub-to-contact distance to
>= 0.947 m, past `maxLength` 0.705, so `selectSuspensionContact` rejected every
contact and all four wheels stayed at full droop with `force = 0`; no ground
load, no tire force, no motion, and the chassis rested on its own dim-0 type-6
parts. Stock `FUN_0049f4d0` takes `piVar1 = param_2 + (current^1)*0x35 + 0x19`,
i.e. the wheel's predicted STATE position (the hub), so a hub-origin anchor is
stock-correct. Live after the fix: `susp reject` samples show `anchorY=0.000`,
`dist=0.147..0.499` against `max=0.705` (accepted), `len=0.46`,
`force=6066/5964`, tire steps accepted 3442, all four wheels spin
(`w=-48..-50 rad/s`), speed 16 -> 23 m/s, gear 3-5, 325 m travelled; run
`20261031T110949Z-14571` ends `status=ok`.


CHASE CAMERA (fixed, no port change needed): the render camera instance does
follow the car -- `camera world id=0xffff040a t=(-2785.2,126.4,10378.4)` is
exactly 10x the car at `(-278.5,12.6,1033.0)`, which is the same 10x space the
ground `positionScale = 10.0` uses. A duplicate `integrateArmedCameraFollows`
call added to `CarWorldStepper` was WITHDRAWN: `stepRegisteredCars` already
pushes the armed camera callbacks into its multi-root step
(`world_model.cpp:1786`, applied at `physics_multi_root_step.cpp:349`), so the
extra call advanced the camera twice per tick.

COLOR-HOLE FIX (corrected): objects.rpk has 'colors 29' lines that are 270
bytes; the stock 256-byte line buffer truncates them, so tail entries do not
parse. The colors handler FUN_004656d0 (FUN_004657a0 is rendertypes, a
distinction an earlier row got wrong) allocates count*4 without zeroing and
never errors -- it reads the truncated entries uninitialized. The port used to
error and abort frame render (resource 16094, count 29, id 346 -> entry 27); it
now renders the hole with color 0 plus a one-time note, so the frame survives.
The value 0 is a logged difference from stock's indeterminate heap, not parity.

SKY GAP (explicit): the black sky is NOT explained by the skydome mesh -- its
350 triangles are rejected as outside the frustum both before and after any
attempt, and re-centring by mesh extent (which also matched ordinary buildings)
or by pack name changed nothing. Both attempts were reverted; the sky remains a
stated visual gap to diagnose from the camera's far plane rather than by
guessing mesh identity.

VISUALS NOW LIVE (contact sheet `local/runs/layers/cam_follow_sheet.png`): city
pavement/texture, trees, buildings, the car with four wheels, the chase camera,
and the minimap (a third viewport with its own camera). Raster stats: ground
`submitted=23376 outside=19857 clipped=499 rasterized=3709 pixels written`,
objects `pieces=1546 textured=1545`, car `pieces=1 textured=1`, car-part
`pieces=90 textured=78 no_texture_slot=10 texture_absent=2`.

REMAINING RUNG-1 GAPS: (a) HUD text/rectangles -- `java.render.Rectangle` and
`java.render.Group` script objects exist and are manipulated through
`RenderRef.setMatrix` (a native the port has), but nothing attaches or draws
them for the OSD viewport: the three viewports carry only type-3 cameras. The
OSD instances are created natively (no script does `new Osd(`); `Osd.<init>
(Ljava.render.Viewport;)` (tree 64) calls `createNativeInstance`, `setFlags`
and reads `vp.getWidth/getHeight/getAspect`, all natives the port has, so the
missing step is the OSD's own draw pass over its `object`/`groups` vectors.
(b) speedometer/gear/minimap markers -- `Navigator.updateNavigator` still
returns a logged no-op, so the marker matrices are never written.
(c) the frozen awake dynamic scene objects gap (11-12 per run) remains explicit.

## Rung 2 (new career): root cause located (2026-10-31T13:0xZ, corrected)

The main menu renders and NEW CAREER opens the "Enter player name"
`StringRequesterDialog`. It never dismisses because the script blocks inside the
event dispatch: `Dialog.display()` calls `Dialog.wait()`, which parks
`host:handleEvent`.

Evidence that survives review: `threads.txt` shows `host:handleEvent waiting on
java.render.osd.dialog.StringRequesterDialog` with the stack
`Dialog.display() @10/20 -> MainMenuDialog.osdCommand -> Gadget.sendMessage ->
Button.click -> Group.handleEvent`, and the thread is frozen at `slices=337
f1098` in BOTH snapshots (f1516 at 16.8 s and f3162 at 36.7 s) while heartbeats
continue to f3083. `gaps.txt` records a 5.0 s stall, "no notify arrived".

WITHDRAWN sub-claim: "Input.lastKey stops at f1098". `EventKind::nativecall` and
`EventKind::cursor` carry `persist = false` (`run_log.hpp:45-59`) and
`run_log.hpp:454` only logs when `kindInfo(kind).persist` is true, so those rows
exist solely in the bounded `flight-*.log` ring snapshot
(`kDumpEvents = 4096`, `run_log.hpp:87`). A capped ring cannot show that polling
stopped; that claim is retracted in decisions.tsv.

Dispatch-model difference (jev-verified 0.88 / 1.00 / 0.92): stock runs each
event dispatch on its OWN VM thread -- `FUN_00425490` (all.c 32012-32078) builds
the name `s_THRD_RUNVMI`, calls `FUN_0041f340(DAT_0063641c,10,1,name)` to create
a thread, loads the args with `FUN_00425240`, runs the method with `FUN_00408a50`
and finishes with `FUN_0041f4d0`; `FUN_0041f340` (all.c 26236-26290) is the VM
thread constructor. The port dispatches every event on one `host:handleEvent`
task, so a modal script deadlocks the event path.

A `sdl_type` script verb was added to the probe to push real keycodes (the name
field reads `event.keycode`; scancode-only events carry no character).

## Current verification boundary (2026-10-31)

The four unmanifested analysis files were copied with preserved metadata to
`local/census/ref-analysis-recovered/`, compared byte-for-byte, and removed
from `ref/analysis/`. `python3 -I tools/ref_guard.py verify` reports 8,739 files
unchanged. The Free Ride spawn remains unreliable: four of the six latest
seeded runs fell more than 2,500 m; one failed in drivetrain processing and one
passed the car trace check. A successful probe status alone does not establish
that the car remained on the road. The used-car purchase rung has exceeded its
attempt limit without a verified purchase.

The proposed Ninja release/debug presets are in `CMakePresets.json`;
`cmake --preset ninja-release` configured successfully. The Ghidra project
was backed up to `local/ghidra-proj-before-symbols`; the symbol script reported
357 renamed entries and saved the project. Named whole-program decompilation
completed with 7,535 functions and no reported decompiler failures. Both
decompilations have the same function addresses and compare equal after
normalizing function names by address, removing the added alias comments, and
ignoring whitespace. `all.c` remains authoritative.

`clang-format` 23.1.4 is installed. `.clang-format` parses successfully and
preserves existing long lines and include ordering; `.clang-tidy` is advisory,
not a gate. The release preset built `car_engine_test`, and its CTest passed.

`tools/check.sh` passes after refreshing the project-source digest for
`tests/vm_test.cpp` in `docs/native_evidence_artifacts.json`: its `buildNumber`
anchor remains present and `vm_unit` passed. This does not attest stock behavior.
Touching `src/vm.cpp` produced 82 lines in the Ninja dry run; rebuilding with
`-j8` executed 81 steps in 10.56 seconds wall time. A shared sanitizer library
was not introduced: this measurement alone does not justify migrating targets
and risking their instrumentation and fixture dependencies.

The `src/` pointer-cast inventory found character/byte views for serialized
buffers, a JPEG error-manager conversion, and converted audio sample storage;
it did not find a file buffer cast directly to a native struct in those
matches. This is a textual audit of explicit pointer-cast syntax, not proof
against casts hidden behind aliases or helpers.

`tools/tufa_dump.py` now identifies native method records, displays signed
relative targets for branch opcodes `14`–`16`, and offers `--all <dir>` to
report decode failures. A City tree displayed target 32 for node 7's +25
branch, the System listing identified `buildNumber()I` as native, and the
47-file installed `sl/Scripts` subtree decoded without failures.
The full installed tree sweep found 1 decode failure in 1,452 files:
`system/Scripts/render/osd/Dialog.class`, the known stale class also reported
by `phase2_check`.

Car traces now emit `woke_by: "controls"` only for a sleeping primary car
observed waking from control input during that step, and `null` otherwise.
In run `20261031T170004Z-29883`, all 2,063 JSON rows parsed without duplicate
keys and one reported a controls wake, matching the run log. The same run
fell 2,561.2 m; trace verification is not a gameplay pass.

The final `tools/check.sh` passed all 116 CTests and `ref_guard` reported 8,739
files unchanged. Its independent RPK, SCX material and SDAT entry listings
matched the Python readers; `docs/phase2.md` records their SHA-256 digests.
The build still warns that `seatIndex` is passed as `int32_t` to `%zu` in
`src/natives/game_type.cpp`.

The opt-in stock walk detour and its bounded x87 oracle checks are documented
in `tools/wine/capture/README.md`. The live stock menu installed the hook but
did not call the walk. The sampled cached-query comparison had 50 matches,
zero mismatches and six rejected cases; no software x87 emulation was added.
This is not evidence of driving parity. Jev's final subset gate escalated the
hook (`safe_to_apply` 0.03): the in-memory patch is non-atomic and quiescence
at installation is not established. Human or stronger-model review remains
required. The dealer purchase screenshots also remain blocked as directed.

## Update 2026-10-31T20:5xZ (supersedes "Open: spawn settle", "Root-flag status" spawn notes, and the gauge activation note above)

- Spawn settle is fixed by stock ride-height placement (`GameRef.setMatrix` -> `FUN_0044c460`, ported as `carRideHeightPlacement`, `CarPhysics::center`). The 0.021 positional shift stays (stock `FUN_00496050` applies it). The core-overlap reject of `FUN_0049c8f0` is ported; the invented fallback re-query is gone. Harness run `20261031T204116Z-78422`: status=ok, car drives, no err notes. Field mappings in the placement port are `[INFERENCE]` (see decisions.tsv 2026-10-31T20:40:00Z).
- Seat osd: the seat osd record (+0x54, resource at +0x5c) is relinked from the native player's osd link by getInfo case 0x1a (`FUN_0047b220`). The port now does this in car `activate`; the gauge gate is open (osd=0xffff0050).
- Gauge creation is NOT ported and is a logged Note gap (`car activate: cockpit gauge text nodes`), no longer a typed error. Stock facts read this session:
  - `FUN_0048c4b0(ecx=dest slot link; osdLink=seat+0x54, text const ptr, P+0x1b3c layout, P+0x1b58 font link, color, align=2, scale 1.0f)` creates an `r_text` child (node +0xc0 text ptr, +0xc4 align, +0xc8 scale, +0xcc color). `FUN_0048c8c0(color)` sets +0xcc (0xff00ff00 initially). The constant strings are single characters: slots 1,3..8,21 = "A","B","C","D","E","T","L","O" (0x610320..0x61033c), then "d","c","b","a" / "1","2","3","4" / "7","8","5","6" per wheel/array (tables 0x60e458/0x60e468/0x60e478, count min(P+8000,4)). Slot 25 = `FUN_0048c670` (r_text2, 0x100-byte buffer, P+0x1c5c/0x1c78 layout); slot 26 = `FUN_0048a3b0(... "gauge")` + `bone00` (needle).
  - Per-frame update is inside `FUN_004518c0` (all.c 62484-63571): `FUN_0048c850(slot25+...)` text format write, `FUN_0043c0a0(seat+0x54, gaugeRec)` per cfg gauge record (type 1 bone rotation, 2 formatted text via `FUN_00553d70`, 4 on/off), and `FUN_0048c8c0` colors driven by damage/ABS/ASR/handbrake fields.
  - Stock frame (`shots/flow/03-drive-08s.png`) shows: odometer, nitro and turbo bars, speed arc + "027" + "1ST", ABS/ASR/handbrake/warning lights, minimap, damage car.
- Remaining Rung 1 visual gaps: those gauges, car paint (`colorscheme`/`FUN_00475ec0`), night sky/lighting (port frame is daylight), minimap placement note, driver skeleton note.
- Rung 3 (dealer cars invisible) still unresolved after ~35 attempts; Rungs 4-6 not started.

### Sky and lighting lead (2026-10-31T21:05Z)
- `sky raster:` now logs every 2 s with view and model translation. Over the whole run the skydome model translation is (0,0,0) while the camera sits near (-10000,*,*): all 350 triangles are rejected outside. Scene.refresh only picks a config by `time2Config(time)`, `setFog`, `setLight`, `duplicate` of the skydome; no script moves the sky, so stock's renderer must centre it on the camera (mechanism not yet found in all.c).
- `src/scene_raster3d.cpp` never reads `SceneRenderLight`; `RenderRef.setLight` values are stored (resource_ref.cpp:593) but unused, so the port is lit as day while stock Free Ride is dusk/night. Port frame shows Emer Nonus under seed 235; stock Wine frame showed a Baiern CoupeSport (different car selection, not a regression).

## Physics gate impasse checkpoint (2026-11-03)
The full integration gate is RED at exactly one CTest: `object_world_step_unit`
(synthetic 20 kg box must settle + reach `kBodyAsleep` in 900 steps). Everything
else in the static gate is green (other CTests, natives/natives-called census,
PTX census, arch, non-capture parity tests). Six subagent investigations
exhausted the tractable physics leads; see `docs/requirement-map.md`
(REQ-GATE-PHYSICS-SLEEP) and `docs/decisions.tsv` 2026-11-02/03 for the full
record. Summary:
- Corrector x87 groupings, per-triangle geometry, contact-free compound step,
  friction formula: all verified EXACT to Stock `.bak.exe`. One verified fix
  APPLIED: new-contact admission band 0.02 (was 0.05) -> box residual velocity
  6.7x lower (0.467->0.183), passes the speed gate; no ctest regression.
- Residual: box hovers ~1cm and micro-rocks (omegaSq 0.41); friction-coupled
  contact-manifold churn. `nofric` SLEEPS at step 98 -> mechanism works,
  friction is the sole differentiator. The fixture is CHAOTIC (sleeps
  "underground" at SPAWN_X +1e-5; omegaSq 0.023-33 for ±5e-6 m), so the sleep
  outcome is not robust.
- BLOCKED: stock ground-truth for a settle-and-sleep object (synthetic fixture
  the PE cannot load; capture ALLOWANCE EXHAUSTED ~7MB reservation headroom).
  Per the goal, do NOT weaken the settling assertions or force settle.
- Working tree: only `src/car_world_step.cpp` (the 0.02-gate fix) is modified;
  all diagnostic work is in `local/verify/issue53-*/`.
DECISION NEEDED (owner): free/replenish the capture allowance to unblock stock
ground-truth + D1-D8 paired verification + production qualification. Until then
the physics gate and most capture-dependent lanes are externally blocked.
