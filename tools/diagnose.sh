#!/usr/bin/env bash
# Explains a startup_probe run from its run directory without re-running it.
#   tools/diagnose.sh            the newest run (local/runs/latest)
#   tools/diagnose.sh list       the newest 10 runs, one line each
#   tools/diagnose.sh DIR        a specific run directory
# SLRR_RUN_DIR changes where 'latest' and 'list' look.
set -euo pipefail
cd "$(dirname "$0")/.."
exec python3 -I tools/diagnose.py "$@"
