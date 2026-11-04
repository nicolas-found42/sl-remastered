# Street Legal Racing: Redline for Apple Silicon

This project is building a standalone native macOS ARM64 game by reconciling the official Street Legal Racing: Redline references. It includes the VM, native interfaces, renderer, audio, physics, persistence and application entry point, with substantial behavior still unverified. It is not a completed or accepted game release.

The [Full Port specification](docs/specs/full-port-1.0.0.md) defines the product. The [requirement map](docs/requirement-map.md) records acceptance and remaining gaps; historical status notes are investigation history. Game implementation is paused until explicitly resumed. Developer-tool improvements do not qualify gameplay or release acceptance.

## Set up development

Use an Apple Silicon Mac with Xcode Command Line Tools, Homebrew and Python 3.11 or newer. CMake 3.20+, Ninja, SDL3 **3.4.0+**, JPEG and PNG are required. CI selects Python 3.13; the environment report records the interpreter/compiler/SDK actually used. Ruff and Pillow are pinned in `requirements-dev.txt`; the selected LLVM family and ShellCheck version are checked against `tools/devtools.json`.

```sh
# Explicit installation: Homebrew dependencies, project venv and Git hook.
python3 -I tools/parity/dev.py setup
source .venv/bin/activate
python3 -I tools/parity/dev.py doctor
python3 -I tools/parity/dev.py configure
python3 -I tools/parity/dev.py portable
```

`setup --skip-brew` installs only the Python environment and hook. `setup` changes the local `core.hooksPath` to `.githooks`; it never stages source. `doctor` is read-only and reports missing tools, exact paths/versions, SDK identity and compilation-database coverage. It does not install tools, build or launch the game. A Brewfile is a dependency inventory, not a lockfile.

The portable lane builds and runs a small audited set of asset-free C++ checks, serially and under shared admission. It works without `ref/` and does not replace the private complete gate. Hosted CI also checks Python, shell/workflow configuration and changed native source.

## Edit and check source

```sh
# Fast checks of actual staged bytes; no build database required.
python3 -I tools/parity/dev.py lint
# Check explicit working files without rewriting them.
python3 -I tools/parity/dev.py lint --paths tools/devtools.py
# Deliberate formatting before staging; the index is never rewritten.
python3 -I tools/parity/dev.py format tools/devtools.py
# Deep analysis needs selected source and tracked headers to match.
python3 -I tools/parity/dev.py lint --staged --tidy --build-dir build \
  --translation-unit @all
```

The hook checks Python syntax and high-signal Ruff rules, shell dialects with ShellCheck, and C/C++/Objective-C formatting. Staged/committed native edits use edited ranges so unrelated formatting debt does not force a blanket rewrite; new files and explicit working-path checks use the full style. Python formatting is opt-in. Deep clang-tidy is an explicit local/CI check with exact compiler commands and compiler-proven header includers. It refuses missing source context instead of inventing native flags for Wine/MinGW experiments.

The canonical Release database is `build/compile_commands.json`, also used by `.clangd`. Debug uses `build/preset-debug`; optional sanitizer diagnostics use `build-sanitize`. Configure either through `dev.py configure --preset ninja-debug` or `--preset ninja-sanitize`, and pass its directory to deep lint. Local overrides belong in ignored `CMakeUserPresets.json` or `CMAKE_PREFIX_PATH`/`SDL3_DIR`. `SLRR_LLVM_PREFIX` selects formatter/linter tools; it does not change the game's compiler. Sanitizer builds instrument production libraries as well as tests and are not parity, performance or release evidence. Keep `-ffp-contract=off` and existing operation grouping in every profile.

## Check the project

With the private references provisioned, run `python3 -I tools/parity/dev.py gate`. It runs `tools/check.sh`: native ARM64 build, serial tests, tooling controls and corpus comparisons. It prints `check: ok` only on complete success. A failure leaves later stages unexecuted; separately passing checks cannot repair that invocation. Test assertions remain enabled in Release. The stock-handler Ghidra test runs when the initialized local Ghidra project is present.

Use [the command guide](tools/parity/agent-tools.md) for targeted tests, repro, corpus, evidence inspection and admission limits. Keep private native/Stock captures on their specialized adapters, with the existing foreground/audio, storage, deadline and cleanup policies. No formatter or lint command starts those captures.

To inspect a texture, run `build/ptx_view ref/raw/frontend/textures/GPS_FrameRaceSetup.ptx` after the check. Press Escape to close the viewer. This is a diagnostic, not the game menu.

## Files

- `docs/specs/full-port-1.0.0.md` and `docs/requirement-map.md` define scope and acceptance.
- `docs/research/devtools-cpp-build-20261103.md`, `devtools-python-shell-20261103.md` and `devtools-game-ci-20261103.md` retain the tooling research and primary sources.
- `docs/imports.md` lists every Windows function the stock executable imports.
- `docs/phase1.md` records the phase 1 results, the build probe of `slrr-reverse`, the call census, and the native-function registry.
- `docs/natives.tsv` lists the 360 native functions that the stock executable offers to game scripts.
- `docs/pointer-width.md` tests how to carry the original 32-bit object layout on a 64-bit Mac.
- `experiments/pointer_width/` holds the script that produces the pointer-width results.
- `docs/decisions.tsv` records each decision with its evidence.
- `src/` holds the data loaders, partial interpreter, platform layer, and other port code.

## The ref folder

`ref/` is not in git. It holds the game install in `ref/raw` and four reference repositories. If you clone this repository on another machine, copy `ref/` back to the same place.

This repository holds no game files. The game, its executable, and its data belong to Invictus Games and the publisher.
