# Run log: what the probe leaves behind

Every run of `build/startup_probe` (and so every run of `tools/play.sh`) writes a folder under `local/runs/`. If a run fails, hangs, or is killed, you can ask `tools/diagnose.sh` what happened without running it again.

## Read a run

```
tools/diagnose.sh            # the newest run
tools/diagnose.sh list       # the newest 10 runs, one line each
tools/diagnose.sh local/runs/20261028T174646Z-75705   # one specific run
```

The answer comes in this order: a one-line verdict (status, exit code or signal, how long, how many frames), the CAUSE, the LAST EVENTS, COUNTERS (for example how many sounds were dropped), WHAT RAN (the command, the git state, the script) and FILES (where everything is).

## What is in a run folder

The folder is named `<UTC date and time>-<process id>`. `local/runs/latest` always points at the newest one. Run folders are preserved; the recorder does not automatically delete older runs.

| File | What it holds |
| --- | --- |
| `run.log` | Everything the probe printed, each line stamped `[+seconds fFRAME] out` or `err`, plus logged events (input, sound, drop, resource, note, error, heartbeat). |
| `meta.txt` | Command, process identity and start time, executable hash, launch-reported source identity, game and data folders, snapshot status, and platform details. A launch-reported source hash does not prove which source built the executable. |
| `initial-save/` and `initial-save-manifest.txt` | Private starting save copy and SHA-256 hashes, when capture succeeds. The source is checked before and after copying, but this is not an atomic snapshot. |
| `script.txt` | A copy of the `--script` file, if one was given. |
| `result.txt` | How it ended: `status` (`ok`, `failed`, `crashed`, `terminated`), `exit_code`, `signal`, `seconds`, `frames`, `reason`. |
| `crash.txt` | Only after a crash or a stop signal: the last 200 events and a list of the code that was running. |

Events that happen very often (every script call, every cursor move, every thread switch) are not written to `run.log`. They sit in a memory buffer of the last 4096 events. That buffer is written out when the run fails (under `--- last events ---` in `run.log`) or crashes (`crash.txt`).

Every 5 seconds in the frame loop the probe writes a `heartbeat` line with the frame count, the speed in frames per second and the last event. If the frame rate drops under 20, it also writes one line per second saying so. If the probe is killed or hangs, the last heartbeat shows where it was.

## Make a run fail on purpose

Add a line to a `--script` file. The number is the seconds since the frame loop started.

```
2 fail error     # prints "startup probe: injected failure" and exits with code 1
2 fail segv      # writes through a null pointer (a crash, SIGSEGV)
2 fail abort     # stops itself with abort (SIGABRT)
2 fail throw     # throws a C++ error nobody catches (SIGABRT, with the error text)
```

Then run `tools/play.sh --script yourfile.txt` and `tools/diagnose.sh`. `tests/run_log_test.py` does this for every kind and checks the answers.

## Turn it off or move it

- `--no-run-log` as a probe option: no folder, no capture of the terminal output.
- `SLRR_RUN_DIR=/some/folder`: put the run folders there instead of `local/runs`. The tests use this so they do not fill `local/runs`.

## Limits

- `kill -9` cannot be caught. That run has no `result.txt` and no `crash.txt`. `diagnose.sh` says so and shows the last heartbeat and the last lines of `run.log`. The frequent events that were only in memory are lost.
- The memory buffer keeps 4096 events, and each event keeps 151 characters of text. Older events are overwritten.
- The list of running code in `crash.txt` is best effort. A badly damaged program can fail to produce it, and release builds show only names, not line numbers.
- A crash is recorded after a 40 millisecond pause that lets the last printed lines reach `run.log`. A line printed in the final instant can be missing.
- Output from several threads can be written to the log in a slightly different order than it appeared on the terminal.
- The log can contain local file paths (your folder names, the game folder). Do not share a raw run folder. Read it first.

## Stock Wine captures

To make a diagnostic folder from an existing capture, run:

```
python3 -I tools/wine/capture/finish_run.py local/wine-runs/capture-YYYYMMDD-HHMMSS
```

The command prints the new folder path. Pass that path to `tools/diagnose.sh`. Packaging reads the capture and leaves it in place. It estimates event timing from the available logs. A verdict based on continuing frames and a finished input script does not establish the game's exit code. `stop.sh` does not package the capture automatically.

## Interrupted input recording work

The recorder and `tools/replay.sh` are present, but a hand-driven recording and replay have not been independently verified. Scripted SDL events are not evidence of physical keyboard or mouse input. Replay uses elapsed time rather than matching frames, and it does not control random choices or restore the initial game state.

## Evidence limits and retention

