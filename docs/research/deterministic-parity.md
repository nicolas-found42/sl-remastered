# Deterministic parity testing research

Research date: 2026-11-01. Status: research and candidate methods, **not an approved design or an implemented harness**. Target: the 2005 Windows x86 Stock Executable versus the C++20 macOS arm64 Port. No stock/port experiment was run for this note.

## What a passing result would mean

**Port repeatability is necessary evidence for a useful harness, but is not Stock/Port Parity.** A Port replay can reproduce its own wrong behavior indefinitely. A parity result needs a named Stock Executable, an equivalent initial condition and stimulus, observations from both implementations at a verified corresponding boundary, and an explicit comparison policy. The result is bounded by those scenarios and observed fields; it cannot establish whole-game equivalence.

Separate these questions in reports:

| Question | Evidence | Limit |
|---|---|---|
| Is the Port repeatable? | Port A versus Port B, same fixture and input | No stock evidence |
| Is the Stock capture repeatable? | Stock A versus Stock B under the selected host and controls | Does not establish equivalence to another stock build or host |
| Is one reconstructed function faithful? | Exact captured inputs into the stock function and Port counterpart | Requires verified ABI, layouts, dependencies and FP environment; says nothing about its callers |
| Does a scenario have simulation parity? | Stock versus Port at aligned simulation boundaries | Only the covered state and scenario |
| Does presentation match? | Matched camera/tick screenshots and audio/event capture | Cannot establish hidden simulation state |

This distinction is a research inference from the examples below: OpenRCT2 and DevilutionX test their own recorded replay outcomes; neither test is a ready-made oracle for this game's original executable. [OpenRCT2 replay test][rct-test], [DevilutionX timedemo test][dev-test].

## Discovery trail and primary evidence

Firecrawl web search found the lists; Firecrawl scraped list READMEs and primary source/docs. Linked repositories were then inspected through their raw source and GitHub tree/commit APIs. A short Firecrawl rate limit interrupted some primary reads; direct public raw-source retrieval completed those reads. Lists are discovery aids, not authorities for a technique's correctness. Branch names below identify inspected paths; commit identifiers provide retrieval provenance, not claims that these projects were built locally.

| Discovery path | Primary evidence actually inspected | Useful transfer and limit |
|---|---|---|
| [Awesome Game Remakes][remakes] → OpenRCT2 | `ReplayManager.cpp`, `test/tests/ReplayTests.cpp`; repository HEAD `ed5c2edb0c835aa0fbb251a09e5c37176d29c57f` | Records tick-indexed entity checksums and snapshots; headless test advances game logic and rejects playback mismatch. Transfer the structure, not its entity schema or assumption that stock supports its replay format. [Manager][rct-manager], [test][rct-test] |
| Same list → DevilutionX | `Source/engine/demomode.cpp`, `test/timedemo_test.cpp`; HEAD `b325996a89c59eb0a680406b7ede3747011b9e08` | Fixture directory contains demo, starting save and expected final save; test overrides options, disables sound/music, runs headless and compares final hero. Demo code distinguishes game ticks from extra rendering. A final hero comparison misses intermediate and non-hero divergence. [Demo][dev-demo], [test][dev-test] |
| Same list → OpenRA | `World.cs`, `Sync.cs`, `Network/SyncReport.cs`; HEAD `9636b63eaee62fcb60ef2e896e7985bdf2f1ee68` | Shared versus local RNG; world hash includes shared RNG output; sync reports record actor/trait details and RNG output/count; `RunUnsynced` checks that presentation work did not mutate synced state. Transfer diagnostics and separation, not the .NET implementation or RTS lockstep model. [World][ora-world], [guard][ora-sync], [report][ora-report] |
| [Awesome Game Engine Development][engine-list] → Jolt Physics | `Docs/Architecture.md`, deterministic simulation and rollback sections; HEAD `ea4599d5044d92ba91b18664ec1df96255d5eb31` | Documents ordered API calls, same state/timestep, FP controls, callback-order risks and snapshot omissions. Strong methodological example; replacing the reconstructed solver with Jolt would change the implementation being compared and does not demonstrate stock fidelity. [Architecture][jolt] |
| Same engine list → RenderDoc | README; HEAD `1d83c25876b315c0a13bc6906c7dd82667b09892` | Frame debugger, not a simulation oracle. Its support table excludes D3D9/10 and does not offer the macOS target needed here. Do not nominate it as a universal stock-to-Port capture tool. [Support][renderdoc] |
| [Awesome Emulators][emu-list] → BizHawk / Dolphin | List entries inspected as discovery leads; no relevant Windows-game execution path or test implementation established | Console-emulator TAS workflows suggest frame stepping and immutable movie provenance, but these entries do not establish support for this Windows x86 executable. No claim that either runs the target game. |
| Additional direct tool discovery → libTAS | Official overview and README; HEAD `03eabffeca35ca54d70e666e0e1dc30e389e80dd` | Useful example of intercepting input/time, frame advance and savestates. Requires GNU/Linux x86/x86_64; README says Wine support is limited and not actively developed. It is not a native macOS arm64 solution, and target compatibility is untested. [Overview][libtas-site], [requirements][libtas-readme] |

