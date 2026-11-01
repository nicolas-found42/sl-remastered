#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p local/verify
out=$(mktemp -d "$PWD/local/verify/road-other.XXXXXX")
cp ref/raw/maps/city/ai/city.trc "$out/city.trc"
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/parity/road-other/probe.c -o "$out/probe.exe"
clang++ -std=c++17 -ffp-contract=off -Isrc tools/parity/road-other/native.cpp src/road_network.cpp -o "$out/native"
fixture="Z:$(printf '%s' "$out/city.trc" | sed 's,/,\\,g')"
SLRR_ORACLE_BACKEND=mac tools/wine/run_original_oracle.sh "$out" "$out/probe.exe" "$fixture" > "$out/stock.txt" 2> "$out/stderr.txt"
"$out/native" "$out/city.trc" > "$out/native.txt"
tr -d '\r' < "$out/stock.txt" > "$out/stock-normalized.txt"
diff -u "$out/stock-normalized.txt" "$out/native.txt" > "$out/comparison.diff"
printf 'road other comparison passed: %s\n' "$out"
