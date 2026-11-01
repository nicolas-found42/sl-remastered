#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build
ctest --test-dir build --output-on-failure
python3 -I tests/natives_called_test.py
check_tmp="$(mktemp -d)"
native_inventory="$check_tmp/natives.tsv"
trap 'rm -rf "$check_tmp"' EXIT
python3 -I tools/natives_called.py "$native_inventory"
cmp docs/natives_called.tsv "$native_inventory"
python3 -I tests/native_audit_test.py
python3 -I tests/native_evidence_audit_test.py
python3 -I tools/native_evidence_audit.py
python3 -I tests/menu_trace_test.py
python3 -I tests/startup_trace_test.py
python3 -I tests/startup_constructor_test.py
python3 -I tests/native_run_paths_test.py
python3 -I tests/run_log_test.py
python3 -I tests/run_screenshot_test.py
python3 -I tests/diagnose_merge_test.py
python3 -I tests/replay_test.py
python3 -I tests/oracle_controller_test.py
python3 -I tests/stock_run_layout_test.py
python3 -I tools/parity/test_compare_frames.py
python3 -I tools/parity/test_save_snapshot.py
python3 -I tools/parity/test_run_native.py
python3 -I tools/parity/test_diagnostic_data.py
python3 -I tests/wine_disc_test.py
python3 -I tools/startup_trace.py
python3 -I tests/ghidra_stock_handler_test.py
python3 -I tools/native_audit.py
build/ptx_corpus ref/raw > "$check_tmp/ptx-cpp.tsv"
python3 -I tools/ptx_census.py ref/raw > "$check_tmp/ptx-python.tsv"
cmp "$check_tmp/ptx-cpp.tsv" "$check_tmp/ptx-python.tsv"
[ "$(find ref/raw -type f -iname '*.ptx' | wc -l | tr -d ' ')" = 127 ] || { echo 'check: expected 127 PTX files' >&2; exit 1; }
echo "ptx corpus: 127 files agree with independent reader"
tools/phase2_check.sh build
archs="$(lipo -archs build/sl_port)"
[ "$archs" = "arm64" ] || { echo "check: build/sl_port is '$archs', expected 'arm64'"; exit 1; }
echo "check: ok"
