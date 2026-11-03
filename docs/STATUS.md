# Status

The current verified state and next steps are in [continuation.md](continuation.md). The sections below are historical checkpoints.

## Native verification skill, 2026-10-29

`.agents/skills/verify-slrr/SKILL.md` now provides isolated native launches, a process/build doctor, timed input, preserved screenshots/logs/save hashes, and owned-child cleanup. Menu and hover were visually verified in `local/verify/slrr-skill/20261029T165549Z-c6ps28r1/`; the failed Free Ride run `local/verify/slrr-skill/20261029T165744Z-_2j7n363/` still stops at unregistered GroundRef.addTrafficN. Both cleaned up their disposable saves and retained evidence. The feature map explicitly records driving and career coverage gaps. This tooling does not establish any new stock parity claim or change gameplay.

## Numeric call conversion, 2026-10-29

The project remains **incomplete**. The VM now accepts a finite, in-range float for an integer parameter in ordinary dynamic calls and their script or native methods, truncating toward zero. Stock selects numeric conversions in `00417c30` and converts arguments in `00417da0`; `0041fbc0` applies the conversion to native arguments. Focused tests cover negative fractions and actual native handlers. Constructors, the wider stock overload ranking, native-origin script calls, and out-of-range conversion remain incomplete; this change does not establish full VM parity.

The seed-235 Free Ride run `local/runs/20261029T165126Z-74971` now resolves the call to `GroundRef.addTrafficN(GameRef,IFFF)I` and exits 1 because that native is unregistered. It does not reach driving. The ground factory, road integration, traffic, and the other checklist gaps remain open. See `docs/continuation.md` for the next work.

`tools/check.sh` exited 0 with 98 compiled tests and the remaining checks passing; its log is `local/verify/vm-numeric-check-final2.log`.

## Install unit landed; next gap is GroundRef.addTrafficN, 2026-10-29

The project remains **incomplete**. This section supersedes the takeover section's install items.

- The install rejections are fixed and unit-tested: attachments-vs-attachments compatibility, attached-part registry fill and slot pairing, the getInfo 0x31 descent into installed parts, and the rim/tyre pair. `tests/car_runtime_test.cpp` (new `car_runtime_unit`) covers all four against real stock data with fixture ids derived through `PackRegistry.resolveLiteral` (slots are first-seen registration order). It passes in the normal build and under `-fsanitize=address`.
- AddressSanitizer caught a real corruption in the new code path: `carPartRemove` and console `remove_part` destroyed `attachedParts` `OwnerLink`s still on the part's `LinkOwner` list. Both now detach before erasing (raw `OwnerLink` has no destructor; `EventSubscription` self-detaches, so those dedupe sites are unchanged). `wheelReferencePose` reads the hub body and fails closed past the real wheel count (stock indexes `(slottype-1)%100` unbounded into a `wheelCount*0x2b4` array — out of range is stock UB; census all.c case 0x2f and all.c:58566). `car_physics_test` corrected accordingly.
- Full `tools/check.sh` exits 0 (98 compiled tests, all suites, ref_guard 8,739 unchanged); full `ctest` 98/98; ASan binaries clean. Decisions logged in `docs/decisions.tsv`; details in `docs/continuation.md`.
- Fresh live run `local/runs/20261029T101314Z-80201` (seed 235, same script): all 54 installs `outcome=ok` (the previous run rejected 10 of 54), muffler/door/mirror/tyre chains resolve through the descent. Exits 1 at a NEW frontier: `unsupported dynamic method: java.util.resource.GroundRef.addTrafficN(Ljava.util.resource.GameRef;FFFF)` in `Valocity` enter via cursor `handleEvent`. Evidence: `local/verify/takeover-install-frontier-{play.log,diagnosis.txt}`; verdict FAILED, 14.4 s, 1255 frames.
- Still open from the takeover section: whether stock's family aging lists include runtime type-3 nodes (the flush sweep question) — the fresh run passed the previous `recent-use type unsupported: 3` point, so re-verify that path when traffic support lands. Ground, routing, driving, sound parity and live car teardown remain unproven.

