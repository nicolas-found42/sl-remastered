#!/usr/bin/env bash
# Stop the Wine session of a capture run. Usage: tools/wine/capture/stop.sh RUN-DIR
set -euo pipefail
RUN="$(cd "${1:?run dir}" && pwd)"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
export WINEPREFIX="$RUN/prefix"
"${WINEDIR:-$ROOT/local/wine-runs/winehq-source-20261028/build}/server/wineserver" -k || true
