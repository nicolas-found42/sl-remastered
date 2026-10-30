#!/usr/bin/env bash
# Replays the real mouse and key input of an earlier run and says whether the same failure happens again.
#   tools/replay.sh latest            the newest run in local/runs
#   tools/replay.sh DIR               a specific run folder
#   tools/replay.sh DIR --same-window accepted for compatibility (see docs/run-log.md)
# The replay is a NEW run folder. It is time-based, not frame-exact (docs/run-log.md, Limits).
set -euo pipefail
cd "$(dirname "$0")/.."
SLRR_SRC_HASH="$(python3 -I tools/src_hash.py)"
export SLRR_SRC_HASH
exec python3 -I tools/replay.py "$@"