New run folders are private to the current user and are not automatically deleted. They can contain save data and local paths, so disk use grows over time and you should review a folder before sharing it. The initial save copy is limited to 1,024 files, 1,024 directories and 16 MiB; symlinks, special files, a changing save and unsafe paths are rejected. `meta.txt` records when a snapshot was unavailable. The game continues without a snapshot. An older run cannot recover a save or seed that was never recorded.

A normal recorded run now logs the random seed and the requested car, when those events occur. A car request does not prove that the car was created. Event timestamps come from the native run clock; ties and ordering across threads can be approximate. `tools/check.sh` fails if the windowed run-log integration test cannot run, rather than reporting a complete check after a skip.

Stock captures started with the new proxies record their separate Windows clock origins. Packaging aligns their events when both origins are present. For older captures without those origins, DirectInput events remain in a separate unaligned section and are not assigned invented timestamps in the reconstructed input. Packaging never runs Wine to infer a version, and it preserves earlier packaged runs by creating a new folder. These clocks still do not align stock and native simulation steps.

Each new run also attempts a private `source-manifest.txt` using the source location embedded in the executable. `src_tree_sha256` identifies that manifest. The manifest describes the working tree observed at launch, not the files used to build the executable; the executable has its own hash. A missing source tree, unsafe names, excessive size, or a source change during capture is recorded as unavailable. The source capture is limited to 20,000 entries and 128 MiB and excludes symlinks and binary or temporary files. Older launcher manifests use different exclusions, so their hashes are not always directly comparable. `tools/diagnose.sh --diff-code RUN_A RUN_B` verifies available manifests before listing differences.

For a missing `result.txt`, the report checks the recorded process ID and start time where macOS permits it. It distinguishes a running process, an exited or reused ID, and an unknown identity. A stale heartbeat alone does not prove a hang.

## Replay a recorded native run

`tools/replay.sh RUN` uses a new private data directory under `local/replays/`. When a recorded initial-save snapshot exists, replay checks its manifest and copies it; an incomplete or altered snapshot stops replay. For older runs without a snapshot, replay copies the current save and warns that it may not match the original. The source save is not the replay's writable save. The input timing and random state are not controlled, so the same reported cause is not proof of deterministic replay.

New stock input proxy logs distinguish commands issued, queued or dropped buffered events, events returned to DirectInput, and held-down states observed during a read. These observations do not prove that the game acted on an input. Older captures lack those counters and report them as unknown. A completed input script without a verified scene or process exit is reported as unknown, not a successful game run.

During the frame loop, the recorder writes two alternating `flight-0.log` and `flight-1.log` checkpoints at five-second heartbeats. Each completed file contains up to 4,096 recent events and a sequence number. A temporary file is synced before replacement; an incomplete temporary file is ignored. These are bounded snapshots, so older checkpoints are overwritten and a stalled frame loop cannot capture later activity. `tools/diagnose.sh` merges valid checkpoints with persistent events and the crash tail; equal timestamps and cross-thread order remain approximate.

## Script screenshots

A script may use `shot @run/name.bmp` to save a screenshot inside its own run folder. The name must be a simple filename, and the probe stops with an error if that file already exists. The Free Ride script uses this form, so each run keeps its own screenshots. Find the folder with `tools/diagnose.sh`, then open the BMP file there.

A plain `shot path.bmp` continues to use the specified path. With `--no-run-log`, use a plain path: `@run/` requires an active run folder and otherwise produces an error. `@run/` paths cannot contain directory separators or `..` path components.

The recorder adds work to the running program: it locks around ring writes and syncs a checkpoint file on the frame thread at each heartbeat. This can affect timing, particularly on slow storage. Compare traced and untraced behavior before attributing a timing-sensitive difference to the game itself.

## Readiness waits

A script line `<seconds> wait menu|driving <timeout-seconds>` holds every later line until the game is ready, pausing only the script clock; loading, the Script VM and simulation keep running. Later offsets resume from the wait's own offset, even when a slow frame reached the wait late, so a throttle burst keeps its length and order after a slow load. Each success prints `startup probe: wait NAME ready (loop=… script=… waited=… frame=…)` and adds the same note to `run.log`. A timeout reports the last blocking condition, for example `wait driving timed out at line 2 (blocked by: active state is not a java.game.Track)`. A timeout, an unknown condition, a nonpositive timeout, an unknown action or key, or an action the probe cannot perform stops the probe with exit code 2.

- `menu`: `GameLogic.actualState` is a `java.game.MainMenu` whose `mmd` dialog is shown, no loading dialog is shown and no resource load is pending.
- `driving`: the active state descends from `java.game.Track`, `GameLogic.player.car.chassis` is a world car with physics, a player device controls that car, the render instance of that player's camera device was attached to a viewport in the last presented frame, and no loading is pending. A menu background car does not satisfy it: `wait driving` without the Free Ride click timed out (2026-11-01, `local/rapid-testing/smoke-no-click-2`).

