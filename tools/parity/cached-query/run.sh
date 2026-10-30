#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
out="${1:-$PWD/local/verify/cached-query-$(date +%Y%m%d-%H%M%S)-$$}"
parent="$(cd "$(dirname "$out")" && pwd -P)"
out="$parent/$(basename "$out")"
case "$out" in "$PWD/ref"|"$PWD/ref/"*) echo 'output must be outside ref/' >&2; exit 1;; esac
mkdir "$out"
mkdir "$out/build"
printf 'Evidence: %s\n' "$out"
fixture="$PWD/local/verify/core-pair-oracle/corpus-data/fixtures.bin"
[ -f "$fixture" ] || { echo "Missing fixture: run tools/wine/core_pair_oracle.sh corpus setup first" >&2; exit 1; }
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/parity/cached-query/stock.c -o "$out/stock.exe"
clang++ -std=c++17 -O2 -ffp-contract=off -Isrc tools/parity/cached-query/compare.cpp src/physics_cached.cpp src/physics_polyhedron.cpp src/physics_body.cpp -o "$out/build/compare"
fixture_windows="Z:$(printf '%s' "$fixture" | sed 's,/,\\,g')"
tools/wine/run_original_oracle.sh "$out" "$out/stock.exe" "$fixture_windows" > "$out/stock.stdout" 2> "$out/stock.stderr"
"$out/build/compare" "$out/stock.stderr" > "$out/comparison.txt"
cat "$out/comparison.txt"
