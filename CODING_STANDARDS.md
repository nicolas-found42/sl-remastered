# Coding Standards

Rules for code in `src/`, `tests/` and `tools/`. Vocabulary comes from `GLOSSARY.md` (domain terms) and the `codebase-design` skill (module, interface, seam, depth). Tooling already enforces formatting and a few lints; this file covers what tooling cannot.

## Parity and evidence

- The **Stock Decompile** and stock scripts are the authority. Behavior that differs from them is a bug in the **Port**, however plausible it looks.
- Name the stock function a unit mirrors in a comment at the unit (`FUN_<address>` or the 8-digit address). Do this where the stock rule is non-obvious: a threshold, an ordering, a flag test.
- A claim about stock behavior needs the decompile line or a captured run behind it. Mark anything unverified `[INFERENCE]` in the comment and in the decision row.
- Never edit `ref/`. Never add game files to the repository.
- Do not widen scope silently. A change that fixes a symptom without the stock rule behind it is recorded as such in `docs/decisions.tsv`, not described as a fix.

## Gaps, not stubs

- Unported stock behavior is an explicit **Gap**: return a typed error or log a one-time note through `runlog`, naming the stock behavior left out. No silent no-ops, no zero-filled fallbacks, no mocks in production paths.
- A command or native that is accepted without effect says so once, naming the command.
- Remove temporary experiments (env-var switches, `TEMP` blocks, diagnostic dumps) before the work is called done. If a diagnostic earns its keep, make it a permanent `runlog` event.

## Numerics

- Compiler flags `-Wall -Wextra -Wshorten-64-to-32 -ffp-contract=off` are mandatory; do not turn off contraction control or add fused operations in physics code.
- Use `float` where stock uses `float`. Keep stock's grouping of operations; reordering sums changes results.
- Do not add x87 emulation without a demonstrated mismatch (decision row 2026-10-31T17:12:13Z).
- Narrowing conversions are written explicitly (`static_cast`); `clang-tidy` flags the rest.

## Module design

- Prefer **deep modules**: one small interface hiding a stock behavior cluster. A new header that mirrors its `.cpp` line for line is a smell; fold it back.
- Apply the deletion test before adding a layer: if removing it only moves code to its one caller, do not add it.
- Put a **seam** only where behavior varies. Real seams today: the class loader behind `ClassRuntime` (a `ClassEnvironment` in the game, a table of synthetic classes in tests), the native registry in `src/natives/`, and platform services behind the platform layer. One adapter means a hypothetical seam; do not add it.
- Dependencies are passed in, not created. Results are returned, not written through hidden state.
- Headers state ownership and lifetime when raw pointers or `OwnerLink`s are involved (see `owner_links.hpp`: owners and links stay at stable addresses; detach before destroying).
- Code that unit tests compile directly (`vm.cpp`, natives) must not require the recorder to link. Keep `runlog` hooks header-only no-ops at the top of `run_log.hpp`.
- New source files go into the library list in `CMakeLists.txt` that matches their dependents (`sl_data`, `NATIVE_REGISTRY_SOURCES`, `CAR_FACTORY_SOURCES`), not into one test target.

## Style

- `.clang-format` (LLVM base, 2-space indent, left pointer alignment, no column limit, includes unsorted) is the format. Do not reformat unrelated lines.
- C++20, no compiler extensions. `#pragma once` in headers. Physics code lives in `namespace physics`.
- Names follow the nearest existing code; domain names follow `GLOSSARY.md`. Do not introduce a synonym the glossary lists under `_Avoid_`.
- Comments say why (the stock rule, the invariant, the limit), not what the line does.

## Observability

- **When debugging or exploring behavior, add observability first.** If the run output cannot answer the question you are asking, add a `runlog` event or cartrace field that can, rerun, and read it before forming a hypothesis or changing behavior. Do not guess from a single frame or from code reading alone. Log the cause and the values that drove the decision (the tripped field, the threshold, the inputs), not just the outcome.
- Pair every screenshot you rely on with the log lines and cartrace rows from the same frames.
- Keep the observability that proved useful as a permanent event. Remove only throwaway dumps (`TEMP` blocks, env-var switches) once the cause is found.
- Every behavior you cannot see from outside the **Run** needs a `runlog` event. Diagnose from run output (`run.log`, **Cartrace**, `gaps.txt`, `threads.txt`, `world.txt`, `crash.txt`), not from a single frame.
- Log state transitions once, with cause and values, not every tick. Per-step data goes to the cartrace channel.
- `status=ok` means the run did not abort. It does not prove parity: check the telemetry criteria for the rung (no `err` notes, no free-fall, no speed jump, matching screenshots).

## Tests

- A test is an executable in `tests/<module>_test.cpp`, registered with CTest. The suite is part of `tools/check.sh` and must stay 100% passing.
- Test through the module's interface with real stock data where possible (fixture ids come from `PackRegistry.resolveLiteral`). Synthetic data only where the real data cannot reach the case.
- Build C++ tests with `-fsanitize=address,undefined -fno-sanitize-recover=undefined`, as the existing targets do.
- A regression test must fail on the old code. Confirm it by reverting the fix or injecting the fault.
- Do not test wiring, forwarding, copies, source text or incidental defaults. Delete tests that pin wording or implementation detail instead of behavior.

## Process

- Append one row per decision to `docs/decisions.tsv` (UTC timestamp, phase, decision, why, evidence, result). Add a correcting row when an earlier row proves wrong; do not rewrite history.
- Run `tools/check.sh` before reporting work. It is the full gate.
- Do not commit or push unless asked.
- A rung or parity-checklist row is marked verified only with a linked run directory or test that exercises it.
