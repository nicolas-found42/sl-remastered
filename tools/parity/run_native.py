#!/usr/bin/env python3
"""Record one native Free Ride attempt with its initial save, script and trace.

Presentation defaults to an offscreen GPU target and a hidden, non-focusable
coordinate window. Background audio is captured privately. --visible opts into
desktop presentation. Native intros run unless --skip-intro is explicit.

Modes:
  rapid (default) -- bundled tools/scripts/free-ride-fast.txt; waits for the
      splash cancel hotkey, skips the splash with normal Escape, clicks the real
      menu and waits for a real driving scene, then a short throttle burst.
  --cold-start -- same driving wait and post-ready throttle burst, but the
      menu is entered with the legacy fixed-timed click (10/13/13.5 s).
  --script FILE -- caller-supplied script instead of a bundled one.
  --background -- render offscreen without desktop fullscreen/focus,
      audible game output, or host cursor changes. Rapid scripts skip the splash
      using Escape. SLRR_PROBE_BACKGROUND=1 also enables this for direct probes.

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
import sys
import time

spec = importlib.util.spec_from_file_location('save_snapshot', Path(__file__).with_name('save_snapshot.py'))
snapshot_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(snapshot_module)
data_spec = importlib.util.spec_from_file_location('diagnostic_data', Path(__file__).with_name('diagnostic-data.py'))
data_module = importlib.util.module_from_spec(data_spec)
data_spec.loader.exec_module(data_module)
focus_spec = importlib.util.spec_from_file_location('focus_evidence', Path(__file__).with_name('focus_evidence.py'))
focus_module = importlib.util.module_from_spec(focus_spec)
focus_spec.loader.exec_module(focus_module)
audio_spec = importlib.util.spec_from_file_location('audio_evidence', Path(__file__).with_name('audio_evidence.py'))
audio_module = importlib.util.module_from_spec(audio_spec)
audio_spec.loader.exec_module(audio_module)
pool_spec = importlib.util.spec_from_file_location('resource_pool', Path(__file__).with_name('resource_pool.py'))
pool_module = importlib.util.module_from_spec(pool_spec)
pool_spec.loader.exec_module(pool_module)
prepaid_spec = importlib.util.spec_from_file_location('prepaid_budget', Path(__file__).with_name('prepaid_budget.py'))
prepaid_module = importlib.util.module_from_spec(prepaid_spec)
prepaid_spec.loader.exec_module(prepaid_module)


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
STORAGE_STATE = TOOLS_DIR.parent/'local/storage-index'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_manifest(game, output):
    # The diagnostic content identity excludes runtime files. Preservation must
    # cover those too: the probe has a separate writable data/save directory.
    value = data_module.collect(game, exclude_runtime=False)
    with output.open('x') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
    return value


def run(lease):
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
    parser.add_argument('--skip-intro', action='store_true',
                        help='explicit diagnostic bypass; this run cannot prove startup-video coverage')
    parser.add_argument('--focus-observer', type=Path,
                        help='built parity_focus_observer (default: beside probe, then repository build/)')
    presentation = parser.add_mutually_exclusive_group()
    presentation.add_argument('--background', dest='background', action='store_true',
                              help='offscreen renderer and hidden coordinate window (default); private captured audio')
    presentation.add_argument('--visible', dest='background', action='store_false',
                              help='show the native window and apply stock fullscreen settings')
    parser.set_defaults(background=True)
    parser.add_argument('--timeout', type=float,
                        help='maximum seconds (default: 120)')
    parser.add_argument('--max-artifact-mib', type=int, default=1024,
                        help='background output-tree sampled logical-byte limit (default: 1024 MiB)')
    parser.add_argument('--artifact-budget-root', type=Path,
                        help='existing exact registered prepaid parent; output must be a unique direct child')
    parser.add_argument('--artifact-parent-max-mib', type=int,
                        help='exact already-registered prepaid parent allowance in MiB (no increase)')
    args = parser.parse_args()
    prepaid = None
    out = None
    status = None
    try:
        mode = 'cold' if args.cold_start else 'rapid'
        probe, game, save = (Path(p).resolve(strict=True) for p in (args.probe, args.game, args.save))
        out = Path(args.output).resolve()
        reference = Path(args.reference_root).resolve()
        timeout = args.timeout if args.timeout is not None else DEFAULT_TIMEOUT
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError('timeout must be positive')
        if args.max_artifact_mib < 1:
            raise ValueError('artifact limit must be positive')
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
        background = args.background or os.environ.get('SLRR_PROBE_BACKGROUND') == '1'
        observer = None
        if background:
            candidate = args.focus_observer or probe.parent / 'parity_focus_observer'
            if args.focus_observer is None and not candidate.is_file():
                candidate = TOOLS_DIR.parent / 'build/parity_focus_observer'
            observer = candidate.resolve(strict=True)
            if not observer.is_file() or not os.access(observer, os.X_OK):
                raise ValueError('background execution requires an executable parity_focus_observer')
        if bool(args.artifact_budget_root) != (args.artifact_parent_max_mib is not None):
            raise ValueError('prepaid root and exact parent maximum are required together')
        if args.artifact_budget_root:
            # Preserve the raw lexical path for canonical/no-symlink validation.
            # Admission creates this unique direct child; never pre-create it.
            prepaid = prepaid_module.LeaseBudgetAdapter.admit(pool_module.ArtifactLimit,
                args.artifact_budget_root, Path(args.output).absolute(),
                STORAGE_STATE, args.artifact_parent_max_mib * 1024**2,
                child_max=args.max_artifact_mib * 1024**2)
            artifact_budget = prepaid
        else:
            out.mkdir(parents=True, exist_ok=False)
            artifact_budget = pool_module.ArtifactBudget([out], args.max_artifact_mib * 1024 * 1024,
                                                         link_policy='count', retained=True)
        manifest_path = out / 'game-data-manifest.json'
        try:
            manifest = data_module.write_manifest(game, manifest_path)
            data_identity = {'status': 'complete' if manifest['complete'] else 'incomplete',
                             'path': str(manifest_path), 'sha256': digest(manifest_path)}
        except (OSError, ValueError) as error:
            data_identity = {'status': 'unavailable', 'reason': str(error)}
        source_before = source_manifest(game, out / 'source-game-before.json')
        if not source_before['complete']:
            raise ValueError('complete source-game preservation identity unavailable before launch')
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
        cmd = [str(probe), '--init', '--renderer', '--loop',
               '--background' if background else '--visible',
               '--script', str(script), '--data-root', str(data),
               '--reference-root', str(reference), str(game)]
        if args.skip_intro:
            cmd.append('--no-intro')
        metadata = {'probe': str(probe), 'probe_sha256': digest(probe), 'game': str(game),
                    'admission': lease.description,
                    'artifact_budget': {'enabled': bool(artifact_budget), 'max_bytes': args.max_artifact_mib * 1024 * 1024,
                                        'roots': [str(out)], 'link_policy': 'count',
                                        'scope': 'sampled logical background output tree; links not followed'},
                    'game_data': data_identity,
                    'source_game': {'path': 'source-game-before.json',
                                    'sha256': digest(out / 'source-game-before.json'),
                                    'scope': 'complete source tree including runtime/executable/save files'},
                    'reference_root': str(reference),
                    'mode': mode, 'script_source': script_label,
                    'presentation': 'background' if background else 'visible',
                    'startup_movies': 'explicit-bypass' if args.skip_intro else 'normal',
                    'script_path': str(script), 'script_sha256': digest(script),
                    'run_dir': str(run_dir),
                    'command': cmd, 'timeout_seconds': timeout,
                    'trace': {'SLRR_TRACE_RANDOM_SEED': '1', 'SLRR_TRACE_CAR_CREATION': '1'},
                    'environment': {key: value for key, value in os.environ.items()
                                    if key.startswith(('SLRR_', 'SDL_'))},
                    'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
        if prepaid:
            metadata['artifact_budget']['prepaid'] = prepaid.budget.ledger
            metadata['artifact_budget']['scope'] = 'entire prepaid parent plus separately bounded new child'
        if observer:
            metadata['focus_observer'] = {'path': str(observer), 'sha256': digest(observer)}
            metadata['execution_command'] = [str(observer), str(out / 'focus.jsonl'),
                                             str(probe), str(timeout), *cmd[1:]]
        (out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
        environment = os.environ.copy()
        environment.update(metadata['trace'])
        # Run-local recorder: @run shots and run telemetry land inside the record.
        environment['SLRR_RUN_DIR'] = str(run_dir)
        start = time.monotonic()
        preparation_seconds = start - preparation_started
        # Keep cwd-relative writes disposable. The probe receives an explicit
        # Reference Folder for both save guards and stock cursor resources.
        stdout_path = out / ('observer.stdout' if observer else 'stdout.log')
        stderr_path = out / ('observer.stderr' if observer else 'stderr.log')
        with stdout_path.open('w') as stdout, stderr_path.open('w') as stderr:
            try:
                if observer:
                    result = lease.run_observer(metadata['execution_command'], env=environment, cwd=data,
                                                stdout=stdout, stderr=stderr, timeout=timeout + 10,
                                                artifact_budget=artifact_budget)
                else:
                    result = lease.run(cmd, env=environment, cwd=data, output=stdout,
                                       timeout=timeout, artifact_budget=artifact_budget)
                status = {'exit_code': result.returncode, 'timed_out': False}
            except pool_module.ResourcePressure as error:
                status = {'exit_code': None, 'timed_out': False, 'resource_pressure': str(error)}
            except pool_module.ArtifactLimit as error:
                status = {'exit_code': None, 'timed_out': False, 'artifact_error': str(error)}
            except subprocess.TimeoutExpired:
                status = {'exit_code': None, 'timed_out': True}
            except OSError as error:
                status = {'exit_code': None, 'timed_out': False, 'error': str(error)}
        if observer:
            status['execution_pressure'] = lease.execution_pressure
            status['execution_artifacts'] = lease.execution_artifacts
            for stream in ('stdout', 'stderr'):
                source = out / f'focus.jsonl.{stream}'
                if source.is_file():
                    source.rename(out / f'{stream}.log')
                else:
                    (out / f'{stream}.log').write_text('')
            try:
                events = focus_module.read_events(out / 'focus.jsonl')
                summary = focus_module.classify(events)
                status['focus'] = summary
                status['observer_exit_code'] = status['exit_code']
                owners = [e['root'] for e in events if e.get('kind') == 'child_started']
                if len(owners) == 1:
                    status['execution_owner'] = owners[0]
                finished = [e for e in events if e.get('kind') == 'observer_finished']
                if finished:
                    status['exit_code'] = finished[0].get('child_exit')
                    status['timed_out'] = finished[0].get('timed_out') is True
                else:
                    status['timed_out'] = status['timed_out'] or status['observer_exit_code'] == 124
                (out / 'focus-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
            except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
                status['focus'] = {'status': 'inconclusive', 'reason': str(error)}
        try:
            status['probe_unchanged'] = digest(probe) == metadata['probe_sha256']
        except OSError:
            status['probe_unchanged'] = False
        status['elapsed_seconds'] = time.monotonic() - start
        if background:
            try:
                status['audio'] = audio_module.native_capture(run_dir)
                (out / 'audio-summary.json').write_text(json.dumps(status['audio'], indent=2) + '\n')
            except (OSError, ValueError) as error:
                status['audio'] = {'status': 'inconclusive', 'reason': str(error)}
        status['source_save_unchanged'] = (snapshot_module.manifest(save) == initial_save['files'])
        try:
            source_after = source_manifest(game, out / 'source-game-after.json')
            status['source_game_unchanged'] = (source_after['complete'] and
                                               source_before['files'] == source_after['files'])
            status['source_game_after'] = {'path': 'source-game-after.json',
                                          'sha256': digest(out / 'source-game-after.json')}
        except (OSError, ValueError) as error:
            status['source_game_unchanged'] = False
            status['source_game_error'] = str(error)
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
        if prepaid:
            status['artifact_finalization'] = 'pending'
        (out / 'result.json').write_text(json.dumps(status, indent=2) + '\n')
        if not prepaid:
            print(json.dumps(status))
        if (status.get('resource_pressure') or status.get('artifact_error') or status['timed_out'] or status['exit_code'] != 0 or not status['source_save_unchanged'] or
                not status['probe_unchanged'] or not status['source_game_unchanged'] or
                (observer and (status.get('observer_exit_code') != 0 or
                               status['focus']['status'] != 'no_owned_activation_observed' or
                               status.get('audio', {}).get('route') != 'sdl-disk'))):
            return 1
        return 0
    except pool_module.ArtifactLimit as error:
        diagnostics = getattr(error, 'prepaid_diagnostics', {})
        # A constructor may have created its own child before refusing. Retain
        # diagnostics only in that exact newly-created inode, never an old path.
        child_identity = diagnostics.get('child_identity')
        if out and child_identity and out.is_dir() and not out.is_symlink() and tuple(child_identity) == (
                out.stat().st_dev, out.stat().st_ino):
            (out/'prepaid-admission-error.json').write_text(json.dumps(diagnostics, indent=2)+'\n')
            (out/'result.json').write_text(json.dumps(dict(exit_code=None, artifact_error=str(error),
                artifact_finalization='failed'), indent=2)+'\n')
        print(f'run_native: {error}', file=sys.stderr)
        return 1
    except (OSError, ValueError) as error:
        parser.exit(2, f'run_native: {error}\n')
    finally:
        if prepaid is not None:
            # All normal output writers have stopped before this point. First
            # verify them, then publish the audit/result, then include those very
            # writes in a second final scan before permitting CLI success.
            try:
                prepaid.check('final')
                if status is not None:
                    status['artifact_finalization'] = 'verified'
                    (out/'result.json').write_text(json.dumps(status, indent=2)+'\n')
                (out/'prepaid-audit.json').write_text(json.dumps(prepaid.budget.report(), indent=2)+'\n')
                prepaid.check('final')
                if status is not None:
                    print(json.dumps(status))
            except (pool_module.ArtifactLimit, OSError, ValueError) as error:
                failed = status if status is not None else dict(exit_code=None)
                failed.update(artifact_error=str(error), artifact_finalization='failed')
                # Do not follow a replaced child when retaining failure evidence.
                bound = prepaid.budget.child_identity
                if out.is_dir() and not out.is_symlink() and (out.stat().st_dev, out.stat().st_ino) == bound:
                    try:
                        (out/'result.json').write_text(json.dumps(failed, indent=2)+'\n')
                        (out/'prepaid-failure.json').write_text(json.dumps(prepaid.budget.report(), indent=2)+'\n')
                    except OSError as writing_error:
                        print(f'run_native: failure evidence unavailable: {writing_error}', file=sys.stderr)
                print(f'run_native: prepaid finalization: {error}', file=sys.stderr)
                raise pool_module.ArtifactLimit('prepaid finalization: '+str(error)) from error
            finally:
                prepaid.close()


def main():
    try:
        # Initial estimates, bounded by the qualified two-native-run lane. These
        # are admission reservations, not hard process-tree CPU/RSS limits.
        with pool_module.ResourcePool(pressure=pool_module.Pressure()).acquire({'cpu': 2, 'host_memory': 4, 'renderer': 1}) as lease:
            return run(lease)
    except (OSError, ValueError, TimeoutError, pool_module.ResourcePressure, pool_module.ArtifactLimit) as error:
        print(f'run_native: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