Resume commands:
```sh
SLRR_FORCE_SEED=235 tools/play.sh --script tools/scripts/free-ride-click.txt
tools/diagnose.sh latest
tools/check.sh
python3 -I tools/ref_guard.py verify
```

No commit or push was made.

## Takeover: flush registered, forced-aging gap exposed, 2026-10-29

The project remains **incomplete**. This run took over after entries through 2026-10-29T07:04:49Z.

- Full `tools/check.sh` exits 0 (98 compiled tests, all Python suites, corpus checks, ref_guard 8,739 unchanged). Two changes landed since `integrated-check2.log`: the `tests/stock_run_layout_test.py` flow fixture now stamps its synthetic flow line at now+10 s (the fixed `12:00:00` made the classifier flip hung/unknown by local time of day), and `java.render.GfxEngine.flush` `()V` `0047c2d0` is registered (`src/natives/registry.cpp`): seven `forceRenderFrames` passes plus `ResourceWorld::ageRecentUses(error, 0)` — stock's `00537ed0(1)` is the same aging sweep with forced threshold 0 instead of 8. Stock's `004b7b90` renderer deferred-cache restore has no host equivalent (the bounded renderer creates device resources eagerly); the difference is recorded in `docs/parity-checklist.tsv` row "3D rendering and depth". Fresh census slices: `local/census/DecompileAt-0047c2d0.log`, `local/census/DecompileAt-004b7b90.log`.
- Latest live run `local/runs/20261029T075652Z-53357` (seed 235, `SLRR_FORCE_SEED=235 tools/play.sh --script tools/scripts/free-ride-click.txt`) advances past `native not registered: GfxEngine.flush` and exits 1 at `GfxEngine.flush: recent-use type unsupported: 3` in `Valocity` enter (tree 1 line 52, via cursor `handleEvent`). The forced sweep visits age-1..8 nodes of families the host does not model (type 3 menu Rectangle first).
- Scout evidence (read-only, census slices): the remaining install rejections have one cause — the car's attached-part registry is never filled (car root resolved through `sectorLink`, `src/natives/game_type.cpp:2930-2946`), so the search cannot descend into installed parts (mirror needs door slots 24/30, tyre needs rim slot 2, muffler exhaust slots 2/3). Additional divergences to fix: stock compat rule is attachments-vs-attachments (`FUN_00470910`, all.c:83745-83840) where the native uses attachments-vs-cachedValue; stock search order is own slots then car children via getInfo 0x31 (`FUN_00473b10`, all.c:86135-86300); wheel slots 101..400 need the wheel-record pose override (`car+0x13e4+((slottype-1)%100)*0x2b4`, all.c:68272-68315). A delegate is implementing these in `src/natives/game_type.cpp` / `src/car_runtime.cpp` with focused tests; not yet built or live-verified.
- Whether stock's family aging lists contain script-created runtime type-3 nodes, and what the sweep does per unmodeled family, is being traced from the cached census before the flush gap is fixed.

Resume commands:
```sh
SLRR_FORCE_SEED=235 tools/play.sh --script tools/scripts/free-ride-click.txt
tools/diagnose.sh latest
tools/check.sh
python3 -I tools/ref_guard.py verify
```

No commit or push was made.

## Free Ride observability iteration, 2026-10-29

The port remains incomplete. This iteration changed diagnostics only. The latest native Free Ride evidence is `local/runs/20261029T065823Z-50178/`, seed 235. It exits 1 at unregistered `GfxEngine.flush` after 54 install attempts. `tools/diagnose.sh` reports versioned assembly records. Attempt 19 for the left mirror examines two targets, reports no matching available slot, and records that both part and target had physics objects. This narrows the rejection but does not prove whether the target search includes all attached parts. The full path, stock parity and native driving remain unproven.

