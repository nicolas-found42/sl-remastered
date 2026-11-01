#!/usr/bin/env python3
"""Exercise native Run path refusals with disposable directories, no game data."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

probe = Path(sys.argv[1] if len(sys.argv) > 1 else 'build/startup_probe').resolve()
with tempfile.TemporaryDirectory(prefix='sl-run-paths-') as name:
    root = Path(name)
    game, reference = root / 'game', root / 'reference'
    game.mkdir()
    reference.mkdir()
    alias = root / 'alias'
    alias.symlink_to(game, target_is_directory=True)
    for source in (game, reference, alias):
        data = source / 'writable'
        result = subprocess.run([str(probe), '--no-run-log', '--data-root', str(data),
                                 '--reference-root', str(reference), str(game)],
                                capture_output=True, text=True, timeout=5, cwd=root)
        assert result.returncode == 2, result.stderr
        assert 'data root must not overlap' in result.stderr, result.stderr
        assert not data.exists()
    for source in (game, reference, alias):
        output = source / 'runs'
        result = subprocess.run([str(probe), '--reference-root', str(reference), str(game)],
                                env=dict(os.environ, SLRR_RUN_DIR=str(output)),
                                capture_output=True, text=True, timeout=5, cwd=root)
        assert result.returncode == 2, result.stderr
        assert 'startup probe: run log:' in result.stderr, result.stderr
        assert not output.exists()
print('native Run refuses installed-game and reference writes, including symlink aliases')
