#!/usr/bin/env bash
# Send synthetic input to the running game through the proxy's input.cmd file.
# Usage: tools/wine/capture/send.sh RUN-DIR "click 790 316" "key enter" "sleep 500" ...
# Commands: move X Y | click [X Y] | dclick [X Y] | mdown | mup | key NAME [ms] | keydown NAME | keyup NAME | sleep MS
set -euo pipefail
RUN="${1:?run dir}"; shift
# Lines starting with "di " go to the DirectInput proxy (dinput.cmd) instead: see dinput8_proxy.c
for line in "$@"; do
  case "$line" in
    "di "*) printf '%s\n' "${line#di }" >> "$RUN/game/dumps/dinput.cmd";;
    *) printf '%s\n' "$line" >> "$RUN/game/dumps/input.cmd";;
  esac
done