`src/natives/game_type.cpp` records compact per-attempt request, up to eight individual candidate results, an aggregate over all examined targets, install-parent link and physics presence before and after, and result with resolved target and slots. `tools/diagnose.py` displays these records and marks malformed, missing, or interrupted transitions uncertain. `docs/run-log.md` documents the format and limitations. Existing build, seed, input, save and data identities remain in the run folder. Old records cannot recover omitted candidate causes or truncated paths.

A forced-stop experiment in `local/verify/obs-kill-runs/20261029T065830Z-50315/` retained six completed attempts and an incomplete seventh. Diagnosis reports the final attempt and exit cause as unknown. This tests SIGKILL during assembly, not power loss or all signal timings. `tests/diagnose_merge_test.py` passes 14 tests, including a synthetic missing-prototype alternative and truncated records. The live run and diagnostic outputs are `local/verify/takeover-20261028/observability-final-play.log` and `observability-final-diagnosis.txt`; the forced-stop report is `observability-kill-diagnosis.txt` in the same folder.

`tools/check.sh` passed all 98 compiled tests and the recorder crash tests, then failed `tests/stock_run_layout_test.py::test_input_delivery_counts_and_finished_flow_are_not_success`: its fixed clock fixture was classified as hung at the current clock time. The exact output is `local/verify/takeover-20261028/observability-full-check.log`. The last full passing check before this iteration is `integrated-check2.log`; later physical and diagnostic changes are not covered by that pass. The focused diagnosis suite passed after the final parser edits. Reference guard passed with 8,739 files unchanged. No commit or push was made.

Next observability iteration: record the car's attached-part inventory and identity of each candidate plus slot-level compatibility, then test whether the intended door or rim was omitted from the search. Record logical and physical body parents and transforms separately, and add explicit ordering and durable-write loss accounting if stronger interruption guarantees are needed. Historical records cannot recreate data that was never recorded. Resume the port only when requested; read the historical physical-assembly notes below before changing behavior.

## Verified takeover milestone, 2026-10-28

The project remains **incomplete**. `tools/check.sh` exited 0 with all **98 compiled tests**, Python suites, data corpus checks and reference guard passing. The exact log is `local/verify/takeover-20261028/integrated-check2.log`. The first integration attempt passed the compiled tests but a startup fixture still expected six counted instances; the new mounted-resource accounting correctly reports seven, and the fixture was updated before the passing run.

The latest completed native Free Ride run is `local/runs/20261029T035453Z-95980/`, launched with seed 235. It stops in recursive `Vehicle.<init>` assembly at `GameRef.queueEvent: install: wheel slot body is unavailable: slot=121 type=201`. The preceding click-only battery failure was fixed. Diagnosis is saved in `local/verify/takeover-20261028/latest-diagnosis.txt`. There is no native drive.

Ordinary part attachment now has geometry alignment, compound indexing, mass recomputation and lifetime tests. Three isolated position/orientation fixtures match the original stock math helpers at x87 precision 0x007f (`local/verify/part-alignment-oracle.il3lcf33/`). These do **not** verify the entire install path. Further stock tracing found that ordinary slots normally reference the configured root geometry body rather than the part compound, and mass recomputation must continue up its physical parents. Those corrections and the wheel-slot reference transform are now the active work. The existing gameplay path is partial and must not be described as stock matched.

The ground material parser now reads the real city file's 108 records and has a compiled test. It is not connected to a live ground factory. Nonempty city loading, ground scene/road registration, active driving physics, full rendering, controls, pause and the remaining parity flows are unfinished.

The last verified full-check source is the checkpoint above. A later ground-resource enum rename and two comment deletions have no behavior change; further physical attachment edits require another build, live run and full check. No commit or push was made.

## Active full-parity takeover, 2026-10-28

The project is **incomplete**. Fresh `tools/check.sh` passed all 97 compiled tests but exited 1 at the saved VM test source checksum in `docs/native_evidence_artifacts.json`. After checking that the buildNumber dispatch assertions still exist, the checksum was refreshed and the evidence audit passed. A full check after the current changes is still required. Baseline output is `local/verify/takeover-20261028/baseline-check.log`.

