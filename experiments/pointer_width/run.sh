#!/usr/bin/env bash
# Test three ways to carry the original 32-bit object layout on a 64-bit Mac, using one real
# structure from ref/slrr-reverse-main. ref/ is only read. Results go to local/pointer_width/.
#
#   A  native 64-bit pointers in the original structure
#   B  4-byte handles in the pointer fields, so every offset and size is unchanged
#   C  real pointers below 4 GB, so the original pointer values fit in 4 bytes
set -uo pipefail
cd "$(dirname "$0")/../.."

src=ref/slrr-reverse-main/engine/Runtime/Resources/Resources_part1.inc
out=local/pointer_width
here=experiments/pointer_width
rm -rf "$out"
mkdir -p "$out"
exec > >(tee "$out/results.txt") 2>&1

echo "== casts in the engine that read or write pointer-sized or 32-bit slots"
python3 -I - <<'EOF'
import pathlib, re
from collections import Counter
pat = re.compile(r"reinterpret_cast<\s*(void|uint32_t|uintptr_t)\s*(\*+)\s*>\(")
counts = Counter()
for p in pathlib.Path("ref/slrr-reverse-main/engine").rglob("*"):
    if p.suffix in (".cpp", ".inc", ".hpp", ".h"):
        for m in pat.finditer(p.read_text(encoding="utf-8", errors="replace")):
            counts[m.group(1) + m.group(2)] += 1
print(f"{sum(counts.values())} casts to void*, void**, uint32_t*, uint32_t**, uintptr_t*:", dict(counts))
EOF
echo

{
	echo '#pragma once'
	echo '#include <cstddef>'
	echo '#include <cstdint>'
	echo '#include <cstring>'
	sed -n '283,331p' "$src"
} > "$out/mid_a.hpp"
echo "extracted $(grep -c . "$out/mid_a.hpp") lines of HostMidPayload from $src"
echo "layout assertions in the structure: $(grep -c '^static_assert' "$out/mid_a.hpp")"

echo
echo "== A  native pointers, assertions kept"
cp "$out/mid_a.hpp" "$out/mid.hpp"
if clang++ -std=c++17 -I"$out" -c "$here/probe.cpp" -o /dev/null 2> "$out/a.err"; then
	echo "compiles"
else
	echo "does not compile: $(grep -c 'static assertion failed' "$out/a.err") failed assertions"
fi

echo
echo "== A  native pointers, assertions removed"
grep -v '^static_assert' "$out/mid_a.hpp" | grep -v '^              "mid' > "$out/mid.hpp"
clang++ -std=c++17 -I"$out" "$here/probe.cpp" -o "$out/probe_a" 2> "$out/a2.err" || { echo "build failed"; cat "$out/a2.err"; }
"$out/probe_a"
echo "exit $?"

echo
echo "== B  4-byte handles in pointer fields"
python3 -I - "$out/mid_a.hpp" "$out/mid_b.hpp" <<'EOF'
import re, sys
text = open(sys.argv[1]).read()
text, fields = re.subn(r"\bvoid\*(\s+)(\w+)\s*=\s*nullptr;", r"Ptr32\1\2;", text)
text, stores = re.subn(
    r"\*reinterpret_cast<void\*\*>\((\w+) \+ 0x0C\) = (\w+);",
    r"{ uint32_t h = HandleTable::add(\2); std::memcpy(\1 + 0x0C, &h, 4); }", text)
open(sys.argv[2], "w").write('#include "ptr32.hpp"\n' + text)
print(f"mechanical edits: {fields} pointer fields changed to Ptr32, {stores} raw pointer stores rewritten")
EOF
cp "$out/mid_b.hpp" "$out/mid.hpp"
if clang++ -std=c++17 -DUSE_PTR32 -I"$out" -I"$here" "$here/probe.cpp" -o "$out/probe_b" 2> "$out/b.err"; then
	echo "compiles with every layout assertion intact"
	"$out/probe_b"
	echo "exit $?"
else
	echo "build failed"; head -5 "$out/b.err"
fi

echo
echo "== C  memory below 4 GB"
for flags in "" "-Wl,-pagezero_size,0x4000" "-Wl,-pagezero_size,0x1000"; do
	echo "-- link flags: ${flags:-default}"
	if clang -O1 $flags "$here/low_memory.c" -o "$out/low" 2> "$out/c.err"; then
		"$out/low" || true
	else
		echo "link failed: $(head -1 "$out/c.err")"
	fi
done
