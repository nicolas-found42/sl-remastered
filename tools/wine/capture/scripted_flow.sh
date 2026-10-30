#!/usr/bin/env bash
# Scripted walk through the stock game on an ALREADY RUNNING capture run:
# Main Menu -> hover Free Ride -> click -> loading screen -> track -> ~65 s of keyboard driving.
# Saves macOS window screenshots under RUN/shots/ and the in-process frame dumps stay in RUN/game/dumps/.
# Usage: tools/wine/capture/scripted_flow.sh RUN-DIR
# Coordinates: the game window sits 33 px below the top of Wine's 800x600 desktop, so the game-space point
# (483,156) of "FREE RIDE" is desktop (483,189). Clicks go through the DirectInput proxy (see README).
set -euo pipefail
cd "$(dirname "$0")/../../.."
RUN="$(cd "${1:?run dir}" && pwd)"
D="$RUN/game/dumps"; S="$RUN/shots"; mkdir -p "$S/flow"
FLOW="$RUN/flow.log"
SEND=tools/wine/capture/send.sh
say() { echo "$(date +%H:%M:%S) $*" | tee -a "$FLOW"; }
winid() { swift tools/wine/capture/winlist.swift wine | grep 'SL2 build' | head -1 | awk '{print $1}'; }
shot() { screencapture -x -o -l"$(winid)" "$S/flow/$1.png"; say "shot flow/$1.png (presents so far: $(grep -o 'presents=[0-9]*' "$D/d3d9proxy.log" | tail -1))"; }

say "waiting for the Main Menu (presents >= 700)"
waited=0
until [ "$(grep -o 'presents=[0-9]*' "$D/d3d9proxy.log" 2>/dev/null | tail -1 | cut -d= -f2)" -ge 700 ] 2>/dev/null; do
  sleep 2; waited=$((waited+2))
  # about 2 of 7 launches died or hung right after the first Present (see README); give up so the run can be repeated
  if ! pgrep -f '^Z:.*StreetLegal_Redline.exe' >/dev/null && [ $waited -gt 10 ]; then say "game process is gone before the menu: intermittent startup crash, relaunch with a new run"; exit 2; fi
  if [ $waited -gt 120 ]; then say "no menu after 120 s: stuck at startup, relaunch with a new run"; exit 3; fi
done
sleep 3
shot 00-main-menu
tools/wine/capture/dump.sh "$RUN"
say "move pointer onto FREE RIDE (desktop 483,189) and wait"
$SEND "$RUN" "where" "move 483 189"; sleep 2
shot 01-free-ride-hover
say "click Free Ride (DirectInput left button 0, 300 ms)"
$SEND "$RUN" "di mclick 0 300"
for t in 1 2 3 4 5 6 7 8 9 10 12 14 16 18 20; do sleep 1; [ $t -ge 12 ] && sleep 1; shot "02-after-click-$(printf %02d $t)s"; done
say "start keyboard script (~65 s): Up held; steer left/right; brake; camera key C; Escape menu at the end"
START=$(date +%s)
$SEND "$RUN" "di kdown 0xC8" "di sleep 6000" "di kdown 0xCB" "di sleep 1200" "di kup 0xCB" "di sleep 3000" \
  "di kdown 0xCD" "di sleep 1200" "di kup 0xCD" "di sleep 5000" "di key 0x2E 150" "di sleep 4000" \
  "di kup 0xC8" "di kdown 0xD0" "di sleep 2500" "di kup 0xD0" "di kdown 0xC8" "di sleep 8000" \
  "di kdown 0xCB" "di sleep 2500" "di kup 0xCB" "di sleep 7000" "di kdown 0xCD" "di sleep 2500" "di kup 0xCD" \
  "di sleep 8000" "di kup 0xC8" "di key 0x01 200"
n=0
while [ $(( $(date +%s) - START )) -lt 68 ]; do n=$((n+1)); shot "03-drive-$(printf %02d $(( $(date +%s) - START )))s"; sleep 4; done
say "script finished; last log lines:"
tail -3 "$D/d3d9proxy.log" | cut -c1-160 | tee -a "$FLOW"
