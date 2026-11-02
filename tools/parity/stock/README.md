# Private Stock capture

From the repository root, with the existing ARM64 Hangover/XTEST image:

```sh
python3 -I tools/parity/run_stock.py \
  --source '.scratch/2005-extract/Program Files/Street Legal Racing - Redline' \
  --stock ref/raw/StreetLegal_Redline.bak.exe \
  --texture-table ref/raw/texture.dat \
  --disc local/wine-runs/synthetic-run/stock-2005/disc \
  --out local/verify/NEW-UNUSED-STOCK-OUTPUT
```

Requires Docker, Python, MinGW `i686-w64-mingw32-gcc`, FFmpeg, Tesseract and
`build/parity_focus_observer`. The launcher checks ARM64 and runs the inspected
immutable image ID. The tested XTEST image is
`sha256:c3105bb5c0fe829f89f6f782e79d7bfdb31708209ee4456e3ab4be5e7f94d88f`.
The combined Dockerfile can be built with:

```sh
docker build --platform linux/arm64 -t slrr-private-stock-hangover1116-xtest-arm64-20261101 \
  -f tools/parity/stock/Dockerfile tools/parity/stock
```

This combined Dockerfile was not rebuilt by these qualification runs; they use
the existing inspected image. It pins the upstream Hangover 11.16 bundle SHA-256;
Debian packages remain build-time versions. Retain the actual image identity.

Each attempt joins the shared admission pool: six CPU permits (four hard-limited
guest CPUs plus host capture/observer work), 3 GiB host-memory estimate, 3 GiB VM
memory, one renderer and the Stock slot. `--timeout` bounds execution including
fresh Wine initialization, after preparation. The outer observer has a separate
preparation/cleanup allowance. Estimates are not measured concurrency maxima.

The game uses a new writable data copy and prefix, no network, all capabilities
dropped, no new privileges and a 256-process limit. Actual inspected configuration
must match. Only private output and a read-only disc fixture are mounted; no host
display, input/audio socket or device is exposed. Xvfb and a Pulse null sink stay
inside the container. Real mixed PCM is retained in `stock.wav`; host audio/device
settings remain independent. The passive macOS observer covers the worker and
observed host descendants. It allows unrelated user app switches and retains
PID/birth ownership, notification/poll limits and gaps.

The controller waits for two observed menu frames and actual input polling,
checks the client origin, sends one coherent XTEST New Career click, observes
the player-name dialog, clicks its actual Cancel button and observes menu return
with dialog absence. Actual native button transitions must be UP/DOWN/UP/DOWN/UP.
No retry qualifies this scenario. Exit zero additionally requires nonempty PCM,
no owned activation observed, preserved source/disc/Stock/texture, the private
null sink and confirmed container removal.

Every attempt retains frozen instrumentation/controller source, hashes, compiler
output, private initial manifest, inspected image/container configuration, exact
commands, native input logs, observed frames/PCM and source audits. Existing
outputs and overlap with protected inputs are refused. Failures retain incomplete
verdicts and partial artifacts. Independent parent cleanup also runs when the
worker dies; the container itself has a deadline if its host launcher disappears.
Arbitrary simultaneous supervisor/worker/daemon failures still need broader
process-tree and resource-lease qualification.

These are **functional captures**, not deterministic playback or paired Parity.
The separate [owned input observer qualification](input-observer.md) is disabled
in this adapter; passing console controls do not supply actual Stock input tapes.
The optional [API interval recorder](control-format.md) documents `--record-controls`
and experimental `--early-controls`, their exact boundaries and missing coverage. Experimental `--rng-observer` (format 5, requires `--early-controls`)
additionally patches two Stock CRT code spans; see the RNG section there.
OCR cannot prove script state, every intro's completion, simulation consumption
or frame equivalence. The synthetic CD metadata driver does not qualify CD-track
audio. Default capture ends through scoped Wine shutdown. The separate
`--ordinary-exit` lane now clicks Exit Game, observes the Exit/Yes/No dialog,
and clicks Yes. A GUI-subsystem guest helper holds the actual game process
handle and atomically records its signaled wait and exit code before Wine
infrastructure teardown. The lane rejects interval-recorder flags until their
ordinary-shutdown draining is qualified. It does not prove every shutdown
callback, ordinary save/reload, or CD-track fidelity.

