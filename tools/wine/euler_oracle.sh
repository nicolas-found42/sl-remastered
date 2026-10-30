#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
stock="$PWD/ref/raw/StreetLegal_Redline.bak.exe"
[ "$(md5 -q "$stock")" = e686099e7233bb7d7c461b1b35c5064c ] || exit 1
out="$(python3 -I tools/wine/oracle_new_dir.py euler-oracle)"
printf 'Oracle evidence: %s\n' "$out" >&2
python3 - "$out/euler_cases.h" <<'PYCODE'
import glob, math, sys
angles = {(0, 0, 0), (1.57079632679, 0, 0), (0, 1.57079632679, 0),
          (0, 0, 1.57079632679), (0.1, 0.2, 0.3), (-0.7, 1.2, -2.3),
          (3.14159265, -3.14159265, 0.4)}
for path in glob.iglob(".scratch/2005-extract/Program Files/Street Legal Racing - Redline/**/*.cfg", recursive=True):
    with open(path, encoding="latin1") as source:
        for line in source:
            fields = line.split()
            if len(fields) < 8 or fields[0].lower() != "slot":
                continue
            try:
                values = tuple(float(value) for value in fields[4:7])
            except ValueError:
                continue
            if all(map(math.isfinite, values)):
                angles.add(values)
def literal(value):
    value = format(value, ".17g")
    return value + ("f" if "." in value or "e" in value else ".0f")
with open(sys.argv[1], "w") as output:
    output.write("static const float cases[][3]={\n")
    for values in sorted(angles):
        output.write("{" + ",".join(map(literal, values)) + "},\n")
    output.write("};\n")
PYCODE
i686-w64-mingw32-gcc -I"$out" -O0 -Wl,--image-base,0x10000000 tools/wine/euler_oracle.c -o "$out/oracle.exe"
clang++ -I"$out" -std=c++17 -O2 -ffp-contract=off -ffunction-sections -Wl,-dead_strip -Isrc tests/part_orientation_probe.cpp src/part_config.cpp -o "$out/native"
"$out/native" > "$out/native.txt"
tools/wine/run_original_oracle.sh "$out" "$out/oracle.exe" > "$out/stock-mac.txt" 2> "$out/wine-mac.err"
tr -d "\r" < "$out/stock-mac.txt" > "$out/stock-normalized.txt"
diff -u "$out/stock-normalized.txt" "$out/native.txt"
