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
        assert not data.exists()
    for source in (game, reference, alias):
        output = source / 'runs'
        result = subprocess.run([str(probe), '--reference-root', str(reference), str(game)],
                                env=dict(os.environ, SLRR_RUN_DIR=str(output)),
                                capture_output=True, text=True, timeout=5, cwd=root)
        assert result.returncode == 2, result.stderr
        assert not output.exists()
    for protected in (game, reference):
        for nested in (False, True):
            data = root / f'data-{protected.name}-{nested}'
            data.mkdir()
            victim = protected / 'victim'
            victim.write_bytes(b'preserve')
            if nested:
                (data / 'save/game').mkdir(parents=True)
                (data / 'save/game/options').symlink_to(victim)
            else:
                (data / 'save').symlink_to(protected, target_is_directory=True)
            output = root / f'run-{protected.name}-{nested}'
            result = subprocess.run([str(probe), '--data-root', str(data),
                                     '--reference-root', str(reference), str(game)],
                                    env=dict(os.environ, SLRR_RUN_DIR=str(output)),
                                    capture_output=True, text=True, timeout=5, cwd=root)
            assert result.returncode == 2, result.stderr
            assert victim.read_bytes() == b'preserve'
            assert not output.exists()
    contained = root / 'contained-data'
    (contained / 'private-save').mkdir(parents=True)
    (contained / 'private-save/options').write_bytes(b'local')
    (contained / 'private-save/alias').symlink_to(contained / 'private-save/options')
    (contained / 'save').symlink_to(contained / 'private-save', target_is_directory=True)
    result = subprocess.run([str(probe), '--no-run-log', '--data-root', str(contained),
                             '--reference-root', str(reference), str(game)],
                            capture_output=True, text=True, timeout=5, cwd=root)
    # The disposable game has no Packs: safe paths reach normal startup failure.
    assert result.returncode == 1, result.stderr
    assert (contained / 'private-save/options').read_bytes() == b'local'
    safe = root / 'duplicate-data'
    for index, paths in enumerate(((game, safe), (safe, game))):
        output = root / f'duplicate-run-{index}'
        result = subprocess.run([str(probe), '--data-root', str(paths[0]),
                                 '--data-root', str(paths[1]),
                                 '--reference-root', str(reference), str(game)],
                                env=dict(os.environ, SLRR_RUN_DIR=str(output)),
                                capture_output=True, text=True, timeout=5, cwd=root)
        assert result.returncode == 2, result.stderr
        assert not output.exists()
print('native Run refuses installed-game and reference writes, including symlink aliases')