Fresh Free Ride initially reached `Valocity` and failed creating the Navigator material (`local/runs/20261029T033715Z-89105/`). The install event parser was incorrectly using fields one and two as target and slot; stock uses fields four and five. After correcting that, seed 235 reached an empty `Part.install` callback. Host callback dispatch now accepts empty void bodies, matching stock and existing script-call behavior. Focused install and VM tests pass in `local/verify/takeover-20261028/assembly-tests.log`.

Render instance creation now defers material-shape validation, and the bounded renderer supports the actual single-quad Navigator tile before transparent rectangles. The real map-tile pixel and texture-replacement tests pass in `local/verify/takeover-20261028/render-tests.log`. This SDL path has no depth buffer and does not establish stock rendering parity.

The latest completed run, `local/runs/20261029T034515Z-93006/`, passes `Vehicle.<init>` and `Valocity.<init>` then fails while leaving the menu at unregistered `RenderRef.getTypeID()I`. The preceding failure at `MouseCursor.disable` was fixed with stock enable/disable/standby flag transitions and a counted system cursor visibility bridge; its focused test passed. The getTypeID implementation and test are currently being verified. The recorded command is `SLRR_FORCE_SEED=235 tools/play.sh --script tools/scripts/free-ride-click.txt`.

Logical stock part installs now occur, but physics-body attachment, complete compound indexing and active simulation remain incomplete. A physical attachment experiment is under verification and has no gameplay hooks. The city type-1 loader still refuses its nonempty payload. Stock invokes the native ground constructor during that same load, and the required resource counting, linked bounds, dependencies and ground scene/physics effects are unfinished. No native drive is claimed.

A fresh stock Wine capture completed the scripted Free Ride and Escape flow in `local/wine-runs/takeover-20261028-stock/`. Its pictures show the city and in-game menu. It was stopped cleanly. This uses synthetic CD metadata without CD audio, and stock seed and simulation steps are not aligned with native. The 62-row parity checklist still has no completed flow. The stock registry has 360 entries, but a fresh compiled runtime inventory contains only 240 handlers and lacks 120 stock signatures. The historical statement that all 360 were implemented confused the stock census with the native implementation. Only six methods are marked matched; handler presence is not a stock-behavior verdict. No commit or push was made.

## Diagnostics takeover, 2026-10-28

The requested diagnostics work is **not complete**. `SLRR_FORCE_SEED` selects a validated 32-bit seed, and replay uses a single seed recorded in `run.log`. Native calls now record bounded arguments and results, part requests and install/remove outcomes persist, repeated random calls no longer fill the ring, and failure stacks show tree, instruction, and source positions without the duplicated callback display. The diagnosis prints the persisted assembly events. World snapshots currently report node and active script-thread counts only. They do not yet report active car identity, position, velocity, RPM, gear, or wheel contacts. Checkpoints and abnormal exits need complete world-state coverage.

A real Free Ride run (`local/runs/20261029T022225Z-63414/`) and replay (`local/runs/20261029T022249Z-63524/`) selected the same Baiern DevilSport and the same ordered 78 part requests using seed 235. Both stopped at unsupported string addition in a brake script. The recorded install attempts were rejected. The two runs used different executable builds, so this is a bounded observation, not proof of general determinism. Replay warns when an older run contains no single seed. It does not reproduce a sequence of different seeds. Some long event text and config paths remain truncated, and full error context and diagnostic tests remain to be completed.

`tools/check.sh` exited 0 (`local/verify/diagnostic-takeover-check2.log`), and `python3 -I tools/ref_guard.py verify` reported 8,739 files unchanged. No commit or push was made.

## Car/Part Runtime & Native Registry Handoff, 2026-10-28

The native port is **incomplete**. Full `tools/check.sh` passed (`check: ok`, 97 of 97 CTest unit tests, all Python diagnostic/replay/parity suites, and `python3 -I tools/ref_guard.py verify` with 8,739 files unchanged). All 360 native methods in `docs/natives.tsv` are now registered (`registered natives: 360` vs. 237 previously), including the 123 `ResourceRef`, `GameRef`, `GameType`, `Part`, `DynoData`, `Chassis`, `WheelRef`, `SfxTable`, and `Vehicle` handlers in `src/natives/resource_ref.cpp`, `src/natives/game_type.cpp`, and `src/natives/car_part.cpp`.

