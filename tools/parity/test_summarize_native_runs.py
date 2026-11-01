import json
from pathlib import Path
import subprocess
import tempfile
import unittest


class NativeSummaryTest(unittest.TestCase):
    def test_reports_each_sample_and_median_range(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            outputs = []
            for mode, times in [('rapid', [2, 1, 4, 5, 3]), ('cold', [9, 6, 10, 8, 7])]:
                for i, value in enumerate(times):
                    out = root / f'{mode}-{i}'
                    out.mkdir()
                    (out / 'metadata.json').write_text(json.dumps({'mode': mode, 'probe_sha256': 'same-probe', 'game_data': {'status': 'complete', 'sha256': 'same-data'}, 'trace': {}, 'environment': {}}))
                    (out / 'initial-save').mkdir()
                    (out / 'initial-save/manifest.json').write_text(json.dumps({'files': {'options': 'same-save'}}))
                    (out / 'result.json').write_text(json.dumps({'exit_code': 0, 'timed_out': False, 'source_save_unchanged': True, 'timing': {'preparation_seconds': .25, 'menu_seconds': 1, 'driving_seconds': value, 'total_seconds': value + 3}}))
                    outputs.append(str(out))
            tool = Path(__file__).with_name('summarize_native_runs.py')
            result = subprocess.run(['python3', '-I', str(tool), *outputs], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(result.stdout)
            self.assertEqual(len(report['samples']), 10)
            self.assertEqual(report['summary']['rapid']['driving_seconds'], {'count': 5, 'median': 3, 'min': 1, 'max': 5})
            self.assertEqual(report['summary']['cold']['driving_seconds']['median'], 8)


if __name__ == '__main__':
    unittest.main()
