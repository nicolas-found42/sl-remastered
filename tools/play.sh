#!/usr/bin/env bash
# Opens the native port in a visible macOS window and runs the real 2005 game scripts.
#   tools/play.sh                       open the window and use the mouse yourself
#   tools/play.sh --script FILE         replay timed mouse and key actions (see tools/scripts/)
# Every run leaves local/runs/<time>-<pid>/ (log, metadata, result). Explain the newest one with tools/diagnose.sh.
# Script verbs, one per line as "<seconds since the loop started> <verb> [args]":
#   move X Y | down | up | keydown NAME | keyup NAME | shot PATH | quit
#   fail error|segv|abort|throw   fail on purpose, to test the run log (docs/run-log.md)
# Your mouse and keys are recorded to input.txt in the run folder; tools/replay.sh <run-dir|latest> plays them back
# (docs/run-log.md). The run's meta.txt also records the code hash (tools/src_hash.py, exported as SLRR_SRC_HASH).
# Close the window or press Ctrl+C in the terminal to stop. Saves go to local/stock-data, never to ref/.
set -euo pipefail
cd "$(dirname "$0")/.."
game=".scratch/2005-extract/Program Files/Street Legal Racing - Redline"
data="local/stock-data"
[ -d "$game" ] || { echo "play: missing $game (extract the 2005 installer there, see docs/phase2.md)" >&2; exit 1; }
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build --target startup_probe
mkdir -p "$data/save/game"
[ -f "$data/save/game/options" ] || cp "$game/save/game/options" "$data/save/game/options"
# Capture a private, bounded inventory before launch. It is not a trace of files read.
unset SLRR_GAME_DATA_MANIFEST SLRR_GAME_DATA_ROOT SLRR_GAME_DATA_SHA256
SLRR_GAME_DATA_STATUS=unavailable
if manifest_info="$(python3 -I - "$game" <<'PYMANIFEST'
import hashlib, importlib.util, os, pathlib, tempfile, sys
root = pathlib.Path(sys.argv[1]).resolve(strict=True)
base = pathlib.Path('local/data-manifests').resolve()
ref = pathlib.Path('ref').resolve()
if base == ref or ref in base.parents or base == root or root in base.parents:
    raise ValueError('manifest directory must be outside ref and the game root')
base.mkdir(parents=True, exist_ok=True)
os.chmod(base, 0o700)
folder = pathlib.Path(tempfile.mkdtemp(prefix='play-', dir=base))
spec = importlib.util.spec_from_file_location('diagnostic_data', 'tools/parity/diagnostic-data.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
manifest = folder / 'manifest.json'
result = module.write_manifest(root, manifest)
digest = hashlib.sha256(manifest.read_bytes()).hexdigest()
print(str(manifest), str(root), digest, 'complete' if result['complete'] else 'incomplete', sep='\t')
PYMANIFEST
)"; then
  IFS=$'\t' read -r SLRR_GAME_DATA_MANIFEST SLRR_GAME_DATA_ROOT SLRR_GAME_DATA_SHA256 SLRR_GAME_DATA_STATUS <<< "$manifest_info"
  export SLRR_GAME_DATA_MANIFEST SLRR_GAME_DATA_ROOT SLRR_GAME_DATA_SHA256
else
  echo 'play: game-data inventory unavailable; continuing without it' >&2
fi
export SLRR_GAME_DATA_STATUS
SLRR_SRC_HASH="$(python3 -I tools/src_hash.py)"
export SLRR_SRC_HASH
exec build/startup_probe --init --renderer --loop --visible --data-root "$data" "$@" "$game"
