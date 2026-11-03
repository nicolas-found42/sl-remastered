#!/usr/bin/env python3
"""Run the owned PE32 input observer controls, never Stock or a native Wine GUI.

Requires an already-qualified, stopped, persisted Wine prefix fixture. Clones it
to unique private state, freezes producer sources, compiles the actual observer
and an FX-restoration fault variant, and runs bounded console helpers in an
isolated container. No success here qualifies actual COM/Wine device lifetimes,
WndProc consumption, scheduling or whole-game replay.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[3]
IMAGE = 'slrr-private-stock-hangover1116-xtest-arm64-20261101'
spec = importlib.util.spec_from_file_location('pool', ROOT / 'tools/parity/resource_pool.py')
pool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pool)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def manifest(directory):
    result = {}
    for path in sorted(directory.rglob('*')):
        key = str(path.relative_to(directory))
        if path.is_symlink():
            result[key] = {'link': os.readlink(path)}
        elif path.is_file():
            result[key] = {'sha256': sha(path), 'size': path.stat().st_size}
        elif path.is_dir():
            result[key] = {'directory': True}
        else:
            raise ValueError(f'unsupported fixture entry: {path}')
    return result


def run(out, seed, image_name):
    out, seed = out.resolve(), seed.resolve(strict=True)
    reference = (ROOT / 'ref').resolve()
    for protected in (reference, seed):
        if out == protected or out in protected.parents or protected in out.parents:
            raise ValueError('output must not overlap ref/ or prefix seed')
    image = json.loads(subprocess.check_output(['docker', 'image', 'inspect', image_name], timeout=15))[0]
    seed_result = json.loads((seed.parent / 'result.json').read_text())
    if image['Architecture'] != 'arm64' or seed_result['image_id'] != image['Id'] or seed_result['cleanup_exit'] != 0:
        raise ValueError('prefix fixture lacks matching qualified image/cleanup')
    if not all((seed / name).is_file() for name in ('system.reg', 'user.reg', 'userdef.reg')):
        raise ValueError('prefix fixture lacks persisted registry hives')
    running = subprocess.check_output(['docker', 'ps', '-q'], text=True, timeout=10).split()
    if running:
        configs = json.loads(subprocess.check_output(['docker', 'inspect', *running], timeout=15))
        for config in configs:
            for mount in config.get('Mounts', []):
                source = Path(mount['Source']).resolve()
                if source == seed or source in seed.parents or seed in source.parents:
                    raise ValueError('prefix seed is exposed to a live container')
    out.mkdir(parents=True, exist_ok=False)
    artifact_budget = pool.ArtifactBudget([out], 4 * 1024**3, link_policy='count', retained=True)
    result = {'scope': 'owned console only; no Stock/device/window/audio; no replay qualification',
              'image_id': image['Id'], 'image_name': image_name, 'passed': False,
              'fresh_Wine_initialization': False, 'warm_prefix_seed': str(seed), 'deadline_seconds': 120}
    name = 'slrr-owned-input-' + uuid.uuid4().hex
    before = None
    command = None
    started = time.monotonic()
    with pool.ResourcePool(pressure=pool.Pressure()).acquire({'cpu': 1, 'host_memory': 2, 'vm_memory': 2}, timeout=30) as lease:
        result['admission'] = lease.description
        try:
            before = manifest(seed)
            save(out / 'seed-manifest.json', before)
            subprocess.run(['cp', '-c', '-R', str(seed), str(out / 'prefix')], check=True, timeout=60)
            private = all((seed / key).stat().st_ino != (out / 'prefix' / key).stat().st_ino
                          for key, value in before.items() if 'sha256' in value)
            if not private or manifest(out / 'prefix') != before or manifest(seed) != before:
                raise ValueError('prefix clone is incomplete/shared or seed changed')
            result['private_prefix_inodes'] = private
            result['seed_result_sha256'] = sha(seed.parent / 'result.json')
            frozen = out / 'source'
            frozen.mkdir()
            names = ('control_tape.c', 'control_tape.h', 'input_observer.h', 'input_observer.inc', 'input_bind.c', 'rng_observer.inc', 'control_writer_test.inc')
            paths = {name: ROOT / 'tools/parity/stock' / name for name in names}
            paths['wine_input_observer_test.c'] = ROOT / 'tests/wine_input_observer_test.c'
            paths['wine_input_binding_test.c'] = ROOT / 'tests/wine_input_binding_test.c'
            paths['wine_input_lifecycle_test.c'] = ROOT / 'tests/wine_input_lifecycle_test.c'
            paths['qualify_input.py'] = Path(__file__).resolve()
            for name_, path in paths.items():
                shutil.copy2(path, frozen / name_)
            result['producer_sha256'] = {str(path.relative_to(ROOT)): sha(path) for path in paths.values()}
            save(out / 'source-manifest.json', manifest(frozen))
            fault = out / 'fault-source'
            shutil.copytree(frozen, fault)
            for name_ in ('control_tape.c', 'input_observer.inc'):
                path = fault / name_
                text = path.read_text()
                if 'fxrstor (%esp)' not in text:
                    raise ValueError('missing expected FX restoration for negative control')
                path.write_text(text.replace('fxrstor (%esp)', 'nop'))
            save(out / 'fault-source-manifest.json', manifest(fault))
            retirement_fault = out / 'retirement-fault-source'
            shutil.copytree(frozen, retirement_fault)
            retirement_path = retirement_fault / 'input_observer.inc'
            retirement_text = retirement_path.read_text()
            if retirement_text.count('d->live=0;++r->retired_interfaces;') != 1:
                raise ValueError('missing zero-Release retirement control')
            retirement_path.write_text(retirement_text.replace('d->live=0;++r->retired_interfaces;', '++r->retired_interfaces;'))
            save(out / 'retirement-fault-source-manifest.json', manifest(retirement_fault))
            compiler = shutil.which('i686-w64-mingw32-gcc')
            if not compiler:
                raise ValueError('MinGW i686 gcc is required')
            with (out / 'compile.log').open('wb') as log:
                subprocess.run([compiler, '--version'], check=True, stdout=log, stderr=subprocess.STDOUT, timeout=10)
                for variant, source in (('input-proof.exe', frozen), ('input-no-fx.exe', fault)):
                    argv = [compiler, '-O2', '-Wall', '-Wextra', '-msse2', '-DCONTROL_TAPE_INPUT', '-DCONTROL_TAPE_TEST',
                            '-I', str(source), str(source / 'wine_input_observer_test.c'), str(source / 'control_tape.c'),
                            '-ladvapi32', '-o', str(out / variant)]
                    save(out / (variant + '.compile.json'), argv)
                    subprocess.run(argv, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=30)
                for variant, source in (('lifecycle-proof.exe', frozen), ('lifecycle-no-fx.exe', fault),
                                        ('lifecycle-no-retirement.exe', retirement_fault)):
                    argv = [compiler, '-O2', '-Wall', '-Wextra', '-msse2', '-DCONTROL_TAPE_INPUT', '-DCONTROL_TAPE_TEST',
                            '-I', str(source), str(source / 'wine_input_lifecycle_test.c'), str(source / 'control_tape.c'),
                            '-ladvapi32', '-o', str(out / variant)]
                    save(out / (variant + '.compile.json'), argv)
                    subprocess.run(argv, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=30)
                (out / 'owner.def').write_text('EXPORTS\nparity_input_observer\ncontrol_tape_test_hook\ncontrol_tape_stop\n')
                compilations = {
                    'input-owner.dll': ['-shared', '-DCONTROL_TAPE_INPUT', '-DCONTROL_TAPE_TEST',
                                        str(frozen / 'control_tape.c'), str(out / 'owner.def'), '-ladvapi32'],
                    'input-client.dll': ['-shared', str(frozen / 'input_bind.c')],
                    'binding-proof.exe': [str(frozen / 'wine_input_binding_test.c')]}
                for target, inputs in compilations.items():
                    argv = [compiler, '-O2', '-Wall', '-Wextra', '-msse2', '-I', str(frozen), *inputs, '-o', str(out / target)]
                    save(out / (target + '.compile.json'), argv)
                    subprocess.run(argv, check=True, stdout=log, stderr=subprocess.STDOUT, timeout=30)
            cases = ('accept', 'refuse', 'negative', 'owner-first', 'client-first', 'lifecycle', 'lifecycle-negative',
                     'lifecycle-refuse', 'lifecycle-no-retirement')
            for case in cases:
                (out / case).mkdir()
            script = '''#!/bin/bash
set -u
unset DISPLAY WAYLAND_DISPLAY PULSE_SERVER XAUTHORITY
export WINEPREFIX=/work/prefix WINEARCH=win64 HODLL=wowbox64.dll WINEDEBUG=-all
export WINEDLLOVERRIDES='winemenubuilder.exe=d'
wine --version > /work/runtime-version.txt 2>&1
sha256sum /usr/lib/wine/aarch64-windows/wowbox64.dll > /work/runtime-dll-sha256.txt
run_case() {
  cd /work/"$1"
  wine /work/"$2" "$3" > helper.log 2>stderr.log
  printf '%s\\n' "$?" > exit.txt
}
run_case accept input-proof.exe accept
run_case refuse input-proof.exe refuse
run_case negative input-no-fx.exe accept
run_case owner-first binding-proof.exe owner-first
run_case client-first binding-proof.exe client-first
run_case lifecycle lifecycle-proof.exe accept
run_case lifecycle-negative lifecycle-no-fx.exe accept
run_case lifecycle-refuse lifecycle-proof.exe refuse
run_case lifecycle-no-retirement lifecycle-no-retirement.exe accept
wineserver -k
wineserver -w
'''
            (out / 'container.sh').write_text(script)
            command = ['docker', 'run', '--rm', '--name', name, '--network', 'none', '--platform', 'linux/arm64',
                       '--cpus', '1', '--memory', '1g', '--memory-swap', '1g', '--pids-limit', '128',
                       '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges', '--read-only',
                       '--tmpfs', '/tmp:rw,nosuid,nodev,size=128m', '--mount', f'type=bind,src={out},dst=/work',
                       '--entrypoint', '/usr/bin/timeout', image['Id'], '--kill-after=5', '90',
                       '/bin/bash', '/work/container.sh']
            result['argv'] = command
            with (out / 'container.log').open('wb') as log:
                done = lease.run(command, output=log, timeout=120,
                                 max_output_bytes=4*1024**2, artifact_budget=artifact_budget)
                result['container_exit'] = done.returncode
            result['helper_exits'] = {case: (out / case / 'exit.txt').read_text().strip()
                                      for case in cases}
        except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
            result['error'] = str(error)
        finally:
            if command is not None:
                try:
                    cleanup = subprocess.run(['docker', 'rm', '-f', name], capture_output=True, timeout=15)
                    result['cleanup_exit'] = cleanup.returncode
                    result['container_removed'] = not subprocess.check_output(
                        ['docker', 'ps', '-aq', '--filter', 'name=^/' + name + '$'], timeout=10).strip()
                except (OSError, subprocess.SubprocessError) as error:
                    result['cleanup_error'] = str(error)
            if before is not None:
                result['seed_unchanged'] = manifest(seed) == before
            result['elapsed_seconds'] = time.monotonic() - started
            result['producer_unchanged'] = all(sha(ROOT / key) == value for key, value in result.get('producer_sha256', {}).items())
            # ABI/output/phase checks are deliberately separate in check_input.py.
            result['execution_finished'] = (result.get('container_exit') == 0 and result.get('container_removed') and
                                            result.get('seed_unchanged') and result.get('producer_unchanged') and
                                            result.get('helper_exits') == {'accept': '0', 'refuse': '0', 'negative': '1',
                                                                           'owner-first': '0', 'client-first': '0',
                                                                           'lifecycle': '0', 'lifecycle-negative': '1',
                                                                           'lifecycle-refuse': '0', 'lifecycle-no-retirement': '33'})
            save(out / 'result.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--prefix-seed', required=True, type=Path)
    parser.add_argument('--image', default=IMAGE)
    args = parser.parse_args()
    try:
        result = run(args.out, args.prefix_seed, args.image)
        print(json.dumps(result))
        return 0 if result.get('execution_finished') else 1
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, pool.ResourcePressure, pool.ArtifactLimit) as error:
        parser.exit(2, f'qualify_input: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
