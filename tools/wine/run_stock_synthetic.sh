#!/usr/bin/env bash
# Run the UNCHANGED stock exe in Wine with a SYNTHETIC cdaudio table. Needs Docker; not part of tools/check.sh.
# Usage: tools/wine/run_stock_synthetic.sh NEW-RUN-DIR
set -euo pipefail
cd "$(dirname "$0")/../.."
RUN="${1:?pass a new run directory}"
IMAGE=slrr-stock-wine-gl
DISC="${DISC:-$PWD/local/wine-runs/synthetic-run/stock-2005/disc}"
STOCK_MD5=e686099e7233bb7d7c461b1b35c5064c
FRAMES="${FRAMES:-60 180 360 600 900}"
LIMIT="${LIMIT:-960}"

[ -d "$DISC" ] || { echo "disc directory not found: $DISC" >&2; exit 1; }
python3 -I - "$RUN" "$PWD/ref" <<'PYGUARD'
import os
import sys
run = os.path.realpath(sys.argv[1])
protected = os.path.realpath(sys.argv[2])
if os.path.commonpath((run, protected)) == protected:
    sys.exit("run directory must be outside ref/")
PYGUARD
mkdir -p "$(dirname "$RUN")"
mkdir "$RUN" || { echo "run directory must not already exist: $RUN" >&2; exit 1; }
RUN="$(cd "$RUN" && pwd)"
DISC="$(cd "$DISC" && pwd)"
docker image inspect "$IMAGE" >/dev/null 2>&1 ||
  docker build --platform linux/amd64 -t "$IMAGE" -f tools/wine/Dockerfile tools/wine
python3 -I tools/wine/synthetic_metadata.py ref/raw/StreetLegal_Redline.bak.exe ref/raw/texture.dat \
  "$RUN/metadata.txt" "$RUN/SYNTHETIC-metadata.txt"
i686-w64-mingw32-gcc -shared -O2 -Wl,--kill-at -o "$RUN/mcicda.dll" tools/wine/synthetic_cd.c -lwinmm
i686-w64-mingw32-gcc -O2 -static -o "$RUN/synthetic_cd_probe.exe" tools/wine/synthetic_cd_probe.c -lwinmm

mkdir "$RUN/game"
cp -R ref/raw/. "$RUN/game/"
chmod -R u+w "$RUN/game"
cp -f ref/raw/StreetLegal_Redline.bak.exe "$RUN/game/StreetLegal_Redline.exe"
[ "$(md5 -q "$RUN/game/StreetLegal_Redline.exe" 2>/dev/null || md5sum "$RUN/game/StreetLegal_Redline.exe" | cut -d' ' -f1)" = "$STOCK_MD5" ]
cp -f "$RUN/mcicda.dll" "$RUN/synthetic_cd_probe.exe" "$RUN/game/"

NAME="slrr-synth-$(basename "$RUN" | tr -cd '[:alnum:]_.-')-$$"
docker run -d --name "$NAME" --platform linux/amd64 --network none \
  -e GALLIUM_DRIVER="${GALLIUM_DRIVER:-llvmpipe}" -e LP_NUM_THREADS="${LP_NUM_THREADS:-0}" -e WINEDEBUG=err+all -e WINEDLLOVERRIDES='mcicda=n' -e SLRR_SYNTHETIC_CD_METADATA=/work/metadata.txt \
  --mount type=bind,src="$RUN",dst=/work --mount type=bind,src="$DISC",dst=/disc,readonly \
  "$IMAGE" bash -c "LIMIT=$LIMIT; "'
    export WINEPREFIX=/work/wineprefix
    wineboot -i >/dev/null 2>&1; wineserver -w
    ln -sfn /disc "$WINEPREFIX/dosdevices/d:"
    wine reg add "HKLM\\Software\\Wine\\Drives" /v "d:" /t REG_SZ /d cdrom /f >/dev/null
    wineserver -k; sleep 2; wineserver -w
    cd /work/game
    wine synthetic_cd_probe.exe > /work/probe.log 2>&1
    wineserver -k; sleep 2; wineserver -w
    timeout $LIMIT xvfb-run -a -s "-screen 0 1024x768x24" wine explorer /desktop=slrr,800x600 StreetLegal_Redline.exe > /work/game-run.log 2>&1' >/dev/null
trap 'docker stop "$NAME" >/dev/null 2>&1 || true' EXIT

prev=0
for t in $FRAMES; do
  sleep $((t - prev)); prev=$t
  if captured=$(docker exec "$NAME" bash -c 'export DISPLAY=:$(ls /tmp/.X11-unix | head -1 | tr -d X)
    export XAUTHORITY=$(ls /tmp/xvfb-run.*/Xauthority | head -1)
    xwd -root -silent -out /work/frame'"$t"'-root.xwd || exit 1
    echo root
    id=$(xwininfo -root -tree | grep "SL2 build" | head -1 | grep -o "0x[0-9a-f]*" | head -1)
    if [ -n "$id" ]; then
      xwd -silent -id "$id" -out /work/frame'"$t"'-game.xwd || exit 1
      echo game
    fi'); then
    for k in $captured; do
      ffmpeg -loglevel error -y -i "$RUN/frame$t-$k.xwd" -pix_fmt rgb24 "$RUN/frame$t-$k.png"
      echo "frame $RUN/frame$t-$k.png"
    done
  else
    echo "no frame at ${t}s (display or capture unavailable)"
  fi
done
docker stop "$NAME" >/dev/null 2>&1 || true
