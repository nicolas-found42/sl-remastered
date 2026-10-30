# Compare captured frames

This tool writes stock, native and absolute RGB difference images, plus a JSON report with source paths, SHA-256 hashes, dimensions and pixel statistics. It requires Python 3 and Pillow. The output directory must not exist, so an earlier comparison cannot be overwritten.

For the existing Free Ride hover captures, from the project directory:

```sh
python3 -I tools/parity/compare_frames.py \
  .scratch/shots/stock-flow-2-free-ride-hover-backbuffer.png \
  .scratch/shots/fr-hover.png local/verify/hover-frame-comparison --resize-native
```

Use a stock **back-buffer** capture, not a Wine window screenshot (see `tools/wine/capture/README.md`). By default the tool rejects different dimensions. `--resize-native` explicitly scales the native frame to stock dimensions with bilinear interpolation; the report records this transformation. Use a fresh output name for each run.

The report compares supplied images, not game states or equivalent moments. Menu animation, pointer position, capture time, resolution, scaling, rendering, and random game state can all change pixels. A difference is a prompt for visual investigation, not a parity score. The tool does not align frames, remove the cursor, or determine whether two screenshots depict the same state.

Run the checks with `python3 -I tools/parity/test_compare_frames.py`.

## Preserve the initial save

Stock `run_capture.sh` copies the extracted game's `save/` at launch. Native `tools/play.sh` uses the persistent `local/stock-data/save/`. Capture both before starting either game:

```sh
python3 -I tools/parity/save_snapshot.py \
  '.scratch/2005-extract/Program Files/Street Legal Racing - Redline/save' \
  local/verify/stock-save-before-run
python3 -I tools/parity/save_snapshot.py local/stock-data/save \
  local/verify/native-save-before-run
```

Each new snapshot contains a `save/` copy and `manifest.json` with a SHA-256 for every file. Compare the `files` objects to see whether their contents match. Run while neither program is writing saves. The tool checks the source before and after copying, rejects symlinks, and refuses an existing destination. If copying fails or the source changes during the copy, discard the incomplete output directory and retry. The copy retains save data, which may contain private information; keep it local.

For an isolated native run, copy a snapshot's `save` folder into a **new** data root and pass that root to `startup_probe --data-root` instead of using `tools/play.sh`'s shared `local/stock-data`. Stock requires a separate writable copy of the whole game data with the chosen `save/`, passed as `SOURCE` to `run_capture.sh`. Do not replace a live game's save directory. A matching save is only one starting condition: this tool does not control random state, timing, input delivery, or selected car.

Existing stock-code readings identify `Math.randomize` (0047c7d0) as seeding a thread-local generator from elapsed performance-counter milliseconds (`docs/decisions.tsv` and `local/census/DecompileAt-0047c7d0.log`). The native implementation also uses an elapsed performance clock (`src/natives/random.cpp`); neither capture workflow fixes or records the actual seed. The observed stock runs selected different cars (`docs/stock-flow.md`), and the native currently stops before a driveable car exists (`docs/continuation.md`). Stock proxy present counts are display events, not simulation steps; native replay is elapsed-time based (`docs/run-log.md`). Consequently, equal saves alone do not make runs deterministic. Record the car shown in each stock capture and the run logs separately; do not infer the seed from the car.

Run the save checks with `python3 -I tools/parity/test_save_snapshot.py`.