In live Free Ride (`tools/play.sh --script tools/scripts/free-ride-click.txt`), clicking Free Ride now:
1. Creates the car `WorldNode` and `CarRuntimeInstance` in `GameRef.create` (`src/game_creation.cpp`).
2. Resolves the car script class (`java.game.cars.Baiern_DevilSport`) and its sibling superclass (`java.game.cars.Baiern_models`) from the pack script directory via `ClassEnvironment` (`src/class_environment.cpp`).
3. Completes `Baiern_DevilSport.<init>`, `Baiern_models.<init>`, `Chassis.<init>`, and `Part.<init>`, and enters `Vehicle.<init>` -> `Baiern_DevilSport.addStockParts(Descriptor)` -> `Chassis.addStockParts(Descriptor)` -> `Part.addPart(int, String, Descriptor)`.
4. In run `local/runs/20261029T015343Z-59083/`, the first `Part.addPart` call (`0x000c0052` in `parts\engines\Baiern_Emer.rpk`) failed at `GameRef.create: pack file not found` because `partDefaults` in `src/car_runtime.cpp` contained a non-existent `"part.rpk"` entry instead of matching stock `0046e320` + `0046e570` (`decals.rpk` `0x0b` and `sound.rpk` `0x0b`). That `"part.rpk"` entry has now been removed and verified with `tools/check.sh`, and the next live `tools/play.sh --script tools/scripts/free-ride-click.txt` run is ready to execute.

### Recommended Next Steps
1. **Run live Free Ride after the `partDefaults` fix:**
   ```sh
   tools/play.sh --script tools/scripts/free-ride-click.txt
   tools/diagnose.sh latest
   ```
   Verify that `Part.addPart` creates and attaches recursive stock parts (`install 0 <carId> 0 <partId> 0`) through `Vehicle.<init>` and advances into `MainMenuDialog` -> `GameLogic.changeActiveSection(new Valocity())`.
2. **Track / Ground / Scene Loading (`Valocity` & `Track.enter()`):**
   - Implement nonempty type-1 pack resource loading (`0053dee0`) for `maps/city.rpk` (`0x00010035`) in `src/resource_world.cpp`.
   - Implement the `native ground` factory (`00463220`) in `src/game_creation.cpp`.
   - Implement `GroundRef`, `Navigator.updateNavigator` (`00482d30`), `RenderRef`, `Camera`, `Viewport.unproject`, `SfxRef`, `Sound`, `ParticleSystem`, and `Animation` natives required by `Track.enter()`.
3. **Active Driving Loop, Physics, Collisions, Pause, & Stock Parity:**
   - Connect active per-frame car/wheel physics stepping (`004a5190`, `00491100`), keyboard driving controls (`0044e690`), collisions, `Escape` -> `IngameMenu` pause, and exit.
   - Compare against the stock executable under Wine (`ref/raw/StreetLegal_Redline.bak.exe`, MD5 `e686099e7233bb7d7c461b1b35c5064c`) and resolve each divergence in order.

## Diagnostics follow-up and current handoff, 2026-10-28

The native port is **incomplete**. The latest actual Free Ride run, `local/verify/diagnostics-live-runs/20261029T001947Z-46753/`, exited 1 at `GameRef.create: car wheel joints, scene objects and world registration are not implemented`. It recorded the seed and requested Ishima car. The scripted replay in `local/verify/replay-live-runs/20261029T002145Z-47172/` used an isolated copy of the recorded initial save and reached the same reported failure. Timing and random state were not controlled, so this is not deterministic parity.

