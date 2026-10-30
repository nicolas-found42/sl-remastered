#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out=$(cd "${1:?output directory}" && pwd -P)
helper="${2:?oracle helper}"
shift 2
refdir=$(cd ref && pwd -P)
case "$out" in "$refdir"|"$refdir/"*) echo 'oracle output must be outside ref/' >&2; exit 2;; esac
stock="$PWD/ref/raw/StreetLegal_Redline.bak.exe"
[ "$(md5 -q "$stock")" = e686099e7233bb7d7c461b1b35c5064c ] || exit 1
attempt=$(mktemp -d "$out/attempt.XXXXXXXX")
timeout=${SLRR_ORACLE_TIMEOUT_SECONDS:-120}
controller=tools/wine/oracle_controller.py
printf 'oracle attempt: %s\n' "$attempt" >&2
case "${SLRR_ORACLE_BACKEND:-mac}" in
  mac)
    wine="${WINEDIR:-$PWD/local/wine-runs/winehq-source-20261028/build}/wine"
    [ -x "$wine" ] || { echo "missing native Wine: $wine" >&2; exit 2; }
    export WINEPREFIX="$out/mac-prefix" WINEDEBUG=-all
    if [ ! -d "$WINEPREFIX" ]; then
      if [ -n "${SLRR_ORACLE_PREFIX_SOURCE:-}" ]; then
        cp -c -R "$SLRR_ORACLE_PREFIX_SOURCE" "$WINEPREFIX"
      else
        WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d' python3 -I "$controller" \
          --record-dir "$attempt/wineboot" --timeout "$timeout" --hash-file "$wine" -- "$wine" wineboot -i > "$attempt/wineboot.log" 2>&1
      fi
    fi
    stock_windows="Z:$(printf '%s' "$stock" | sed 's,/,\\,g')"
    echo 'oracle backend: native macOS Wine' >&2
    exec python3 -I "$controller" --record-dir "$attempt/run" --timeout "$timeout" \
      --hash-file "$stock" --hash-file "$helper" --hash-file "$wine" --fixture-root "$out" -- \
      "$wine" "$helper" "$stock_windows" "$@"
    ;;
  docker)
    echo 'oracle backend: Docker Wine; results may differ from native macOS Wine' >&2
    exec python3 -I "$controller" --record-dir "$attempt/run" --timeout "$timeout" \
      --hash-file "$stock" --hash-file "$helper" --fixture-root "$out" --docker-cidfile "$attempt/container.id" -- \
      docker run --rm --cidfile "$attempt/container.id" --platform linux/amd64 -v "$out:/out" -v "$stock:/stock.exe:ro" \
      -e WINEPREFIX=/out/docker-prefix -e WINEDEBUG=-all slrr-stock-wine-oracle \
      wine "/out/$(basename "$helper")" 'Z:\stock.exe' "$@"
    ;;
  *) echo "unknown oracle backend" >&2; exit 2 ;;
esac
