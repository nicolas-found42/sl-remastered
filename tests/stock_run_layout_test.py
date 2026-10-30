#!/usr/bin/env python3
"""Exercise packaging and diagnosis without requiring Wine or game files."""
import pathlib
import os
import importlib.util
import hashlib
import json
import subprocess
import tempfile
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACK = ROOT / 'tools/wine/capture/finish_run.py'
DIAGNOSE = ROOT / 'tools/diagnose.sh'
spec = importlib.util.spec_from_file_location('finish_run', PACK)
finish_run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(finish_run)


class StockRunTest(unittest.TestCase):
    def test_existing_output_is_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            capture = root / 'capture'
            capture.mkdir()
            out = root / 'packaged'
            out.mkdir()
            (out / 'run.log').write_text('previous evidence')
            result = subprocess.run(['python3', '-I', str(PACK), str(capture), '--out', str(out)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('output already exists', result.stderr)
            self.assertEqual((out / 'run.log').read_text(), 'previous evidence')

    def test_proxy_clock_offsets_and_wrap(self):
        self.assertEqual(finish_run.proxy_clock_delta('clock_origin_tick=1000', 'clock_origin_tick=1200'), 0.2)
        self.assertEqual(finish_run.proxy_clock_delta('clock_origin_tick=4294967040', 'clock_origin_tick=244'), 0.5)
        self.assertEqual(finish_run.proxy_clock_delta('clock_origin_tick=244', 'clock_origin_tick=4294967040'), -0.5)
        self.assertIsNone(finish_run.proxy_clock_delta('clock_origin_tick=1000', 'old log'))

    def test_packager_aligns_proxy_clocks_without_running_wine(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp); capture = root / 'capture'; dumps = capture / 'game/dumps'
            dumps.mkdir(parents=True)
            (dumps / 'd3d9proxy.log').write_text('[ 0 ms] clock_origin_tick=4294967040\n[ 600 ms] input: move 483 189\n')
            (dumps / 'dinput8proxy.log').write_text('[ 0 ms] clock_origin_tick=244\n[ 200 ms] cmd: mclick 0 300\n')
            (capture / 'game-run.log').write_text('')
            fake = root / 'wine'; marker = root / 'ran'
            fake.write_text(f'#!/bin/sh\ntouch "{marker}"\n'); fake.chmod(0o755)
            manifest = capture / 'game-data-manifest.json'
            manifest.write_text('{"complete": true}\n')
            identity = {'status': 'complete', 'sha256': hashlib.sha256(manifest.read_bytes()).hexdigest()}
            (capture / 'launch.json').write_text(json.dumps({'wine_path': str(fake), 'wine_version': None, 'argv': [str(fake), 'game.exe'], 'game_data': identity}))
            out = root / 'out'
            subprocess.run(['python3', '-I', str(PACK), str(capture), '--out', str(out)], check=True, capture_output=True, text=True, env=dict(os.environ, WINEDIR=str(root)))
            self.assertFalse(marker.exists())
            self.assertIn('DirectInput origin is +0.500 s', (out / 'meta.txt').read_text())
            self.assertIn('game_data_manifest_check: MATCH', (out / 'meta.txt').read_text())
            manifest.write_text('changed')
            second = root / 'out-second'
            subprocess.run(['python3', '-I', str(PACK), str(capture), '--out', str(second)], check=True, capture_output=True, text=True)
            self.assertIn('game_data_manifest_check: MISMATCH', (second / 'meta.txt').read_text())
            replay = (out / 'input.txt').read_text()
            self.assertIn('0.600 move 483 156', replay)
            self.assertIn('0.700 down', replay)
            self.assertLess(replay.index('0.600 move'), replay.index('0.700 down'))

    def test_old_capture_keeps_unaligned_input_out_of_timeline(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp); capture = root / 'capture'; dumps = capture / 'game/dumps'
            dumps.mkdir(parents=True)
            (dumps / 'd3d9proxy.log').write_text('[ 15000 ms] input: move 483 189\n')
            (dumps / 'dinput8proxy.log').write_text('[ 14000 ms] cmd: mclick 0 300\n')
            out = root / 'out'
            subprocess.run(['python3', '-I', str(PACK), str(capture), '--out', str(out)], check=True, capture_output=True, text=True)
            self.assertIn('DirectInput proxy clock is unaligned', (out / 'run.log').read_text())
            self.assertIn('mclick 0 300', (out / 'run.log').read_text())
            replay = (out / 'input.txt').read_text()
            self.assertIn('DirectInput events are omitted', replay)
            self.assertNotIn(' down\n', replay)
            self.assertIn('unknown; DirectInput events are not merged', (out / 'meta.txt').read_text())

    def test_capture_and_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            capture = root / 'capture-20261028-120000'
            dumps = capture / 'game/dumps'
            dumps.mkdir(parents=True)
            (dumps / 'd3d9proxy.log').write_text(
                '[ 1000 ms] stats: presents=100 draws=12 (+12) clears=3 resets=0 dumps=0 dump_fail=0\n'
                '[ 40000 ms] stats: presents=100 draws=12 (+0) clears=3 resets=0 dumps=0 dump_fail=0\n')
            (capture / 'game-run.log').write_text('')
            out = root / 'packaged'
            env = dict(os.environ, WINEDIR=str(root / 'no-wine'))
            subprocess.run(['python3', '-I', str(PACK), str(capture), '--out', str(out)], check=True, capture_output=True, text=True, env=env)
            result = (out / 'result.txt').read_text()
            self.assertIn('status=hung', result)
            report = subprocess.run([str(DIAGNOSE), str(out)], check=True, capture_output=True, text=True).stdout
            self.assertIn('STOCK WINE RUN: HUNG', report)
            self.assertIn('frames stopped at 100', report)
            self.assertIn('presents: 100', report)
            self.assertIn('clock_note:', report)
            self.assertEqual((dumps / 'd3d9proxy.log').read_text().count('stats:'), 2)

    def test_input_delivery_counts_and_finished_flow_are_not_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp); capture = root / 'capture'; dumps = capture / 'game/dumps'
            dumps.mkdir(parents=True)
            (dumps / 'd3d9proxy.log').write_text('[ 0 ms] clock_origin_tick=1000\n[ 10000 ms] stats: presents=100 draws=12 (+12) clears=3 resets=0 dumps=0 dump_fail=0\n')
            (dumps / 'dinput8proxy.log').write_text(
                '[ 0 ms] clock_origin_tick=1000\n[ 0 ms] input_telemetry=v1\n'
                '[ 1000 ms] cmd: mclick 0 300\n'
                '[ 1001 ms] input_queue: queued kind=1 offset=12 data=128 depth=1\n'
                '[ 1002 ms] input_buffered: delivered kind=1 offset=12 data=128\n'
                '[ 1003 ms] input_state: observed_down kind=1 offset=12\n'
                '[ 1004 ms] input_queue: dropped kind=1 offset=12 data=0 depth=256\n'
                '[ 1005 ms] input_buffered: source_overflow kind=1\n')
            # The proxy clock anchor comes from file mtimes (now); the flow line must sit
            # on that same timeline or the classifier reads a hours-long frame gap.
            flow_now = time.strftime('%H:%M:%S', time.localtime(time.time() + 10))
            (capture / 'flow.log').write_text(f'{flow_now} script finished; last log lines:\n')
            out = root / 'out'
            subprocess.run(['python3', '-I', str(PACK), str(capture), '--out', str(out)], check=True, capture_output=True, text=True)
            result = (out / 'result.txt').read_text()
            self.assertIn('status=unknown', result)
            self.assertIn('scene success and process exit were not recorded', result)
            self.assertIn('1 buffered event(s) dropped', result)
            self.assertIn('1 buffered event(s) returned', result)
            self.assertIn('1 underlying DirectInput overflow response(s)', result)
            counters = (out / 'counters.txt').read_text()
            self.assertIn('dinput_commands_issued=1', counters)
            self.assertIn('dinput_dropped=1', counters)
            self.assertIn('dinput_state_down_observed=1', counters)
            self.assertIn('not proof the game received', (out / 'input.txt').read_text())
            report = subprocess.run([str(DIAGNOSE), str(out)], check=True, capture_output=True, text=True).stdout
            self.assertIn('returned input does not establish game response', report)

    def test_old_input_log_does_not_claim_zero_drops(self):
        old = '[ 10 ms] cmd: mclick 0 300\n'
        _, commands, _, _ = finish_run.parse_dinput(old)
        facts = finish_run.input_delivery(old, commands)
        self.assertEqual(facts['commands'], 1)
        self.assertFalse(facts['available'])
        with tempfile.TemporaryDirectory() as tmp:
            capture = pathlib.Path(tmp); dumps = capture / 'game/dumps'; dumps.mkdir(parents=True)
            (dumps / 'dinput8proxy.log').write_text(old)
            info = finish_run.package(capture, capture / 'unused')
            self.assertTrue(any('drops and state observations were not recorded' in line for line in info['evidence']))
            self.assertNotIn('dinput_dropped=', finish_run.build_counters(info))

    def test_exception_is_not_filtered_as_noise(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            capture = root / 'capture'
            capture.mkdir()
            (capture / 'game-run.log').write_text('0001:trace:seh:Unhandled exception in Wine\n')
            out = root / 'packaged'
            env = dict(os.environ, WINEDIR=str(root / 'no-wine'))
            subprocess.run(['python3', '-I', str(PACK), str(capture), '--out', str(out)], check=True, capture_output=True, text=True, env=env)
            self.assertIn('status=crashed', (out / 'result.txt').read_text())
            report = subprocess.run([str(DIAGNOSE), str(out)], check=True, capture_output=True, text=True).stdout
            self.assertIn('Unhandled exception', report)


if __name__ == '__main__':
    unittest.main()