The diagnostics follow-up added private run-local starting-save and launch-source manifests, process-start identity, seed and car-request logging, two rotating recent-event checkpoints, and screenshots in individual run folders. `tools/diagnose.sh` merges preserved events, reports traces and process identity when available, and checks manifest integrity. Replay verifies and isolates saves; older runs without a snapshot use a warned copy of the current save. Stock capture logs now record proxy clock origins and input queue/delivery observations. Older captures without those origins remain unaligned, and input delivery does not prove a game response. Stock packaging no longer runs Wine for a version string or overwrites earlier reports. Original-code oracle runs now use separate output folders and bounded waits.

Prelaunch game-data manifests are produced by `tools/play.sh`, the native parity scenario runner and the stock capture launcher. They describe a bounded prelaunch tree, not the files actually read or later mutations; bare `startup_probe` launches report game-data identity unavailable. Native evidence audit checks saved artifact hashes and anchors, but the whole-program decompile's stock origin remains unverified. No diagnostic tool can recover a historical seed, save, complete event stream or state that was never recorded. The snapshots are not atomic, the recent-event checkpoints are bounded and depend on frame heartbeats, replay is time-based, and logging can affect timing.

The last full check, `local/verify/diagnostics-final-check.log`, exited 0 with 97 compiled tests, and included the screenshot tests and evidence audit. **Later launcher, game-data manifest and reporting edits were made afterward.** The current focused recorder test and diagnostic, replay, stock-packaging and data-manifest tests passed. The last launcher/data-manifest change has not been exercised in a fresh real `tools/play.sh` run, and a full `tools/check.sh` rerun is still required. Earlier Wine helper processes became uninterruptible; their recorded PIDs are no longer present at this inspection, but native Wine recovery and a new live stock capture have not been verified. `python3 -I tools/ref_guard.py verify` passed with 8,739 files unchanged. No commit or push was made.

To resume diagnostics, run `tools/check.sh`, then run `tools/play.sh --script tools/scripts/free-ride-click.txt` and inspect the resulting folder with `tools/diagnose.sh`. Verify the data-manifest identity and replay with `tools/replay.sh <run-folder>`. Continue actual car ownership, joint and scene registration, then ground loading and the remaining stock-parity work documented in `docs/continuation.md` and `docs/parity-checklist.tsv`.


## Historical full-parity checkpoint, 2026-10-28

The following section records an earlier checkpoint; the current handoff above supersedes its running and pending statements.

The project remains incomplete and native driving has not been reached. The last
full integration check passed 93 compiled tests and the script/data checks. Its
log is `local/verify/native-parity-20261028/physics-integration-full-check.log`.
A newer check after compound finalization changes is running.

The actual Free Ride creation path builds linked resource prototypes, collision
meshes, a live detached part compound, a separate car root, suspension cylinders,
hubs, arms and optional wheel bones. It still fails explicitly before returning
a successful game instance because joints, scene objects and world registration
are incomplete. The latest application evidence is `free-ride-car-root.stderr`
in that verification directory; another run after the latest changes is pending.

`car_factory_census --require-physics` builds detached physics for all 39 complete
car configurations. It still exits1. Three Baiern Turbo configuration references
are missing, and the base `parts/scripts/Chassis.cfg` has no chassis geometry and
cannot use this bounded physical-instance constructor. Prototype records exist
for all40 available configs. This does not establish stock instance parity.

Original-code comparisons using native macOS Wine at observed precision007f
match 309 real car body mass cases plus2 synthetic cases,11 orientation cases,
78 Euler cases,4 compound mass cases,5 normal-contact cases,4 topology cases,
10 suspension cases,10 centered direct-contact/friction cases, and265 committed
state words across compound stepping cases. The point/segment helper also matches
its direct cases; reversed output is reconstructed from the dispatcher rather
than executing that dispatcher. These are bounded comparisons, not full-game
state or rendering parity. Other Wine/Windows precision and physics worker
behavior remain unresolved.

`docs/parity-checklist.tsv` tracks the remaining whole-game work.
`docs/continuation.md` records the active run. Sections below describe historical
checkpoints and retain their original observations.

The sections below are historical checkpoints and are superseded by the current handoff above.

