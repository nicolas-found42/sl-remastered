#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
python3 -I tools/ref_guard.py verify
mkdir -p local/verify
out=$(mktemp -d "$PWD/local/verify/road-other-fresh.XXXXXX")
cp ref/raw/maps/city/ai/city.trc "$out/city.trc"
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/parity/road-other/fresh.c -o "$out/fresh.exe"
clang++ -std=c++17 -ffp-contract=off -Isrc tools/parity/road-other/fresh_native.cpp src/road_network.cpp -o "$out/native"
fixture="Z:$(printf '%s' "$out/city.trc" | sed 's,/,\\,g')"
SLRR_ORACLE_BACKEND=mac tools/wine/run_original_oracle.sh "$out" "$out/fresh.exe" "$fixture" > "$out/stock.txt" 2> "$out/stderr.txt"
"$out/native" "$out/city.trc" > "$out/native.txt"
python3 -I tools/parity/road-other/compare_fresh.py "$out/stock.txt" "$out/native.txt" > "$out/comparison.txt"
python3 -I tools/ref_guard.py verify
printf 'fresh road other comparison passed: %s\n' "$out"