The emulator-list branch remains a screened lead rather than verified primary implementation evidence. It is included to make that research boundary visible. No dedicated awesome list found during this bounded search supplied a stronger game-testing implementation than the linked reimplementation repositories above.

## Control the experiment before comparing outcomes

The following is a **candidate protocol**, not a claim that current stock hooks expose these controls.

1. **Bind the oracle.** Record Stock Executable digest, asset/script digests, configuration, save/profile fixture, locale, working directory and execution environment (real Windows, Wine or another host), plus Port revision/build/compiler flags. Keep a stock-under-Wine result distinct from a stock-under-Windows result until measured. Existing [port tooling research](../port-tooling-research.md) records a two-byte branch difference between executable copies; do not silently switch them.
2. **Create a fresh fixture copy per run.** Include player, inventory/car state, save data, settings and persistent caches that can affect behavior. Verify what loading actually reconstructs. A save file need not contain solver caches, pending events or RNG state; Jolt explicitly warns that its own snapshots omit externally changed properties and require additional object-lifecycle handling. This is why “same save” alone is insufficient. [Jolt rollback limitations][jolt].
3. **Verify the simulation boundary.** Identify whether the relevant subsystem uses fixed ticks, variable elapsed time, substeps, or multiple update rates. Record tick/substep identifiers and actual `dt` inputs. A graphics Present or SDL swap count is not automatically a simulation tick. Start by reproducing Stock's measured schedule; imposing a new fixed timestep can change the behavior being tested. Jolt requires the same timestep; DevilutionX's replay loop explicitly treats extra rendering separately from game ticks. [Jolt][jolt], [DevilutionX loop][dev-demo].
4. **Control all consumed time sources.** Candidate hooks include performance counters, millisecond timers, wall time and engine-local elapsed time; enumerate actual use before substituting them. Replay a virtual time sequence at the consumed boundary rather than rely on real-time sleeps. A single changed clock API does not establish full control; render, audio and worker threads may query different clocks. libTAS demonstrates interposition of system time but does not establish target coverage. [libTAS overview][libtas-site].
5. **Record input at the game's consumption boundary.** Include press/release ordering, held state, analog values, focus, mouse coordinate space and sampling tick/substep. Record any UI commands that create later simulation actions. A tick-level “accelerate=true” script is useful only after establishing how it corresponds to stock sampling; sending OS events on a wall-clock schedule can miss a polling boundary. OpenRCT2's replay manager records actions against ticks, while DevilutionX also records event messages and progress within a game tick. [OpenRCT2][rct-manager], [DevilutionX][dev-demo].
6. **Account for RNG state and call sequence.** Capture algorithm/state, seeds, streams and call counts where accessible. A seed alone is inadequate if one extra render/audio call consumes the shared generator. Treat substituting a different RNG as changing behavior until justified. OpenRA provides a concrete example of seeded shared randomness separated from local randomness and includes the shared generator in diagnostics. [World][ora-world], [report][ora-report].
7. **Control ordering that affects simulation.** Asset discovery order, object insertion, map/container iteration, asynchronous completion and callback order can matter. Preserve observed order or expose it in the trace before adding sorting. Jolt explicitly documents nondeterministic broadphase result, active-body and listener ordering and the need for consistent result ordering when relevant. Its advice transfers as a risk checklist, not evidence that this game shares those exact thread paths. [Jolt][jolt].
8. **Check observation effects.** Repeated stock captures should establish whether instrumentation changes `dt`, scheduling or outcomes. Prefer bounded buffering and deferred writes; count dropped/unreadable records. A missing stock observation is unknown, not a match or a zero-valued field. This is experimental-design advice, not a measured result.

First run Stock twice and Port twice. If a side disagrees with itself, identify the earliest unstable field and missing control before interpreting cross-side differences. A stable endpoint does not prove a stable intervening trace.

## State capture, hashing and first divergence

