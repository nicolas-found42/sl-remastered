#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out="$(python3 -I tools/wine/oracle_new_dir.py contact-geometry)"
printf 'Oracle evidence: %s\n' "$out" >&2
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/wine/contact_geometry_oracle.c -o "$out/stock.exe"
clang++ -std=c++17 -O2 -ffp-contract=off -Isrc tests/physics_contact_geometry_probe.cpp src/physics_contact_geometry.cpp src/physics_body.cpp -o "$out/native"
"$out/native" > "$out/native.txt"
tools/wine/run_original_oracle.sh "$out" "$out/stock.exe" > "$out/stock.txt" 2> "$out/wine.err"
tr -d '\r' < "$out/stock.txt" > "$out/stock-normalized.txt"
diff -u "$out/stock-normalized.txt" "$out/native.txt"
