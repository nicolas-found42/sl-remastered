#!/usr/bin/env bash
# macOS window screenshot (works for occluded windows) of a Wine window.
# Usage: tools/wine/capture/shot.sh OUT.png [owner-substring]   (default owner: wine)
set -euo pipefail
OUT="${1:?output png}"; OWNER="${2:-wine}"
ID="$(swift "$(dirname "$0")/winlist.swift" "$OWNER" | head -1 | awk '{print $1}')"
[ -n "$ID" ] || { echo "no window found for owner '$OWNER'" >&2; exit 1; }
screencapture -x -o -l"$ID" "$OUT"
