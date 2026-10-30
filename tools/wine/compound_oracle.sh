#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
stock="$PWD/ref/raw/StreetLegal_Redline.bak.exe"
actual=$(md5 -q "$stock")
[ "$actual" = e686099e7233bb7d7c461b1b35c5064c ] || { echo "Stock MD5 mismatch: $actual" >&2; exit 1; }
out="$(python3 -I tools/wine/oracle_new_dir.py compound-oracle)"
printf 'Oracle evidence: %s\n' "$out" >&2
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/wine/compound_oracle.c -o "$out/stock.exe"
clang++ -std=c++17 -O2 -ffp-contract=off -Isrc tests/physics_compound_probe.cpp src/physics_compound_finalize.cpp src/physics_compound_mass.cpp src/physics_body.cpp src/part_physics.cpp src/physics_compound.cpp src/physics_polyhedron.cpp src/part_config.cpp src/car_config.cpp src/owner_links.cpp src/resource_id.cpp -o "$out/native"
"$out/native" > "$out/native.txt"
tools/wine/run_original_oracle.sh "$out" "$out/stock.exe" > "$out/stock-mac.txt" 2> "$out/wine-mac.err"
python3 - "$out" <<'PYCOMPARE'
from pathlib import Path
import sys
p=Path(sys.argv[1]); stock=[s.split() for s in (p/'stock-mac.txt').read_text().splitlines() if s.startswith('case=')]; native=[s.split() for s in (p/'native.txt').read_text().splitlines() if s.startswith('case=')]
if len(stock)!=4 or len(native)!=2: raise SystemExit('missing cases')
diff=[]
for s,n in zip(stock[:2],native):
 for label,count in [('mass',1),('inertia',12),('center',3),('root',3),('child0',3),('child1',3)]:
  for i in range(count):
   if label=='inertia' and i in (3,7,11): continue
   a=s[s.index(label)+i+1]; b=n[n.index(label)+i+1]
   if a!=b: diff.append(f'{s[0]} {label}[{i}] stock={a} native={b}')
parts=[s.split() for s in (p/'native.txt').read_text().splitlines() if s.startswith('part=')]
if len(parts)!=2: raise SystemExit('missing part cases')
for s,n in zip(stock[2:],parts):
 for label,count in [('mass',1),('inertia',12),('center',3),('root',3),('child0',3),('child1',3),('bound',1)]:
  for i in range(count):
   if label=='inertia' and i in (3,7,11): continue
   a=s[s.index(label)+i+1]; b=n[n.index(label)+i+1]
   if a!=b: diff.append(f'{n[0]} {label}[{i}] stock={a} native={b}')
(p/'comparison.txt').write_text('\n'.join(diff)+'\n' if diff else 'exact match\n')
print(f'{len(diff)} differing words; see {p / "comparison.txt"}')
raise SystemExit(bool(diff))
PYCOMPARE
