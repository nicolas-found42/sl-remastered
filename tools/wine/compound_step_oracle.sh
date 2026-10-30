#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out="$(python3 -I tools/wine/oracle_new_dir.py compound-step-oracle)"
printf 'Oracle evidence: %s\n' "$out" >&2
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/wine/compound_step_oracle.c -o "$out/stock.exe"
clang++ -std=c++17 -O2 -ffp-contract=off -Isrc tests/physics_compound_step_probe.cpp src/physics_compound_step.cpp src/physics_compound.cpp src/physics_body.cpp -o "$out/native"
"$out/native" > "$out/native.txt"
tools/wine/run_original_oracle.sh "$out" "$out/stock.exe" > "$out/stock.txt"
tools/wine/run_original_oracle.sh "$out" "$out/stock.exe" > "$out/stock-repeat.txt"
cmp "$out/stock.txt" "$out/stock-repeat.txt" || { echo "stock runs differ" >&2; exit 1; }
shasum -a 256 ref/raw/StreetLegal_Redline.bak.exe tools/wine/compound_step_oracle.c tools/wine/compound_step_oracle.sh tests/physics_compound_step_probe.cpp "$out/stock.txt" "$out/stock-repeat.txt" "$out/native.txt" > "$out/manifest.txt"
printf "stock_md5=e686099e7233bb7d7c461b1b35c5064c fpu=007f synthetic_chain steps=100 dt=0.01 invmass=1 drag=-0.005 gravity=(0,-9.81,0) velocity=(2,1,0) omega=(0.25,0,0) angular_momentum=(0.25,0,0) inverse_inertia=identity root_flags=0x9 child_flags=0xd,0x5 relative_positions=(2,1,0),(1,3,0) relative_quaternions=(0,0,0.70710677,0.70710677),(0,0,0,1) static_init_7a0560_bit1=1 no_contacts_or_joints\n" >> "$out/manifest.txt"
python3 - "$out" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); stock=[s.split() for s in (p/'stock.txt').read_text().splitlines() if s.startswith('case=')]; native=[s.split() for s in (p/'native.txt').read_text().splitlines() if s.startswith('case=')]
if len(stock)!=5 or len(native)!=5: raise SystemExit('missing cases')
diff=[]
for a,b in zip(stock,native):
 if a[:4]!=b[:4] or len(a)!=57 or len(b)!=57: raise SystemExit('case mismatch')
 for i,(x,y) in enumerate(zip(a[4:],b[4:])):
  if x!=y:diff.append(f'{a[0]} {a[1]} state word {i}: stock={x} native={y}')
(p/'comparison.txt').write_text('\n'.join(diff)+'\n' if diff else 'exact match\n')
stock_lines=[s.split() for s in (p/'stock.txt').read_text().splitlines() if s.startswith(('force ', 'trajectory '))]
native_lines=[s.split() for s in (p/'native.txt').read_text().splitlines() if s.startswith(('force ', 'trajectory '))]
if len(stock_lines)!=400 or len(native_lines)!=400: raise SystemExit('missing trajectory records')
trajectory_diff=[]
for a,b in zip(stock_lines,native_lines):
 if len(a)!=len(b): raise SystemExit('trajectory format mismatch')
 for i,(x,y) in enumerate(zip(a,b)):
  if x!=y: trajectory_diff.append(f'{a[0]} {a[1]} {a[2] if a[0]=="trajectory" else ""} token={i} stock={x} native={y}')
(p/'trajectory-comparison.txt').write_text('\n'.join(trajectory_diff)+'\n' if trajectory_diff else 'exact match\n')
sleep_a=[s.split() for s in (p/'stock.txt').read_text().splitlines() if s.startswith(('sleep ', 'sleepstep '))]
sleep_b=[s.split() for s in (p/'native.txt').read_text().splitlines() if s.startswith(('sleep ', 'sleepstep '))]
if len(sleep_a)!=18 or len(sleep_b)!=18: raise SystemExit('missing sleep records')
sleep_diff=[]
for a,b in zip(sleep_a,sleep_b):
 if len(a)!=len(b): raise SystemExit('sleep format mismatch')
 for i,(x,y) in enumerate(zip(a,b)):
  if x!=y:sleep_diff.append(f'{a[1]} {a[2]} token={i} stock={x} native={y}')
(p/'sleep-comparison.txt').write_text('\n'.join(sleep_diff)+'\n' if sleep_diff else 'exact match\n')
print(f'stock repeatability: exact; trajectory differing tokens: {len(trajectory_diff)}; sleep differing tokens: {len(sleep_diff)}')
if sleep_diff: print('first sleep divergence: '+sleep_diff[0])
if trajectory_diff: print('first divergence: '+trajectory_diff[0])
print(f'{len(diff)} differing words; see {p / "comparison.txt"}')
if diff: print('\n'.join(diff[:20]))
raise SystemExit(bool(diff or trajectory_diff or sleep_diff))
PY
