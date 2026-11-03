# Reference Folder audit for Parity and reproducible tests

Audit date: 2026-11-01. Read-only examination; no game executable or bundled tool was launched. Only this report is authored by this audit. The Reference Folder is immutable. The report is a local artifact: `.gitignore:7` ignores `/docs/`; no staging, commit or push was performed.

## Scope and meaning of “all files”

Every regular file under `ref/` was enumerated, sized and SHA-256 hashed, including hidden files, archives, binaries, reference repositories, saves, and pre-existing diagnostic artifacts. Python `os.walk(..., followlinks=False)` was used; no symlinks were present. No archive was recursively extracted. Hashing proves corpus identity, not validity, behavior, or successful parsing. Source and decompile reading below is targeted; this is **not** a claim to have semantically read every binary, every compiled Script, every source file or every asset.

`jg files ref --list` reported 35,842 eligible files and skipped 12 hidden paths; its full eligible listing exceeded tool output limits. The independent filesystem inventory below includes those hidden files and is authoritative for this audit's scope. A first behavioral `jg` search on `ref/slrr-reverse-main` returned a request-limit/incomplete notice; a narrower completed search on `ref/slrr-reverse-main/engine/Runtime/System` returned `GameType.cpp:846–964`, which explicitly distinguishes cooperative stock scheduling from host stand-ins. No missing result is taken as evidence of absence.

Read prerequisites: `AGENTS.md`, `GLOSSARY.md`, `CODING_STANDARDS.md`, `docs/agents/domain.md`. No `docs/adr/` files were found. Existing evidence documents are records of earlier work unless explicitly described below as freshly inspected. The saved `local/census/all.c` was read directly for the cited functions; no fresh decompilation or dynamic capture was performed.

## Complete filesystem inventory

Regular files: **35,854**; bytes: **3,242,688,986**; symlinks: **0**; other nonregular file entries: **0**.

| Top-level entry | Files | Bytes | Role |
|---|---:|---:|---|
| `.DS_Store` | 1 | 18,436 | Filesystem metadata |
| `SLRR-JVM-compiler-main` | 17 | 80,358 | Java-like source to Tufa compiler and fixture |
| `SLRR_rev-main` | 157 | 4,441,716 | Independent TypeScript/WebGL research prototype and recorded benchmark reports |
| `blender-slrr-scx-importer-main` | 9 | 98,515 | Blender SCX/CFG importer, packaged ZIP and source |
| `must-not-be-created` | 3 | 516 | Pre-existing diagnostic artifact directory; inventoried, not created or executed |
| `raw` | 8,411 | 535,606,101 | Older Installed Game candidate; mixed/mod Script data, two executable variants |
| `slrr-reverse-main` | 148 | 3,590,457 | C++ reverse host; VA annotations plus explicit host stand-ins |
| `version 2-3-1` | 27,108 | 2,698,852,887 | Later release/mod comparison corpus; game, tools, source and compiled Scripts |

Canonical inventory SHA-256: `1c16c52d41349935550de7d33e7c4cc71d99ec810e0fbbdd1a0d6154cceec6a7`. Reproduction: sort rows by root-relative POSIX path, with each row `[path, size, sha256(file_bytes)]`; serialize each using Python `json.dumps(row, separators=(',', ':'), ensure_ascii=True)` plus LF, concatenate UTF-8 bytes, then SHA-256. A full per-path manifest is deliberately not added to version control. This digest fingerprints all files, including `.DS_Store` and pre-existing diagnostics; a later metadata artifact change can alter it without a game-content change.

### Every extension counted

Extensions are lowercased filename suffixes, not validated format signatures. Numeric suffixes are included literally; they have not been assumed to be individual physics formats.

