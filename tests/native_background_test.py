#!/usr/bin/env python3
"""Exercise normal menu entry and capture without showing the native window."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
game = root / '.scratch/2005-extract/Program Files/Street Legal Racing - Redline'
probe = root / 'build/startup_probe'
if not probe.is_file() or not game.is_dir():
    raise SystemExit('background test requires startup_probe and the Installed Game')

with tempfile.TemporaryDirectory(prefix='sl-background-') as temp:
    base = Path(temp)
    (base / 'save').mkdir()
    script = base / 'menu.txt'
    script.write_text('0 wait splash 30\n0 keydown Escape\n0 wait menu 30\n'
                      '0 keyup Escape\n0 shot @run/menu.bmp\n0.1 quit\n')
    for mode in ('flag', 'environment'):
        out = base / mode
        environment = dict(os.environ, SDL_AUDIODRIVER='dummy')
        environment.pop('SLRR_PROBE_BACKGROUND', None)
        args = ['python3', '-I', str(root / 'tools/parity/run_native.py'),
                str(out), '--probe', str(probe), '--game', str(game),
                '--reference-root', str(root / 'ref'), '--save', str(base / 'save'),
                '--script', str(script), '--timeout', '30']
        if mode == 'flag':
            args.append('--background')
        else:
            environment['SLRR_PROBE_BACKGROUND'] = '1'
            args.append('--visible')
        result = subprocess.run(args, env=environment, capture_output=True,
                                text=True, timeout=40)
        assert result.returncode == 0, result.stderr
        status = json.loads((out / 'result.json').read_text())
        assert status['source_save_unchanged'] and not status['timed_out']
        assert status['milestones']['menu']['frame'] > 0
        runs = [p for p in (out / 'run').iterdir() if not p.is_symlink()]
        assert len(runs) == 1
        assert (runs[0] / 'menu.bmp').read_bytes()[:2] == b'BM'
        log = (runs[0] / 'run.log').read_text()
        window = re.search(r'background window: hidden=(\d+) fullscreen=(\d+) inputFocus=(\d+)', log)
        assert window and window.groups() == ('1', '0', '0'), log

print('native background: flag/environment entry, hidden window and menu capture pass')
