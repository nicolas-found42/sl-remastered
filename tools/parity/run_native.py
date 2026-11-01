#!/usr/bin/env python3
"""Record one native Free Ride attempt with its initial save, script and trace.

Modes:
  rapid (default) -- bundled tools/scripts/free-ride-fast.txt; waits for the
      splash cancel hotkey, skips the splash with normal Escape, clicks the real
      menu and waits for a real driving scene, then a short throttle burst.
  --cold-start -- same driving wait and post-ready throttle burst, but the
      menu is entered with the legacy fixed-timed click (10/13/13.5 s).
  --script FILE -- caller-supplied script instead of a bundled one.

Every run snapshots the --save fixture into a fresh data root (the fixture is
never modified), copies the exact script that ran into the output directory,
and points the probe's run recorder (SLRR_RUN_DIR) at <output>/run so
`shot @run/...` lines work and run telemetry stays inside the record. The
probe runs from the isolated writable data root with an explicit Reference
Folder; scripts should use `@run/` shot paths. A nonzero
exit or subprocess timeout is a failure; screenshots and exit codes alone do
not prove driving or parity.
"""
import argparse
import datetime
import hashlib
import importlib.util
import json
import math
import os
import re
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

TOOLS_DIR = Path(__file__).resolve().parent.parent
RAPID_SCRIPT = TOOLS_DIR / 'scripts' / 'free-ride-fast.txt'
# Cold start keeps the legacy fixed-timed menu entry, then shares the rapid
# route's driving wait and post-ready throttle burst for a comparable scene.
COLD_SCRIPT_TEXT = (
    '# Cold-start Free Ride: legacy timed menu click, then wait driving and\n'
    '# the same post-ready throttle burst as free-ride-fast.txt.\n'
    '10 moven 0.65625 0.264583333\n13 down\n13.5 up\n13.5 wait driving 60\n'
    '13.5 shot @run/driving-ready.bmp\n13.6 keydown Up\n15.6 keyup Up\n'
    '15.7 shot @run/driving-throttle.bmp\n15.8 quit\n')
