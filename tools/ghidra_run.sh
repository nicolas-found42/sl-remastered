#!/usr/bin/env bash
# Run a Ghidra script on the stock executable without opening the Ghidra window.
#
#   tools/ghidra_run.sh [--save] <Script.java> [script args...]
#
# The first run builds local/ghidra-proj. It imports and analyzes the executable, then runs
# DefineIndirectFunctions.java and DefineGapFunctions.java once each, which takes about a
# minute. Later runs reuse that project and never run the define scripts again, because the
# gap script is not idempotent: a second pass creates more functions. Without --save a run
# is read-only. The source file in ref/ is only read.
set -euo pipefail
cd "$(dirname "$0")/.."

stock=ref/raw/StreetLegal_Redline.bak.exe
want=e686099e7233bb7d7c461b1b35c5064c
[ "$(md5 -q "$stock")" = "$want" ] || { echo "ghidra_run: $stock is not the stock executable"; exit 1; }

headless=${GHIDRA_HEADLESS:-analyzeHeadless}
proj=local/ghidra-proj
name=stock
mkdir -p "$proj" local/census

run_script() {
	local mode=$1 script=$2
	shift 2
	local base="${script%.java}"
	local log="local/census/${base}.log"
	# Address-parameterized decompiles get one log per address so a second run
	# never overwrites the first decompilation.
	if [ "$script" = DecompileAt.java ]; then
		local slug
		slug="$(printf '%s' "$1" | tr -c 'A-Za-z0-9' '_')"
		log="local/census/${base}-${slug}.log"
	fi
	"$headless" "$proj" "$name" -process "$(basename "$stock")" -noanalysis $mode \
		-scriptPath tools/ghidra -postScript "$script" "$@" > "$log" 2>&1
	# Ghidra headless can exit 0 when the script raised, so scripts that print a
	# success marker are checked for it (see docs/enable-debug-dump.md).
	local marker=''
	case "$script" in
		DecompileStockHandler.java) marker='STOCK_HANDLER_DECOMPILE_OK' ;;
		DecompileAt.java) marker='DECOMPILE_AT_OK' ;;
		DecompileSqrt.java) marker='SQRT_DECOMPILE_OK' ;;
		DecompileDebugDump.java) marker='DEBUG_DUMP_DECOMPILE_OK' ;;
		DecompileFileHandlers.java) marker='FILE_HANDLERS_DECOMPILE_OK' ;;
		ApplyProjectSymbols.java) marker='APPLY_PROJECT_SYMBOLS_OK' ;;
	esac
	if [ -n "$marker" ] && ! grep -Fq "$marker" "$log"; then
		echo "ghidra_run: $script failed; see $log" >&2
		return 1
	fi
	echo "ghidra_run: output in $log"
}

if [ ! -f "$proj/defined.ok" ]; then
	if [ -n "$(ls -A "$proj")" ]; then
		echo "ghidra_run: $proj is not empty and has no defined.ok; preserve it and choose a new project location" >&2
		exit 1
	fi
	"$headless" "$proj" "$name" -import "$stock" > local/census/import.log 2>&1
	run_script "" DefineIndirectFunctions.java local/census/defined_first.tsv docs/natives.tsv
	run_script "" DefineGapFunctions.java local/census/defined_gaps.tsv
	touch "$proj/defined.ok"
fi

readonly=-readOnly
if [ "${1:-}" = "--save" ]; then
	readonly=
	shift
fi

run_script "$readonly" "$@"
