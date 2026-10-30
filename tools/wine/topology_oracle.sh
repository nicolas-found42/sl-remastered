#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out="$(python3 -I tools/wine/oracle_new_dir.py topology-oracle)"
printf 'Oracle evidence: %s\n' "$out" >&2
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/wine/topology_oracle.c -o "$out/stock.exe"
clang++ -std=c++17 -O2 -ffp-contract=off -Isrc tests/physics_topology_probe.cpp src/physics_polyhedron.cpp -o "$out/native"
"$out/native" > "$out/native.txt"
tools/wine/run_original_oracle.sh "$out" "$out/stock.exe" > "$out/stock-mac.txt" 2> "$out/wine-mac.err"
python3 - "$out" <<'PY'
from pathlib import Path
import difflib,sys
p=Path(sys.argv[1]); stock=(p/'stock-mac.txt').read_text().splitlines(); native=(p/'native.txt').read_text().splitlines(); stock=stock[next(i for i,s in enumerate(stock) if s.startswith('case=')):]
diff=list(difflib.unified_diff(stock,native,fromfile='stock',tofile='native',lineterm=''))
(p/'comparison.txt').write_text('\n'.join(diff)+'\n' if diff else 'exact match\n')
print(f'{len(diff)} diff lines; see {p / "comparison.txt"}')
raise SystemExit(bool(diff))
PY