OpenRCT2 supplies a particularly concrete pattern: normal recording schedules entity checksums each tick, silent recording uses a 40-tick interval, snapshots link to game ticks/RNG, and playback can compare snapshots and write desync reports. Its headless replay test calls the logic update directly. OpenRA complements this with per-actor/trait diagnostics and RNG call counts. [Replay manager][rct-manager], [test][rct-test], [sync report][ora-report].

For this project, a proposed **semantic snapshot** should have a versioned schema and provenance, tick/substep, stable logical object identities, ordered events and explicit coverage. Candidate domains include loaded resources, VM/script state, player/save state, car/body poses and velocities, forces, contacts/caches, timers, RNG and pending commands. Which are observable and necessary remains open.

Do not hash raw C++ objects across Windows x86 and macOS arm64. Pointers, allocator addresses, padding, ownership containers and host-width integers are not portable semantic identity. Serialize named fixed-width fields in a documented byte order; preserve sequence order when it affects behavior and normalize only demonstrably irrelevant representation. Keep unknown fields explicitly unknown. The local [`State` declaration](../../src/physics_body.hpp) has asserted size/offsets, which is a useful verified capture subset; it does **not** imply a full physics-world snapshot or authorize treating all host structures as stock layouts.

Use a cheap whole-snapshot digest to locate a candidate first divergent tick, then subsystem/object digests and a readable field diff. Retain underlying records: equal hashes cover only serialized fields and have collision risk; unequal hashes alone do not explain a defect. For numeric data retain original float bit patterns and interpreted values. Exact comparison and a separate numerical diagnostic should coexist; a tolerance cannot be applied to an ordinary byte hash. Quantizing before hashing introduces bin boundaries and can hide cumulative drift.

First-divergence procedure:

1. Verify fixture, assets, input consumption, RNG and `dt` agree before the first divergent transition.
2. Compare pre-state, ordered calls/events and then post-state. If pre-state differs, do not attribute the result to the solver or function currently under inspection.
3. Narrow to a captured function invocation with complete dependency data; replay that invocation against the Stock function oracle and Port counterpart.
4. Preserve the smallest reproducing fixture and add it to a regression corpus with stock provenance and expected outputs.

This extends the snapshot/checksum patterns above. It remains a proposal until a verified stock tick boundary, stable identity mapping and capture schema exist.

## x86 versus arm64 numeric behavior

Same source expression and same nominal `float` type do not guarantee identical intermediates. Jolt's cross-platform determinism guidance calls out precise FP compilation, disabled contraction and consistent rounding/denormal state; Microsoft's `/fp` documentation explains that fused multiply-add rounds differently from separate multiply/add and that intermediate machine precision can differ from source precision. Modern MSVC defaults are **not evidence of the 2005 binary's compiler options or x87 behavior**. [Jolt][jolt], [Microsoft FP behavior][ms-fp].

The local [stock collision oracle](../../tools/wine/core_pair_oracle.c) deliberately sets x87 control word `0x007f` around calls and prints float bits. That is an oracle configuration, not proof of the live game's physics-thread control word. Existing [x87 research](../port-tooling-research.md) also flags intermediate spills, extended exponent behavior and instruction ordering. Capture live per-thread FP state and verify actual instructions before declaring an ARM/x87 mismatch.

Candidate policy:

- Compare IDs, counts, flags, command ordering and control-flow decisions exactly.
- Begin stock function cases with raw numeric bits and exact comparison. Also report absolute/relative error and ULP distance where meaningful, preserving signed zero, NaN and infinity distinctions.
- Define field-specific numerical acceptance only from investigated behavior and gameplay requirements. One blanket epsilon can hide collision branch flips, contact changes and long-term drift. Passing a loose positional tolerance does not imply exact simulation parity.
- Exercise near-zero values, clipping/contact boundaries, near-ties, large magnitudes and underflow cases. A discrepancy becomes a numeric explanation only after identical input and first differing operation are established.
- Do not assume `-ffp-contract=off` emulates x87. Do not replace stock arithmetic wholesale with fixed point or a modern solver to make a deterministic test pass. Those can create a repeatable different simulation.

The last three points are proposed experimental methods, not observed diagnoses of the current Port.

## Differential and metamorphic testing

**Differential tests** use the named Stock result as oracle: parser/resource outputs for identical bytes; native/VM behavior for identical arguments and state; isolated collision/integration transitions; then full scenario traces. The existing collision oracle is a useful seed, subject to its patched environment, manually constructed cases and FP settings. Its existence does not establish complete live-game coverage. [Local oracle](../../tools/wine/core_pair_oracle.c), [existing limitations](../port-tooling-research.md).

