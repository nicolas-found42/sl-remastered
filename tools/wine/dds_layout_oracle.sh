#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out="$(python3 -I tools/wine/oracle_new_dir.py dds-layout-oracle)"
printf 'Oracle evidence: %s\n' "$out" >&2
python3 -I tools/dds_layout_fixtures.py ".scratch/2005-extract/Program Files/Street Legal Racing - Redline" "$out"
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/wine/dds_layout_oracle.c -o "$out/stock.exe"
clang++ -std=c++17 -O2 -Isrc tests/dds_layout_probe.cpp src/dds.cpp -o "$out/native"
"$out/native" "$out/fixtures.bin" > "$out/native.txt"
case "${SLRR_ORACLE_BACKEND:-mac}" in
  docker) fixture='Z:\out\fixtures.bin' ;;
  mac) fixture="Z:$(printf '%s' "$out/fixtures.bin" | sed 's,/,\\,g')" ;;
esac
tools/wine/run_original_oracle.sh "$out" "$out/stock.exe" "$fixture" > "$out/stock.txt" 2> "$out/wine.err"
tr -d '\r' < "$out/stock.txt" > "$out/stock-normalized.txt"
diff -u "$out/stock-normalized.txt" "$out/native.txt" > "$out/comparison.txt"
