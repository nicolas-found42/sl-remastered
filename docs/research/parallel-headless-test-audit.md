# Parallel headless test audit

Audit date: 2026-11-01. Target host supplied by the user: M5 Pro, 18 CPU cores,
48 GiB RAM, 20 GPU cores, macOS 27.0.1, about 146 GiB free. This is an audit and
benchmark proposal, not a production change. No game, GUI, Wine, full test suite,
or window was launched by this audit. The parent task ran the bounded CPU subset
recorded below. Repository files were changing in other chats during inspection;
refresh inventories and build before interpreting any timing.

## Recommendation

Parallelize independent CPU tests and read-only corpus readers first. Use one
CTest scheduler per build tree and a bounded job count. Keep native rendered Runs
and Wine captures in separate lanes until their presentation and isolation are
verified. A hidden native window is a useful no-focus route, but still requires
Cocoa/SDL video and is not a display-server-free test.

The parent task measured the 15-test CPU allowlist below at job limits 1, 2, 4, 6,
8, 12, and 18, three repetitions each. All 315 individual test executions passed.
Median wall times were respectively 0.1302, 0.0725, 0.0430, 0.0350, 0.0351,
0.0276, and 0.0262 seconds. At limit 18 there can be at most 15 simultaneous tests;
its third repetition took 0.10885 seconds. These tiny measurements support
parallel CPU scheduling but do not determine a full-gate optimum. Evidence:
`local/verify/headless-capacity-503sad4q/results.json` and its 21 CTest logs.
Choose the smallest count close to the fastest median for each representative
family, stopping if memory pressure, swap, failures, or desktop interference
appear.

## Observed build and test topology

Read-only commands: `ctest --test-dir build --show-only=json-v1`, the same for
`build/preset-release`, source reads, cache/compile-command inspection, CMake/Ninja
version queries. Installed versions observed: CMake 4.4.4 and Ninja 1.13.2.

| Fact at inspection | Evidence |
| --- | --- |
| Root `build` registers 123 CTest tests; `build/preset-release` registers 116 | Parsed CTest JSON; this difference demonstrates that the trees are not interchangeable |
| Every test has a working directory; neither tree has labels, explicit timeouts, `PROCESSORS`, `RUN_SERIAL`, resource locks/groups, or fixtures | Parsed CTest JSON properties; focused search in `CMakeLists.txt` |
| Only five tests in each inventory have an explicit environment | SDL dummy smoke tests, `sfx_unit`, and `startup_probe_unit` |
| Root build is Release/Ninja, has no configured pools or compiler launcher | `build/CMakeCache.txt`, `build/CMakeFiles/rules.ninja` |
| Preset Release caps compile jobs at 6 and link jobs at 2 | `CMakePresets.json:17`, generated preset rules/cache |
| `tools/check.sh` does not use those presets | It configures `build` directly and calls `cmake --build build` |
| The full gate executes CTest then 27 top-level Python invocations, comparisons, PTX corpus checks, Phase 2 corpus checks, and an architecture check sequentially | `tools/check.sh`; 27 includes readers/audits, not just unit-test scripts |
| Root compile database has 1,043 entries, including 114 `sl_data` translation units and 114 test translation units | Parsed `build/compile_commands.json`; generated inventory, not executed compilation |
| None of those 114 `sl_data` units have sanitizer flags; 56 of the 114 test units do | Same compile database; target-local sanitizer flags do not instrument linked library code |
| Preset database has 1,003 entries, 111 `sl_data` units (none sanitized), and 107 test units (52 sanitized) | Parsed preset compile database |

