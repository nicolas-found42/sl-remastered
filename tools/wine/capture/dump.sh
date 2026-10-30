#!/usr/bin/env bash
# Ask the proxy for one frame dump on the next Present. Usage: tools/wine/capture/dump.sh RUN-DIR
set -euo pipefail
touch "${1:?run dir}/game/dumps/dump.now"