| Suffix | Files |
|---|---:|
| `.scx` | 10,480 |
| `.dds` | 4,913 |
| `.class` | 4,424 |
| `.cfg` | 4,247 |
| `.java` | 3,456 |
| `.png` | 2,500 |
| `.tex` | 867 |
| `.wav` | 506 |
| `(none)` | 281 |
| `.rpk` | 236 |
| `.ptx` | 194 |
| `.1` | 161 |
| `.2` | 161 |
| `.4` | 160 |
| `.3` | 158 |
| `.6` | 156 |
| `.7` | 156 |
| `.5` | 155 |
| `.8` | 154 |
| `.9` | 151 |
| `.10` | 149 |
| `.11` | 147 |
| `.12` | 120 |
| `.rdb` | 118 |
| `.box` | 114 |
| `.ts` | 108 |
| `.13` | 108 |
| `.jpg` | 108 |
| `.tga` | 99 |
| `.14` | 95 |
| `.15` | 89 |
| `.vax` | 70 |
| `.16` | 62 |
| `.spl` | 58 |
| `.scy` | 56 |
| `.17` | 46 |
| `.bat` | 46 |
| `.md` | 45 |
| `.cpp` | 43 |
| `.18` | 40 |
| `.bon` | 40 |
| `.mp3` | 40 |
| `.19` | 35 |
| `.matrixdata` | 35 |
| `.skl` | 35 |
| `.inc` | 35 |
| `.scm` | 32 |
| `.20` | 28 |
| `.vaz` | 23 |
| `.21` | 22 |
| `.txt` | 21 |
| `.hpp` | 21 |
| `.db` | 20 |
| `.ini` | 19 |
| `.json` | 18 |
| `.py` | 18 |
| `.smb` | 17 |
| `.gif` | 14 |
| `.exe` | 12 |
| `.22` | 11 |
| `.bmp` | 10 |
| `.cur` | 10 |
| `.23` | 9 |
| `.dat` | 8 |
| `.psd` | 8 |
| `.htm` | 7 |
| `.dll` | 6 |
| `.avi` | 5 |
| `.log` | 4 |
| `.xml` | 4 |
| `.h` | 4 |
| `.24` | 4 |
| `.bin` | 3 |
| `.25` | 3 |
| `.bak` | 3 |
| `.zip` | 2 |
| `.url` | 2 |
| `.ico` | 2 |
| `.rest` | 2 |
| `.trc` | 2 |
| `.walk` | 2 |
| `.ps1` | 2 |
| `.dle` | 2 |
| `.scx2` | 2 |
| `.tex2` | 2 |
| `.vdf` | 2 |
| `.html` | 1 |
| `.yml` | 1 |
| `.css` | 1 |
| `.toml` | 1 |
| `.rar` | 1 |
| `.3ds` | 1 |
| `.26` | 1 |
| `.zmf` | 1 |
| `.rep` | 1 |
| `.hdr` | 1 |
| `.scxdump` | 1 |

## Authority and identity

The Stock Executable freshly hashes as:

- `ref/raw/StreetLegal_Redline.bak.exe`: 2,408,448 bytes; MD5 `e686099e7233bb7d7c461b1b35c5064c`; SHA-256 `7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde`.
- `ref/raw/StreetLegal_Redline.exe`: same byte length but MD5 `c89e34cab4ca0192f0464fdf87dad1e4`; SHA-256 `b284a7a53909d0b9ce59d59ddc11e14df40a7eb74d86fa76efb06305aadc2b03`. This is the Mod Executable, not the Parity authority.
- `ref/version 2-3-1/StreetLegal_Redline.exe`: 2,446,848 bytes; MD5 `973a8a23a2bb8019343eb318da638b2c`; SHA-256 `f44fb88a4335c657e20701cdb84afb8d1d85f7c3aceb93d30113e6e6e44fefc2`. It is a distinct comparison build, not a substitute Stock Executable.

The remaining nine `.exe` paths are installer/modding/import tooling under the later corpus, not alternate stock authorities. All twelve were hashed; none were executed. The Reference Folder contains no whole-program `.c` decompile: saved decompile evidence used here is outside it in `local/census/all.c`.

**Executable identity does not establish Installed Game identity.** `ref/raw` includes `MW_Mod`, `DestructionTools`, an addon Script source, and modified compiled Scripts. Fresh byte inspection found `Miran` in `sl/Scripts/game/MainMenu.class` at offset 12482 and `GameLogic.class` at 15270. This agrees with the older contamination record in `docs/wine-comparison.md:67`. There is only one `.java` under `ref/raw`: `maps/Addon_Maps/Drag_Track/Scripts/src/Drag_Track.java`; it is an addon, not recovered stock source.

