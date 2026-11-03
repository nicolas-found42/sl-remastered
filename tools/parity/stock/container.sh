set -euo pipefail
export DISPLAY=:99 WINEPREFIX=/work/wineprefix WINEDEBUG=err+all,trace+quartz PULSE_SERVER=unix:/tmp/pulse/native
if [ "${SLRR_PREFIX_EPHEMERAL:-0}" = 1 ]; then
 export WINEPREFIX=/privateprefix PREFIX_PAYLOAD=/work/instrumentation-source
 finish_prefix() {
  local status=$? stopped waited archived
  trap - EXIT
  set +e
  timeout --kill-after=2 15 wineserver -k
  stopped=$?
  timeout --kill-after=2 15 wineserver -w
  waited=$?
  printf '%s %s\n' "$stopped" "$waited" >/work/prefix-quiescence.txt
  sha256sum /usr/lib/wine/aarch64-windows/wowbox64.dll "$WINEPREFIX/drive_c/windows/system32/wowbox64.dll" "$WINEPREFIX/drive_c/windows/syswow64/dinput8.dll" >/work/input-runtime-final-sha256.txt
  export PREFIX_QUIESCENCE=0
  if [ "$stopped" = 0 ] && [ "$waited" = 0 ]; then export PREFIX_QUIESCENCE=1; fi
  /bin/bash "$PREFIX_PAYLOAD/prefix_archive.sh" finish
  archived=$?
  printf '%s\n' "$archived" >/work/prefix-archive-exit.txt
  if [ "$archived" != 0 ] || [ "$PREFIX_QUIESCENCE" != 1 ]; then status=67; fi
  exit "$status"
 }
 trap finish_prefix EXIT
 /bin/bash "$PREFIX_PAYLOAD/prefix_archive.sh" prepare
fi
mkdir -p /work/game/dumps
for artifact in stock-input-setup.jsonl stock-control-tape.jsonl stock-control-shutdown.json; do
 if [ -e "/work/game/dumps/$artifact" ] || [ -L "/work/game/dumps/$artifact" ]; then
  printf 'refusing existing capture artifact: %s\n' "$artifact" >&2
  exit 64
 fi
done
export SLRR_DUMP_DIR='Z:\work\game\dumps'
mkdir -p /tmp/pulse
pulseaudio -n --daemonize=no --exit-idle-time=-1 -L 'module-native-protocol-unix socket=/tmp/pulse/native auth-anonymous=1' -L 'module-null-sink sink_name=stock rate=48000 channels=2' >/work/pulse.log 2>&1 &
timeout 15 sh -c 'until pactl info >/dev/null 2>&1; do sleep 1; done'
Xvfb :99 -screen 0 800x600x24 -nolisten tcp >/work/xvfb.log 2>&1 &
timeout 15 sh -c 'until xdpyinfo >/dev/null 2>&1; do sleep 1; done'
wine --version >/work/wine-version.txt
if [ "${SLRR_PREFIX_SEEDED:-0}" != 1 ]; then
 WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' timeout 30 wine wineboot.exe -i >/work/wineboot.log 2>&1
else
 printf 'qualified stopped private used-prefix clone; no explicit wineboot -i\n' >/work/wineboot.log
fi
sha256sum /usr/lib/wine/aarch64-windows/wowbox64.dll "$WINEPREFIX/drive_c/windows/system32/wowbox64.dll" "$WINEPREFIX/drive_c/windows/syswow64/dinput8.dll" >/work/input-runtime-sha256.txt
ln -sfn /disc "$WINEPREFIX/dosdevices/d:"
WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' timeout 10 wine reg add 'HKCU\Software\Wine\Drivers' /v Audio /t REG_SZ /d pulse /f >/work/audio-selection.log 2>&1
WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' timeout 10 wine reg add 'HKLM\Software\Wine\Drives' /v 'd:' /t REG_SZ /d cdrom /f >/work/disc-selection.log 2>&1
wineserver -k || true
timeout 10 wineserver -w || true
set +e
WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' timeout 10 wine /work/probe32.exe >/work/probe32.log 2>&1
echo $? >/work/probe32-exit.txt
set -e
parecord --device=stock.monitor --file-format=wav --rate=48000 --channels=2 /work/stock.wav >/work/record.log 2>&1 &
REC=$!
cd /work/game
export WINEDLLOVERRIDES='mcicda=n;d3d9=n,b;dinput8=n,b;mscoree,mshtml=;winemenubuilder.exe=d' SLRR_SYNTHETIC_CD_METADATA=/work/metadata.txt SLRR_DUMP_EVERY=30
TARGET='Z:\work\game\StreetLegal_Redline.exe'
if [ "${SLRR_ORDINARY_EXIT:-0}" = 1 ]; then TARGET='Z:\work\wait_game.exe'; fi
wine explorer /desktop=slrr,800x600 "$TARGET" >/work/game.log 2>&1 &
GAME=$!
date -u +%s%N > /work/game-start-time.txt
touch /work/game.ready
for elapsed in $(seq 0 "${SLRR_CONTROLLER_SECONDS:-90}"); do
 if [ -e /work/controller.done ]; then break; fi
 case "$elapsed" in
  3|12|24) date -u +%s%N > /work/frame-${elapsed}-time.txt; xwd -root -silent -out /work/frame-${elapsed}.xwd;;
 esac
 sleep 1
done
ps -eo pid,comm,args >/work/processes-final.txt
if [ "${SLRR_ORDINARY_EXIT:-0}" != 1 ]; then
 xwininfo -root -tree >/work/windows.txt
 xwd -root -silent -out /work/root.xwd
fi
pactl list sink-inputs >/work/audio-streams.txt
pactl list sinks >/work/audio-sinks.txt
STATUS=0
if [ "${SLRR_CONTROL_TAPE:-0}" = 1 ] && [ "${SLRR_ORDINARY_EXIT:-0}" != 1 ]; then
 printf 'tape_stop\n' >> /work/game/dumps/input.cmd
 if ! timeout 5 sh -c 'until grep -q "control_tape_stop result=" /work/game/dumps/d3d9proxy.log; do sleep .1; done'; then
  echo 'record-only API recorder did not stop before its deadline' >/work/control-stop-error.txt
  STATUS=65
 elif ! python3 /work/instrumentation-source/shutdown_evidence.py --stop-receipt /work/game/dumps/d3d9proxy.log >/work/control-stop-receipt.json; then
  echo 'record-only API recorder explicitly refused its final commit' >/work/control-stop-error.txt
  STATUS=65
 fi
fi
if [ "${SLRR_ORDINARY_EXIT:-0}" = 1 ]; then
 if [ ! -e /work/game-process-exit.json ]; then
  echo "game handle did not signal ordinary exit" >/work/ordinary-exit-error.txt
  STATUS=66
 else
  date -u +%s%N >/work/ordinary-exit-observed-time.txt
 fi
fi
# Teardown of Wine infrastructure happens only after the exit observation above.
if [ "${SLRR_PREFIX_EPHEMERAL:-0}" != 1 ]; then wineserver -k || true; fi
kill -INT "$REC"; wait "$REC" || true
sha256sum /usr/lib/wine/aarch64-windows/wowbox64.dll "$WINEPREFIX/drive_c/windows/system32/wowbox64.dll" "$WINEPREFIX/drive_c/windows/syswow64/dinput8.dll" >/work/input-runtime-after-sha256.txt
exit "$STATUS"