DEFAULT_TIMEOUT = 120.0


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    preparation_started = time.monotonic()
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('output', help='new output directory outside ref/')
    parser.add_argument('--probe', required=True, help='built startup_probe')
    parser.add_argument('--game', required=True, help='extracted 2005 game directory')
    parser.add_argument('--reference-root', default=str(snapshot_module.REF),
                        help='immutable Reference Folder (default: repository ref/)')
    parser.add_argument('--save', required=True, help='save directory to copy before the run')
    parser.add_argument('--script', help='script file to run instead of the bundled mode script')
    parser.add_argument('--cold-start', action='store_true',
                        help='legacy timed menu entry instead of wait menu; same driving wait')
    parser.add_argument('--timeout', type=float,
                        help='maximum seconds (default: 120)')
    args = parser.parse_args()
    try:
        mode = 'cold' if args.cold_start else 'rapid'
        probe, game, save = (Path(p).resolve(strict=True) for p in (args.probe, args.game, args.save))
        out = Path(args.output).resolve()
        reference = Path(args.reference_root).resolve()
        timeout = args.timeout if args.timeout is not None else DEFAULT_TIMEOUT
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError('timeout must be positive')
        if out == reference or reference in out.parents or out in reference.parents:
            raise ValueError('output must be outside reference root and must not contain it')
        if not probe.is_file() or not game.is_dir() or not save.is_dir():
            raise ValueError('probe must be a file; game and save must be directories')
        if any(out == source or source in out.parents or out in source.parents for source in (game, save)):
            raise ValueError('output and game/save directories must not contain one another')
        if args.script:
            script_source = Path(args.script).resolve(strict=True)
            if not script_source.is_file():
                raise ValueError('--script must be a file')
            script_label = 'caller'
            # Plain `shot PATH` writes anywhere the probe can; only run-local
            # `@run/` shots keep a custom scenario out of game and save trees.
            for number, line in enumerate(script_source.read_text().splitlines(), 1):
                fields = line.split()
                if len(fields) >= 2 and fields[1] == 'shot' and (
                        len(fields) < 3 or not fields[2].startswith('@run/')):
                    raise ValueError(f'--script line {number}: shot must use @run/NAME')
        else:
            script_source = RAPID_SCRIPT.resolve(strict=True) if mode == 'rapid' else None
            script_label = f'bundled-{mode}'
        out.mkdir(parents=True, exist_ok=False)
        manifest_path = out / 'game-data-manifest.json'
        try:
            manifest = data_module.write_manifest(game, manifest_path)
            data_identity = {'status': 'complete' if manifest['complete'] else 'incomplete',
                             'path': str(manifest_path), 'sha256': digest(manifest_path)}
        except (OSError, ValueError) as error:
            data_identity = {'status': 'unavailable', 'reason': str(error)}
        initial_save = snapshot_module.snapshot(save, out / 'initial-save')
        data = out / 'data'
        data.mkdir()
        shutil.copytree(out / 'initial-save/save', data / 'save')
        # Record the exact bytes the probe ran; bundled cold text is materialized
        # the same way so every run's script is verifiable from the output alone.
        script = out / 'script.txt'
        if script_source is not None:
            shutil.copyfile(script_source, script)
        else:
            script.write_text(COLD_SCRIPT_TEXT)
        run_dir = out / 'run'
        run_dir.mkdir()
        cmd = [str(probe), '--init', '--renderer', '--loop', '--visible',
               '--script', str(script), '--data-root', str(data),
               '--reference-root', str(reference), str(game)]
        metadata = {'probe': str(probe), 'probe_sha256': digest(probe), 'game': str(game),
                    'game_data': data_identity,
                    'reference_root': str(reference),
                    'mode': mode, 'script_source': script_label,
                    'script_path': str(script), 'script_sha256': digest(script),
                    'run_dir': str(run_dir),
                    'command': cmd, 'timeout_seconds': timeout,
                    'trace': {'SLRR_TRACE_RANDOM_SEED': '1', 'SLRR_TRACE_CAR_CREATION': '1'},
                    'environment': {key: value for key, value in os.environ.items()
                                    if key.startswith(('SLRR_', 'SDL_'))},
                    'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
        (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        environment = os.environ.copy()
        environment.update(metadata['trace'])
        # Run-local recorder: @run shots and run telemetry land inside the record.
        environment['SLRR_RUN_DIR'] = str(run_dir)
        start = time.monotonic()
        preparation_seconds = start - preparation_started
        # Keep cwd-relative writes disposable. The probe receives an explicit
        # Reference Folder for both save guards and stock cursor resources.
        with (out / 'stdout.log').open('w') as stdout, (out / 'stderr.log').open('w') as stderr:
            try:
                result = subprocess.run(cmd, env=environment, cwd=data,
                                        stdout=stdout, stderr=stderr, timeout=timeout)
                status = {'exit_code': result.returncode, 'timed_out': False}
            except subprocess.TimeoutExpired:
                status = {'exit_code': None, 'timed_out': True}
            except OSError as error:
                status = {'exit_code': None, 'timed_out': False, 'error': str(error)}
        status['elapsed_seconds'] = time.monotonic() - start
        status['source_save_unchanged'] = (snapshot_module.manifest(save) == initial_save['files'])
        milestones = {}
        for name, process, loop, frame in re.findall(
                r'startup probe: milestone (menu|driving) process=([0-9.]+) loop=([0-9.]+) frame=(\d+)',
                (out / 'stdout.log').read_text(errors='replace')):
            milestones.setdefault(name, {'process_seconds': float(process),
                                         'loop_seconds': float(loop), 'frame': int(frame)})
        status['milestones'] = milestones
        status['timing'] = {'preparation_seconds': preparation_seconds,
                            'menu_seconds': milestones.get('menu', {}).get('process_seconds'),
                            'driving_seconds': milestones.get('driving', {}).get('process_seconds'),
                            'total_seconds': time.monotonic() - preparation_started}
        (out / 'result.json').write_text(json.dumps(status, indent=2) + '\n')
        print(json.dumps(status))
        if status['timed_out'] or status['exit_code'] != 0 or not status['source_save_unchanged']:
            return 1
        return 0
    except (OSError, ValueError) as error:
        parser.exit(2, f'run_native: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