`ref/version 2-3-1` has 3,454 `.java` and 2,921 `.class` files, while `ref/raw` has 1,501 `.class` files. The later Java sources cannot silently define stock Script semantics. Its `sl/Scripts/game/src/GameLogic.java:107–120` labels additional v2.3.1 save directories, `:276` invokes `Math.randomize()`, `:391–394` starts a time refresher and timer, and `:413–415` loads defaults then chooses a random time. These are concrete later-build examples and useful leads, not authority for the 2005 Scripts.

Earlier run documentation identifies the clean-install candidate outside `ref/`: `.scratch/2005-extract/Program Files/Street Legal Racing - Redline`. `docs/stock-flow.md:13–17` records an installer-data copy, the verified stock backup, Wine, and synthetic CD setup. Those provenance/capture claims were read, not rerun. Presence of key `.class` paths there was checked; full installer identity and every Script against that source were not revalidated in this audit. A Parity scenario must name a content-hashed Installed Game rather than “whatever is under ref.”

## Determinism evidence and test implications

| Area | Evidence read directly | Consequence for a test contract |
|---|---|---|
| RNG algorithm and explicit seed | `local/census/all.c:91991–92040`: `0047c7d0` reads engine time then seeds, `0047c7f0` seeds from the unboxed argument, `0047c820` consumes `_rand`, masks 15 bits, clamps 32767 to 32766. `:379185–379215`: seed and recurrence use state returned by `005dd8d3` at offset `+0x14`, multiplier `0x343fd`, increment `0x269ec3`, then bits 16–30. | Record seed/reseed events, state ownership and consumption order. A single seed alone cannot control subsequent time-based `randomize` or random consumers on other native threads. `all.c:387854–387884` directly confirms `005dd8d3` uses `TlsGetValue/TlsSetValue` and records `GetCurrentThreadId`; mapping cooperative VM threads to native CRT state still needs explicit proof. |
| Clock sources | `all.c:252744–252779`: `00551690` initializes a QPC epoch and `005516c0` reads frequency/counter and scales elapsed time. `:91395–91415` exposes simulation-time and wall-time paths separately. `:34409–34447` updates simulation epoch/time-warp fields. | Treat wall clock, simulation time, time warp, tick-count and input polling time as distinct inputs. A fixed physics dt does not by itself freeze UI timers, seeding or media. |
| Script scheduling | `all.c:26236–26305` constructs VM thread/list state; `:91836–91926` starts, stops, sleeps, suspends and resumes through VM helpers. `ref/slrr-reverse-main/engine/Runtime/IO/IO_part2.inc:591–704` explicitly calls stock scheduling cooperative but uses a detached OS thread in its host path. `Runtime/System/GameType.cpp:884–887` explicitly frees a constructor thread where its comment says the stock scheduler retains it. | Reference-host scheduling is not stock scheduling evidence. Capture runnable order, wake/deadline rules, priorities, waits and event delivery relative to simulation boundaries before claiming deterministic behavioral Parity. These inspected functions do not prove every stock thread's scheduling. |
| Input and smoothing | `all.c:248772–248900` (`0054d830`) walks mapping slots and uses timing globals in smoothing. Existing `docs/gfx-input-evidence.md:71–87` records hotkey arming/press/release ordering and clock dependencies. Reference-host `Core/Platform/input_win32.cpp:939–964` polls QPC and computes elapsed milliseconds. | Replay sampled device values and text/button transitions at the actual poll boundary, including initial mapping samples, hotkey arming and smoothing dt. “Press at frame 100” is ambiguous without identifying the frame phase and mapping state. |
| Physics step | `all.c:120995–121115` (`004a5190`) accumulates dt, returns below a threshold, iterates body/constraint lists, accumulates forces and integrates before contact handling. `docs/physics-evidence.md:30–49` records the broader step, RK4, double buffers and bounded contact passes. | Define starting body order, list insertion/dispatch order, dt stream, contacts, commit boundary and numeric comparison. A physically plausible approximation or matching final position is weaker than matching stock transitions. Remaining scheduler callers, threshold constant bytes and all solver branches were not rederived here. |
| Initial saves/configuration | Fresh inventory finds `ref/raw/save/controls/active_control_set` (2,973 bytes), `save/game/options` (200 bytes), and zero-byte `save/game/enable_audio_section`. Existing `docs/player-startup-evidence.md:35–58` records ControlSet loading and first physical sample during mapping. | Initial save presence/absence, bytes, names, config, control mappings and persisted options belong to test input. Start from a fresh writable scenario copy each time. No assumption that an empty career directory means a default configuration. Complete stock Career field initialization remains unverified in this audit. |
| Music and filesystem order | `all.c:251990–252022` (`00550be0`) chooses initial playlist index with `GetTickCount() % count`; `:252024–252090` recursively builds the list using directory enumeration. This independently confirms the record in `docs/music-evidence.md:19–25`. | Forced Seed cannot control playlist selection. Either control/record tick-count and enumeration order or declare audio coverage separately. Audio playback completion/callback timing still needs dynamic proof. |
| Renderer/media | `docs/gfx-input-evidence.md:25–51` records force-frame/world/audio ordering and unresolved video paths; `docs/stock-flow.md:14–18,70–79` records proxies, Wine, missing intro filters, clipping and untested audio. | Compare visual structure and rendered output under a named display/backend/config; do not equate Wine pixels or missing decoded video with universal stock output. GPU/driver/codec variation and timing effects are open questions, not demonstrated universal nondeterminism. |

