#!/usr/bin/env bash
# Print one function from the saved whole-program decompile (local/census/all.c), e.g. tools/decomp_fn.sh 00442f90.
# Read-only; does not run Ghidra. all.c was made by DecompileAll.java on the stock StreetLegal_Redline.bak.exe project.
cd "$(dirname "$0")/.."
for a in "$@"; do
  awk -v want="//// FUNCTION FUN_$a $a" '
    index($0, "//// FUNCTION") == 1 { on = ($0 == want) }
    on { print }' local/census/all.c
done
