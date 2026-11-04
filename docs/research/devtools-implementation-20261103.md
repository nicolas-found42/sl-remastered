# Developer tooling improvements — 2026-11-03

All three research passes finished before implementation began. Their primary sources and repository findings are retained in [native/CMake research](devtools-cpp-build-20261103.md), [Python/shell research](devtools-python-shell-20261103.md) and [game/CI research](devtools-game-ci-20261103.md). The scope is developer infrastructure; the Full Port implementation goal remains paused.

## Selected changes

| Gap | Implemented response | Boundary |
| --- | --- | --- |
| Seven prior source checkpoints bypassed whole-file formatting | Staged/committed C++ edits use edited ranges; new files use full formatting | Clang-format may expand a range to an enclosing construct. Explicit working-path checks cover the full file. Checks never rewrite or stage files. |
| Python syntax alone misses runtime name errors; shell was unchecked | Pinned Ruff error rules and ShellCheck with selected-byte shell dialect | No import sorting, bulk Python rewrite, shell execution, Docker build or new test framework. |
| Hook required configured C++ compilation context | Fast staged hook and separate explicit deep analysis | Deep analysis requires exact real compile commands and compiler-discovered header dependencies, including distinct command variants. Mixed selected/working dependencies refuse. |
| Formatting could affect aliases or partial staging | Explicit formatter refuses conflicting staged/working bytes, symlinks and hard links | Working-file writes are opt-in; protected reference bytes and index bytes remain unchanged. |
| Setup and editor/database conventions disagreed | Brewfile, pinned Python requirements, tool policy, read-only doctor, pooled configure lane, canonical Release database and clangd config | Brewfile is an inventory, not a lockfile. Native compiler/SDK versions are recorded, not silently replaced by lint LLVM. |
| Public CI never built C++ and used a retired image | Supported macOS 15 ARM64, pinned actions, hosted-only cancellation, explicit asset-free native target and separate checks | Four audited tests, serial execution, no private assets or artifact uploads. The complete private gate remains separate. |
| Release could compile assertions away in some tests | Shared CMake test assertion policy and a behavioral liveness check | Production player Release semantics remain unchanged; newly exposed failures must be reported. |
| Sanitized test executables did not instrument shared engine code | Separate opt-in sanitizer tree instruments libraries and consumers | Not parity, performance or signed-release evidence. Numerical grouping and contraction control stay intact. |

The new commands live in the existing [developer entry point](../../tools/parity/dev.py), not a parallel task framework: `setup`, `doctor`, `configure`, `lint`, `format` and `portable`. The [README](../../README.md) and [command guide](../../tools/parity/agent-tools.md) give exact usage.

Tool pins match the existing qualified environment: [Ruff 0.17.0](https://pypi.org/project/ruff/0.17.0/) and [Pillow 13.0.0](https://pypi.org/project/pillow/13.0.0/), with Python 3.11+ and Python 3.13 selected by CI. LLVM major and ShellCheck are checked by [the tool policy](../../tools/devtools.json). Formatting and lint use one LLVM installation; native builds retain their own compiler. The CMake 3.20 manual already documents [no-tests refusal](https://cmake.org/cmake/help/v3.20/manual/ctest.1.html), so the public lane does not need a hidden minimum-version increase. JUnit output was deferred rather than requiring its newer CMake feature incidentally.

## Verification and review

Real-tool scratch Git tests cover index/HEAD/working separation, paths with spaces, incremental format debt, renames/deletions, protected-path deletion, shell diagnostics, missing/wrong tools, configuration mismatch, partial staging, symlink/hardlink preservation, compile variants and real header dependencies. The hardlink regression failed before its fix. Independent standards/specification review also caught missing default sanitizer flags on the new assertion test; that target uses the established test policy after correction.

A fresh source snapshot excluding both `ref/` and `local/` configured, built and passed all four portable C++ tests. Release and separate sanitizer configurations passed the same slice. Actual temporary negative controls showed disabled assertions fail and UB inside an instrumented static library aborts. These are infrastructure checks, not game parity. The complete integration gate and final tooling result are recorded separately after the final changes; a red gate must retain failed and unexecuted stages.

## Deferred work

- Broad typing migration, a new Python/package/test framework, repository-wide formatting and extra JavaScript tooling have no demonstrated need in this C++/Python stack.
- Full source formatting and broader static-analysis rules should be rolled out from measured findings, without weakening Stock-derived numerical behavior or rewriting retained fixtures.
- Docker/Wine image upgrades, protocol-2 rollout, capture containment, representative game performance, paired Journeys, signing/notarization and clean installation remain their existing acceptance tasks.
- Local workflow validation does not establish that GitHub Actions ran successfully. No private runner, credentials, release service or public game release was changed by this pass.
