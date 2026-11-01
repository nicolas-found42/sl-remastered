#!/usr/bin/env python3
"""Exercise per-run and explicit scripted screenshots with the native probe."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

probe = Path(sys.argv[1] if len(sys.argv) > 1 else 'build/startup_probe').resolve()
game = Path('.scratch/2005-extract/Program Files/Street Legal Racing - Redline').resolve()
if not probe.is_file() or not game.is_dir():
    raise SystemExit('screenshot test requires startup_probe and 2005 game extract')

with tempfile.TemporaryDirectory(prefix='sl-shot-') as temp:
    root = Path(temp)
    data = root / 'data'
    (data / 'save/game').mkdir(parents=True)
    shutil.copyfile(game / 'save/game/options', data / 'save/game/options')
    def run(name, actions, no_log=False):
        script = root / (name + '.txt')
        script.write_text(actions)
        base = root / ('runs-' + name)
        env = dict(os.environ, SLRR_RUN_DIR=str(base))
        args = [str(probe), '--init', '--renderer', '--loop', '--background', '--no-intro', '--script', str(script), '--data-root', str(data)]
        if no_log:
            args.append('--no-run-log')
        return subprocess.run(args + [str(game)], env=env, capture_output=True, text=True, timeout=90), base

    result, base = run('per-run', '0.5 shot @run/frame.bmp\n2 quit\n')
    assert result.returncode == 0, result.stderr
    dirs = [p for p in base.iterdir() if p.is_dir() and not p.is_symlink()]
    assert len(dirs) == 1 and (dirs[0] / 'frame.bmp').read_bytes()[:2] == b'BM'
    assert str(dirs[0] / 'frame.bmp') in (dirs[0] / 'run.log').read_text()

    # An overdue quit in the same frame must still honor the earlier shot.
    result, base = run('shot-quit', '0 shot @run/final.bmp\n0 quit\n')
    assert result.returncode == 0, result.stderr
    dirs = [p for p in base.iterdir() if p.is_dir() and not p.is_symlink()]
    assert len(dirs) == 1 and (dirs[0] / 'final.bmp').read_bytes()[:2] == b'BM'

    explicit = root / 'explicit.bmp'
    result, _ = run('explicit', f'0.5 shot {explicit}\n2 quit\n', no_log=True)
    assert result.returncode == 0 and explicit.read_bytes()[:2] == b'BM', result.stderr

    result, _ = run('no-log', '0.5 shot @run/frame.bmp\n2 quit\n', no_log=True)
    assert result.returncode == 2 and '@run requires an active run log' in result.stderr, result.stderr

    result, base = run('duplicate', '0.5 shot @run/frame.bmp\n1 shot @run/frame.bmp\n2 quit\n')
    assert result.returncode == 2 and 'script shot already exists' in result.stderr, result.stderr
    dirs = [p for p in base.iterdir() if p.is_dir() and not p.is_symlink()]
    assert len(dirs) == 1 and (dirs[0] / 'frame.bmp').read_bytes()[:2] == b'BM'

    result, _ = run('escape', '0.5 shot @run/../bad.bmp\n2 quit\n')
    assert result.returncode == 2 and '@run requires a simple filename' in result.stderr, result.stderr

print('script screenshots: per-run, explicit, disabled log, overwrite and traversal checks passed')
