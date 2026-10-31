#!/usr/bin/env bash
# Phase 2 exit check. For each data format it runs the C++ loader census and the independent
# Python reader on ref/raw, compares them, and compares the file count with a count taken
# straight from the file system. It prints one table and exits 1 on any mismatch.
#
#   tools/phase2_check.sh [build-dir]
set -euo pipefail
cd "$(dirname "$0")/.."

build=${1:-build}
cpp=$build/sl_port
root=${SL_ROOT:-ref/raw}
[ -x "$cpp" ] || { echo "phase2_check: $cpp is missing, run tools/check.sh first"; exit 1; }

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
bad=0

# Number of files with an extension, case-insensitive, counted with find and not with either reader.
count_ext() { find "$root" -type f -iname "*.$1" | wc -l | tr -d ' '; }
count_sdat() {
	python3 -I -c '
import os, sys
n = 0
for d, _, fs in os.walk(sys.argv[1]):
    for f in fs:
        try:
            with open(os.path.join(d, f), "rb") as fh:
                n += fh.read(4) == b"SDAT"
        except OSError:
            pass
print(n)' "$root"
}

# grep -c exits 1 when it counts 0, which would stop the script under set -e.
lines_in() { grep -c "$1" "$2" || true; }

field() { sed -n "s/^$1: *//p" "$2" | head -1; }

printf '%-6s %8s %8s %8s %6s  %s\n' format on-disk cpp-open py-open stale "per-file diff"

# check <label> <on-disk count> <cpp args...> -- <python args...>
check() {
	local label=$1 disk=$2 stale=${3:-0}
	local cpp_out=$scratch/$label.cpp py_out=$scratch/$label.py
	local cpp_rc=0 py_rc=0
	shift 3
	local cpp_args=() py_args=()
	while [ "$1" != "--" ]; do cpp_args+=("$1"); shift; done
	shift
	py_args=("$@")
	"$cpp" "${cpp_args[@]}" > "$cpp_out" 2> "$scratch/$label.cpp.err" || cpp_rc=$?
	python3 -I "${py_args[@]}" > "$py_out" 2> "$scratch/$label.py.err" || py_rc=$?

	local cpp_found cpp_open py_found py_open cpp_stale
	cpp_found=$(field "files found" "$cpp_out")
	cpp_open=$(field "files opened" "$cpp_out")
	py_found=$(field "files found" "$py_out")
	py_open=$(field "files opened" "$py_out")
	cpp_stale=$(field "files known stale" "$cpp_out")
	cpp_stale=${cpp_stale:-0}

	local diffnote="same"
	if [ "$label" = rpk ]; then
		# The Python RPK reader prints totals only, so compare the totals.
		for key in "total dependencies" "total entries"; do
			[ "$(field "$key" "$cpp_out")" = "$(field "$key" "$py_out")" ] || diffnote="totals differ: $key"
		done
		local cpp_digest py_digest
		cpp_digest=$({ grep '^ENTRY ' "$cpp_out" || true; } | LC_ALL=C sort | shasum -a 256 | cut -d ' ' -f 1)
		py_digest=$({ grep '^ENTRY ' "$py_out" || true; } | LC_ALL=C sort | shasum -a 256 | cut -d ' ' -f 1)
		if [ "$cpp_digest" != "$py_digest" ]; then
			diffnote="entry digest differs"
		elif [ "$diffnote" = same ]; then
			diffnote="same (entries sha256=$cpp_digest)"
		fi
	else
		local lines=' OK \| v[0-9] materials='
		diff <(grep "$lines" "$cpp_out" | sort) <(grep "$lines" "$py_out" | sort) > "$scratch/$label.diff" || diffnote="DIFFERENT"
		[ "$diffnote" = same ] && diffnote="same ($(lines_in . "$scratch/$label.diff") lines differ, $(lines_in "$lines" "$py_out") compared)"
		if [ "$label" = sdat ] || [ "$label" = scx ]; then
			local cpp_digest py_digest
			cpp_digest=$({ grep '^ENTRY ' "$cpp_out" || true; } | LC_ALL=C sort | shasum -a 256 | cut -d ' ' -f 1)
			py_digest=$({ grep '^ENTRY ' "$py_out" || true; } | LC_ALL=C sort | shasum -a 256 | cut -d ' ' -f 1)
			if [ "$cpp_digest" != "$py_digest" ]; then
				diffnote="entry digest differs"
			elif [[ "$diffnote" = same* ]]; then
				diffnote="$diffnote entries sha256=$cpp_digest"
			fi
		fi
	fi

	local ok=1
	[ "$cpp_found" = "$disk" ] || ok=0
	[ "$py_found" = "$disk" ] || ok=0
	[ "$cpp_open" = "$(( disk - stale ))" ] || ok=0
	[ "$cpp_stale" = "$stale" ] || ok=0
	[ "$cpp_rc" = 0 ] || ok=0
	case "$diffnote" in same*) ;; *) ok=0 ;; esac
	# The Python class reader reports the stale file as a failure, so it opens one fewer.
	if [ "$py_open" != "$cpp_open" ]; then ok=0; fi
	[ "$ok" = 1 ] || { bad=1; diffnote="$diffnote  <-- FAIL (cpp rc $cpp_rc, py rc $py_rc)"; }
	printf '%-6s %8s %8s %8s %6s  %s\n' "$label" "$disk" "$cpp_open" "$py_open" "$cpp_stale" "$diffnote"
}

# The Python readers for class files and SCX exit 1 when a file fails, so the known stale class file
# makes the class reader exit 1. That is expected, and the open counts are compared instead.
check rpk   "$(count_ext rpk)"   0 --rpk-census "$root" -- tools/rpk_census.py "$root"
check scx   "$(count_ext scx)"   0 --scx-census "$root" --per-file -- tools/scx_census.py "$root" --per-file
check scy   "$(count_ext scy)"   0 --scy-census "$root" --per-file -- tools/scy_census.py "$root" --per-file
check class "$(count_ext class)" 1 --class-census "$root" --per-file --expect-stale 1 -- tools/tufa_census.py "$root" --per-file
check sdat  "$(count_sdat)"      0 --sdat-census "$root" --per-file -- tools/sdat_census.py "$root" --per-file

echo
echo "class files known stale (the stock loader rejects them too):"
sed -n '/^known stale files:/,/^failed files:/p' "$scratch/class.cpp" | sed '1d;$d'
echo
echo "totals from the C++ loaders:"
for l in rpk scx scy class sdat; do
	printf '  %s: ' "$l"
	grep -E '^(total dependencies|total entries|total materials|total meshes|total vertices|total indices|classes|constants|method records|field records|trees|tree nodes|unsaved resource records|pack records)' "$scratch/$l.cpp" | tr '\n' ';' | sed 's/;$/\n/; s/;/; /g'
done

if [ "$bad" = 0 ]; then
	echo "phase2_check: ok"
else
	echo "phase2_check: FAILED"
	exit 1
fi