### Why the reference repositories are leads, not oracles

`ref/SLRR_rev-main/README.md:3–5` identifies itself as an independent research prototype using selected Java source methods rather than a complete Tufa execution path. Its `src/engine/original-stock.ts:10–36` implements a deliberately small numeric/branch reader and says it is not a Java VM. Its `benchmarks/baiern-coupesport-2.5/README.md:3–19` records screenshot provenance, unexplained displayed values and an empirical driving calibration, explicitly not a recovered native algorithm. Its `docs/reports/pe-comparison.json` first executable entry records 2,441,216 bytes and SHA-256 `4052c78d499a765f3b063080f836db4ecfbae67d798bf1229b6357a2711afdb8`, which differs from every primary executable freshly identified above. Report names such as “stock” do not bridge that identity gap.

`ref/slrr-reverse-main/engine/Runtime/System/Lang.cpp:331–380` has useful stock addresses and RNG notes, but its implementation calls host `std::rand/std::srand`. Its `Core/Jvm/runtime.cpp:71–81` derives simulation time on demand from a wall clock. Those are implementations to inspect, not proof that a port copying them would match stock.

`ref/SLRR-JVM-compiler-main/README.md:3–5` describes a source-to-Tufa compiler, useful for format/compiler tests. `ref/blender-slrr-scx-importer-main/README.md:15–35` describes model/CFG assembly and material interpretation. Neither establishes stock VM scheduling, physics or native behavior. Archives and packaged Blender code were inventoried/hashed but not installed or run.

## Proposed boundary for Parity claims

This audit does not change the glossary definition. It makes “same input” concrete: identify Stock Executable, Installed Game manifest, initial saves/configuration, random state and reseeds, clock values, logical input samples, scheduler/event order and platform configuration. Each scenario also needs explicit observed outputs, comparison boundary, numeric/pixel tolerances with evidence, and unsupported paths. Record the earliest observable divergence rather than accepting a matching final screenshot after differing state history.

Data Parity can use content/resource identity and parser results; logic Parity can compare native/VM transitions and results; behavior Parity needs stock-executable evidence for the whole scenario. Exact byte/bit equality is appropriate for demonstrated discrete/serialization contracts. Floating-point tolerances require a stock comparison and rationale; neither “all floats must be bit-identical everywhere” nor an arbitrary broad epsilon is established by this audit. Existing numeric standards already forbid changing grouping or enabling contraction and require a demonstrated mismatch before x87 emulation.

Reproducibility within the Port and Parity with stock are separate claims. Repeated deterministic Port results can still repeat a wrong rule. A Forced Seed, a fixed timestep, a successful process exit, structural asset parsing and the TypeScript benchmark numbers are each insufficient alone.

## Coverage still required