## Recovery check (2026-10-28)

The interrupted diagnostics work is still being integrated. A clean `tools/check.sh` passed, and a later check passed with the stock diagnostic tests included. Evidence is in `local/verify/codex-recovery/`. A scripted SDL key event was recorded and replayed to reproduce an injected failure. Physical keyboard and mouse recording has not been independently tested. An existing stock Wine capture can be packaged and diagnosed, but no new Wine run was made in this check.

A new scripted Free Ride click still exits with `unsupported script native device 'car'`. Car creation, the complete physics system, scene rendering in the game loop, and native driving remain unfinished. The car configuration reader alone does not create a car.

## In one paragraph

The native port opens a window, runs the real 2005 game scripts, and shows the real Main Menu. Hovering and clicking work (through a scripted mouse). Clicking Free Ride shows the real "PLEASE WAIT / LOADING..." screen, then the run stops: the game scripts ask for a car (`native car ...cfg`), and the port has no car factory, no physics engine and no track scene yet. **The port is not playable.** Nobody has driven a car in it. The unchanged stock game, by contrast, has now been seen running under Wine on this Mac, including a Free Ride scene with a car on a street (see "Stock under Wine").

## What works, with proof

| Claim | How to check | Proof |
|---|---|---|
| `tools/check.sh` passes from a clean build | `rm -rf build && tools/check.sh` | `check: ok`, 76 of 76 compiled tests, reference guard "8739 files unchanged", `sl_port` arm64 (`local/verify/verify-final-1/native-build/clean-check.stdout`) |
| One command opens the port at the Main Menu | `tools/play.sh` | `.scratch/shots/PROOF-native-main-menu.png` (macOS window capture of the real window) |
| Free Ride click reaches the game | `tools/play.sh --script tools/scripts/free-ride-click.txt` | `.scratch/shots/fr-hover.png`, `.scratch/shots/fr-loading-screen.png` |
| Rapid hovering no longer ends the run with the sound error | 30 s hover script, exit 0 | see `docs/decisions.tsv`, audio row |

## What stops the port today

After the Free Ride click the probe exits with code 1:

```
GameRef.create: unsupported script native device 'car' (script resource ..., payload "script cars\racers\...class\r\nnative car cars\racers\..._data\scripts\_main_....cfg\r\n")
```

The car differs from run to run. Pieces built and verified in this run, none of them wired into the game loop yet:

| Piece | State | Where |
|---|---|---|
| Car config reader | All 107 directive handlers read from the stock decompile; 39 of 39 real 2005 car cfg files parse. Static reading, not compared with running stock. | `src/car_config.*`, `docs/car-config-evidence.md`, test `car_config_unit` |
| Physics core | Shape prototypes and mass properties, the stock RK4 integrator, one collision primitive. The solver and most collision (about 12,000 to 16,000 more decompile lines) are not ported. Static reading only. | `src/physics_body.*`, `docs/physics-evidence.md`, test `physics_body_unit` |
| 3D rasterizer | Depth buffer, perspective-correct textures, clipping, alpha test and blend. Draws a real car chassis in `model_view`. D3D9 rasterization rules not matched; body panels untextured where texture names do not match. | `src/raster3d.*`, `tools/model_view.cpp`, `docs/render3d-evidence.md`, test `raster3d_test` |
| Run log and crash dump | Done, see below. | `src/run_log.*`, `tools/diagnose.sh`, `docs/run-log.md` |

Still missing for a drive: wiring the `car` device into `GameRef.create` (plan in `docs/car-config-evidence.md`), the car factory and its 63 natives (`00442500`, `00446180`), the physics solver and car dynamics, the track scene, and drawing the 3D scene in the probe loop (plan in `docs/render3d-evidence.md`). Estimate in `docs/car-factory-evidence.md`: 15,000 to 25,000 lines of C++ plus tests.

## Run log and diagnose (new)

