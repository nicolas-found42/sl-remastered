# Full Port 1.0.0

Status: agreed product specification; implementation and release qualification incomplete.

Tracking issue: [#53](https://github.com/nicolas-found42/sl-remastered/issues/53).

## Problem Statement

Players on Apple Silicon cannot download and install this project as a complete, standalone Street Legal Racing: Redline game. The current Port demonstrates bounded functionality but still has engine, gameplay, fidelity, verification and distribution gaps. Passing module tests, booting, or driving does not establish a complete game.

The two official versions in the Reference Folder contain differing behavior and content. Treating them as interchangeable loses requirements; delivering separate versions or requiring players to supply an original installation does not satisfy the requested product.

## Solution

Deliver one native Apple Silicon Port, with one reconciled behavior set and all required game files bundled in a signed, notarized application. A player downloads a DMG from GitHub Releases, drags the application to Applications, launches it and plays. No original game installation, resource import, version profile, developer checkout, probe, interpreter installation or Terminal command is required.

Use both official versions in the Reference Folder as validation authorities. Inventory shared features and all differences. Prefer the newer official behavior where they differ, preserve older-only features, and explicitly resolve incompatible combinations. Record the chosen behavior and its evidence instead of claiming simultaneous equality to contradictory versions. Reconstruction repositories provide research leads, not automatic behavioral authority.

The first public release is **1.0.0**, gated on full completion of the reconciled specification, all eight parity-workflow deliverables, independent evidence review and release qualification. There are no public prereleases or partial development releases. Changes after full stock parity belong to subsequent versions.

## User Stories

1. As a player, I want one complete game, so that I do not have to choose between stock-version profiles.
2. As a player, I want all required game files included, so that I can play without owning or locating another installation.
3. As a player, I want a GitHub release with a DMG, so that I can download the finished game directly.
4. As a player, I want drag-to-Applications installation, so that setup works like an ordinary Mac application.
5. As a player, I want a signed and notarized application, so that installation requires no security-bypass procedure.
6. As a player, I want native Apple Silicon execution and bundled dependencies, so that developer tools are unnecessary.
7. As a player, I want normal intro movies, menus and dialogs, so that the complete startup experience is preserved.
8. As a player, I want working settings and input configuration, so that I can use the supported stock controls and options.
9. As a player, I want keyboard, mouse and applicable controller behavior, so that supported gameplay actions work consistently.
10. As a player, I want to create, select, save and load a Career, so that progress survives application restarts.
11. As a player, I want compatible original save behavior, so that supported stock saves retain their meaning when imported optionally.
12. As a player, I want safe handling of invalid or incompatible saves, so that existing progress is preserved.
13. As a player, I want the Garage and Car Lot, so that I can manage my owned Cars.
14. As a player, I want the Used Car Dealer and all other official purchasing flows, so that I can inspect and acquire Cars.
15. As a player, I want stock purchasing, selling and money rules, so that the economy behaves correctly.
16. As a player, I want to install, remove, replace and repair Parts, so that assembly rules and dependencies work.
17. As a player, I want tuning and applicable diagnostic or dyno facilities, so that changes affect my Car as specified.
18. As a player, I want Paint and stock appearance choices, so that Cars render with their intended appearance.
19. As a player, I want correct handling, suspension, drivetrain, braking and tires, so that driving matches the chosen stock behavior.
20. As a player, I want correct collisions, damage and deformation, so that impacts have the expected physical and visual effects.
21. As a player, I want Free Ride and the complete City, so that ordinary exploration works.
22. As a player, I want all official maps, Cars, Parts and reachable content, so that content is not silently omitted.
23. As a player, I want traffic, pedestrians and applicable AI behavior, so that the world responds correctly.
24. As a player, I want the day/night cycle and lighting, so that time-dependent gameplay and presentation work.
25. As a player, I want Night Drag Races and every other official race mode, so that the full racing game is available.
26. As a player, I want betting, pink slips, prestige, clubs and Race of Champions progression where present, so that Career can be completed.
27. As a player, I want police interactions and their consequences, so that the corresponding stock rules are preserved.
28. As a player, I want accurate HUD, gauges, cameras, minimap and status indicators, so that I can understand and control the Car.
29. As a player, I want correct geometry, materials, textures, animation and effects, so that the game looks complete.
30. As a player, I want normal music, engine sounds and other file audio, so that the soundscape and its timing work.
31. As a player, I want dialogs to support acceptance, cancellation and invalid input, so that routine interaction does not strand me.
32. As a player, I want repeated scene changes and long play sessions to remain stable, so that progress does not depend on a short demonstration.
33. As a player, I want ordinary exit to save settings and clean up correctly, so that the next launch works.
34. As a player, I want saves and settings outside the installed application, so that replacing the app does not erase progress.
35. As a player, I want features unique to either official reference retained, so that reconciliation does not reduce the game.
36. As a player, I want 1.0.0 to mean the full Port is complete, so that I am not receiving a prototype under a final version number.
37. As a maintainer, I want an identity-bound inventory of official content and version differences, so that every inclusion and behavior choice is reviewable.
38. As a maintainer, I want original Tufa Script and Native interface semantics covered, so that gameplay is not replaced by narrow scripted demonstrations.
39. As a maintainer, I want small behavior contracts connected into Journeys, so that a passing example cannot hide missing branches or variants.
40. As a maintainer, I want Stock and Port to consume declared equivalent stimuli, so that comparisons diagnose real differences.
41. As a maintainer, I want independent same-side repeatability before cross-comparison, so that nondeterminism is not mistaken for Parity.
42. As a maintainer, I want first-divergence evidence and failure reproduction, so that defects can be repaired causally.
43. As a maintainer, I want corrupt and incomplete evidence rejected, so that a faulty recorder cannot manufacture completion.
44. As the host user, I want automated captures to preserve my foreground, input, audio and responsiveness, so that development does not interrupt my work.
45. As a maintainer, I want bounded concurrent execution and retained failures, so that experiments preserve the machine and prior evidence.
46. As a maintainer, I want release artifacts tied to the verified source and content, so that the downloaded game is the game that passed qualification.

## Implementation Decisions

### Product and authority

- Build one native macOS ARM64 application with a single reconciled content set and behavior specification. Stock versions are test references, not selectable products.
- Identify every official reference distribution by executable, scripts, Packs, Resources and configuration hashes. The currently identified families are 2.2.1/Build 606 and 2.3.1/Build 940; verify identities rather than relying on historical logs. Distinguish official files, modifications, backups and research reconstructions. Resolve unknown provenance before counting content as official or excluded.
- Maintain a difference inventory with stable requirement identifiers, both reference identities, observed behavior, affected content/interfaces/saves, selected Port behavior, rationale, dependencies, evidence and status. Prefer newer behavior; retain older-only features. Escalate genuinely incompatible combinations for an explicit resolution instead of silently dropping them.
- Cover all original features and Resources in those official distributions, including official additions present there. Community modifications, downloaded community content, Workshop integration and legacy authoring tools are not automatically included merely because they are colocated with references.
- Preserve stock Script VM, Tufa, Native interfaces, resource resolution and persistence semantics. Avoid replacing game logic with behavior that only satisfies a demonstration. Any necessary content reconciliation must retain provenance and be covered by the selected behavior contracts.
- Keep consistent stock quirks unless reconciliation or a separately approved deviation specifies otherwise. Do not import a reconstruction's approximations as verified stock behavior.
- The sole existing fidelity exception is authentic CD metadata/audio: the user declined obtaining the original disc package. Retain the synthetic-CD disclosure; normal intro/video and file-audio decode, render, mix and timing remain required.

### Required eight deliverables

| ID | Deliverable | Required completion evidence |
|---|---|---|
| D1 | Versioned proof records and strict evaluator | Identity/provenance, stimulus, observations, same-side repeatability, paired comparison, first divergence and explicit coverage; meaningful mutations rejected; real paired captures processed by the evaluator. |
| D2 | Content manifest and proof graph | Every official content item and behavior accounted for across both references; difference inventory reconciled; semantic branches and variants bound to Stock expectations and Port evidence; unknowns remain visible until resolved. |
| D3 | Native invisible complete pipeline | Normal intro/video, coherent scripted device state, real mixed nonempty PCM retained privately, frames, ordinary saves and exit; independent foreground and source-preservation observations. |
| D4 | Stock isolated complete pipeline | Private display/audio/input, game/save copies and Wine prefix; normal intro/menu/dialog/video/audio, save/load, ordinary exit and teardown verified with artifacts. |
| D5 | Deterministic controls | Observe and replay actual Stock time/input/RNG/scheduler boundaries, including clocks, reseeding, streams and workers; qualify corresponding Port consumption; independent repeats on each side precede paired comparison. |
| D6 | Shared bounded execution scheduler | CPU/memory/renderer budgets across preparation/builds/tests/probes/Stock; unique writable state, deadlines, process-tree cleanup, locks and one build writer; representative throughput/responsiveness and failure containment verified. |
| D7 | Developer entry point | Working targeted-feedback, corpus, paired-proof, coverage, reproduction, performance and complete integration lanes, backed by actual artifacts. |
| D8 | Actual-evidence validation | Corruptions and concurrency/isolation failures rejected; no-focus and silent-but-nonempty PCM proofs; actual Stock/Port boot/menu/dialog proof; full integration gate; independent review and Jev completion review of all eight deliverables. |

These deliverables enable verification; completing them alone does not finish the game. Full gameplay, content, presentation, stability and release acceptance also remain mandatory.

### Execution sequence and exit conditions

1. **Reconcile the starting state.** Preserve the dirty shared checkout, prior captures and failed attempts. Regenerate the bound evidence index, inspect current sources and identify superseded findings. Exit when each retained claim has its actual scope, evidence identity and current status; historical goal text does not resume or complete a goal.
2. **Establish the full requirement inventory.** Inspect all reference collections, using format-aware analysis for binaries, Packs, meshes, audio and saves. Read updated reconstruction sources as research. Inventory official content, semantic branches, success/refusal/cancellation/repetition/interruption/cleanup/persistence variants and cross-version differences. Exit when every item is classified, assigned to a contract or justified as non-product material, and incompatible behavior choices are resolved. Counting filenames is not semantic coverage.
3. **Qualify capture and replay.** Reuse the existing adapters, evaluator and local qualification evidence. Finish actual Stock interception, ABI/lifetime/thread/synchronization handling, durable collection, ordinary recorder shutdown and matching Port consumption. Preserve incomplete and overflow outcomes. Exit when each side independently repeats declared observations under its recorded stimuli and the first real normal-boot/Main Menu/dialog-open/cancel pair passes the strict evaluator. Reproduce the observed Stock schedule instead of inventing a fixed rate.
4. **Complete the engine and Journeys.** Use the contract dependency graph to repair VM/native/resource ownership, callbacks/timers/threads, rendering/audio, physics/contact/cache/sleep, vehicle/damage, UI and persistence. Progress through installation/startup, Career creation/load, dealer purchase, Garage/Parts/tuning, driving/racing/progression and save/reload, then every remaining content family and variant. Exit only when all reconciled contracts have passing evidence. Preserve test assertions and diagnose the first causal divergence.
5. **Qualify production operation.** Complete containment and pressure behavior, realistic capacity/performance measurements and sustained gameplay. Establish explicit hardware/OS/workload/duration/performance acceptance criteria before declaring success; no measured values are assumed by this spec. Exit when complete Journeys and lifecycle stress meet those recorded criteria with no unresolved required failures.
6. **Build the standalone distribution.** Package the real game entry point, complete selected game content and dependencies into an application; use normal user locations for mutable saves/settings/logs. Implement reproducible packaging, signing, notarization, DMG creation and GitHub release automation. Exit when the exact candidate installs and plays on a clean supported Apple Silicon system without a checkout or original installation.
7. **Audit and release.** Independently audit D1–D8 and every product requirement against retained evidence; resolve review findings, run the complete integration gate and validate the exact signed DMG. Publish the verified source and 1.0.0 release to GitHub only after the completion gate is satisfied. Exit when the downloaded release artifact's identity, installation and launch are verified. No earlier public release is permitted.

Independent work may proceed in parallel under shared admission; dependency gates remain mandatory. Packaging may be developed before parity is complete, but the release remains blocked.

### Development constraints

- Preserve the shared dirty checkout and all prior evidence. Keep the Reference Folder read-only. Use isolated writable copies/worktrees for experiments or upstream research; never pull into the dirty shared checkout to obtain reference updates.
- Use REA MCP for shipped-artifact questions and Jev before and after every REA call. Retain source identities, evidence, limitations and judgments; close owned sessions. Use Jevgrep first for unfamiliar source behavior, with explicit roots and bounded searches. Distinguish incomplete searches from absence.
- Automated game captures must preserve host foreground, keyboard/mouse, normal audible audio and responsiveness. Exercise actual video/audio processing with private frames and PCM; muting the host or bypassing processing is not qualification. The player application uses normal visible presentation and audible audio.
- Keep existing shared admission, pressure, output and retention controls. No ledger reset, prior-evidence deletion, budget increase or disk-floor reduction to force progress. Reinspect capacity at launch; stopped or escaped writers and cleanup failures remain failures.
- Retain failed and superseded results with provenance. Local harness success, names in a registry, decoded branches and model judgments never substitute for actual game evidence. Jev escalation is not approval; resolve its concrete uncertainties with evidence and independent review.

## Testing Decisions

- **Primary acceptance seam:** observable behavior of the whole application along declared Journeys, using the existing Stock and Port adapters and strict proof evaluator. The user approved complete feature/content acceptance evidence and packaged-application testing; this preserves that agreed seam without another interview.
- Test externally observable behavior: frames, audio, input consumption, world/Car state where instrumented, outcomes, saves/settings and ordinary lifecycle. Bind observations to executable/content/source identity, starting conditions and stimuli. Use lower-level tests only where they isolate a causal defect or qualify the capture/evaluation mechanism.
- Start with exact comparisons. Numerical tolerances require field-specific Stock repeatability evidence, a recorded justification and preserved raw values; they cannot excuse discrete branch differences. Match each requirement to the applicable Stock reference and reconciled behavior decision.
- Reuse existing proof-evaluator mutations, native private-audio/offscreen/focus tests, Stock adapter isolation tests, Career save/load audits, Tufa/corpus checks, original-code physics fixtures, input/clock raw-state controls and shared-pool lifecycle tests. Extend these seams rather than creating independent feature-specific test frameworks.
- Qualify VM/native/resource contracts, physics and damage, rendering and audio, UI/input, Career/economy/race progression, persistence, cleanup and release installation. Cover success, refusal, cancellation, repetition, interruption and recovery where applicable.
- Prove the comparator rejects deliberate stimulus/record corruption, missing observations, mismatched identities, lost calls, incomplete replay and false repeatability. Qualify concurrency, deadline, pressure, process-death and isolation failures using actual executions.
- Keep decoder coverage, runtime branch coverage, semantic behavior evidence and paired Parity Proofs distinct. Inventory every required variant; a representative example does not promote an entire family. No untested requirement counts as complete.
- Run the full integration gate successfully as a complete invocation. Separately passing later phases do not repair an earlier failed gate. Report failing and unexecuted checks separately.
- Test clean DMG installation, Gatekeeper/signature/notarization validation, dependency closure, first launch, normal input/audio, settings, save/reload, app replacement preserving progress and sustained gameplay against the exact release candidate. Record supported macOS/hardware and tested limits before release.
- The final audit must map every requirement and D1–D8 to current bound evidence, independently review the mapping and obtain the Jev completion assessment. Resolve all required gaps and unexplained version differences before 1.0.0. Test pass ratios are not completion percentages.

## Out of Scope

- Separate products or user-selectable stock-version profiles; an external-installation requirement or asset-import setup flow.
- Public alpha, beta, early-access or other releases before full completion of 1.0.0.
- New gameplay, speculative enhancements or approximation-based behavior changes presented as stock parity. Post-parity changes belong to later versions.
- Universal compatibility with every community mod, non-reference distribution, external DLC, Workshop service or obsolete authoring tool. Official content present in the two reference distributions remains in scope; ambiguous items must be classified rather than silently excluded.
- Intel Mac, Windows, Linux or mobile release products. Those environments may remain development/reference tools.
- Authentic disc metadata/CD-track fidelity, under the already approved exception. This does not exempt file audio or video.

## Further Notes

### Starting evidence, not completion claims

At specification time, retained evidence demonstrates native Free Ride driving and bounded native/Stock menu, ordinary-exit and Career save/load flows. These are functional observations, not complete paired Parity. The latest indexed complete CTest run is 126/127, with the world-step speed assertion failing; passing separate post-CTest checks is not a green full gate. Required work includes actual Stock replay and Port consumption, semantic coverage, physics repair, production containment, complete gameplay/presentation and release packaging.

The corrected local input stage3 evidence supersedes the older incomplete COM checkpoint, but it does not qualify Stock game replay. Likewise, the owned clock harness is not process/scheduler qualification. The updated reconstruction contains useful input/OSD/String fixes; it is not a completed engine or a validated substitute for stock physics. No exhaustive semantic read of every reference binary/resource has yet been claimed.

### Repository navigation and preserved history

- This specification replaces the former active workflow plan. Its relevant steps and all eight deliverables are integrated above.
- [Historical workflow checkpoints](../history/parity-workflow-checkpoints.md) preserve the former document byte-for-byte solely for provenance. Its active-goal wording, single-reference assumptions, old permissions and superseded status entries are historical, not current instructions.
- [Agent command guide](../../tools/parity/agent-tools.md), [shared admission](../../tools/parity/iteration.md), [retention](../../tools/parity/storage.md), [proof format](../../tools/parity/proof-format.md) and [current evidence bindings](../../tools/parity/session-evidence.json) supply operational details. Regenerate the evidence index before continuing implementation.
- [Glossary](../../GLOSSARY.md) defines Port, Stock Executable, Rung, Journey and Parity Proof. No architecture decision records were present at synthesis time.
- Source publication and the finished GitHub release are authorized by the product request. Publishing this specification is not a game release. Signing/notarization credentials and release infrastructure are prerequisites to fulfill, not grounds to weaken the release gate.