- Establish and manifest the authoritative Installed Game; classify modified/later Script and resource differences instead of inheriting them from `ref/raw` or 2.3.1.
- Complete stock Script decoding/semantic coverage and save initialization for the selected scenarios; this audit does not certify all 4,424 compiled `.class` files or 3,456 `.java` files as stock or valid.
- Trace all native RNG consumers, state initialization, reseeding and actual native-thread ownership; match clock and scheduling boundaries for whole scenarios.
- Read the remaining main-loop/VM scheduler, timer ordering, input buffering, physics/contact/traffic ordering and media callbacks at the needed depth. Source addresses here are starting evidence, not whole-program closure.
- Validate archives, models, textures, resources, animation/skeleton data, packed Script/resource relationships and encoding errors using existing parsers when relevant; no all-asset validation was executed here.
- Produce same-contract stock and Port Run pairs for each Rung. No new Run, screenshot, telemetry result or verified Parity row is created by this report. Existing Wine records remain platform-specific, instrumented records.

The audit supports a bounded, evidence-backed deterministic test design. It does not establish whole-game determinism or whole-game Parity.


## Candidate Stock Executable control seams (static follow-up)

This follow-up locates research instrumentation points; none is a validated live hook. Addresses below are preferred image virtual addresses for the identified Stock Executable, not offsets in later/mod executables. Ghidra's omitted register arguments and suspect inferred types mean signatures, instruction boundaries, relocations and complete overwritten bytes must be checked against disassembly before implementing any hook. No such implementation or binary modification was performed.

### Strongest candidate: retain the stock outer simulation step

`FUN_00428450`, `local/census/all.c:34447–34535`, contains the following verbatim selections in execution order:

```c
local_8 = DAT_0063c534 * *(float *)(param_1 + 0x110);
if (DAT_0060c8cc < local_8) {
  local_8 = DAT_0060c8cc;
}
fVar3 = DAT_0063c538 + local_8;
DAT_0063c538 = fVar3;
/* ... control update loop calls FUN_004291e0(fVar3,puVar6) ... */
fVar3 = *(float *)(PTR_DAT_00617650 + 8);
/* ... chooses local_c from local_8 and fVar3 ... */
iVar4 = FUN_004a5190(local_c);
if (iVar4 == 0) break;
local_8 = local_8 - local_c;
local_10 = local_c + local_10;
FUN_00427160();
FUN_004277f0();
```

[HYPOTHESIS] A breakpoint/trace at **`00428450` entry**, a watch on **`0063c534`**, and entry/return traces at **`004a5190`** give a useful frame-to-physics ledger. Controlling the frame elapsed value before `00428450` while retaining its native splitting/dispatch is a better candidate for full-scenario experiments than independently replacing each physics-call argument. The latter would bypass the outer control accumulator, time-warp/clamp logic and the relationship to timer dispatch. This proposal is not evidence that setting one global is sufficient.

`00427160` directly compares timer due time to the simulation-time pointer and dispatches events (`all.c:33468–33512`). `004277f0` processes destruction/cleanup queues (`:33890–33979`). The alternative function **`00428790`** (`:34600–34680`) also calls `004a5190` and then these two helpers, with dt derived from the configured step and warp. Its callers are not resolved here; observing only `00428450` may miss an alternate step path.

### Clock, Script and input boundaries that a frame hook misses

