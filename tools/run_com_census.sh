#!/usr/bin/env bash
# Rebuild the COM call census from the stock executable.
#
# Lists every COM-style call and prints the attribution table. The first run builds
# local/ghidra-proj through tools/ghidra_run.sh, which also defines the functions that
# Ghidra's own analysis missed. Delete that directory to start from an empty project.
set -euo pipefail
cd "$(dirname "$0")/.."

tools/ghidra_run.sh ComCallCensus.java local/census/com_calls.tsv 40
echo "run_com_census: wrote $(($(wc -l < local/census/com_calls.tsv) - 1)) call sites to local/census/com_calls.tsv"
python3 -I tools/com_attrib.py
