# Project rules

This project ports Street Legal Racing: Redline to native Apple Silicon. Read `docs/PLAN.md` first.

## Fences

- `ref/` is read-only. Never write, move, rename, or delete anything under it. Copy files out of it instead.
- Run `python3 -I tools/ref_guard.py verify` before you report any task done. A failure means something changed `ref/`. Stop and tell the user.
- Never commit game files, saves, or anything from `ref/`. It is in `.gitignore`.
- Do not commit or push unless the user asks.

## Ground truth

- The reference executable is `ref/raw/StreetLegal_Redline.bak.exe`, MD5 `e686099e7233bb7d7c461b1b35c5064c`. `ref/raw/StreetLegal_Redline.exe` is a mod ("Miran Wichur Mod v0.9b") and is not ground truth.
- Use the Ghidra MCP as described in `docs/ghidra.md`. Codex uses LaurieWired (`get_program_info`). Cursor and OMP use Bethington (`get_program_options` with group `Program Information`, then `get_metadata`). The tool lists differ. Confirm the stock MD5 before you trust any decompiled output.
- Code in the `ref/` repositories is a lead, not evidence. It counts as matched only after you check it against the decompiled stock executable.

## Working with the user

- The user has not coded before. Say what you did in plain words, give exact click or command steps, and explain a term the first time you use it.
- Do not drive Ghidra's windows with computer use unless the user asks. Keystrokes do not reach its Java windows, and clicks need a single click before a double click. Give the user steps instead.
- Log each decision with the `show-me-your-work` skill's `log.sh` into `docs/decisions.tsv`. The log is append-only.

## Build

- `tools/check.sh` is the one command that checks everything.
- The target is arm64 macOS only. The build turns on `-Wshorten-64-to-32` because the original game is 32-bit.

## Rule table

Each rule pairs a repeated mistake with what enforces it. When a correction happens twice, fix the cause here and add its enforcement.

| Rule | Enforced by |
| --- | --- |
| Synthetic test trees: every operand indexes the executing class's constant pool. Build references only through the shared harness (`tests/vm_test_harness.hpp`: `w.main.ref()`, `w.main.t()`, `Cb::make`, `World::alloc`). Never build operands from another class's pool. | Architecture: the harness is the only copy; new tests include it, so the nearest example is the right one. |
| Ghidra decompiles are read once and sliced, never re-run. Output lands in `local/census/<Script>.log` (`DecompileAt-<address>.log` per address); the runner's last line names it and checks the script's success marker. | `tools/ghidra_run.sh` (per-address logs, marker checks, prints the log path); `docs/ghidra.md` documents the slice pattern. |
| Run a test binary only after its build exits 0 in the same pipeline. Never pipe build output through `grep -c` (it reports 0 and exits 0 on a failed build) — grep only for specific error text with the build's exit code intact. | Agent discipline (nothing in CI runs agent commands). If this rule is hit again, add a `tools/run_test.sh` that refuses to run a stale binary. |
| Before instrumenting a failure, find the exact failing check: read the full validation chain that produces the error message. Several sites can print the same message. | Agent discipline (judgment call). |
