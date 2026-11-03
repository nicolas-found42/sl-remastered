# Iteration efficiency and bounded concurrency

Research/design note, 2026-11-01. This proposes how to find the highest useful throughput on this machine; it does not approve a production scheduler or claim a measured full-game concurrency limit. This audit inspected local tools, configuration, manifests, retained timing evidence and official documentation. It did not launch the Port or Wine, install packages, change runtime settings, or run the full gate. A separate concurrent audit supplied the new CPU-test sweep and bounded Docker graphics smoke test described below; their records were inspected directly.

## Machine and available execution paths

The user reports an M5 Pro with 18 CPU cores, 48 GiB unified memory and 20 GPU cores. Local `sysctl` confirms 18 logical CPUs and 51,539,607,552 bytes RAM. `df -h .` reports 146 GiB available; the containing Data volume is APFS. Neither GPU core count nor RAM capacity predicts how many complete game Runs fit.

| Component | Observed local state | Implication |
|---|---|---|
| Build tools | CMake 4.4.4, Ninja 1.13.2, Apple clang 21.0.0; arm64 compiler target | Existing incremental native toolchain is available. |
| Compiler caches | `ccache` and `sccache` absent from PATH | Compiler caching is a proposal, not an existing speedup. No installation performed. |
| Native Wine | Absent from PATH; launcher default `local/wine-runs/winehq-source-20261028/build/wine` exists as a wrapper symlink. Its `loader/wine` is Mach-O x86_64. Separate build-x11 also exists. | Discover configured runtime paths, not just PATH. This is a macOS-hosted x86_64 Wine path; do not describe it as arm64 execution or assume backend equivalence. |
| Docker | Initially unreachable. After the user opened Docker, `docker info` reports Linux/aarch64, 18 vCPUs, 8,317,267,968 bytes total VM RAM (7.75 GiB); CLI/server 29.8.2. | Current Wine-container budget is constrained by the VM's 7.75 GiB, not the host's 48 GiB. |
| Images | Existing `slrr-stock-wine-oracle`, `-gl`, `-diagnostics`, `-composite`, `-local` and `slrr-wine-zink-20261028`; no running containers at inventory time | No pull/rebuild needed merely to establish image presence. Image size is not runtime memory cost. |
| Other VM CLIs | `podman`, `colima`, `orb`, `lima`, `prlctl` absent from PATH | No alternative runtime was established by this inventory. |
| Storage | `build/` 1.5 GiB, including 911 MiB `CMakeFiles` and 167 MiB `libsl_data.a`; `build/preset-release` only 2.7 MiB; `ref/` 3.1 GiB; each inspected Wine source-build directory about 3.2 GiB | Reuse existing outputs; do not multiply full build/game/Wine trees per worker. `du` is not a reliable unique-block accounting method for clones. |

An early memory snapshot already had approximately 2.0 GiB swap used. This is an observation, not proof of present pressure or attributable to this task. Admission should use pressure and **growth relative to baseline**, not assume all 48 GiB is free, and should count host CPU/GPU unified-memory use and Docker's actual allocation together.

## Existing scheduling and the short-test measurement

[`CMakePresets.json`](../../CMakePresets.json) sets Ninja compile/link pools to 6/2 in `build/preset-release` and a separate Debug directory. [`tools/check.sh`](../../tools/check.sh) configures `build/` directly and does not select either preset. Its current cache contains no job-pool settings, and `CMakeLists.txt` supplies none. Thus the preset's pool limits do not bound the normal full gate. Release and sanitizer-related flags remain part of the existing test setup; do not remove numerics flags or sanitizer checks to win throughput.