`cmake --build ... --parallel N` and CTest `-j N` control different stages.
Preset compile pools still cap assigned compilation at six even if the outer
build limit is 18. Ninja pools are independent named limits for assigned rules;
they do not govern Python checks or CTest. See the official
[CMake 4.4 JOB_POOLS documentation](https://cmake.org/cmake/help/v4.4/prop_gbl/JOB_POOLS.html).

The root build compiles large source lists repeatedly for standalone sanitized
targets (notably the native/VM sources) in addition to `sl_data`. This presents a
potential build-time saving, but consolidating sanitizer-compatible object
libraries is a code change and needs its own design/verification. Keep Release,
Debug, and any fully instrumented sanitizer build in distinct binary directories;
do not race two configurations or two Ninja processes against one directory.
Compiler caches are a separate optimization from CTest concurrency; no launcher
is configured in the inspected caches.

## Safe families and exclusions

The strongest vetted initial allowlist contains 15 CTest names:

```text
physics_body_unit
physics_box_ray_unit
physics_compound_unit
physics_compound_mass_unit
physics_polyhedron_unit
physics_solver_unit
physics_suspension_unit
physics_core_pair_unit
physics_cached_unit
physics_friction_unit
physics_compound_step_unit
physics_contact_geometry_unit
car_engine_unit
wheel_tire_force_unit
probe_script_unit
```

Their CMake targets compile explicit CPU sources, do not link `sl_data` or SDL,
and test/source inspection found no window creation, process launch, sockets,
or writable filesystem operations. Some physics sources call header-only runlog
hooks without a recorder. Their globals and static diagnostics are process-local;
separate executables do not share these in-memory values. This is static evidence
of suitability for a first bounded benchmark, not proof of full-suite safety.

| Family | Headless/parallel assessment | Next action |
| --- | --- | --- |
| Above CPU allowlist | Suitable for bounded no-window benchmark | Build once; run one CTest invocation per measurement |
| `native_arm64`, `rpk_corpus`, `scx_corpus`, `scy_corpus`, `class_corpus`, `sdat_corpus`, PTX corpus readers | `src/main.cpp` census/selftest paths parse data and return without creating a platform window; corpus inputs are read-only | Useful second benchmark after matching binaries to current source; benchmark I/O contention separately |
| Phase 2 C++/Python readers | `tools/phase2_check.sh` creates a private `mktemp -d`; per-format outputs have different labels | Run independent format pairs concurrently inside that private scratch directory, then aggregate; retain expected-stale handling and all comparisons |
| Python parsers/audit fixtures and parity helper unit tests | Many use `TemporaryDirectory`, synthetic inputs, fake executables or mocked subprocesses | Vet the exact command list, then use a bounded process pool; separate real probe tests below |
| SDL surface-only tests | A surface renderer can operate without a real window; linking SDL alone does not prove activation | Audit initialization; assign dummy video/audio explicitly where compatible before admitting to no-focus lane |
| `platform_unit` | Selects dummy video internally unless explicitly invoked with `--visible` | Safe candidate for the dummy-driver lane; never add `--visible` |
| `ptx_view_smoke`, `rpk_texture_view_smoke`, `loading_view_smoke` | CTest supplies `SDL_VIDEODRIVER=dummy` and short tool timeouts | Retain dummy environment if moved to another runner |
| Native background integration (`native_background_test.py`, `run_log_test.py`, `run_screenshot_test.py`) | Real renderer/game runs, private temporary roots, explicit background/no-intro route | Treat as Cocoa-backed integration, not CPU headless; verify no activation and resource use before concurrent runs |
| Wine numerical helpers | Usually independent output directories and prefixes; helpers load the Stock Executable | Review individual helper loading/initialization and explicit output/prefix isolation before running; no blanket headless approval |
| Native Wine stock capture | Starts macOS Wine desktop/game presentation | Excluded from this no-focus benchmark |
| Docker synthetic stock capture | Container-local Xvfb can avoid host windows, but default software-renderer thread count is uncapped | Separate opt-in lane with unique run/container, bounded Docker CPU/RAM and `LP_NUM_THREADS`; do not assume parity with native Wine |

`tools/native_audit.py` itself can launch configured native inventory/VM test
programs, so a Python suffix is not a sufficient headless classification.
`tests/startup_constructor_test.py` launches the actual probe with constructor/init
paths; its `--no-input` variation and private data root do not establish that every
other probe route avoids video initialization.

## Shared writable state and hidden presentation risks

1. `tests/raster3d_test.cpp:793` uses the fixed temporary filename
   `raster3d_test.bmp`. Running the same test from two build trees can corrupt or
   remove the other's file. `tests/music_test.cpp:322` forms directories from tag
   plus default `std::rand()` output and deletes the directory first; repeated
   simultaneous executions can collide. Replace these with an atomically created
   private directory, or serialize them across independent runners until changed.
   Different test names in a single CTest invocation do not currently share these
   particular filenames; this is chiefly a cross-invocation hazard.
2. Many filesystem tests already use `mkdtemp` (`native_file`, `game_file`,
   `file_state`, `vm`, `resource_world`, `pack_registry`, `class_environment`,
   `ground_files`), and the resource texture test atomically creates its temporary
   directory. `run_log_test.cpp` adds the PID to its temporary root. These are
   preferable patterns, but they do not imply their dependencies are headless.
3. `src/run_log.cpp:1211` creates Run names from UTC time plus PID and atomically
   creates directories with suffix retries. However all recorders using one base
   remove/recreate its `latest` symlink (`src/run_log.cpp:1403`). Consumers must use
   their explicit Run paths. Give each worker its own `SLRR_RUN_DIR`; reading a
   shared `latest` is nondeterministic under concurrency. CTest directs its single
   startup probe to `${CMAKE_BINARY_DIR}/test-runs`; overlapping CTest runs still
   share that base and CTest's own `Testing/Temporary` state.
4. `tools/parity/run_native.py` creates a fresh output, snapshots the save into a
   private writable data root, uses private recorder output, and runs with that
   data directory as cwd. It validates custom screenshot commands as `@run/`
   paths. Preserve this wrapper and unique output per attempt instead of launching
   `startup_probe` against a common writable tree. Its default background route
   still uses a hidden native window and renderer, so GPU/WindowServer load remains.
5. Current `tools/check.sh` exports `SLRR_PROBE_BACKGROUND=1` by default. Current
   `tools/startup_probe.cpp:144` sets `SDL_HINT_MAC_BACKGROUND_APP` and disables
   activate-on-show/raise *before* SDL video initialization for the background
   route. The code explicitly notes that Cocoa can activate before a window exists.
   `tests/native_background_test.py` checks logged hidden=1/fullscreen=0/inputFocus=0
   and a screenshot; those flags do not independently observe foreground ownership.
   A changed source file does not prove the already-built executable has these
   protections. Verify source/build identity before any rendered integration.
6. `force_render_unit` calls `SDL_Init(SDL_INIT_VIDEO)` without a dummy-driver
   property (`tests/force_render_test.cpp:16`). `resource_ref_set_unit` does the
   same in multiple blocks. Neither needs a host window for its surface-rendering
   assertions, but backend initialization can activate Cocoa. They are excluded
   from the initial allowlist. Add explicit dummy environment after confirming
   compatibility, and retain that environment in every runner.
7. No fixed TCP listener/socket use was found in the focused test-source search.
   This is a search observation, not proof that every linked dependency is free
   from network or platform-global state.
8. `tools/wine/oracle_new_dir.py` now makes an atomic unique directory for most
   numerical wrappers. `run_original_oracle.sh` makes unique attempt logs but
   reuses `$out/mac-prefix` or `$out/docker-prefix` for a supplied output directory.
   Never overlap attempts that reuse an output/prefix. Prefix creation checks are
   not a mutual-exclusion lock. `SLRR_ORACLE_PREFIX_SOURCE` must be a quiescent
   template. The controller bounds process groups and Docker container IDs; this
   controls termination, not presentation or prefix safety.
9. Native Wine capture makes a private writable game copy and prefix, but launches
   a Wine virtual desktop/window; its background shell process is not a no-focus
   guarantee. `tools/wine/capture/shot.sh` selects the first matching Wine window
   by owner substring, so concurrent captures can screenshot the wrong Run.
   Use run-specific PID/window identity before parallel native capture.
10. `tools/wine/capture/send.sh` appends to run-local `input.cmd`/`dinput.cmd`.
    One controller per Run is required; several writers can change input ordering.
    `stop.sh` scopes `wineserver -k` through the Run's `WINEPREFIX`; preserve that
    scope. Docker synthetic capture uses unique names and private prefixes, but
    image builds/tags are shared and its image entrypoint allows software-renderer
    worker threads; concurrent cold image builds and unconstrained rendering are
    separate bottlenecks. Shared corpus generation under
    `local/verify/core-pair-oracle/corpus-data` must finish before readers start.

## Scheduling changes worth making separately

An actionable starting configuration is compile pool 8, link pool 2, headless
CTest limit 8, and one native background Run at a time, then tune those independent
limits with representative measurements. The values are a conservative starting
proposal, not benchmarked build/render optima. The current compile pool 6 is also
reasonable until clean/incremental build measurements justify increasing it.

Add explicit labels such as `headless`, `dummy-sdl`, `native-background`,
`corpus`, and `wine` only after auditing the command/dependencies. Keep the CPU
allowlist positive so newly added tests do not silently become eligible.
Add per-test deadlines. Use `RESOURCE_LOCK` for a genuinely shared resource;
use `RUN_SERIAL` only for a test that must exclude every other test in the same
CTest run. Their semantics are documented by
[RESOURCE_LOCK](https://cmake.org/cmake/help/latest/prop_test/RESOURCE_LOCK.html)
and [RUN_SERIAL](https://cmake.org/cmake/help/latest/prop_test/RUN_SERIAL.html).
These scheduler properties are not host-wide locks between independent CTest
processes. Prefer private files/prefixes and a single scheduler over adding locks
around inherently separable work. Resource groups are appropriate only when
workers actually consume an explicit supplied GPU/device resource specification.

Move vetted Python checks into the same scheduling graph or implement a bounded
shell/Python runner that waits for every child and preserves each return code and
log. Preserve build-before-test, generation-before-compare, native inventory
validation, all corpus counts/digests, and the final architecture check. Phase 2
currently runs C++ then Python sequentially within each format; these readers and
different format pairs can overlap with private outputs. Do not let parallel
background shell failures disappear behind the last successful `wait`.

## Bounded representative benchmark

The parent may run the following against a freshly built, otherwise idle tree.
Only this positive allowlist is authorized by the static no-window evidence here:

```sh
ctest --test-dir build \
  -R '^(physics_(body|box_ray|compound|compound_mass|polyhedron|solver|suspension|core_pair|cached|friction|compound_step|contact_geometry)|car_engine|wheel_tire_force|probe_script)_unit$' \
  --output-on-failure --timeout 30 --no-tests=error -j 4
```

Use `--show-only=json-v1` with the same regex first and require exactly the 15
expected names. For a sweep, perform sequential complete CTest invocations at
1/2/4/6/8/12/18 jobs with three repetitions each, saving wall time, exit status,
CTest/JUnit output and `time -l` peak RSS/pageout statistics per invocation in a
fresh evidence directory. Peak RSS of a parent command alone is not the aggregate
memory of all descendants: also sample system memory pressure/swap. Do not run
the sweep concurrently with builds or other probes. These tiny CPU tests may be
dominated by sanitizer/process startup; that result cannot choose concurrency for
corpus readers, native rendering, full checks, or builds.

Next use the six read-only CTest census/selftest commands as a separate family,
then independently sweep vetted Python checks and build jobs (6/8/12/18 compile,
links initially capped at 2). For real native background Runs, start at one, then
two only after no-focus behavior and per-Run isolation are independently observed;
measure preparation and menu/driving time separately and preserve the Forced Seed,
save identity, executable/source identity, script and telemetry. For Wine, begin
with one vetted numerical helper, not a stock game capture. Never use `-j 0`:
CTest documents it as unbounded outside a job server in modern versions. See
the official [CTest command manual](https://cmake.org/cmake/help/latest/manual/ctest.1.html).

## Evidence limits

The required first behavioral query used `jg` with the explicit repository root,
`--max-requests 8 --max-output-bytes 24000 --concurrency 4`. It returned an explicit
request-limit/incomplete-discovery notice and four locations-only leads. Focused
source/CMake/CTest reads supplied the findings above; missing jg results were not
treated as absence of a hazard. No claim of exhaustive whole-program GUI safety
is made. Official CMake pages were screened using Jev (`pass`) before use. The
latest online manual was 4.5.0-rc2, so installed 4.4.4 capability and local help
should govern version-sensitive switches. No full gate was run because this
read-only audit does not yet establish every full-gate command as no-focus safe.
