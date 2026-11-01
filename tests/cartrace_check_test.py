"""Exercise the Run trace checker at its file/CLI boundary."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


class CartraceCheckTest(unittest.TestCase):
    def check(self, rows):
        with tempfile.TemporaryDirectory() as temp:
            Path(temp, 'cartrace-full.jsonl').write_text(
                ''.join(json.dumps(row) + '\n' for row in rows))
            return subprocess.run(['python3', '-I', str(Path(__file__).resolve().parents[1] / 'tools/cartrace_check.py'), temp], capture_output=True, text=True)

    def test_actual_production_anomaly_fails(self):
        row = {'tick': 2, 'car': 'player', 'spd': [0, 0],
               'post': {'p': [0, 10, 0]}, 'anom': 'iteration_cap'}
        self.assertEqual(self.check([row]).returncode, 1)

    def test_empty_trace_is_not_driving_evidence(self):
        self.assertEqual(self.check([]).returncode, 1)

    def test_nonfinite_speed_is_rejected(self):
        row = {'tick': 2, 'car': 'player', 'spd': [0, float('nan')],
               'post': {'p': [0, 10, 0]}}
        self.assertEqual(self.check([row]).returncode, 1)

    def test_first_recorded_step_cannot_hide_a_speed_jump(self):
        row = {'tick': 1, 'car': 'player', 'spd': [0, 12],
               'post': {'p': [0, 10, 0]}}
        self.assertEqual(self.check([row]).returncode, 1)

    def test_finite_unflagged_steps_pass(self):
        rows = [{'tick': i, 'car': 'player', 'spd': [i - 1, i],
                 'post': {'p': [i, 10, 0]}} for i in (1, 2)]
        self.assertEqual(self.check(rows).returncode, 0)


if __name__ == '__main__':
    unittest.main()
