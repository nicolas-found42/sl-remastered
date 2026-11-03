#!/usr/bin/env python3
"""Bounded owned Wine DirectInput qualification in private Xvfb; never Stock.

The execution verdict is separate from check_real_input.py's ABI/identity audit.
No host display/input/audio socket/device or native Wine process is used.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import time
import uuid

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location('owned_input_runner', HERE / 'qualify_input.py')
owned = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owned)


def run(output, seed, image_name, binaries_from=None, safe_flags=None, profile_control=False,guard_control=False,diagnostics=False):
    output, seed = output.resolve(), seed.resolve(strict=True)
    for protected in (seed, (ROOT / 'ref').resolve()):
        if output == protected or output in protected.parents or protected in output.parents:
            raise ValueError('output overlaps protected fixture/reference')
    image = json.loads(subprocess.check_output(['docker', 'image', 'inspect', image_name], timeout=15))[0]
    qualified = json.loads((seed.parent / 'result.json').read_text())
    stopped = (qualified.get('cleanup_exit') == 0 or
               (qualified.get('execution_finished') is True and qualified.get('container_removed') is True and
                qualified.get('helper_exits', {}).get('actual') == '0'))
    if (image['Architecture'] != 'arm64' or qualified['image_id'] != image['Id'] or not stopped or
            not all((seed / n).is_file() for n in ('system.reg', 'user.reg', 'userdef.reg'))):
        raise ValueError('unqualified image or unpersisted prefix seed')
    active = subprocess.check_output(['docker', 'ps', '-q'], timeout=10).decode().split()
    if active:
        for container in json.loads(subprocess.check_output(['docker', 'inspect', *active], timeout=15)):
            for mount in container['Mounts']:
                path = Path(mount['Source']).resolve()
                if path == seed or path in seed.parents or seed in path.parents:
                    raise ValueError('seed exposed to live container')
    output.mkdir(parents=True, exist_ok=False)
    result = {'scope': 'actual Wine DirectInput in an owned private window; never Stock or replay',
              'image_id': image['Id'], 'prefix_seed': str(seed), 'execution_finished': False,
              'transparency_qualified': False, 'seed_result_sha256': owned.sha(seed.parent / 'result.json'),
              'safe_flags_requested': safe_flags, 'binaries_from': str(binaries_from) if binaries_from else None}
    result['runtime_profile_countercontrol']=profile_control
    result['cold_guard_countercontrol']=guard_control
    result['extra_diagnostics']=diagnostics
    name = 'slrr-real-input-' + uuid.uuid4().hex
    before = None
    started = time.monotonic()
    with owned.pool.ResourcePool(pressure=owned.pool.Pressure()).acquire(
            {'cpu': 2, 'host_memory': 2, 'vm_memory': 2, 'renderer': 1, 'stock': 1}, timeout=60) as lease:
        result['admission'] = lease.description
        try:
            before = owned.manifest(seed)
            owned.save(output / 'seed-before.json', before)
            subprocess.run(['cp', '-c', '-p', '-R', str(seed), str(output / 'prefix')], check=True, timeout=60)
            result['private_prefix_inodes'] = all(
                (seed / k).stat().st_ino != (output / 'prefix' / k).stat().st_ino
                for k, v in before.items() if 'sha256' in v)
            if not result['private_prefix_inodes'] or owned.manifest(output / 'prefix') != before:
                raise ValueError('prefix clone differs or shares inodes')
            sources = {n: HERE / n for n in ('wine_real_input_test.c', 'wine_real_input_com_test.c',
                        'wine_input_alias_guard_test.c','control_tape.c', 'control_tape.h',
                        'input_observer.h', 'input_observer.inc', 'rng_observer.inc',
                        'qualify_real_input.py', 'qualify_input.py', 'check_real_input.py')}
            sources['wine_input_observer_test.c'] = ROOT / 'tests/wine_input_observer_test.c'
            sources['wine_input_lifecycle_test.c'] = ROOT / 'tests/wine_input_lifecycle_test.c'
            source = output / 'source';source.mkdir()
            for n, p in sources.items(): shutil.copy2(p, source / n)
            result['producer_sha256'] = {str(p.relative_to(ROOT)): owned.sha(p) for p in sources.values()}
            owned.save(output / 'source-manifest.json', owned.manifest(source))
            fault = output / 'fault-source';shutil.copytree(source, fault)
            for n in ('control_tape.c', 'input_observer.inc'):
                p = fault / n;text = p.read_text()
                if 'fxrstor (%esp)' not in text: raise ValueError('FX fault source anchor missing')
                p.write_text(text.replace('fxrstor (%esp)', 'nop'))
            alias_fault = output / 'alias-fault-source';shutil.copytree(source, alias_fault)
            path = alias_fault / 'input_observer.inc';text = path.read_text()
            if text.count('input_alias_layout[index]=1;result=1;') != 1: raise ValueError('alias fault anchor missing')
            path.write_text(text.replace('input_alias_layout[index]=1;result=1;', 'result=1;'))
            retirement_fault = output / 'retirement-fault-source';shutil.copytree(source, retirement_fault)
            path = retirement_fault / 'input_observer.inc';text = path.read_text()
            if text.count('d->live=0;++r->retired_interfaces;') != 1: raise ValueError('retirement fault anchor missing')
            path.write_text(text.replace('d->live=0;++r->retired_interfaces;', '++r->retired_interfaces;'))
            cold_fault=output/'cold-vtable-fault-source';shutil.copytree(source,cold_fault)
            path=cold_fault/'input_observer.inc';text=path.read_text()
            needle='''d->id==id && d->incarnation==incarnation &&
         input_vtables[d->vtable_index]==*(void***)alias &&
         input_publish_vtable(*(void***)alias,1)==d->vtable_index'''
            # The pre-fix red lane deliberately has no new guard to omit.
            if needle in text:path.write_text(text.replace(needle,'d->id==id && d->incarnation==incarnation'))
            diagnostic = output / 'NO-QUERY-GUARDS-DIAGNOSTIC';shutil.copytree(source, diagnostic)
            path = diagnostic / 'input_observer.inc';text = path.read_text()
            begin = text.index('static int input_buffer(');end = text.index('static void input_refusal', begin)
            text = text[:begin] + '''/* Deliberately unsafe diagnostic: never qualified or promoted. */
static int input_buffer(DWORD address,DWORD size) {return address && size && address<=0xffffffffu-size;}
static int input_readable(DWORD address,DWORD size) {return input_buffer(address,size);}
''' + text[end:]
            path.write_text(text)
            flags_entry = output / 'flags-entry-source';shutil.copytree(source, flags_entry)
            path = flags_entry / 'input_observer.inc';text = path.read_text()
            needle = r'popfl\n\tjmp *_input_targets'
            if text.count(needle) != 1: raise ValueError('flags-entry diagnostic anchor missing')
            text = text.replace(needle, r'popfl\n\tpushl %eax\n\tlahf\n\tsahf\n\tpopl %eax\n\tjmp *_input_targets')
            path.write_text(text)
            flags_both = output / 'flags-both-source';shutil.copytree(flags_entry, flags_both)
            path = flags_both / 'control_tape.c';text = path.read_text()
            needle = r'popfl\n\tpushl -40(%esp)'
            if text.count(needle) != 1: raise ValueError('flags-return diagnostic anchor missing')
            path.write_text(text.replace(needle, r'popfl\n\tpushl %eax\n\tlahf\n\tsahf\n\tpopl %eax\n\tpushl -40(%esp)'))
            artifacts = output / 'artifacts';artifacts.mkdir()
            compiler = shutil.which('i686-w64-mingw32-gcc')
            if not compiler: raise ValueError('MinGW compiler missing')
            with (artifacts / 'compile.log').open('wb') as log:
                targets = [('real-input.exe', source, 'wine_real_input_test.c'),
                           ('real-input-no-fx.exe', fault, 'wine_real_input_test.c'),
                           ('real-com.exe', source, 'wine_real_input_com_test.c'),
                           ('real-com-no-fx.exe', fault, 'wine_real_input_com_test.c'),
                           ('real-com-no-alias.exe', alias_fault, 'wine_real_input_com_test.c'),
                           ('real-com-no-retirement.exe', retirement_fault, 'wine_real_input_com_test.c'),
                           ('real-com-no-query-DIAGNOSTIC.exe', diagnostic, 'wine_real_input_com_test.c'),
                           ('real-com-flags-entry.exe', flags_entry, 'wine_real_input_com_test.c'),
                           ('real-com-flags-both.exe', flags_both, 'wine_real_input_com_test.c'),
                           ('cold-guard.exe',source,'wine_input_alias_guard_test.c'),
                           ('cold-guard-no-vtable.exe',cold_fault,'wine_input_alias_guard_test.c'),
                           ('v1-state.exe',source,'wine_input_observer_test.c'),
                           ('v1-state-no-fx.exe',fault,'wine_input_observer_test.c'),
                           ('v1-lifecycle.exe',source,'wine_input_lifecycle_test.c'),
                           ('v1-lifecycle-no-fx.exe',fault,'wine_input_lifecycle_test.c')]
                if profile_control:targets=[row for row in targets if row[0] in ('real-input.exe','real-com.exe')]
                if guard_control:targets=[row for row in targets if row[0]=='cold-guard.exe']
                if not diagnostics:targets=[row for row in targets if row[0] not in
                    ('real-com-no-query-DIAGNOSTIC.exe','real-com-flags-entry.exe','real-com-flags-both.exe')]
                for target, frozen, helper in targets:
                    argv = [compiler, '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mwindows',
                            '-DCONTROL_TAPE_INPUT', '-DCONTROL_TAPE_TEST', '-I', str(frozen),
                            str(frozen / helper), str(frozen / 'control_tape.c'),
                            '-ldinput8', '-ldxguid', '-ladvapi32', '-o', str(output / target)]
                    if target.startswith('v1-'):argv.remove('-mwindows')
                    owned.save(artifacts / (target + '.compile.json'), argv)
                    if binaries_from:
                        previous = Path(binaries_from).resolve(strict=True)
                        for name_ in (helper, 'wine_input_observer_test.c', 'control_tape.c', 'control_tape.h',
                                      'input_observer.h', 'input_observer.inc'):
                            old_directory = {'source': 'source', 'fault-source': 'fault-source',
                                             'alias-fault-source': 'alias-fault-source',
                                             'retirement-fault-source': 'retirement-fault-source',
                                             'NO-QUERY-GUARDS-DIAGNOSTIC': 'NO-QUERY-GUARDS-DIAGNOSTIC',
                                             'flags-entry-source': 'flags-entry-source',
                                             'flags-both-source': 'flags-both-source',
                                             'cold-vtable-fault-source':'cold-vtable-fault-source'}[frozen.name]
                            if owned.sha(previous / old_directory / name_) != owned.sha(frozen / name_):
                                raise ValueError('reused binary source differs: ' + name_)
                        shutil.copy2(previous / target, output / target)
                        owned.save(artifacts / (target + '.reuse.json'), {'path': str(previous / target),
                            'sha256': owned.sha(previous / target), 'copied_sha256': owned.sha(output / target)})
                    else:
                        done = lease.run(argv, timeout=30, output=log)
                        if done.returncode: raise ValueError('helper compilation failed')
            result['binary_sha256'] = {target: owned.sha(output / target) for target, _, _ in targets}
            cases = ('actual', 'fx-fault', 'com', 'com-fx-fault', 'com-alias-fault', 'com-refuse', 'com-retirement-fault',
                     'com-no-query-DIAGNOSTIC', 'com-flags-entry', 'com-flags-both','cold-guard','cold-guard-no-vtable',
                     'v1-state','v1-state-no-fx','v1-lifecycle','v1-lifecycle-no-fx')
            if profile_control:cases=('actual','com')
            if guard_control:cases=('cold-guard',)
            if not diagnostics:cases=tuple(c for c in cases if c not in
                ('com-no-query-DIAGNOSTIC','com-flags-entry','com-flags-both'))
            for case in cases: (artifacts / case).mkdir()
            script = '''#!/bin/bash
set -euo pipefail
unset WAYLAND_DISPLAY PULSE_SERVER XAUTHORITY
export DISPLAY=:99 WINEPREFIX=/work/prefix WINEARCH=win64 HODLL=wowbox64.dll WINEDEBUG=-all
export WINEDLLOVERRIDES='winemenubuilder.exe=d;mscoree,mshtml='
Xvfb :99 -screen 0 800x600x24 -nolisten tcp >/work/artifacts/xvfb.log 2>&1 &
timeout 10 sh -c 'until xdpyinfo >/dev/null 2>&1; do sleep .1; done'
wine --version >/work/artifacts/wine-version.txt
sha256sum /usr/lib/wine/aarch64-windows/wowbox64.dll >/work/artifacts/runtime-dll-sha256.txt
for spec in 'actual|real-input.exe|' 'fx-fault|real-input-no-fx.exe|' 'com|real-com.exe|' 'com-fx-fault|real-com-no-fx.exe|' 'com-alias-fault|real-com-no-alias.exe|' 'com-refuse|real-com.exe|refuse' 'com-retirement-fault|real-com-no-retirement.exe|' 'com-no-query-DIAGNOSTIC|real-com-no-query-DIAGNOSTIC.exe|' 'com-flags-entry|real-com-flags-entry.exe|' 'com-flags-both|real-com-flags-both.exe|' 'cold-guard|cold-guard.exe|' 'cold-guard-no-vtable|cold-guard-no-vtable.exe|' 'v1-state|v1-state.exe|accept' 'v1-state-no-fx|v1-state-no-fx.exe|accept' 'v1-lifecycle|v1-lifecycle.exe|accept' 'v1-lifecycle-no-fx|v1-lifecycle-no-fx.exe|accept'; do
  IFS='|' read -r case exe argument <<< "$spec"
  if [ "${SLRR_RUNTIME_PROFILE_CONTROL:-0}" = 1 ] && [ "$case" != actual ] && [ "$case" != com ]; then continue; fi
  if [ "${SLRR_COLD_GUARD_CONTROL:-0}" = 1 ] && [ "$case" != cold-guard ]; then continue; fi
  case "$case" in
    com-no-query-DIAGNOSTIC|com-flags-entry|com-flags-both) if [ "${SLRR_EXTRA_DIAGNOSTICS:-0}" != 1 ]; then continue; fi;;
  esac
  cd /work/artifacts/"$case"
  set +e
  timeout 25 wine /work/"$exe" "$argument" >helper.log 2>stderr.log
  printf '%s\\n' "$?" >launcher-exit.txt
  set -e
  wineserver -k
  timeout 5 wineserver -w
done
sha256sum /work/prefix/drive_c/windows/syswow64/dinput8.dll >/work/artifacts/dinput8-file-sha256.txt
'''
            (output / 'container.sh').write_text(script)
            create = ['docker', 'create', '--name', name, '--network', 'none', '--platform', 'linux/arm64',
                       '--cpus', '1', '--memory', '1g', '--memory-swap', '1g', '--pids-limit', '128',
                       '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges', '--read-only',
                       '--tmpfs', '/tmp:rw,nosuid,nodev,size=128m', '--mount', f'type=bind,src={output},dst=/work',
                       '--entrypoint', '/usr/bin/timeout', image['Id'], '--kill-after=5', '200',
                       '/bin/bash', '/work/container.sh']
            if safe_flags is not None:
                at=create.index('--entrypoint')
                create[at:at]=['-e', f'BOX64_DYNAREC_SAFEFLAGS={safe_flags}', '-e', 'BOX64_LOG=1']
            if profile_control:
                at=create.index('--entrypoint');create[at:at]=['-e','SLRR_RUNTIME_PROFILE_CONTROL=1']
            if guard_control:
                at=create.index('--entrypoint');create[at:at]=['-e','SLRR_COLD_GUARD_CONTROL=1']
            if diagnostics:
                at=create.index('--entrypoint');create[at:at]=['-e','SLRR_EXTRA_DIAGNOSTICS=1']
            subprocess.run(create, check=True, capture_output=True, timeout=20)
            inspected = json.loads(subprocess.check_output(['docker', 'inspect', name], timeout=10))[0]
            owned.save(output / 'container-inspect.json', inspected)
            host = inspected['HostConfig']
            mounts = inspected['Mounts']
            result['actual_isolation'] = (inspected['Image'] == image['Id'] and host['NetworkMode'] == 'none' and
                host['ReadonlyRootfs'] is True and host['Privileged'] is False and
                not host.get('Devices') and not host.get('DeviceRequests') and not host.get('CapAdd') and
                host['CapDrop'] == ['ALL'] and host['SecurityOpt'] == ['no-new-privileges'] and
                host['NanoCpus'] == 1_000_000_000 and host['Memory'] == 1024**3 and
                host['MemorySwap'] == 1024**3 and host['PidsLimit'] == 128 and
                len(mounts) == 1 and mounts[0]['Source'] == str(output) and mounts[0]['Destination'] == '/work' and
                mounts[0]['RW'] is True)
            if not result['actual_isolation']: raise ValueError('actual owned container isolation differs')
            command = ['docker', 'start', '--attach', name]
            owned.save(output / 'launch.json', {'create': create, 'argv': command})
            result['container_name'] = name
            with (artifacts / 'container.log').open('wb') as log:
                done = lease.run(command, timeout=220, output=log, max_output_bytes=8 * 1024**2)
                result['container_exit'] = done.returncode
                result['execution_pressure'] = lease.execution_pressure
            result['complete_helper_records'] = all(
                (artifacts / c / 'real-snapshots.bin').is_file() and
                (artifacts / c / 'real-input-tape.jsonl').is_file() for c in ('actual', 'fx-fault'))
            if profile_control:
                result['complete_helper_records'] = ((artifacts/'actual/real-snapshots.bin').is_file() and
                                                       (artifacts/'com/real-com-snapshots.bin').is_file())
            if guard_control:result['complete_helper_records']=(artifacts/'cold-guard/cold-guards.jsonl').is_file()
            result['helper_exits'] = {c: (artifacts / c / 'launcher-exit.txt').read_text().strip() for c in cases}
        except (OSError, ValueError, KeyError, subprocess.SubprocessError, owned.pool.ResourcePressure) as error:
            result['error'] = str(error)
        finally:
            subprocess.run(['docker', 'rm', '-f', name], capture_output=True, timeout=15)
            result['container_removed'] = not subprocess.check_output(
                ['docker', 'ps', '-aq', '--filter', 'name=^/' + name + '$'], timeout=10).strip()
            result['seed_unchanged'] = before is not None and owned.manifest(seed) == before
            result['producer_unchanged'] = all(owned.sha(ROOT / p) == h for p, h in result.get('producer_sha256', {}).items())
            result['binaries_unchanged'] = all(owned.sha(output / p) == h for p, h in result.get('binary_sha256', {}).items())
            result['elapsed_seconds'] = time.monotonic() - started
            result['execution_finished'] = (result.get('container_exit') == 0 and result.get('complete_helper_records') and
                                           all(result.get(k) is True for k in ('container_removed', 'seed_unchanged',
                                                                            'producer_unchanged', 'private_prefix_inodes',
                                                                            'binaries_unchanged')))
            owned.save(output / 'result.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--prefix-seed', type=Path, required=True)
    parser.add_argument('--image', default=owned.IMAGE)
    parser.add_argument('--binaries-from', type=Path, help='reuse exact retained binaries after source/byte checks')
    parser.add_argument('--safe-flags', type=int, choices=(0, 1, 2), help='explicit recorded WowBox64 call/return flags profile')
    parser.add_argument('--profile-control', action='store_true',help='run only state/COM matrices; never a complete qualification')
    parser.add_argument('--guard-control',action='store_true',help='run only cold refusal control; never a complete qualification')
    parser.add_argument('--include-diagnostics',action='store_true',help='also reproduce ruled-out flags/unsafe-guard diagnostics')
    args = parser.parse_args()
    if args.profile_control and args.guard_control:parser.error('countercontrol lanes are mutually exclusive')
    result = run(args.out, args.prefix_seed, args.image, args.binaries_from, args.safe_flags,args.profile_control,args.guard_control,args.include_diagnostics)
    print(json.dumps(result))
    return 0 if result['execution_finished'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
