#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p local/verify
out=$(mktemp -d "$PWD/local/verify/road-record-docker.XXXXXX")
cp ref/raw/maps/city/ai/city.trc "$out/city.trc"
for part in nodes payload; do
  i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 "tools/parity/road-record/$part.c" -o "$out/$part.exe"
  clang++ -std=c++17 -Isrc "tools/parity/road-record/$part.cpp" src/road_network.cpp -o "$out/$part-native"
  SLRR_ORACLE_BACKEND=docker tools/wine/run_original_oracle.sh "$out" "$out/$part.exe" 'Z:\out\city.trc' > "$out/$part-stock.txt" 2> "$out/$part-stderr.txt"
  "$out/$part-native" "$out/city.trc" > "$out/$part-native.txt"
  tr -d '\r' < "$out/$part-stock.txt" > "$out/$part-stock-normalized.txt"
  diff -u "$out/$part-stock-normalized.txt" "$out/$part-native.txt" > "$out/$part.diff"
done
printf 'road record comparison passed: %s\n' "$out"
