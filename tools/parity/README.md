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

Existing stock-code readings identify `Math.randomize` (0047c7d0) as seeding a thread-local generator from elapsed performance-counter milliseconds (`docs/decisions.tsv` and `local/census/DecompileAt-0047c7d0.log`). The native implementation also uses an elapsed performance clock (`src/natives/random.cpp`); neither capture workflow fixes or records the actual seed. The observed stock runs selected different cars (`docs/stock-flow.md`). Native Free Ride now reaches a driveable player car (2026-11-01 `run_native.py` runs: chase camera, HUD speed/gear respond to throttle), but that is not Parity evidence. Stock proxy present counts are display events, not simulation steps; native replay is elapsed-time based (`docs/run-log.md`). Consequently, equal saves alone do not make runs deterministic. Record the car shown in each stock capture and the run logs separately; do not infer the seed from the car.

Run the save checks with `python3 -I tools/parity/test_save_snapshot.py`.

## Record a native Free Ride run

`run_native.py` performs one isolated native Free Ride attempt. It snapshots the
`--save` fixture into a fresh data root (the fixture itself is never modified),
writes a game-data manifest, copies the exact script that ran into
`<output>/script.txt` (SHA-256 in `metadata.json`), and points the probe's run
recorder at `<output>/run` so `shot @run/...` lines and run telemetry stay in
the record. The output directory must be new and outside `ref/`; it must not
contain or be contained by the game or save directories. A nonzero probe exit
or a subprocess timeout is a failure; screenshots and process exit alone do
not prove a driving scene or parity.

Rapid default (waits for the real menu, clicks Free Ride at (420,127), waits
for a real driving scene, short throttle burst, two captures):

```sh
python3 -I tools/parity/run_native.py local/verify/fast-1 \
  --probe build/startup_probe \
  --game '.scratch/2005-extract/Program Files/Street Legal Racing - Redline' \
  --save local/stock-data/save
```

The launcher runs the probe from the isolated `<output>/data` directory.
`--reference-root PATH` selects the immutable Reference Folder for save guards,
Run output guards, and stock cursor resources. The default is the launcher's
repository `ref/`, resolved through symlinks. From a worktree without its own
Reference Folder, pass the original checkout's absolute `ref/` path. The native
probe also accepts `--reference-root`; its default remains cwd-relative `ref`
for existing callers. Native Runs refuse writable data and recorder directories
that overlap the Installed Game or Reference Folder, including symlink aliases.
Recorder initialization failure stops the probe before game startup.

`result.json` records whether the source fixture's file hashes still match the
prelaunch snapshot. A changed fixture, process launch error, nonzero exit, or
bounded process timeout fails the launcher. Timeouts must be finite and positive.
Exact scripts, source-save manifests, and executable hashes remain in the output.

The [ticket #2 evidence record](rapid-run-evidence.md) distinguishes the executed
workflow checks from unresolved gameplay and full-gate failures.

The bundled rapid script is `tools/scripts/free-ride-fast.txt`. The process
timeout is 120 s, covering a menu wait of up to 30 s, a driving wait of up to
60 s, startup and post-ready actions; override with `--timeout SECONDS`.

Explicit cold baseline — same driving wait and post-ready throttle burst, but
the menu is entered with the legacy fixed-timed click (10/13/13.5 s):

```sh
python3 -I tools/parity/run_native.py local/verify/cold-1 \
  --probe build/startup_probe --game <game-dir> --save <save-dir> --cold-start
```

Custom script (the fixture is copied, never written in place; verify normal
load and entry for each later-state save):

```sh
python3 -I tools/parity/run_native.py local/verify/custom-1 \
  --probe build/startup_probe --game <game-dir> --save <save-dir> \
  --script tools/scripts/free-ride-drive.txt --timeout 120
```

Script lines are `<seconds> <verb> [args]`; seconds are script-clock offsets
from frame-loop start. `wait menu|driving <timeout-seconds>` blocks following
actions and pauses only the script clock (real loop/simulation clocks run
normally); success logs one stdout/runlog milestone with real loop elapsed and
frame, and a wait timeout or invalid input is a nonzero failure. Offsets after
a wait are logical script time, shifted by the actual blocked duration. A
`--script` may only take screenshots as `shot @run/NAME`, inside the run
record; any other `shot` path is rejected before the output is created. A
matching visible-mode interactive run of the same script format is
`tools/play.sh --script FILE` (no build caching; it rebuilds `startup_probe`
each launch). The full cold/end-to-end UI paths remain the supplied
`tools/scripts/rung*-*.txt` scripts; one Free Ride cold run does not cover the
career flow.

Measured 2026-11-01 on the integrated Release arm64 executable, five alternating
Runs per route with the same source fixture, tracing, 640×480 window and two-second
throttle workload. Median total duration including preparation and verification
is 6.147 s rapid (6.050–6.234) versus 19.410 s cold (19.354–19.445).
Menu readiness from `probeMain`, excluding recorder startup, is 0.607 s rapid
versus 3.460 s cold. All ten Runs preserve the source fixture and produce both
bundled screenshots. Driving outcomes are comparable, with remaining physics,
HUD and camera Gaps. See the [final benchmark evidence](rapid-benchmark-evidence.md)
for all samples, clock origins, hashes, retained artifacts and limitations.

Run the checks with `python3 -I tools/parity/test_run_native.py`.
