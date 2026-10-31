#!/usr/bin/env bash
# Run the UNCHANGED stock exe under native macOS Wine 11 with the synthetic CD
# shim and the in-process frame dumper (d3d9_proxy.c). Not part of tools/check.sh.
# Usage: tools/wine/capture/run_capture.sh [RUN-DIR]    (default: local/wine-runs/capture-<timestamp>)
# The run directory must not exist. Leaves Wine running in the background; stop it with
#   tools/wine/capture/stop.sh RUN-DIR
set -euo pipefail
cd "$(dirname "$0")/../../.."
ROOT="$PWD"
RUN="${1:-local/wine-runs/capture-$(date +%Y%m%d-%H%M%S)}"
WINEDIR="${WINEDIR:-$ROOT/local/wine-runs/winehq-source-20261028/build}"   # macOS-driver build; use build-x11 for XQuartz
SOURCE="${SOURCE:-$ROOT/.scratch/2005-extract/Program Files/Street Legal Racing - Redline}"
DISC="${DISC:-$ROOT/local/wine-runs/synthetic-run/stock-2005/disc}"
STOCK_MD5=e686099e7233bb7d7c461b1b35c5064c
DESKTOP="${DESKTOP:-800x600}"          # set DESKTOP=none to run without a Wine virtual desktop
GCC=/opt/homebrew/bin/i686-w64-mingw32-gcc

[ -d "$SOURCE" ] || { echo "game data not found: $SOURCE" >&2; exit 1; }
[ -d "$DISC" ] || { echo "disc directory not found: $DISC" >&2; exit 1; }
case "$(cd "$(dirname "$RUN")" && pwd)/$(basename "$RUN")" in
  "$ROOT"/ref/*) echo "run directory must be outside ref/" >&2; exit 1;; esac
mkdir -p "$(dirname "$RUN")"
mkdir "$RUN" || { echo "run directory must not already exist: $RUN" >&2; exit 1; }
RUN="$(cd "$RUN" && pwd)"
python3 -I tools/ref_guard.py verify

# 1. writable game copy with the stock executable (clone copy is fast on APFS)
mkdir "$RUN/game"
cp -c -R "$SOURCE/." "$RUN/game/" 2>/dev/null || cp -R "$SOURCE/." "$RUN/game/"
chmod -R u+w "$RUN/game"
cp -f ref/raw/StreetLegal_Redline.bak.exe "$RUN/game/StreetLegal_Redline.exe"
[ "$(md5 -q "$RUN/game/StreetLegal_Redline.exe")" = "$STOCK_MD5" ] || { echo "exe MD5 mismatch" >&2; exit 1; }

# 2. synthetic CD table + driver (SYNTHETIC: see docs/wine-comparison.md) and the frame-dump proxy
python3 -I tools/wine/synthetic_metadata.py ref/raw/StreetLegal_Redline.bak.exe ref/raw/texture.dat \
  "$RUN/metadata.txt" "$RUN/SYNTHETIC-metadata.txt"
"$GCC" -shared -O2 -Wl,--kill-at -o "$RUN/game/mcicda.dll" tools/wine/synthetic_cd.c -lwinmm
"$GCC" -shared -O2 -Wall -Wno-format-truncation -Wl,--kill-at -static-libgcc -o "$RUN/game/d3d9.dll" tools/wine/capture/d3d9_proxy.c tools/wine/capture/walk_trace.c -ladvapi32
"$GCC" -shared -O2 -Wall -Wno-format-truncation -Wl,--kill-at -static-libgcc -o "$RUN/game/dinput8.dll" tools/wine/capture/dinput8_proxy.c

# Record the data actually present in the private game copy before launch.
# A failed or incomplete scan is recorded and does not fabricate an identity.
python3 -I "$ROOT/tools/parity/diagnostic-data.py" "$RUN/game" "$RUN/game-data-manifest.json" > "$RUN/data-manifest.log" 2>&1 || true

# 3. Wine prefix with a fake CD-ROM drive D:
export WINEPREFIX="$RUN/prefix" WINEDEBUG="${WINEDEBUG:-err+all}"
WINE="$WINEDIR/wine"
if [ -n "${PREFIX_FROM:-}" ]; then
  # faster: clone the prefix of an earlier capture run (it must not be running)
  cp -c -R "$PREFIX_FROM" "$WINEPREFIX" 2>/dev/null || cp -R "$PREFIX_FROM" "$WINEPREFIX"
else
  # no Mono/Gecko: their installer dialogs block wineboot (no network needed for this game)
  WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' "$WINE" wineboot -i >"$RUN/wineboot.log" 2>&1 || true
  "$WINEDIR/server/wineserver" -w || true
  ln -sfn "$DISC" "$WINEPREFIX/dosdevices/d:"
  WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' "$WINE" reg add 'HKLM\Software\Wine\Drives' /v 'd:' /t REG_SZ /d cdrom /f >"$RUN/drive-setting.log" 2>&1
  "$WINEDIR/server/wineserver" -k || true; sleep 2; "$WINEDIR/server/wineserver" -w || true
fi

# 4. launch (background)
cd "$RUN/game"
WIN_METADATA="Z:${RUN//\//\\}\\metadata.txt"
export WINEDLLOVERRIDES='mcicda=n;d3d9=n,b;dinput8=n,b;mscoree,mshtml=' SLRR_SYNTHETIC_CD_METADATA="$WIN_METADATA"
export SLRR_DUMP_EVERY="${SLRR_DUMP_EVERY:-60}"
GAME_WIN="Z:${RUN//\//\\}\\game\\StreetLegal_Redline.exe"   # explorer does not search the current directory
if [ "$DESKTOP" = none ]; then CMD=("$GAME_WIN"); else CMD=(explorer "/desktop=slrr,$DESKTOP" "$GAME_WIN"); fi
python3 - "$RUN/launch.json" "$WINE" "$SOURCE" "$DESKTOP" "${CMD[@]}" <<'PY'
import hashlib, json, pathlib, sys
path, wine, source, desktop, *args = sys.argv[1:]
manifest_path = pathlib.Path(path).parent / 'game-data-manifest.json'
try:
    raw = manifest_path.read_bytes()
    manifest = json.loads(raw)
    identity = {'status': 'complete' if manifest['complete'] else 'incomplete',
                'path': str(manifest_path), 'sha256': hashlib.sha256(raw).hexdigest()}
except (OSError, ValueError, KeyError) as error:
    identity = {'status': 'unavailable', 'reason': str(error)}
with open(path, 'x') as f:
    json.dump({'wine_path': wine, 'wine_version': None, 'source': source,
               'desktop': desktop, 'argv': [wine, *args], 'game_data': identity}, f, indent=2)
    f.write('\n')
PY
nohup "$WINE" "${CMD[@]}" >"$RUN/game-run.log" 2>&1 &
echo $! >"$RUN/wine.pid"
echo "$RUN"