Every `startup_probe` run now writes `local/runs/<UTC time>-<pid>/` with `run.log`, `meta.txt`, the script used, `result.txt`, and `crash.txt` on a signal. `tools/diagnose.sh [run-dir|latest|list]` prints the verdict, cause, last events and counters without re-running. Verified by the lead on deliberate failures: `fail error` (exit 1, "injected failure") and `fail segv` (SIGSEGV, fault address 0x0 with stack); the worker also ran abort, throw, SIGTERM, SIGKILL and the real Free Ride failure. `--script` accepts `fail error|segv|abort|throw`. Measured frame-rate cost: none (about 100 fps with and without). Limits: `kill -9` loses the in-memory ring; the "last events" list is dominated by routine native calls; run folders can contain local paths. See `docs/run-log.md`.

## Stock under Wine

Full write-up: `docs/stock-flow.md`. Commands: `tools/wine/capture/README.md`. Older attempts: `docs/wine-comparison.md`.

**Verified by the lead this run.** The unchanged stock executable (MD5 `e686099e7233bb7d7c461b1b35c5064c`, checked in `local/wine-runs/capture-20261028-133131/exe-md5.txt` and by `md5` on the run copy) runs under native Wine 11 on this Mac, and the screenshots below are the game's real pixels. The earlier black captures were the screen-capture path, not the game. A proxy `d3d9.dll` saves the game's own back buffer each frame (479,219 of 480,000 pixels non-black), and a proxy `dinput8.dll` injects clicks and keys because the game ignores ordinary Windows input.

| Step | Stock proof |
|---|---|
| Main Menu | `.scratch/shots/stock-flow-1-main-menu-backbuffer.png` |
| Free Ride loading | `.scratch/shots/stock-flow-3-loading-backbuffer.png` |
| Car on track | `.scratch/shots/stock-flow-4-track-first-frame-backbuffer.png` |
| Driving with the keyboard | `.scratch/shots/stock-flow-5-driving-backbuffer.png` (speed 041, 2nd gear) |
| Escape menu | `.scratch/shots/stock-flow-6-ingame-menu-macos-window.png` |

The worker reports about 65 seconds of keyboard driving (car reached speed 016 in 3rd gear in its log, hit a fence), and the game kept presenting frames to 263 seconds. The lead viewed the images and the run's logs; the lead did not watch the run live.

**Limits.** The CD table is synthetic (track 11 length and total are invented; no CD audio). Intro videos do not play (Wine DirectShow). Audio output is untested. 2 of the 6 launches died or hung right after the first frame (`capture-20261028-131724`, `capture-20261028-132857`); the cause is not investigated. The game window sits 33 px below the Wine desktop top, so scripted clicks need a +33 offset.

**Differences from native so far** (native side from the lead's own runs, stock side from the worker's):
- Window: native 640x480 "Startup diagnostic"; stock 800x600.
- Main Menu background: stock is a smooth animated dark-blue 3D scene; native is a coarse, blocky car photo. Layout, text and highlight match.
- Free Ride loading screen: the same in both.
- After loading: stock shows the 3D scene, HUD and keyboard driving, plus an Escape menu; native stops with `unsupported script native device 'car'`.
- Not compared: splash screen, audio, frame timing.

## How to run

```
tools/check.sh                                  # builds and tests everything
tools/play.sh                                   # opens the window; use the mouse
tools/play.sh --script tools/scripts/free-ride-click.txt   # scripted Free Ride click
```

`--script` files hold lines like `12 move 420 127`, `13 down`, `13.5 up`, `16 shot file.bmp`, `40 quit` (seconds since the loop started; window points). Shots are BMP; convert with `sips -s format png a.bmp --out a.png`.

Every probe run leaves a run directory in `local/runs/`. `tools/diagnose.sh` explains the newest one (cause, last events, counters); see `docs/run-log.md`.

## Open items

- Class files of compiler versions `0x24f5c` and `0x24f9d` are not on this machine.
- The end-of-run review of the decision trail by a model from another family has not been done.
- The music and sound code is tested with dummy devices; audible output has not been compared with stock.
- Stock priority ordering and stealing of sound voices is not modeled (a request that does not fit is dropped).