`tools/parity/run_native.py` uses these waits for its rapid default (`tools/scripts/free-ride-fast.txt`); `--cold-start` keeps the timed menu click.

## Ground and car snapshot

On each five-second sample, and again when an animation callback fails, the probe writes notes. `world state` includes `cars`. `car=unavailable` means no player car. One car adds `car=0x` plus its instance id and `pos` from the host car record. More than one car is `car=many` with no position.

`ground1` version 2 JSON uses short keys so the line fits the 151-character event limit: `c` cities, `r` published road tables, `t` stored traffic cars, `p` registered pedestrian type ids, `wa` cities with water active, `ds` cities that stored a pedestrian density, `y` that density, `z` the high density. Version 1 lines without `p` or `wa` still parse; diagnosis treats missing keys as zero. With several cities, `y` and `z` are the last city that stored a density, in node creation order.

When any city has water active, the same snapshot also writes `ground2` version 1 JSON with `px`, `py`, `pz`, `ea`, and `eb` from the first active factory. `tools/diagnose.sh` prints the last valid records under GROUND STATE. A missing `ground1` line means the run predates this note. The traffic count does not apply lane rejection. The position is the host record, not a physics step.

With one player car, `world state` also records host `spd`, `gear`, and `rpm` from the car runtime. Each later snapshot for that car writes `card1` JSON with `dx`, `dy`, and `dz` since the previous snapshot. The first snapshot has no `card1`.

Every `GroundRef` native appends `grlast` JSON with `m` (method name) and `ok` (1 void success, 0 error). Diagnosis prints the last one under LAST GROUND NATIVE.

When an animation callback fails, the probe writes `fail1` JSON with `cls` set to the innermost frame from the VM failure trace (class name only). Diagnosis prints it under FAILURE SCRIPT.

## Type-1 load stages

When a packed type-1 resource is loaded through `GameType.addNotification`, the recorder writes `load1` version 1 JSON notes to `run.log`. Each has a process-local attempt ID, resource ID, event (`enter` or `end`), and stage. An `enter` records the operation about to run. The next `enter` shows that control advanced past the preceding operation. The final `end` records that the tracing scope returned or unwound; it does not mean the resource loaded successfully. Read the reported cause alongside it.

`tools/diagnose.sh` lists the stages for up to the last 20 attempts. Missing end records, malformed records and inconsistent sequences are reported as unknown. The notes use the existing persistent run log and 151-character event limit. They are not individually synced: a crash or forced stop can lose buffered output, and a missing end does not identify the cause. Older runs have no `load1` history. The trace covers this notification load path, not every resource-loading entry point.

## Assembly traces

Install attempts append compact `asm1` version 1 JSON records to `run.log`. A per-process attempt ID connects the requested part, target and slot to candidate checks, the resolved target and slots, the install-parent link, physics-object presence and the result. Candidate reason bits distinguish missing prototypes, occupied or disabled slots, already attached parts, and searches with no matching pair. An aggregate records all examined candidates. At most eight individual candidate records are retained per failed attempt; `omitted` reports the rest. A reason can describe multiple checks, so a no-match result does not identify which exact slot or compatibility rule failed.

`tools/diagnose.sh` reads the saved records and reports incomplete or malformed records as uncertainty. The ID identifies an attempt within one process; it is not a cross-thread event clock. Existing timestamps and ordering across threads are approximate. The recorder persists these notes through its existing run log, but it does not sync each line to disk. A forced stop can leave a request without a result; power-loss durability is not established. Instance IDs are local to the run and cannot recover a missing resource ID or a truncated earlier creation note. These records do not contain the complete body pose or prove deterministic gameplay.

Failed installs also record `asm1` slot and inventory records for the part (candidate 8) and up to eight recorded targets (candidates 0 to 7). Inventory gives the prototype resource ID, total runtime slot entries, number of non-null slots shown, and attached-part link count. Up to eight non-null slots per node show their ID, flags and resolved attached node ID; zero also covers an unresolved link. A difference between total and shown can include null entries or omitted slots. These are snapshots after the search, not a record of each compatibility comparison. Missing candidate records, truncation, and interruption can still hide evidence.

