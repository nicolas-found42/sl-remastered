# Street Legal Racing: Redline for Apple Silicon

This project is building a native macOS arm64 port of Street Legal Racing: Redline from the stock 2005 executable. The game is not playable yet. The data loaders and a partial script interpreter work, and the real 2005 scripts show the Main Menu in a native window (run `tools/play.sh`). Clicking Free Ride shows the loading screen and then stops, because the car factory, the physics engine and the track scene are not built yet. See `docs/STATUS.md` for the current state and proof.

## Check the project

In Terminal, run `tools/check.sh` from the project folder. It verifies `ref/`, builds for arm64, and runs the tests and corpus comparisons. It prints `check: ok` when they pass. The build requires CMake, Ninja, SDL3, and libjpeg; the independent PTX comparison also requires Pillow for Python 3. The stock-handler Ghidra test runs when the initialized local Ghidra project is present.

To inspect a texture, run `build/ptx_view ref/raw/frontend/textures/GPS_FrameRaceSetup.ptx` after the check. Press Escape to close the viewer. This is a diagnostic, not the game menu.

## Files

- `docs/PLAN.md` describes the goal, what "1:1" means, the phases, and the open risks.
- `docs/ghidra.md` explains how to point Ghidra and its MCP server at the stock executable.
- `docs/imports.md` lists every Windows function the stock executable imports.
- `docs/phase1.md` records the phase 1 results, the build probe of `slrr-reverse`, the call census, and the native-function registry.
- `docs/natives.tsv` lists the 360 native functions that the stock executable offers to game scripts.
- `docs/pointer-width.md` tests how to carry the original 32-bit object layout on a 64-bit Mac.
- `experiments/pointer_width/` holds the script that produces the pointer-width results.
- `docs/decisions.tsv` records each decision with its evidence.
- `tools/ref_guard.py` proves that nothing under `ref/` has changed.
- `src/` holds the data loaders, partial interpreter, platform layer, and other port code.

## The ref folder

`ref/` is not in git. It holds the game install in `ref/raw` and four reference repositories. If you clone this repository on another machine, copy `ref/` back to the same place. `tools/ref.manifest` lists the SHA-256 hash of every file that must be in it.

This repository holds no game files. The game, its executable, and its data belong to Invictus Games and the publisher.