CMake Ninja pools cap jobs assigned to a named pool; global and per-target compile/link assignments are available. A pool is local to that build invocation, so several independent `ninja -j 12` processes can still oversubscribe one host. [CMake JOB_POOLS](https://cmake.org/cmake/help/latest/prop_gbl/JOB_POOLS.html). Ninja normally schedules parallel commands automatically and supports explicit `-j`; current Ninja 1.13 can also participate in a compatible GNU jobserver under documented conditions. The installed manual was read at `/opt/homebrew/share/doc/ninja/manual.asciidoc`; see [Ninja manual](https://ninja-build.org/manual.html). Do not assume unrelated Python launchers or containers share those tokens.

The CTest registrations have no `PROCESSORS`, `RESOURCE_GROUPS`, `RESOURCE_LOCK` or `RUN_SERIAL` declarations. Some smoke tests explicitly use SDL dummy drivers, while `startup_probe_unit` has a build-local recorder root. Their presence is not an audit that every test can safely share all writable state.

The new bounded sweep exercised the same 15 pure CPU physics/script tests at seven CTest job ceilings, three repetitions each: 315 test executions, all passing. Results/logs: [`local/verify/headless-capacity-503sad4q/results.json`](../../local/verify/headless-capacity-503sad4q/results.json). These are warm, tiny tests, not complete game instances or the full gate.

| CTest ceiling | Median batch seconds | Observed range seconds |
|---:|---:|---:|
| 1 | 0.1302 | 0.1238–0.2065 |
| 2 | 0.0725 | 0.0709–0.0775 |
| 4 | 0.0430 | 0.0427–0.0451 |
| 6 | 0.0350 | 0.0338–0.0373 |
| 8 | 0.0351 | 0.0334–0.0365 |
| 12 | 0.0276 | 0.0254–0.0397 |
| 18 | 0.0262 | 0.0254–0.1088 |

Ceiling 18 can admit at most 15 tests in this batch; it does not demonstrate 18 concurrent workers. The 12-to-18 median improvement is about 5%, while the slowest ceiling-18 repetition is about 4.15 times its median. Three subsecond repetitions are insufficient for stable capacity or tail-latency estimates. **Proposal:** use a ceiling of 12 for this audited short-test subset during interactive work; retain 18 as an experiment for a longer isolated workload. This recommendation does not cap future qualified pools at 12.

CTest supports explicit bounded `-j N`; `-j 0` can be unbounded. `PROCESSORS` accounts for process slots, not OS CPU quotas. Resource groups require both a resource specification and participating tests; `RESOURCE_LOCK` excludes tests sharing a named lock, and `RUN_SERIAL` excludes other tests in that CTest invocation. These mechanisms do not arbitrate separate independent CTest processes. [CTest manual](https://cmake.org/cmake/help/latest/manual/ctest.1.html), [PROCESSORS](https://cmake.org/cmake/help/latest/prop_test/PROCESSORS.html), [RESOURCE_GROUPS](https://cmake.org/cmake/help/latest/prop_test/RESOURCE_GROUPS.html), [RESOURCE_LOCK](https://cmake.org/cmake/help/latest/prop_test/RESOURCE_LOCK.html), [RUN_SERIAL](https://cmake.org/cmake/help/latest/prop_test/RUN_SERIAL.html). Property semantics were also checked against installed CMake help.

## Proposed fast lanes

| Lane | Work admitted | Initial design budget and escalation |
|---|---|---|
| Edit feedback | Build the affected target/dependencies, then its real module tests and relevant Python tooling tests | One build-tree writer; begin compile 6/link 2 as existing presets, compare 6/8/12 only after measuring compiler/link RSS. Use the measured ceiling-12 CPU subset where relevant. |
| Deterministic correctness | Independent narrow Stock helper comparisons, corpus cases, then isolated scenario Runs once repeatability is qualified | Share immutable inputs; each worker has private save, output, prefix/server/display context. Start full native scenarios at 1/2/4 workers and Wine at 1/2; then 4/6/8/etc only if admission limits and exact outcome checks hold. These starts are proposals, not measured capacities. |
| Performance | A fixed workload, declared cold/warm policy, same binary/data/backend/instrumentation; separate measurements for readiness and steady state | Begin with one game Run and stop builds/correctness batches. Multi-instance throughput is a separate benchmark, with per-instance latency and failure rate reported. |
| Integration gate | The complete `tools/check.sh` contract | Run after relevant changes; targeted green tests cannot replace it. Add scheduling only after the whole suite's isolation contract is audited. |

The existing [rapid/cold benchmark](../../tools/parity/rapid-benchmark-evidence.md) reports median total 6.147 s versus 19.410 s for the same short throttle workload with visible 640×480 rendering. Its faster route avoids legacy fixed waits by using observed readiness; preparation remains about 0.43 s. This supports reducing redundant setup, not a headless throughput estimate or tick-exact execution. The [baseline audit](parity-test-baseline.md) explains unresolved logical-time, random-call-order and shared-input seams. Do not let load-induced wall-clock differences masquerade as physics differences.

`run_native.py` currently uses hidden, non-focusable rendered windows by default, with run-local writable data/save/output, but also bypasses native intros. That route needs an explicit feature contract before it can satisfy a request for full functionality. A dummy video or audio backend must not silently stand in for full renderer/audio correctness. Invisible presentation, full feature coverage, and deterministic scheduling need separate acceptance evidence.

## One adaptive budget, including child threads

Use one admission controller across build jobs, native Runs and Docker/Wine processes. This is a design requirement, not implemented here. Each workload class should supply its measured CPU-thread demand, peak resident/private memory, unified GPU memory demand, output-growth bound and resources requiring exclusive use. Count process trees: a container is not one CPU worker, and llvmpipe, Wine services, decoders and compression can each spawn threads.

For homogeneous class `w`, a proposed admission bound is:

```text
n(w) <= min(
  queued cases,
  floor(available CPU tokens / measured runnable-thread demand(w)),
  floor(available host memory budget / measured peak incremental memory(w)),
  floor(available VM memory budget / measured peak VM memory(w)),  # containers
  qualified renderer/display/backend slots,
  floor(available output budget / worst-case output bytes(w))
)
```

For mixed work, enforce summed demands rather than computing separate maxima and adding them. A **provisional interactive envelope** is 12 CPU tokens, preserving room for the desktop and agent; actual available tokens can be lower. No fixed host/VM RAM envelope is justified before measuring per-class incremental peaks. Current Docker VM RAM makes reserving 2 GiB for its kernel/services and leaving approximately 5.75 GiB for candidate containers a conservative starting *experiment*, not proof two games fit. Host headroom must independently pass. Renderer slots remain unqualified until complete offscreen execution is proven.

At every increase, run enough real cases/repetitions to amortize launch noise. Record total accepted cases/second, p50/p95 latency, timeout/failure counts, first deterministic divergence, process-tree CPU time and peak memory, renderer queue behavior, host/VM pressure and swap growth, disk writes and bytes retained. Suggested decision rule: keep the **smallest** count within 5% of the best stable throughput while satisfying all correctness/feature and latency limits. The 5% threshold is a proposed policy, not a measured optimum.

Raise concurrency one step only when all declared contracts pass. Stop admitting on rising pressure/swap, growing queues, persistent CPU saturation without throughput gain, output budget exhaustion or any new mismatch; let current bounded Runs finish, retain failure evidence, and return to the last qualified count. Resource exhaustion is a capacity failure; a deterministic mismatch is a correctness failure. An isolated benchmark must not adapt its job count halfway through the reported sample.

## Docker and Apple Silicon caveats

Docker Desktop imposes its own VM CPU, memory, swap and disk budgets. These are independent of host capacity; its documented memory default is 50%, but this machine's observed 7.75 GiB is the relevant value. Rosetta acceleration is an option associated with the Apple Virtualization framework; do not infer that it is enabled here. Docker documents host-file-sharing overhead and recommends Linux VM volumes for suitable non-code caches. Record any staging location and copied input identity before comparing it with bind-mounted work. [Docker settings](https://docs.docker.com/desktop/settings-and-maintenance/settings/).

The repository's Docker paths request `linux/amd64`, whereas this VM is aarch64. Docker says nonmatching architectures require emulation, which can substantially slow compute-intensive tasks. That is an execution-cost caveat, not evidence that this workload fails. Backend/translation differences must remain explicit in numerical Parity and performance records. [Multi-platform builds](https://docs.docker.com/build/building/multi-platform/). Existing [`run_original_oracle.sh`](../../tools/wine/run_original_oracle.sh) already warns that Docker results may differ from native macOS Wine.

[`run_stock_synthetic.sh`](../../tools/wine/run_stock_synthetic.sh) uses software rendering and defaults `LP_NUM_THREADS` to 0, with no explicit Docker CPU/memory limits in the script. Before multiplying it, measure its actual renderer thread count and qualify a bounded setting. Do not assume 18 containers means 18 runnable threads. Image startup, fresh Wine initialization and complete game launch should be separate timing categories.

A fresh bounded virtual-display/OpenGL smoke test ran existing `slrr-stock-wine-gl:latest` under `linux/amd64`, network disabled, 2 CPUs, 1 GiB memory, Xvfb 800×600×24 and `glxinfo -B`. It timed out after 45.006 s; record: [`docker-virtual-gl.json`](../../local/verify/headless-capacity-503sad4q/docker-virtual-gl.json). This does not establish graphics availability under that bound or prove that all configurations fail. No game was launched by that test, and it supplies no qualified full-game renderer-pool capacity.

## Reuse inputs and outputs without weakening evidence

1. **Keep one incremental build tree per build configuration/toolchain**, serialize writers, and reuse its dependency graph. Debug, Release and sanitizer/flag variants need distinct identities. Source snapshots used for a batch must remain stable; do not rebuild an executable while that same path is being launched as batch input.
2. **Consider an optional compiler cache later.** CMake exposes `CMAKE_<LANG>_COMPILER_LAUNCHER`; no launcher is configured here. Ccache keys compilation inputs, checks compiler identity and validates cached integrity; its `sloppiness` options relax correctness checks. Keep such relaxations off for this project, preserve `-ffp-contract=off`, sanitizer and optimization variants, and verify representative cache-hit versus uncached results before adopting it. Path rewriting has dependency-file caveats; use it only with validation. [Ccache manual](https://ccache.dev/manual/latest.html), [CMake compiler launcher](https://cmake.org/cmake/help/latest/variable/CMAKE_LANG_COMPILER_LAUNCHER.html).
3. **Prepare a verified immutable batch input once, clone only writable fixtures.** APFS supports copy-on-write clones. Installed `man clonefile` says writes to either file remain private and later writes can still fail with ENOSPC; installed `man cp` documents `cp -c` with fallback to an ordinary copy. The existing native Wine launcher already uses `cp -c -R` for prefix seeding. Stop all Wine users of a seed prefix before cloning and give each concurrent Run its own prefix; never clone a live server's mutable state. Do not use hard links for writable game/save/prefix data. [Apple APFS guide](https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/APFS_Guide/Features/Features.html) is an archived overview; current local man pages supplied exact syscall/CLI behavior.
4. **Cache identities, not assumptions.** [`diagnostic-data.py`](../../tools/parity/diagnostic-data.py) hashes a bounded tree with per-file/directory mutation checks; it explicitly is not an atomic snapshot or file-access trace. [`run_native.py`](../../tools/parity/run_native.py) currently repeats that manifest per Run; [`save_snapshot.py`](../../tools/parity/save_snapshot.py) verifies source/copy/source equality. A future optimization can share one content-addressed, immutable prepared game manifest across a batch and record its digest in every Run. It must not replace identity with mtime alone or mark an incomplete manifest complete. Revalidate mutable sources before admission and preserve per-Run save/output isolation.
5. **Retain provenance for reused results.** [`src_hash.py`](../../tools/src_hash.py) covers source/tools/tests and `CMakeLists.txt`, but not `CMakePresets.json`, compiler/SDK contents, external libraries, game data, scripts' selected environment or render backend as a complete cache key. Include those identities, executable/helper hashes, initial-save hash, timeout policy, random/time/input contract, instrumentation and comparator version. Cache hit means a result for that declared identity; historical success is not a fresh execution. Benchmark records must label input/cache warmth and never substitute cached results for timing samples.
6. **Bound output growth and warmups.** Estimate from pilot Runs, reserve capacity for crash/timeout records, deduplicate immutable blobs by hash, and give each active Run an explicit byte/time limit. The 146 GiB free figure is shared free space, not a license to reserve it all. APFS clone savings can shrink as prefixes/saves/logs diverge; keep an explicit free-space floor and retention policy. Warm stable game data once if the question is warm iteration, and retain separate cold-cache measurements when that is the question.

## Next evidence required

The short CPU subset is the only new measured concurrency result in this note. A full-game maximum remains unknown. Before setting a default game pool: prove invisible feature-complete execution for each backend; identify all writable paths and per-worker Wine/display/audio ownership; measure one complete Run's process-tree footprint; execute bounded 1/2/4/... sweeps against identical scenarios; compare outcomes and actual simulated work; then select a stable throughput/latency point under host **and** VM limits. An 18-core label alone cannot answer that question.

External source screening used Jev. Docker pages were flagged for inspection at injection probabilities 0.36/0.43; inspected passages were ordinary documentation/UI/navigation and executable examples, with no unresolved instruction overriding this task. Only separable reference facts were used. No source instructions to run examples, install components or change settings were followed.
