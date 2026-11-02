#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Automated probes keep rendering and scripted input without activating Cocoa.
export SLRR_PROBE_BACKGROUND="${SLRR_PROBE_BACKGROUND:-1}"
python3 -I tests/guardrails_test.py
python3 -I tools/parity/resource_pool.py --cpu 2 --host-memory 2 --timeout 90 -- python3 -I tests/test_release_bundle.py
python3 -I tools/parity/resource_pool.py --cpu 2 --host-memory 2 --timeout 90 -- python3 -I tests/test_release_stage.py
python3 -I tools/parity/resource_pool.py --cpu 2 --host-memory 2 --timeout 90 -- python3 -I tests/test_release_distribution.py

python3 -I tools/parity/resource_pool.py --cpu 1 --host-memory 2 --build-writer 1 -- \
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
python3 -I tools/parity/resource_pool.py --cpu 6 --host-memory 12 --build-writer 1 -- \
  cmake --build build --parallel 6
# Preserve suite ordering until every test's writable-state contract is audited.
# This lease also prevents independent native probes from entering its renderer lane.
python3 -I tools/parity/resource_pool.py --cpu 6 --host-memory 12 --renderer 2 -- \
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
python3 -I tests/native_background_test.py
python3 -I tests/run_log_test.py
python3 -I tests/run_screenshot_test.py
python3 -I tests/diagnose_merge_test.py
python3 -I tests/cartrace_check_test.py
python3 -I tests/replay_test.py
python3 -I tests/oracle_controller_test.py
python3 -I tests/stock_run_layout_test.py
python3 -I tools/parity/test_compare_frames.py
python3 -I tools/parity/test_proof.py
python3 -I tools/parity/test_audio_evidence.py
python3 -I tools/parity/test_resource_pool.py
python3 -I tools/parity/test_prepaid_budget.py
python3 -I tools/parity/test_catalog.py
python3 -I tools/parity/test_career_save.py
python3 -I tools/parity/test_stock_career_save.py
python3 -I tools/parity/test_save_snapshot.py
python3 -I tools/parity/test_run_native.py
python3 -I tools/parity/test_run_stock.py
python3 -I tools/parity/test_control_interval.py
python3 -I tools/parity/test_input_interval.py
python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 --host-memory 1 --timeout 30 -- python3 -I tools/parity/test_response_replay_unit.py
python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 --host-memory 1 --timeout 30 -- python3 -I tools/parity/test_performance_projection.py
python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 --host-memory 1 --timeout 30 -- python3 -I tools/parity/stock/test_stock_input_tape.py
python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 --host-memory 1 --timeout 30 -- python3 -I tools/parity/stock/test_stock_prefix.py
python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 --host-memory 1 --timeout 30 -- python3 -I tools/parity/stock/test_prefix_archive.py
python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 --host-memory 1 --timeout 30 -- python3 -I tools/parity/stock/test_career_flow.py
python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 --host-memory 1 --timeout 30 -- python3 -I tools/parity/stock/test_durable_archive.py
python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 --host-memory 1 --timeout 30 -- python3 -I tools/parity/stock/test_durable_wire.py
python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 --host-memory 1 --timeout 30 -- python3 -I tools/parity/stock/test_container_preflight.py
python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 --host-memory 1 --timeout 60 -- python3 -I tools/parity/test_storage.py
python3 -I tools/parity/test_dev.py
python3 -I tools/parity/test_experiment_result.py
python3 -I tools/parity/test_capacity.py
python3 -I tools/parity/test_jev_packet.py
python3 -I tools/parity/test_evidence_index.py
python3 -I tools/parity/test_trace_inspect.py
python3 -I tools/parity/test_summarize_native_runs.py
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