**Metamorphic tests** need no complete expected answer, but their relations require justification. Candidates include save→load continuation versus uninterrupted execution, headless versus rendered simulation, different render cadence with the same logic schedule, and pair-order symmetry for an isolated geometry query where stock establishes the relation. OpenRA's `RunUnsynced` before/after hash guard provides a primary example of presentation-isolation checking; Jolt's rewind-and-step determinism check demonstrates state restoration validation. [OpenRA][ora-sync], [Jolt][jolt].

These relations can expose bugs in a single implementation, including incomplete snapshots. They do not prove Stock parity. Do not assume translation/rotation symmetry, conservation or argument-swap symmetry across the entire game: world boundaries, sleeping thresholds, integration error and intentional asymmetries can invalidate those relations. Establish the domain on Stock before enforcing a property as a compatibility requirement.

## Render and audio comparisons

Compare presentation after tick alignment and a known camera/viewport/UI state. Keep semantic render-command/scene capture separate from final pixels so a texture or geometry error can be distinguished from rasterization differences. Candidate screenshot records include simulation tick, presentation index, resolution, color space, antialiasing, camera and renderer/driver metadata. Use image differences and reviewable overlays; masks/tolerances must be explicit and must not erase whole unimplemented effects. Pixel equality across different graphics stacks is an experiment to evaluate, not a prerequisite to assume.

Audio needs its own clock and oracle. Candidate records are ordered sound events (asset ID, simulation time, gain/pitch/loop state) plus captured PCM with sample rate/channel layout. Compare event ordering exactly where justified; measure onset/envelope or aligned waveform differences separately when resampling/mixing differs. A muted headless test such as DevilutionX's cannot validate audio parity. [DevilutionX test][dev-test].

RenderDoc is useful only within its supported platform/API scope; its README explicitly excludes D3D9 and lacks macOS support. It cannot by itself cover this stock-to-native-Port comparison. libTAS is similarly a methodological reference with unverified Wine compatibility for this game, rather than an available macOS harness. [RenderDoc][renderdoc], [libTAS][libtas-readme]. No capture-tool installation or target compatibility experiment was performed.

## Candidate test tiers

| Tier | Proposed purpose | Oracle and gate |
|---|---|---|
| 0: provenance and repeatability | Validate executable/assets/fixture manifests and twice-run stability | Per-side repeatability; explicitly not a parity pass |
| 1: captured functions | Small parser, VM/native, collision and numeric cases | Stock function output with verified environment; exact/field-specific policy |
| 2: short simulation scenarios | Start/load, one car, steering/braking/contact, transitions and save continuation | Stock/Port aligned pre/post ticks plus first-divergence diff |
| 3: presentation | Selected cameras/UI/effects and sound events/PCM | Named Stock capture; separate image/audio policy |
| 4: longer integration | Scenario corpus, lifecycle/order risks, accumulated drift | Stock trace checkpoints plus retained failing interval |

Run inexpensive isolated cases frequently; use shorter matched scenario traces for ordinary integration; reserve long and presentation captures for changed subsystems or scheduled verification. This ordering is a candidate cost strategy, not a repository CI decision. Test artifacts should report covered fields, missing/dropped observations, comparison rules and exact oracle identity. A skipped asset-dependent run must be visible: DevilutionX's test explicitly skips without game data, illustrating why green CI alone cannot imply the scenario executed. [Timedemo test][dev-test].

## Unresolved decisions and useful next evidence

1. Which exact Stock Executable and host define the primary reference? Does Windows versus Wine materially change observed results?
2. Where are the actual game/physics boundaries, how are `dt` and input sampled, and which threads mutate simulation state?
3. Which RNG streams, persistent data and hidden caches must be captured to reproduce a scenario?
4. Which stock fields and identity mappings are verified enough for a semantic snapshot? Is a full restart more trustworthy than a partial restore initially?
5. Is exact numerical parity required for each subsystem, or may a specifically justified behavioral tolerance pass? Who approves those tolerances and intentional deviations?
6. Which screenshot/audio differences are acceptable, and which renderer/audio path is the reference?
7. What fixture distribution and automated execution are practical with required original assets? Which tests must visibly report “not run” when those assets or the stock runner are unavailable?

The highest-value next experiment is a very short controlled Stock trace repeated twice, with fixture/asset digests, the verified simulation `dt`, consumed input and a small verified state subset; then run the same case on the Port. That tests observability and control before investing in a universal harness. This is a research recommendation pending the ongoing design interview.

## Jev evidence validation