Part removals append the same style as `asmr` records with their own per-process ID: a `req` half (part, requested slot, the runtime slot the remove will use, the host target and its slot ID) followed by a `done` half (links detached from the car's attached-part registry, registry size and used-slot count before and after). A `req` without a `done` means the removal stopped before the registry step, most often because the part had installed physical children or no car root resolved. These records cannot detect destroyed-but-still-linked owner links: that corruption is only visible to AddressSanitizer builds or the allocator's own guard, because the linked list is inspected after the memory is gone.

When the wheel pose override for slot types 101 to 400 declines, `wheelref` records the slot type, the derived wheel index, the real wheel count and a reason (`no_indexed_car`, `no_wheel_record`, `record_without_proto`). The call then falls back to the root geometry pose. Stock reads out of bounds in the same case, so every decline is a place where the port knowingly diverges. The report aggregates these by slot type and reason.

## Camera follow transition

When player camera setup reaches the unsupported follow simulation, `camfollow1` version 1 records the player, camera, car, target body, look target and move target IDs. It also records whether the target body is registered and the reason code. IDs are local to that run. A zero target ID means no link was recorded; it does not prove that a target never existed earlier.

`tools/diagnose.sh` reads the last valid record and reports malformed records. Older runs have no `camfollow1` record. The note is written at this specific transition, not on every frame or every camera failure. It uses the existing run log and its buffering and interruption limits.

## Car step trace (`cartrace`)

Every physics step of a world with a player car appends one JSON line to an in-memory ring of the newest 512 steps (`CarWorldStepper::step`, `src/car_world_step.cpp`; ring and hooks in `src/run_log.hpp`). The ring is always on. It is written out in four ways, so a run that fails, hangs or is killed still leaves the steps that led there:

| Where | When |
|---|---|
| `cartrace.jsonl` | every 5 s heartbeat and at the end of the run (a `kill -9` keeps the last heartbeat) |
| `crash.txt`, section `--- trace cartrace ---` | on a crash or `SIGTERM`; the newest 256 steps, oldest first |
| `cartrace-anomaly-N.jsonl` | the ring as it stood at each of the first 6 anomalies (`car anomaly tick N: tags` is also noted in `run.log`) |
| `cartrace-full.jsonl` | every step of the run, only with `SLRR_CAR_TRACE=1`; one `write(2)` per step |

Anomaly tags: `step_failed`, `nonfinite`, `speed_jump` (root speed changes by more than 8 m/s in one step), `iteration_cap` (the 20-pass contact loop hit its limit), `dt_spike` (step <= 0 or > 0.1 s).

Line fields: `tick` (step number, contiguous), `f` (frame), `wall` (monotonic seconds), `wdt` (wall time since the previous step), `dt` and `pt` (step length and physics time), `ok`, `pre`/`post` (root position, velocity, and post angular velocity), `spd`, `asleep`, `ctl` (sampled controls: throttle, brake, clutch, steer, gear, engine rad/s), `wh` (per wheel: `i` index, `w` angular speed, `l` suspension length, `F` suspension force, `cb` contact body held, `wi` tire witness held, `s` selection result this step `S` selected / `R` rejected / `-` no face hit, `t` tire step `o` ok / `r` rejected / `-` no witness, `h` face hits, `cy` contact height, `wy` wheel height, `sl` slip magnitude, `sa` applied steering angle (rad), `ls` longitudinal slip, `aa` slip angle), `col` (`pairs` body/face pairs, `iter` contact passes, `n` contacts per pass, `imp` largest impulse per pass, `worst` largest impulse with its normal, separation and body), and `anom` / `err` when set.

Read it with `python3 -c 'import json; [print(json.loads(l)["tick"]) for l in open("cartrace.jsonl")]'` or `grep -a '"anom"' cartrace.jsonl`.

## Historical queries and recorder health

See [the coverage inventory and investigation runbook](observability.md) for time,
run and operation retrieval. New records include `telemetry.json` with log loss,
IO failures and checkpoint status; a missing or stale health record is unknown.
The run log defaults to a 16 MiB byte cap while stdout/stderr continue draining.
Failure context is bounded to 64 stack lines of 1,024 characters; loss is counted.
The existing diagnosis prints nonzero health counters near its verdict.

Typed `op1` save operations join begin/end, parent, FileState lifetime scope and
handle without storing filenames or contents. Logging setup/write failure is
best effort and does not grant authority to interrupt normal application work.
Save-store and path-containment failures remain real application errors.

### 2026-11-04 branch integration checkpoint

The staged physics/observability checkpoint and subsequent Stock discovery work
were committed separately; both remain unfinished as described in their bound
records. The staged fast lint passed for each checkpoint. `dev.py gate` configured
and built, then CTest passed 130 of 131 tests; `object_world_step_unit` failed
at `object_world_step_test.cpp:240` (`speed < 0.3f`, observed 0.468). Because the
gate stops there, the checks after CTest in `tools/check.sh` were not executed.
Deep staged clang-tidy and a post-merge full gate were not run in this checkpoint.

The collisions, Career, display, clipping, GroundRef, and music branch histories
were merged, preserving later implementations and an alternate Career script.
The SFX and startup remote branches remain unmerged: their older implementations
conflict with later extracted application/audio and video code. The SFX branch's
real-device test requires the private-audio capture workflow before execution.
Their remote commits remain available for a deliberate reconciliation.