- **Outer frame/input boundary:** `00428960` calls `0054dda0(DAT_0063c534)` then `00428450` at `all.c:34801–34806`. Later it assigns timer-slot-7 elapsed time to `DAT_0063c534` at `:34933–34944`. Candidate observation: input helper **`0054dda0`** entry and exit, plus frame-global writes. The helper stores its supplied elapsed value, calls **`00556bc0`**, increments the input sample counter `007686ec` and updates mapped-input blocks (`:249055–249073`). `00556bc0` independently measures QPC elapsed time before DirectInput device calls (`:258806–258866`). Replaying keyboard state alone does not replay those elapsed values or prior smoothing state.
- **Independent animation delta:** after simulation/Script pumping, the loop reads **`005516c0`**, computes elapsed versus `0063c530`, clamps, and passes the result to **`00426fd0`** (`:34808–34817`). That function reaches Script `animate` or native animation virtual calls (`:33403–33464`). Candidate trace: that argument separately from physics dt. [HYPOTHESIS] a deterministic simulation with uncontrolled animation dt may produce varying screenshots even before considering the GPU.
- **Script execution budget:** **`00416940`** reads the engine clock for its deadline and checks sleeping-thread deadlines at `:18253–18335`; **`0041f630`** sets a sleeper deadline to `005516c0()+delay` (`:26437–26449`). VM execution **`00420ff0`** starts with an engine-clock sample at `:27939–27946` and its displayed exit condition at `:29726–29731` includes a fresh clock read versus start-plus-budget. [HYPOTHESIS] returning one frozen time for every read within a frame can prevent clock-based expiry, changing Script progress or causing a loop that would otherwise yield to run too long. Capture Script identities, budgets, PCs at yield, wakeups and dispatch order before choosing any time policy. Do not replace cooperative dispatch with OS scheduling.
- **General clock API candidates:** **`005516c0`** is elapsed engine milliseconds via QPC (`:252762–252777`); **`00552310`/`00552330`** implement QPC stopwatch slots (`:253674–253700`); the input path has its own QPC call site above. API interception of `QueryPerformanceCounter/Frequency` could cover those paths, but its return policy must be keyed to phase/read/caller or use a recorded sequence if budgeted progress depends on reads. Advancing time on every API call is also not established equivalent: extra calls from hooks or another thread would change logical time. API virtualization therefore remains an experiment, not a determinism result.

### RNG, native threads and filesystem controls

**`005d73fb`** (seed), **`005d7408`** (`_rand`), and **`005dd8d3`** (CRT TLS getter) are candidate observation points. Record native-thread identity, caller, pre/post state and reseeds; `005dd8c0` initializes native TLS RNG state to 1 (`all.c:387843–387884`). `0047c7d0` time-seeding and `0047c7f0` explicit seeding both reach the same seed routine. [HYPOTHESIS] multiple native threads that consume `_rand` have separate streams; cooperative VM threads sharing an OS thread may share one stream. This audit does not establish which stock consumers execute on which native thread. Numerous native `_rand` callers exist beyond `Math.random`; for example `all.c:96785` consumes and discards a value on the paint operation path. Hooking only the Script math native can miss stream advancement.

Music constructor **`00550be0`** directly initializes its index from `GetTickCount()%count` (`:252015–252017`), so the expression is separate from `_rand`. Directory helpers **`00559340`** and **`005593f0`** call `FindFirstFileA`/`FindNextFileA` (`:260780–260870`), which supply the order used by playlist recursion **`00550c60`**. Candidate controls: record/replay enumeration results and tick-count values from the initial launch; forcing lexicographic ordering would be a changed input unless the stock run used that order. The broad effect of filesystem order on all other loaders was not audited.

The Stock Executable also starts native workers: `CreateThread` for **`00554ea0`** at `all.c:257057` and **`00559bc0`** at `:261397`. The inspected worker bodies wait on synchronization objects and process queued/native virtual work (`:257068–257165`, `:261413–261434`). Their complete data ownership, readiness and completion ordering remain unresolved. [HYPOTHESIS] deterministic clocks and input may still leave timing-dependent worker completion; API hooks alone are not demonstrated sufficient.

### Research decision and verification limits

First observe a frame ledger: input poll/sample → `00428450` dt/split sequence → successful `004a5190` returns → timers/cleanup → Script dispatch/yields → animation delta → resource work/present, with native thread/caller IDs for RNG/clock events. Then compare repeated launches under unchanged conditions and under one explicit control at a time. Any controlled run must record its instrumentation and altered inputs; it is not an untouched-environment capture. No repeated Stock Executable or Port determinism result follows from this static analysis.

Jev claim checks were performed against focused verbatim `all.c` excerpts. The composite step mapping received support 0.91/confidence 0.86; narrower main-loop ordering, input-QPC, TLS-state and tick-count-expression claims were accepted automatically (support 0.88, 0.97, 0.88 and 0.96). Broader scheduler/budget interpretation remained **review**, including a narrow combined check with support 0.43/confidence 0.14 despite visible clock comparisons. Its consequence is therefore retained above as a hypothesis requiring disassembly/live verification, not claimed as a proven control intervention. No contradictory live evidence was collected; no intervention was tested.