On 2026-11-01, `jev_screen` screened bounded primary-source excerpts before this validation pass (`pass`: injection probabilities 0.07 and 0.02). Two `jev_verify` calls using `typesafe/jev-1.14-20261014` assessed eight claims: **seven verified, zero contradicted, one unsupported**, with no review actions. These are fallible evidence-relation judgments, not target execution results.

| Bounded claim | Judgment and supporting probability | Scope |
|---|---|---|
| OpenRCT2 compares checksums at matching `currentTicks` and records mismatch | Verified, 1.00 | Observation alignment pattern. [Source][rct-manager] |
| OpenRCT2 records actions with ticks and dispatches at corresponding ticks | Verified, 0.96 | Game commands; does not establish synchronized OS input injection into Stock. [Source][rct-manager] |
| DevilutionX distinguishes extra rendering from game ticks | Verified, 0.97 | Supports separate logic/presentation counters. [Source][dev-demo] |
| OpenRA constructs separate shared/local RNGs and records shared output/count | Verified, 0.99 | Separate streams and diagnostics; not a claim of statistical independence or known Stock RNGs. [World][ora-world], [report][ora-report] |
| OpenRA's guarded unsynced action can detect changes in world hash | Verified, 0.99 | Only code using that guard and state covered by the hash. [Source][ora-sync] |
| Jolt documents deterministic conditions and repeat-step validation | Verified, 0.92 | Evidence for self-repeat testing, not proof of Stock observability or repeatability. [Source][jolt] |
| OpenRA explicitly selects actor/trait state and shared RNG for its hash | Verified, 0.98 | Supports deliberate semantic-state selection; the proposed cross-ABI canonical serialization remains a design inference. [Source][ora-world] |
| These primary sources explicitly require this Stock Executable to run twice before Stock/Port comparison | Unsupported, `says_nothing` 0.98 | **Not asserted as a source requirement.** Repeating Stock first is this note's experimental-design recommendation, motivated by the need to distinguish unstable observation/control from a cross-implementation mismatch. |

Still unresolved: complete Stock state coverage, verified input/tick capture boundaries, recoverable RNG state, and a canonical schema linking Stock to Port. Neither Jev nor the cited other-game implementations establish that the proposed harness is feasible for this target. The canonical-state format, repeated-Stock protocol and tolerances await design decisions and target experiments.

## Source notes

Local discovery followed the required `jg` entry point: `jg "How does the port control time, input, random seeds and physics-step replay for stock comparisons?" src --max-requests 6 --max-output-bytes 18000 --concurrency 4`. It returned zero relevant files with **discovery incomplete** (`request-limit: 27`). Consequently this note makes no exhaustive claim about existing implementation coverage; local statements above are focused direct reads of the cited files and prior research.

Upstream paths are primary evidence; discovery lists are cited only for their links. Documentation and source were read, not built or executed. This note does not approve a new dependency, arithmetic model, fixture format or capture hook.

[remakes]: https://github.com/radek-sprta/awesome-game-remakes/blob/48ceb49045d8fc40a2c7c8463561d9206de94217/README.md
[engine-list]: https://github.com/stevinz/awesome-game-engine-dev/blob/f7f76faaf7f78ab3c5f24cbe10e7b9a83a205a4b/README.md
[emu-list]: https://github.com/DerekTurtleRoe/awesome-emulators/blob/14cf2fac6de1228e2112a3b6870733d2f2e58632/README.md
[rct-manager]: https://github.com/OpenRCT2/OpenRCT2/blob/develop/src/openrct2/ReplayManager.cpp
[rct-test]: https://github.com/OpenRCT2/OpenRCT2/blob/develop/test/tests/ReplayTests.cpp
[dev-test]: https://github.com/diasurgical/devilutionX/blob/master/test/timedemo_test.cpp
[dev-demo]: https://github.com/diasurgical/devilutionX/blob/master/Source/engine/demomode.cpp
[ora-world]: https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Game/World.cs
[ora-sync]: https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Game/Sync.cs
[ora-report]: https://github.com/OpenRA/OpenRA/blob/bleed/OpenRA.Game/Network/SyncReport.cs
[jolt]: https://github.com/jrouwe/JoltPhysics/blob/master/Docs/Architecture.md#deterministic-simulation
[renderdoc]: https://github.com/baldurk/renderdoc/blob/v1.x/README.md
[libtas-site]: https://clementgallet.github.io/libTAS/
[libtas-readme]: https://github.com/clementgallet/libTAS/blob/master/README.md
[ms-fp]: https://learn.microsoft.com/en-us/cpp/build/reference/fp-specify-floating-point-behavior?view=msvc-170
