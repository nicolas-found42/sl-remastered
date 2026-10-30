#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out="$(python3 -I tools/wine/oracle_new_dir.py core-pair-oracle)"
printf 'Oracle evidence: %s\n' "$out" >&2
corpus="$PWD/local/verify/core-pair-oracle/corpus-data/fixtures.bin"
manifest="$PWD/local/verify/core-pair-oracle/corpus-data/manifest.tsv"
if [ ! -f "$corpus" ] || [ ! -f "$manifest" ]; then
  echo "Missing car collision corpus. Run these commands from the repository root:" >&2
  echo "cmake -S . -B local/verify/core-pair-oracle/build -G Ninja -DCMAKE_BUILD_TYPE=Release" >&2
  echo "cmake --build local/verify/core-pair-oracle/build --target sl_data" >&2
  echo "clang++ -std=c++17 -O2 -ffp-contract=off -Isrc tools/mesh_mass_corpus.cpp local/verify/core-pair-oracle/build/libsl_data.a -o local/verify/core-pair-oracle/corpus" >&2
  echo 'local/verify/core-pair-oracle/corpus "${SLRR_GAME_ROOT:-$PWD/.scratch/2005-extract/Program Files/Street Legal Racing - Redline}" "$PWD/local/verify/core-pair-oracle/corpus-data"' >&2
  exit 1
fi
python3 - "$corpus" "$manifest" <<'PYCODE'
import struct, sys
with open(sys.argv[1], 'rb') as f:
    data = f.read(4)
if len(data) != 4:
    raise SystemExit('truncated car collision corpus')
count, = struct.unpack('<I', data)
with open(sys.argv[2]) as f:
    lines = sum(1 for _ in f) - 1
if count != 309 or lines != count:
    raise SystemExit(f'expected 309 car fixtures and matching manifest, found {count} fixtures and {lines} rows')
PYCODE
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/wine/core_pair_oracle.c -o "$out/stock.exe"
clang++ -std=c++17 -O2 -ffp-contract=off -Isrc tests/physics_core_pair_probe.cpp src/physics_core_pair.cpp src/physics_polyhedron.cpp src/physics_body.cpp -o "$out/native"
"$out/native" "$corpus" > "$out/native.txt"
cp "$corpus" "$out/fixtures.bin"
cp "$manifest" "$out/corpus-manifest.tsv"
case "${SLRR_ORACLE_BACKEND:-mac}" in
  docker) fixture='Z:\out\fixtures.bin' ;;
  mac) fixture="Z:$(printf '%s' "$out/fixtures.bin" | sed 's,/,\\,g')" ;;
esac
tools/wine/run_original_oracle.sh "$out" "$out/stock.exe" "$fixture" > "$out/stock.txt" 2> "$out/wine.err"
tr -d '\r' < "$out/stock.txt" > "$out/stock-normalized.txt"
if ! diff -u "$out/stock-normalized.txt" "$out/native.txt" > "$out/comparison.diff"; then
  head -80 "$out/comparison.diff"
  echo "Full comparison: $out/comparison.diff; car case N maps to manifest case N+2 in $manifest" >&2
  exit 1
fi
python3 - "$out/stock-normalized.txt" "$manifest" <<'PYCODE'
import sys
lines = open(sys.argv[1]).read().splitlines()
failures = [line for line in lines if line.startswith('car ') and line.endswith('geometry-failure')]
queries = sum(line.startswith('car ') and not line.endswith('geometry-failure') for line in lines)
print(f'core pair oracle: {queries} car queries match; {len(failures)} matching stock geometry construction failures')
for line in failures:
    print(line)
PYCODE
