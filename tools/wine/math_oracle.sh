#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
stock="$PWD/ref/raw/StreetLegal_Redline.bak.exe"
expected=e686099e7233bb7d7c461b1b35c5064c
actual=$(md5 -q "$stock")
[ "$actual" = "$expected" ] || { echo "Stock MD5 mismatch: $actual" >&2; exit 1; }
out="$(python3 -I tools/wine/oracle_new_dir.py math-oracle)"
printf 'Oracle evidence: %s\n' "$out" >&2
i686-w64-mingw32-gcc -O0 -Wl,--image-base,0x10000000 tools/wine/math_oracle.c -o "$out/math_oracle.exe"
clang++ -std=c++17 -O2 -ffp-contract=off -Isrc tests/physics_orientation_probe.cpp src/physics_body.cpp -o "$out/native"
"$out/native" > "$out/native.txt"
printf 'stock md5=%s\n' "$actual" > "$out/identity.txt"
tools/wine/run_original_oracle.sh "$out" "$out/math_oracle.exe" > "$out/stock-mac.txt" 2> "$out/wine-mac.err"
python3 - "$out" <<'PY'
from pathlib import Path
import sys
out = Path(sys.argv[1])
native = (out / 'native.txt').read_text().splitlines()
stock = (out / 'stock-mac.txt').read_text().splitlines()
report = []
required = []
seen = set()
for line in stock:
    if not line.startswith('case='):
        continue
    original = line.split()
    case = int(original[0].split('=')[1])
    key = (case, original[1])
    if key in seen:
        raise SystemExit('duplicate stock scenario')
    seen.add(key)
    comparison = native[case].split()
    for label, count in [('input', 12), ('quat', 4), ('matrix', 12)]:
        a, b = original.index(label), comparison.index(label)
        for i in range(count):
            if original[a + 1 + i] != comparison[b + 1 + i]:
                difference = f'{original[0]} {original[1]} {label}[{i}] stock={original[a + 1 + i]} native={comparison[b + 1 + i]}'
                report.append(difference)
                if original[1] == 'cw=007f':
                    required.append(difference)
(out / 'comparison.txt').write_text('\n'.join(report) + ('\n' if report else 'exact match\n'))
expected = {(i, f'cw={cw}') for i in range(len(native)) for cw in ('007f', '027f', '037f')}
if not native or seen != expected:
    raise SystemExit('missing stock scenarios')
print(f'cw=007f: {len(required)} differing words in {len(native)} cases')
print(f'other modes: {len(report) - len(required)} differing words; details in {out / "comparison.txt"}')
raise SystemExit(bool(required))
PY
