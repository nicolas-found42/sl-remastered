#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
python3 -I tools/ref_guard.py verify
mkdir -p local/verify
out=$(mktemp -d "$PWD/local/verify/road-inside-docker.XXXXXX")
cp ref/raw/maps/city/ai/city.trc "$out/city.trc"
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/parity/road-inside/inside.c -o "$out/inside.exe"
clang++ -std=c++17 -ffp-contract=off -Isrc tools/parity/road-inside/native.cpp src/road_network.cpp -o "$out/native"
SLRR_ORACLE_BACKEND=docker tools/wine/run_original_oracle.sh "$out" "$out/inside.exe" 'Z:\out\city.trc' > "$out/stock.txt" 2> "$out/stderr.txt"
"$out/native" "$out/city.trc" > "$out/native.txt"
tr -d '\r' < "$out/stock.txt" > "$out/stock-normalized.txt"
diff -u "$out/stock-normalized.txt" "$out/native.txt" > "$out/comparison.diff"
python3 -I tools/ref_guard.py verify
printf 'road inside comparison passed: %s\n' "$out"
