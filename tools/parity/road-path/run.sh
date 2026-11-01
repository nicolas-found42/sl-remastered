#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p local/verify
out=$(mktemp -d "$PWD/local/verify/road-path.XXXXXX")
cp ref/raw/maps/city/ai/city.trc "$out/city.trc"
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/parity/road-path/probe.c -o "$out/probe.exe"
clang++ -std=c++17 -ffp-contract=off -Isrc tools/parity/road-path/native.cpp src/road_network.cpp -o "$out/native"
fixture="Z:$(printf '%s' "$out/city.trc" | sed 's,/,\\,g')"
SLRR_ORACLE_BACKEND=mac tools/wine/run_original_oracle.sh "$out" "$out/probe.exe" "$fixture" > "$out/stock.txt" 2> "$out/stderr.txt"
python3 -I tools/parity/road-path/compare.py "$out/stock.txt" > "$out/comparison.txt"
"$out/native" "$out/city.trc" > "$out/native.txt"
python3 -I tools/parity/road-path/compare_native.py "$out/stock.txt" "$out/native.txt" > "$out/native-comparison.txt"
printf 'road path comparison passed: %s\n' "$out"