The first passing ordinary-exit capture is retained at
`local/verify/parity-completion-20261102/stock-ordinary-exit-fifth/`.
Earlier attempts retain OCR refusal, missing initial UP, a guest capture failure
and a disappearing-window capture race. The GUI helper creates no console or
window. Readiness now requires an actual initial native mouse UP before the
first click; failed polling counts cannot establish input readiness. Final
window evidence is captured before confirmation, while process-handle exit and
PCM finalization are observed afterward. Complete time/input/RNG/scheduler
controls remain open work.

Retained actual checks (each source bundle identifies the exact revision run):

- `local/verify/tracked-stock-adapter-first/`: normal intro/menu/dialog flow,
  625 polls, exact two clicks, 3,228,140 nonzero PCM samples and no owned activation.
- `local/verify/tracked-stock-adapter-second/`: bounded OCR controller, 723 polls,
  exact two clicks, unchanged sources and no owned activation.
- `local/verify/tracked-stock-adapter-timeout/`: one-second deadline rejected as
  incomplete/nonzero, sources preserved and container removed.
- `local/verify/tracked-stock-adapter-interrupted/`: SIGTERM during actual game
  execution; parent rejected missing worker verdict, independently audited source
  preservation and removed the container.

`test_run_stock.py` checks corrupted isolation/flow evidence and invalid identity
rejection. Its counterfactual fixtures never constitute game-run evidence.

## Ordinary Career creation and fixture load

`--career-flow create --ordinary-exit` starts from a private copy of the source
save with no existing Careers, types `SaveProof` through guest XTEST input,
accepts the name/portrait dialogs, observes Garage identity/$20,000, returns to
Main Menu and uses Exit/Yes. `--career-flow load --save-fixture PATH` copies an
explicit immutable generated save into the private game and follows LOAD CAREER.
Both lanes retain normal startup video/audio processing. Interval-recorder flags
remain refused with ordinary exit.

```sh
python3 -I tools/parity/run_stock.py --source "$GAME" \
  --stock ref/raw/StreetLegal_Redline.bak.exe --texture-table ref/raw/texture.dat \
  --disc "$DISC" --out local/verify/NEW-STOCK-CREATE \
  --ordinary-exit --career-flow create --timeout 180
python3 -I tools/parity/save_snapshot.py \
  local/verify/NEW-STOCK-CREATE/game/save local/verify/NEW-STOCK-FIXTURE
python3 -I tools/parity/run_stock.py --source "$GAME" \
  --stock ref/raw/StreetLegal_Redline.bak.exe --texture-table ref/raw/texture.dat \
  --disc "$DISC" --out local/verify/NEW-STOCK-LOAD \
  --ordinary-exit --career-flow load --save-fixture local/verify/NEW-STOCK-FIXTURE/save --timeout 180
python3 -I tools/parity/stock_career_save.py \
  --create local/verify/NEW-STOCK-CREATE --load local/verify/NEW-STOCK-LOAD \
  --fixture local/verify/NEW-STOCK-FIXTURE --out local/verify/NEW-STOCK-PERSISTENCE.json
```

Actual captures are retained at
`local/verify/parity-save-reload-20261102/stock-create-third/` and `stock-load-first/`.
Two earlier creation attempts remain incomplete: a missed OCR title and a
redundant `xdotool mousemove --sync` that waited for nonexistent movement.
The pointer is now observed before each action; redundant movement is omitted
while real native press/release observations remain required.

The ordinary load preserves the observed name, $20,000, portrait word `0x100f0`,
flags, racer index and resource-pack bindings. The full re-saved Career is **not
byte-identical**: its current-time float changes exactly from 43240 to 43320 at
offset10488; two bytes differ and every other file byte is exact. Shipped
`GameLogic.class` save tree94 line801 writes `getTime()`, followed by day and
seven reserved ints; load tree96 line937 restores that float through `setTime`.
The auditor retains this first divergence. It introduces no numeric tolerance
and establishes no deterministic replay or Stock/Port equivalence.

The load-selected frame already shows loading before the scheduled lower-button
action. The capture preserves both the action request and observed frontier;
it does not attribute loading uniquely to that later click. Mouse holds can
trigger repeated UI callbacks, so planned clicks are not consumption tapes.
Both completed captures retain source/fixture preservation, private nonzero PCM,
no owned activation observed and process-handle exit0 before Wine teardown.
Synthetic CD metadata remains disclosed; authentic CD layout/audio validation
was explicitly excluded by the user. File/video audio remains exercised.
