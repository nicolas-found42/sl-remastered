# Record a native Free Ride attempt

This records one run's starting save copy, executable hash, timed input, command, stdout, stderr and exit status in a new folder. It runs the native probe against a private writable copy of the save, leaving the source save untouched. Build the probe separately first; it must include the optional seed and car-creation traces.

```sh
python3 -I tools/parity/run_native.py local/verify/native-attempt-unique \
  --probe local/build-parity-trace/startup_probe \
  --game '.scratch/2005-extract/Program Files/Street Legal Racing - Redline' \
  --save local/stock-data/save
```

The program opens a window, moves to Free Ride at 10 seconds, clicks at 13 seconds, and quits at 40 seconds if it has not already stopped. It returns a nonzero status if the game fails or times out (60 seconds by default). It writes `metadata.json` before launch, `initial-save/` for the untouched snapshot and hashes, `data/save/` for the writable copy, `input.txt`, `stdout.log`, `stderr.log`, and `result.json`. The output folder must be new and outside `ref/`. Save data may contain private information; keep it local. The tool does not hash all game assets or record the source revision. The probe hash identifies the executable, not the complete data set.

Trace output records seed calls and car creation requests, not which thread seeded or whether the car was successfully created. Output can alter timing. The script is time-based, and neither game timing nor random state is controlled. One run is not evidence of determinism; compare repeated runs and independently capture stock behavior.

Observed run `local/verify/parity-native-record-1` recorded seed 555 and a request for `java.game.cars.Emer_Nonus_Street_GT`, then exited 1 at the unfinished car wheel joints, scene objects and world registration. Its starting options hash matched `local/verify/parity-stock-fresh-save/manifest.json`. The subsequent fresh stock launch stalled in `wineboot -i` before the game started, so it supplies no selected-car observation; its incomplete run is `local/wine-runs/parity-stock-fresh/`. A killed Wine process remained in uninterruptible state at the time of the attempt. Historical stock screenshots identify cars in `docs/stock-flow.md`, but cannot be paired to this native run as the same initial conditions.

Run the wrapper test with `python3 -I tools/parity/test_run_native.py`. It uses a fake probe to check that the initial snapshot remains unchanged when the private working save is modified; it does not test game behavior.
