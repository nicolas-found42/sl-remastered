#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
stock="$PWD/ref/raw/StreetLegal_Redline.bak.exe"
actual=$(md5 -q "$stock")
[ "$actual" = e686099e7233bb7d7c461b1b35c5064c ] || { echo "Stock MD5 mismatch: $actual" >&2; exit 1; }
out="$(python3 -I tools/wine/oracle_new_dir.py mesh-mass-oracle)"
printf 'Oracle evidence: %s\n' "$out" >&2
cmake -S . -B "$out/build" -G Ninja -DCMAKE_BUILD_TYPE=Release > "$out/configure.log"
cmake --build "$out/build" --target sl_data > "$out/build.log" 2>&1
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/wine/mesh_mass_oracle.c -o "$out/stock.exe"
clang++ -std=c++17 -O2 -ffp-contract=off -Isrc tests/physics_mesh_mass_probe.cpp src/physics_mesh_mass.cpp src/physics_body.cpp -o "$out/native"
clang++ -std=c++17 -O2 -ffp-contract=off -Isrc tools/mesh_mass_corpus.cpp "$out/build/libsl_data.a" -o "$out/corpus"
game_root="${SLRR_GAME_ROOT:-$PWD/.scratch/2005-extract/Program Files/Street Legal Racing - Redline}"
"$out/corpus" "$game_root" "$out"
"$out/native" "$out/fixtures.bin" > "$out/native.txt"
case "${SLRR_ORACLE_BACKEND:-mac}" in
  mac) fixture_path="Z:$(printf '%s' "$out/fixtures.bin" | sed 's,/,\\,g')" ;;
  docker) fixture_path='Z:\out\fixtures.bin' ;;
esac
tools/wine/run_original_oracle.sh "$out" "$out/stock.exe" "$fixture_path" > "$out/stock-mac.txt" 2> "$out/wine-mac.err"
python3 - "$out" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); stock=[s.split() for s in (p/'stock-mac.txt').read_text().splitlines() if s.startswith('case=')]; native=[s.split() for s in (p/'native.txt').read_text().splitlines() if s.startswith('case=')]
expected = len((p/'manifest.tsv').read_text().splitlines()) + 1
if len(stock)!=expected or len(native)!=expected: raise SystemExit(f'missing cases: expected {expected}, stock {len(stock)}, native {len(native)}')
diff=[]
for s,n in zip(stock,native):
 if s[0]!=n[0] or s[1]!='cw=007f': raise SystemExit('case mismatch')
 for label,count in [('volume',1),('center',3),('inverse',12)]:
  a=s.index(label);b=n.index(label)
  for i in range(count):
   if label == 'inverse' and i in (3,7,11): continue
   if s[a+i+1]!=n[b+i+1]: diff.append(f'{s[0]} {label}[{i}] stock={s[a+i+1]} native={n[b+i+1]}')
(p/'comparison.txt').write_text('\n'.join(diff)+'\n' if diff else 'exact match\n')
print(f'{len(diff)} differing words; see {p / "comparison.txt"}')
raise SystemExit(bool(diff))
PY
