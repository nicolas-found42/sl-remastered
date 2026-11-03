# Physics failures on the workflow baseline

Both full-gate physics failures reproduce on exact committed HEAD `ceaa48df09ed157e6517ce131236b9101c4721a5`, independently of the pending audio, rendering, startup, and proof-workflow changes. This establishes baseline failure status on this machine and installed dataset; it does not establish the underlying Stock Parity error or the correct fix.

## Source and build isolation

On 2026-11-01 a fresh `git archive` of that commit was extracted into an owned ignored directory. The working tree was not reset, checked out, or modified. The archive SHA-256 is `e96dadf8bc5d5ef511394eafe04541f82d8cde710cfd258756a6bf50a2a2c4f3`.

Artifact root: `.scratch/workflow-physics-baseline-20261101T215004Z-546b1d`. Its `manifest.json` records the exact source, build, commit, environment, and installed dataset paths. `working-tree-status.txt` and `working-tree-diff.patch` preserve the pending-change state before the experiment.

The archived source has only one external symlink: its `.scratch/2005-extract` points to the existing Installed Game. The selected tests read that data; no canonical `ref/` files were modified. Five selected pack/config hashes are recorded in `selected-dataset-hashes.json`.

A source/flags audit covers all 116 translation units belonging to `sl_data`, `car_runtime_test`, and `object_world_step_test`. All compiler commands match the current tree after replacing the absolute source-root path. Only `src/audio.cpp` and `src/render2d.cpp` differ among those translation-unit source files; both tests and all physics translation units match HEAD. Separately, `source-hashes.json` records seven focused test/physics files plus CMake, and `all-baseline-target-source-audit.json` records the complete target translation-unit audit. Header changes are preserved in the overall working-tree diff; the current audio/render headers also differ, while the archived build uses exact HEAD headers.

Both test compile commands contain `-O3 -DNDEBUG -std=c++20 -arch arm64 -Wall -Wextra -Wshorten-64-to-32 -ffp-contract=off -UNDEBUG`. Assertions therefore remain active. The configured compiler is AppleClang 21.0.0.21000334. These exact targets use the existing repository flags; no sanitizer or floating-point flag was changed.

## Commands and results

The baseline was configured with `cmake -S <artifact>/head-source -B <artifact>/head-build -G Ninja -DCMAKE_BUILD_TYPE=Release`. Configuration succeeded. Only the two requested targets and their shared library dependencies were built:

```text
cmake --build <artifact>/head-build --target car_runtime_test object_world_step_test --parallel 3
119 build actions; exit 0; elapsed 25.11 seconds
```

The complete command and build output are retained in `build-command.json`, `configure.log`, `build.log`, and `build-result.json`. No writer used the existing shared `build/` directory during this experiment.

Each baseline binary ran twice from `<artifact>/head-source` with `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, and `SLRR_PROBE_BACKGROUND=1`. Both trials returned signal-abort (`subprocess` return code `-6`) with the same assertions and telemetry as the current binaries:

| Test | Exact failure on HEAD and current working tree |
| --- | --- |
| `car_runtime_test` | `car_runtime_test.cpp:330`: `physics::updateSuspension(u) == physics::SuspensionUpdateResult::Active` |
| `object_world_step_test` | `object_world_step_test.cpp:232`: `(boxRoot->flags & physics::kBodyAsleep) != 0` |

The object test reports these same values on current and both HEAD trials:

```text
box: lowestY=0.283 finalY=0.293 vel=0.124 pairs=6
box rest gate: omegaSq=1.53189 scaledTorqueSq=0 scaledActivitySq=17.3394 flags=0x9
box support: lastForce=(-21.2912,142.143,71.9434) omega=(-1.14787,-0.000994247,-0.462917) activity=(-1.95864,-83.1387,4.45838)
```

The direct-trial evidence is retained in `current-results.json`, `head-results.json`, and individual `*-current.log` / `*-head-trial1.log` / `*-head-trial2.log` files. `head-results.json` also records each baseline binary's SHA-256.

The normal CTest registration was then exercised independently:

```text
ctest --test-dir <artifact>/head-build -R '^(car_runtime_unit|object_world_step_unit)$' --output-on-failure
0% tests passed, 2 tests failed out of 2
Total Test time (real) = 0.35 sec
exit 8
```

Complete output: `ctest-head.log` and `ctest-head-result.json`. No GUI or audible output was used.

## Meaning and remaining work

The workflow edits are **not necessary** for either observed failure: the exact committed baseline reproduces both. This is stronger than inferring safety from unchanged physics files, and does not claim the workflow patch has no other possible interactions. Neither a physics behavior change nor a relaxed assertion was applied in this experiment. Investigating the correct fix still requires the Stock Decompile / stock-script evidence and a focused red-green loop for each failure. The full integration gate remains failed until those failures are resolved.

Jev verified the baseline reproduction claims and the corrected complete target/flag comparison, with no unresolved contradictions. An initial audit indexed multiply-compiled files by path without target identity and incorrectly selected other targets' commands; the corrected audit filters to the three exact targets before matching and reports zero command differences.
