#!/usr/bin/env bash
# Convert every BMP in a dumps directory to PNG (uses macOS sips). Usage: tools/wine/capture/bmp2png.sh DIR
set -euo pipefail
for f in "${1:?dir}"/*.bmp; do [ -e "$f" ] || continue; [ -e "${f%.bmp}.png" ] || sips -s format png "$f" --out "${f%.bmp}.png" >/dev/null; done
