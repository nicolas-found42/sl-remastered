#!/usr/bin/env bash
# Read-only view of saved decompiles with exe strings resolved and noise trimmed.
cd "$(dirname "$0")/.."
tools/decomp_fn.sh "$@" | python3 -I tools/decomp_annot.py | awk '
/^\/\/\/\/ FUNCTION/ {print; next}
/^  (undefined[0-9]?|int|uint|char|byte|float|bool|short|code|void|float10|ushort|longlong)[ *]*[A-Za-z_0-9]+(\[[0-9]+\])?;$/ {next}
/^ *\/\* WARNING/ {next}
/uVar[0-9]* = uVar[0-9]* - 1;|cVar[0-9]* = \*pcVar|pcVar[0-9]* = pcVar[0-9]* \+ 1;|= 0xffffffff;|if \(uVar[0-9]* == 0\) break;|^ *do \{$|\} while \(cVar/ {next}
/^$/ {next}
{print}'
