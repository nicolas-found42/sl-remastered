#!/usr/bin/env python3
"""Record one native Free Ride attempt with its initial save and trace."""
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

spec = importlib.util.spec_from_file_location('save_snapshot', Path(__file__).with_name('save_snapshot.py'))
snapshot_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(snapshot_module)
data_spec = importlib.util.spec_from_file_location('diagnostic_data', Path(__file__).with_name('diagnostic-data.py'))
data_module = importlib.util.module_from_spec(data_spec)
data_spec.loader.exec_module(data_module)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', help='new output directory outside ref/')
    parser.add_argument('--probe', required=True, help='built startup_probe')
    parser.add_argument('--game', required=True, help='extracted 2005 game directory')
    parser.add_argument('--save', required=True, help='save directory to copy before the run')
    parser.add_argument('--timeout', type=float, default=60, help='maximum seconds (default 60)')
    args = parser.parse_args()
    try:
        probe, game, save = (Path(p).resolve(strict=True) for p in (args.probe, args.game, args.save))
        out = Path(args.output).resolve()
        if args.timeout <= 0:
            raise ValueError('timeout must be positive')
        if out == snapshot_module.REF or snapshot_module.REF in out.parents:
            raise ValueError('output must be outside ref/')
        if not probe.is_file() or not game.is_dir() or not save.is_dir():
            raise ValueError('probe must be a file; game and save must be directories')
        if any(out == source or source in out.parents or out in source.parents for source in (game, save)):
            raise ValueError('output and game/save directories must not contain one another')
        out.mkdir(parents=True, exist_ok=False)
        manifest_path = out / 'game-data-manifest.json'
        try:
            manifest = data_module.write_manifest(game, manifest_path)
            data_identity = {'status': 'complete' if manifest['complete'] else 'incomplete',
                             'path': str(manifest_path), 'sha256': digest(manifest_path)}
        except (OSError, ValueError) as error:
            data_identity = {'status': 'unavailable', 'reason': str(error)}
        snapshot_module.snapshot(save, out / 'initial-save')
        data = out / 'data'
        data.mkdir()
        shutil.copytree(out / 'initial-save/save', data / 'save')
        script = out / 'input.txt'
        script.write_text('# Seconds from native frame loop start; 640x480 window points.\n10 move 420 127\n13 down\n13.5 up\n40 quit\n')
        cmd = [str(probe), '--init', '--renderer', '--loop', '--visible', '--no-run-log', '--script', str(script), '--data-root', str(data), str(game)]
        metadata = {'probe': str(probe), 'probe_sha256': digest(probe), 'game': str(game),
                    'game_data': data_identity,
                    'script_sha256': digest(script), 'command': cmd, 'timeout_seconds': args.timeout,
                    'trace': {'SLRR_TRACE_RANDOM_SEED': '1', 'SLRR_TRACE_CAR_CREATION': '1'},
                    'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
        (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        environment = os.environ.copy()
        environment.update(metadata['trace'])
        start = time.monotonic()
        with (out / 'stdout.log').open('w') as stdout, (out / 'stderr.log').open('w') as stderr:
            try:
                result = subprocess.run(cmd, env=environment, stdout=stdout, stderr=stderr, timeout=args.timeout)
                status = {'exit_code': result.returncode, 'timed_out': False}
            except subprocess.TimeoutExpired:
                status = {'exit_code': None, 'timed_out': True}
        status['elapsed_seconds'] = time.monotonic() - start
        (out / 'result.json').write_text(json.dumps(status, indent=2) + '\n')
        print(json.dumps(status))
        if status['timed_out'] or status['exit_code'] != 0:
            return 1
        return 0
    except (OSError, ValueError) as error:
        parser.exit(2, f'run_native: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
