#!/usr/bin/env python3
"""Observed Stock intro/menu/dialog qualification in an isolated ARM64 container.

This is a functional capture, not deterministic playback or a Parity Proof.
No host display/audio/input sockets or devices are exposed to the container.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import uuid
import wave

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
STOCK_SHA = '7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde'
# Frozen capture helpers must remain byte-identical even when host imports run.
sys.dont_write_bytecode = True


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


data = module('stock_data', HERE / 'diagnostic-data.py')
audio = module('stock_audio', HERE / 'audio_evidence.py')
focus = module('stock_focus', HERE / 'focus_evidence.py')
pool = module('stock_pool', HERE / 'resource_pool.py')
storage = module('stock_storage', HERE / 'storage.py')
prepaid = module('stock_prepaid', HERE / 'prepaid_budget.py')
prefix_archive = module('stock_prefix_archive', HERE / 'stock/prefix_archive.py')


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def save(path, value):
    with Path(path).open('x') as stream:
        json.dump(value, stream, indent=2, allow_nan=False)
        stream.write('\n')


def manifest(root):
    result = data.collect(root, exclude_runtime=False)
    if not result['complete']:
        raise ValueError(f'incomplete source inventory: {result["errors"][:3]}')
    return result['files']


def prefix_manifest(root):
    """Include directories and link bytes without traversing guest links."""
    result = {}
    for path in sorted(Path(root).rglob('*')):
        key = str(path.relative_to(root))
        if path.is_symlink():
            result[key] = {'link': os.readlink(path)}
        elif path.is_file():
            result[key] = {'sha256': digest(path), 'size': path.stat().st_size}
        elif path.is_dir():
            result[key] = {'directory': True}
        else:
            raise ValueError('unsupported prefix entry')
    return result


def qualify_functional_seed(seed, audit_path, image_id):
    """Recheck an ordinary Career run before reusing its full Wine prefix."""
    capture = seed.parent
    if seed.name != 'wineprefix' or audit_path != capture / 'result.json':
        raise ValueError('functional seed requires its original capture result and wineprefix')
    config = json.loads((capture / 'config.json').read_text())
    inspected = json.loads((capture / 'container-inspect.json').read_text())
    if (config.get('image_id') != image_id or config.get('career_flow') not in ('create', 'load') or
            any(value.startswith('BOX64_') for value in inspected['Config']['Env'])):
        raise ValueError('functional seed must use the matching ordinary game runtime profile')
    auditor = module('functional_prefix_audit', HERE / 'stock_career_save.py')
    audit = auditor.inspect_capture(capture, config['career_flow'])
    if audit['image_id'] != image_id:
        raise ValueError('functional seed audit runtime mismatch')
    return {'image_id': image_id, 'BOX64_DYNAREC_SAFEFLAGS': 'image_default',
            'qualification': 'normal Career capture; not raw ABI observer qualification',
            'capture': audit}


def qualify_stopped_prefix(seed, audit_path, out, image_id, docker, *, functional=False):
    """Bind the stopped seed before either supported private-copy strategy."""
    seed = Path(seed).resolve(strict=True)
    if seed == out or seed in out.parents or out in seed.parents:
        raise ValueError('prefix seed overlaps output')
    evidence = seed.parent / 'result.json'
    qualification = json.loads(evidence.read_text())
    audit_path = Path(audit_path).resolve(strict=True)
    if audit_path.parent != seed.parent:
        raise ValueError('prefix qualification must belong to retained seed execution')
    checked = json.loads(audit_path.read_text())
    runtime = checked.get('runtime_identity', {})
    expected_dinput = '0332b5210a9f287b0c75aee67d3c8e1a5e5a7b05ad5d51e1c8526685b152b582'
    expected_wowbox = '58d036888d638f05a72193702f97071aabc9baf7959e7736d48d9c450acc3440'
    for relative in ('drive_c/windows/syswow64/dinput8.dll', 'drive_c/windows/system32/wowbox64.dll',
                     'system.reg', 'user.reg', 'userdef.reg'):
        path = seed / relative
        if not path.is_file() or path.resolve() != path:
            raise ValueError('runtime modules and persisted hives must be ordinary seed files')
    if (digest(seed / 'drive_c/windows/syswow64/dinput8.dll') != expected_dinput or
            digest(seed / 'drive_c/windows/system32/wowbox64.dll') != expected_wowbox):
        raise ValueError('prefix runtime bytes differ from retained actual-module qualification')
    if functional:
        runtime = qualify_functional_seed(seed, audit_path, image_id)
    else:
        if (checked.get('passed') is not True or runtime.get('image_id') != image_id or
                runtime.get('BOX64_DYNAREC_SAFEFLAGS') != 2 or
                not runtime.get('dinput8_file', '').startswith(expected_dinput + '  ') or
                not runtime.get('wowbox64_file', '').startswith(expected_wowbox + '  ')):
            raise ValueError('prefix runtime bytes differ from retained actual-module qualification')
        stopped = (qualification.get('execution_finished') is True and
                   qualification.get('container_removed') is True and
                   qualification.get('helper_exits', {}).get('actual') == '0')
        if qualification.get('image_id') != image_id or not stopped:
            raise ValueError('unqualified stopped/persisted prefix seed')
    active = subprocess.check_output([docker, 'ps', '-q'], timeout=10).decode().split()
    if active:
        for container in json.loads(subprocess.check_output([docker, 'inspect', *active], timeout=15)):
            for mount in container['Mounts']:
                path = Path(mount['Source']).resolve()
                if path == seed or path in seed.parents or seed in path.parents:
                    raise ValueError('seed exposed to live container')
    before = prefix_manifest(seed)
    save(out / 'prefix-seed-before.json', before)
    return {'path': str(seed), 'result_sha256': digest(evidence),
            'qualification_sha256': digest(audit_path), 'runtime_identity': runtime,
            'image_id': image_id, 'preserved': False}, before


def clone_stopped_prefix(seed, audit_path, out, image_id, docker):
    """Require retained stopped execution, matching image and private inodes."""
    result, before = qualify_stopped_prefix(seed, audit_path, out, image_id, docker)
    seed = Path(result['path'])
    subprocess.run(['cp', '-c', '-p', '-R', str(seed), str(out / 'wineprefix')],
                   check=True, timeout=60)
    require_private = all((seed / name).stat().st_ino != (out / 'wineprefix' / name).stat().st_ino
                          for name, row in before.items() if 'sha256' in row)
    if not require_private or prefix_manifest(out / 'wineprefix') != before:
        raise ValueError('prefix clone differs or shares inodes')
    result['private_inodes'] = True
    return result


def prepare_ephemeral_prefix(seed, audit_path, out, image_id, docker):
    """Retain the exact seed manifest; guest copy/private-inode checks follow."""
    result, before = qualify_stopped_prefix(seed, audit_path, out, image_id, docker, functional=True)
    (out / 'instrumentation-source/expected-prefix.tsv').write_bytes(prefix_archive.encode_manifest(before))
    result['storage'] = 'private_tmpfs_with_retained_delta'
    return result


def audit_prefix_seed(seed, out, after_name):
    """Audit a started clone even if its caller never produced a result."""
    before = Path(out) / 'prefix-seed-before.json'
    if not before.is_file():
        return None
    after = prefix_manifest(Path(seed))
    save(Path(out) / after_name, after)
    return after == json.loads(before.read_text())


def audit_runtime_modules(out, prefix_path, final_prefix=None):
    """Require both observed module inventories to match the qualified bytes."""
    before = (out / 'input-runtime-sha256.txt').read_text()
    after = (out / 'input-runtime-after-sha256.txt').read_text()
    expected = {
        '/usr/lib/wine/aarch64-windows/wowbox64.dll': '58d036888d638f05a72193702f97071aabc9baf7959e7736d48d9c450acc3440',
        prefix_path + '/drive_c/windows/system32/wowbox64.dll': '58d036888d638f05a72193702f97071aabc9baf7959e7736d48d9c450acc3440',
        prefix_path + '/drive_c/windows/syswow64/dinput8.dll': '0332b5210a9f287b0c75aee67d3c8e1a5e5a7b05ad5d51e1c8526685b152b582'}
    rows = [line.split(None, 1) for line in before.splitlines()]
    if any(len(row) != 2 for row in rows) or len(rows) != len(expected):
        raise ValueError('input runtime module inventory is incomplete or duplicated')
    actual = {path: hashed for hashed, path in rows}
    if before != after or actual != expected:
        raise ValueError('input runtime modules differ from qualified backend identity')
    if final_prefix is not None:
        if (out / 'input-runtime-final-sha256.txt').read_text() != before:
            raise ValueError('runtime modules changed during final Wine shutdown')
        for relative in ('drive_c/windows/system32/wowbox64.dll', 'drive_c/windows/syswow64/dinput8.dll'):
            if final_prefix.get(relative, {}).get('sha256') != expected[prefix_path + '/' + relative]:
                raise ValueError('final prefix module differs from qualified backend identity')
    return actual


def paths_valid(out, source, stock, texture, disc):
    for protected in ((ROOT / 'ref').resolve(), source, stock, texture, disc):
        if out == protected or protected in out.parents or out in protected.parents:
            raise ValueError('output overlaps protected inputs')
    if not source.is_dir() or not disc.is_dir() or not stock.is_file() or not texture.is_file():
        raise ValueError('source/disc must be directories and Stock/texture must be files')
    if digest(stock) != STOCK_SHA:
        raise ValueError('Stock Executable SHA-256 mismatch')


def isolated(inspected, out, disc, image_id, prefix_seed=None, *, memory_limit_bytes=3 * 1024**3):
    """Audit actual Docker configuration, not just the requested CLI flags."""
    host = inspected['HostConfig']
    mounts = {(m['Destination'], str(Path(m['Source']).resolve()), m['RW'])
              for m in inspected['Mounts']}
    expected_mounts = {('/work', str(out), True), ('/disc', str(disc), False)}
    if memory_limit_bytes not in (3 * 1024**3, 20 * 1024**3):
        return False
    memory = memory_limit_bytes
    if prefix_seed:
        expected_mounts.add(('/seed', str(prefix_seed), False))
        expected_mounts.add(('/work/instrumentation-source', str(out / 'instrumentation-source'), False))
        if (host.get('Tmpfs') != {'/privateprefix': 'rw,exec,nosuid,nodev,size=3g'} or
                host.get('MemorySwap') != memory):
            return False
    return (inspected['Image'] == image_id and host['NetworkMode'] == 'none' and
            not host['Privileged'] and not host.get('Devices') and not host.get('DeviceRequests') and
            not host.get('CapAdd') and {x.upper() for x in host.get('CapDrop', [])} == {'ALL'} and
            host.get('SecurityOpt') == ['no-new-privileges'] and
            host.get('PidMode', '') != 'host' and host.get('IpcMode', '') != 'host' and
            host['NanoCpus'] == 4_000_000_000 and host['Memory'] == memory and
            host['PidsLimit'] == 256 and
            mounts == expected_mounts)


def control_stop_evidence(text):
    """A completed-looking tape cannot override a missing/failed stop result."""
    values = re.findall(r'^\[[ \t]*[0-9]+ ms\] control_tape_stop result=(-?[0-9]+)$', text, re.MULTILINE)
    count = int(values[0]) if len(values) == 1 else None
    return {'passed': len(values) == text.count('control_tape_stop result=') == 1 and
                     not values[0].startswith('-') and 0 <= count <= 65536,
            'observations': len(values), 'committed_api_rows': count}


def scenario_evidence(flow, text, consumed=None):
    actions = [a['label'] for a in flow.get('actions', [])]
    transitions = [int(v) for v in re.findall(r'input_poll: kind=1 offset=12 data=(\d+) source=native', text)]
    polls = max([int(n) for n in re.findall(r'GetDevice(?:State|Data) calls=(\d+)', text)], default=0)
    if consumed is not None:
        transitions, polls = consumed['mouse_transitions'], consumed['successful_mouse_reads']
    before, dialog, after = (flow.get(k, {}) for k in ('before', 'name_dialog', 'after'))
    menu = lambda row: row.get('menu') == {'MAIN MENU': True, 'FREE RIDE': True, 'OPTIONS': True}
    dialog_shown = dialog.get('dialog') == {'name_title': True, 'cancel_button': True}
    dialog_absent = after.get('dialog') == {'name_title': False, 'cancel_button': False}
    result = {'actions': actions, 'mouse_transitions': transitions, 'polls': polls,
              'queue_drops': text.count('input_queue: dropped ')}
    result['complete'] = (flow.get('passed') is True and menu(before) and menu(after) and
                          dialog_shown and dialog_absent and polls > 0 and result['queue_drops'] == 0 and
                          actions == ['New Career', 'Cancel button'] and transitions == [0, 128, 0, 128, 0])
    return result


def ordinary_exit_evidence(out, flow):
    """Validate the guest waiter's records separately from controller success."""
    shutdown = flow.get('shutdown', {})
    started = json.loads((out / 'game-process-start.json').read_text())
    exited = json.loads((out / 'game-process-exit.json').read_text())
    confirmed = shutdown.get('confirmation', {})
    actions = shutdown.get('actions', [])
    valid = (set(started) == {'pid', 'tick'} and
             set(exited) == {'pid', 'tick', 'wait_result', 'exit_code'} and
             all(type(v) is int and 0 <= v <= 0xffffffff for v in [*started.values(), *exited.values()]) and
             started['pid'] > 0 and started['pid'] == exited['pid'] and
             exited['wait_result'] == 0 and exited['exit_code'] == 0 and
             0 < ((exited['tick'] - started['tick']) & 0xffffffff) < 600000 and
             shutdown.get('process_start') == started and shutdown.get('process_exit') == exited and
             [a.get('label') for a in actions] == ['Exit Game', 'Confirm Yes'] and
             confirmed.get('labels') == {'title': True, 'question': True, 'yes': True, 'no': True} and
             confirmed.get('yes_center') == [320, 470] and
             confirmed.get('image') in [r.get('image') for r in flow.get('observations', [])] and
             (out / 'ordinary-exit-observed-time.txt').is_file() and
             not (out / 'ordinary-exit-error.txt').exists())
    return {'passed': valid, 'process_start': started, 'process_exit': exited,
            'method': 'private guest UI confirmation followed by signaled game process handle',
            'limits': ['does not independently prove every internal shutdown callback or a save-write/reload']}


def native_return_evidence(path, expected):
    """Audit the native-only, bounded Return witness independently of UI flags."""
    if path.is_symlink() or not path.is_file() or not 0 < path.stat().st_size <= 16384:
        raise ValueError('native Return witness unavailable or unsupported')
    raw = path.read_bytes()
    rows = [json.loads(line) for line in raw.decode('utf-8').splitlines()]
    keys = {'sequence', 'source', 'device', 'offset', 'data', 'state_call', 'tick'}
    if not rows or len(rows) > 64:
        raise ValueError('native Return witness count unsupported')
    for index, row in enumerate(rows, 1):
        if (set(row) != keys or row['source'] != 'native'
                or any(type(row[key]) is not int for key in keys - {'source'})
                or row['sequence'] != index or row['offset'] != 28 or not 0 <= row['device'] < 16
                or row['device'] != rows[0]['device'] or row['state_call'] <= 0
                or row['data'] not in (0, 128)
                or (index > 1 and row['state_call'] <= rows[index - 2]['state_call'])):
            raise ValueError('native Return witness malformed or discontinuous')
    if [row['data'] for row in rows] != expected:
        raise ValueError('native Return transitions differ')
    return {'passed': True, 'rows': rows, 'sha256': hashlib.sha256(raw).hexdigest()}


def career_scenario_evidence(flow, text, mode, native_return=None):
    career = flow.get('career', {})
    actions = [row.get('label') for row in flow.get('actions', [])]
    expected = (['New Career', 'Type Career name', 'Accept name', 'Next portrait', 'Accept character']
                if mode == 'create' else ['Load Career', 'Select SaveProof', 'Load selected Career'])
    if career.get('welcome_dismissed') is True:
        expected += ['Dismiss Welcome']
    extension, extension_complete = module('stock_career_scenario', HERE / 'stock/career_flow.py').scenario_extension(mode, career)
    expected += extension
    expected += ['Garage Main Menu']
    intro = career.get('dealer_intro') is not None
    clicks = len(expected) - (1 if mode == 'create' else 0) - (1 if intro else 0)
    transitions = [int(v) for v in re.findall(r'input_poll: kind=1 offset=12 data=(\d+) source=native', text)]
    garage = career.get('garage', {}).get('career', {})
    before = flow.get('before', {})
    after = flow.get('after', {})
    frontiers = ((career.get('name_dialog', {}).get('dialog') == {'name_title': True, 'cancel_button': True}
                  and career.get('character_before', {}).get('career', {}).get('character') is True
                  and career.get('character_after', {}).get('career', {}).get('character') is True
                  and [a.get('text') for a in flow.get('actions', []) if a.get('label') == 'Type Career name'] == ['SaveProof'])
                 if mode == 'create' else career.get('load_list', {}).get('career', {}).get('load_list') is True)
    complete = (mode in ('create', 'load', 'dealer-inspect', 'dealer-vision') and extension_complete
                and flow.get('passed') is True and career.get('passed') is True
                and career.get('mode') == mode and career.get('name') == 'SaveProof' and actions == expected
                and frontiers and garage.get('welcome') is career.get('welcome_dismissed')
                and garage.get('identity') is True and garage.get('money') is True
                and all(row.get('menu', {}).get(key) is True for row in (before, after)
                        for key in ('MAIN MENU', 'FREE RIDE', 'OPTIONS'))
                and after.get('dialog') == {'name_title': False, 'cancel_button': False}
                and transitions == [0] + [value for _ in range(clicks) for value in (128, 0)]
                and (not intro or native_return is not None and native_return.get('passed') is True
                     and [row['data'] for row in native_return['rows']] == [0, 128, 0])
                and 'input_queue: dropped ' not in text)
    return {'mode': mode, 'actions': actions, 'mouse_transitions': transitions, 'complete': complete,
            'limits': ['external functional UI and input observations; no response replay or paired Parity']}


def worker(config_path):
    config_path = Path(config_path).resolve(strict=True)
    config = json.loads(config_path.read_text())
    out = config_path.parent
    source, stock, texture, disc = (Path(config[k]).resolve(strict=True)
                                    for k in ('source', 'stock', 'texture', 'disc'))
    paths_valid(out, source, stock, texture, disc)
    docker, compiler = config['docker'], config['compiler']
    if not config['image_id'].startswith('sha256:'):
        raise ValueError('worker requires an immutable image id')
    result = {'schema': 1, 'status': 'incomplete', 'passed': False, 'timed_out': False,
              'controls': {'mode': 'record_only' if config['record_controls'] else 'uncontrolled',
                           'process_replay_qualified': False},
              'environment': config['environment'], 'container_name': config['container_name'],
              'limits': ['observed wall-clock scenario; no deterministic replay claim',
                         'synthetic CD metadata, no CD-track audio qualification',
                         'scoped forced Wine shutdown; ordinary user quit not qualified']}
    before = process = None
    start = time.monotonic()
    def execute(argv, **kwargs):
        return subprocess.run([str(a) for a in argv], check=True, timeout=30, **kwargs)
    try:
        before = manifest(source)
        save(out / 'source-before.json', before)
        save(out / 'disc-before.json', manifest(disc))
        frozen = out / 'instrumentation-source'
        frozen.mkdir()
        sources = {'d3d9_proxy.c': ROOT / 'tools/wine/capture/d3d9_proxy.c',
                   'dinput8_proxy.c': ROOT / 'tools/wine/capture/dinput8_proxy.c',
                   'walk_trace.c': ROOT / 'tools/wine/capture/walk_trace.c',
                   'synthetic_cd.c': ROOT / 'tools/wine/synthetic_cd.c',
                   'synthetic_metadata.py': ROOT / 'tools/wine/synthetic_metadata.py',
                   'runner.py': Path(__file__)}
        sources.update({name: HERE / 'stock' / name for name in
                        ('container.sh', 'state_flow.py', 'career_flow.py', 'menu_ocr.py', 'probe32.c', 'wait_game.c')})
        final_prefix = None
        if config.get('ephemeral_prefix'):
            sources.update({name: HERE / 'stock' / name for name in
                            ('prefix_archive.sh', 'prefix_archive.py')})
        if config['record_controls']:
            sources.update({name: HERE / 'stock' / name for name in ('control_tape.c', 'control_tape.h')})
            sources['control_interval.py'] = HERE / 'control_interval.py'
            if config.get('input_observer'):
                sources.update({name: HERE / 'stock' / name for name in
                                ('input_observer.h', 'input_observer.inc', 'input_bind.c',
                                 'stock_input_proxy.c', 'stock_input_tape.py')})
            if config['early_controls']:
                sources['stock_bootstrap.c'] = HERE / 'stock/stock_bootstrap.c'
            if config['rng_observer']:
                # control_tape.c includes this at compile time; freeze it with the other sources.
                sources['rng_observer.inc'] = HERE / 'stock/rng_observer.inc'
            if config.get('native_clock_observer'):
                sources['native_clock_observer.inc'] = HERE / 'stock/native_clock_observer.inc'
        for name, path in sources.items():
            shutil.copy2(path, frozen / name)
        if config.get('prefix_seed'):
            prepare_prefix = prepare_ephemeral_prefix if config.get('ephemeral_prefix') else clone_stopped_prefix
            result['prefix_seed'] = prepare_prefix(config['prefix_seed'], config['prefix_seed_checks'], out, config['image_id'], docker)
        frozen_manifest = manifest(frozen)
        save(out / 'instrumentation-source.json', frozen_manifest)
        with (out / 'prepare.log').open('wb') as log:
            execute([compiler, '--version'], stdout=log, stderr=subprocess.STDOUT)
            copied = subprocess.run(['cp', '-c', '-R', str(source), str(out / 'game')],
                                    stdout=log, stderr=subprocess.STDOUT, timeout=30)
            if copied.returncode:
                raise ValueError('APFS private clone failed; full-copy fallback refused to prevent storage amplification')
            if manifest(out / 'game') != before:
                raise ValueError('private copy does not match source inventory')
            execute(['chmod', '-R', 'u+w', out / 'game'], stdout=log, stderr=subprocess.STDOUT)
            shutil.copy2(stock, out / 'game/StreetLegal_Redline.exe')
            execute([sys.executable, '-I', frozen / 'synthetic_metadata.py', stock, texture,
                     out / 'metadata.txt', out / 'SYNTHETIC-metadata.txt'], stdout=log, stderr=subprocess.STDOUT)
            common = [compiler, '-shared', '-O2', '-Wl,--kill-at', '-static-libgcc', '-o']
            execute(common + [out / 'game/mcicda.dll', frozen / 'synthetic_cd.c', '-lwinmm'], stdout=log, stderr=subprocess.STDOUT)
            record_args = (['-DSLRR_CONTROL_TAPE', '-msse2', '-I', frozen, frozen / 'control_tape.c']
                           if config['record_controls'] else [])
            if config['early_controls']:
                record_args += ['-DSLRR_EARLY_CONTROL_TAPE', frozen / 'stock_bootstrap.c', '-lwinmm']
            if config['rng_observer']:
                record_args += ['-DCONTROL_TAPE_RNG', '-DSLRR_STOCK_RNG_OBSERVER']
            if config.get('native_clock_observer'):
                record_args += ['-DCONTROL_TAPE_NATIVE_CLOCK', '-DSLRR_STOCK_NATIVE_CLOCK_OBSERVER']
            if config.get('input_observer'):
                record_args += ['-DCONTROL_TAPE_INPUT', '-DCONTROL_TAPE_INPUT_STOCK']
            execute(common + [out / 'game/d3d9.dll', frozen / 'd3d9_proxy.c', frozen / 'walk_trace.c',
                              *record_args, '-ladvapi32'], stdout=log, stderr=subprocess.STDOUT)
            input_args = ([frozen / 'stock_input_proxy.c', frozen / 'input_bind.c', '-I', frozen]
                          if config.get('input_observer') else [frozen / 'dinput8_proxy.c'])
            execute(common + [out / 'game/dinput8.dll', *input_args], stdout=log, stderr=subprocess.STDOUT)
            execute([compiler, '-O2', '-o', out / 'probe32.exe', frozen / 'probe32.c'], stdout=log, stderr=subprocess.STDOUT)
            if config.get('ordinary_exit'):
                execute([compiler, '-O2', '-Wall', '-Wextra', '-mwindows', '-o', out / 'wait_game.exe', frozen / 'wait_game.c'], stdout=log, stderr=subprocess.STDOUT)
        fixture = config.get('save_fixture')
        if fixture:
            fixture = Path(fixture).resolve(strict=True)
            if fixture == out or fixture in out.parents or out in fixture.parents:
                raise ValueError('save fixture overlaps private output')
            initial_fixture = manifest(fixture)
            save(out / 'save-fixture-before.json', initial_fixture)
            target = out / 'game/save'
            if target.exists():
                shutil.rmtree(target)
            shutil.copytree(fixture, target)
            if manifest(target) != initial_fixture:
                raise ValueError('save fixture copy does not match source')
            career_module = module('stock_initial_career', frozen / 'career_flow.py')
            result['initial_career'] = career_module.saved_prefix(target / 'carrer/SaveProof-1/main')
        if config.get('career_flow') == 'create' and any((out / 'game/save/carrer').rglob('*')):
            raise ValueError('Career creation requires a source with no existing careers')
        save(out / 'private-initial.json', manifest(out / 'game'))
        argv = [docker, 'run', '--rm', '--name', config['container_name'], '--network', 'none',
                '--platform', 'linux/arm64', '--cpus', '4', '--memory', '20g', '--pids-limit', '256',
                '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges',
                '-e', 'WINEARCH=win64', '-e', 'HODLL=wowbox64.dll', '-e', 'GALLIUM_DRIVER=llvmpipe',
                '-e', 'LP_NUM_THREADS=1', '-e', 'LIBGL_ALWAYS_SOFTWARE=1',
                '--mount', f'type=bind,src={out},dst=/work',
                '--mount', f'type=bind,src={disc},dst=/disc,readonly',
                '--entrypoint', '/usr/bin/timeout', config['image_id'], '--kill-after=5',
                str(config['timeout']), '/bin/bash', '/work/instrumentation-source/container.sh']
        if config.get('ordinary_exit'):
            argv[argv.index('--entrypoint'):argv.index('--entrypoint')] = ['-e', 'SLRR_ORDINARY_EXIT=1']
        if config.get('career_flow'):
            argv[argv.index('--entrypoint'):argv.index('--entrypoint')] = ['-e', 'SLRR_CONTROLLER_SECONDS=' + str(math.ceil(config['timeout']))]
        if config.get('career_flow') == 'dealer-vision':
            argv[argv.index('--entrypoint'):argv.index('--entrypoint')] = ['-e', 'SLRR_NATIVE_RETURN_OBSERVER=1']
        if config['record_controls']:
            argv[argv.index('--entrypoint'):argv.index('--entrypoint')] = ['-e', 'SLRR_CONTROL_TAPE=1']
        if config.get('input_observer'):
            argv[argv.index('--entrypoint'):argv.index('--entrypoint')] = ['-e', 'BOX64_DYNAREC_SAFEFLAGS=2']
        if config.get('prefix_seed'):
            argv[argv.index('--entrypoint'):argv.index('--entrypoint')] = ['-e', 'SLRR_PREFIX_SEEDED=1']
        if config.get('ephemeral_prefix'):
            argv[argv.index('--entrypoint'):argv.index('--entrypoint')] = [
                '--memory-swap', '20g', '--tmpfs', '/privateprefix:rw,exec,nosuid,nodev,size=3g',
                '--mount', f'type=bind,src={config["prefix_seed"]},dst=/seed,readonly',
                '--mount', f'type=bind,src={frozen},dst=/work/instrumentation-source,readonly',
                '-e', 'SLRR_PREFIX_EPHEMERAL=1']
        save(out / 'launch.json', {'argv': argv, 'image_id': config['image_id']})
        deadline = time.monotonic() + config['timeout']
        with (out / 'container.log').open('wb') as log:
            process = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
            # The daemon may not have registered the new container immediately.
            while True:
                inspected = subprocess.run([docker, 'inspect', config['container_name']],
                                           capture_output=True, timeout=10)
                if inspected.returncode == 0:
                    inspected = json.loads(inspected.stdout)[0]
                    break
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('container ended before isolation inspection')
                time.sleep(.05)
            save(out / 'container-inspect.json', inspected)
            result['container_isolated'] = isolated(inspected, out, disc, config['image_id'],
                config['prefix_seed'] if config.get('ephemeral_prefix') else None, memory_limit_bytes=20 * 1024**3)
            if config.get('input_observer'):
                if 'BOX64_DYNAREC_SAFEFLAGS=2' not in inspected['Config']['Env']:
                    raise ValueError('actual input backend profile is not SAFEFLAGS2')
            elif config.get('ephemeral_prefix') and any(value.startswith('BOX64_') for value in inspected['Config']['Env']):
                raise ValueError('functional prefix runtime has an unexpected BOX64 override')
            if not result['container_isolated']:
                raise ValueError('actual container configuration failed isolation audit')
            flow = module('stock_state_flow', frozen / 'state_flow.py')
            result['flow'] = flow.run(config['container_name'], out, process, deadline, ordinary_exit=config.get('ordinary_exit', False), career_flow=config.get('career_flow'), input_observer=config.get('input_observer', False))
            result['container_exit'] = process.wait(timeout=max(.1, deadline - time.monotonic() + 6))
            result['timed_out'] = result['container_exit'] == 124 or time.monotonic() >= deadline
        if result['timed_out']:
            raise TimeoutError('container execution deadline exceeded')
        if result['container_exit'] != 0 or not result['flow'].get('passed'):
            raise RuntimeError('scenario incomplete: ' + result['flow'].get('error', f'container exit {result["container_exit"]}'))
        if manifest(frozen) != frozen_manifest:
            raise ValueError('frozen instrumentation sources changed during capture')
        if config.get('ephemeral_prefix'):
            # Never import Python from the guest-writable capture mount. The
            # trusted host validator was loaded before launching the container.
            before_prefix = prefix_archive.parse_manifest(frozen / 'expected-prefix.tsv')
            prepared, finished, final_prefix = prefix_archive.validate_capture(out, before_prefix)
            if prefix_manifest(Path(config['prefix_seed'])) != before_prefix:
                raise ValueError('immutable prefix seed changed')
            result['prefix_archive'] = {'prepared': prepared, 'finished': finished}
        text = '' if config.get('input_observer') else (out / 'game/dumps/dinput8proxy.log').read_text(errors='replace')
        if config['record_controls']:
            stop = control_stop_evidence((out / 'game/dumps/d3d9proxy.log').read_text(errors='replace'))
            result['controls']['stop_receipt'] = stop
            if not stop['passed']:
                raise ValueError('record-only interval has no unique successful stop acknowledgement')
        consumed = None
        if config.get('input_observer') or config.get('ephemeral_prefix'):
            prefix_path = '/privateprefix' if config.get('ephemeral_prefix') else '/work/wineprefix'
            actual_modules = audit_runtime_modules(out, prefix_path, final_prefix)
            result['runtime_modules'] = actual_modules
            result['BOX64_DYNAREC_SAFEFLAGS'] = 'image_default' if config.get('ephemeral_prefix') else 2
        if config.get('input_observer'):
            input_module = module('stock_input_inventory', frozen / 'stock_input_tape.py')
            consumed = input_module.inventory(out / 'game/dumps/stock-control-tape.jsonl', out / 'game/dumps/stock-input-setup.jsonl')
            save(out / 'input-consumption.json', consumed)
            result['controls'].update(input_inventory=consumed, interval_complete=False,
                                      input_consumption_qualified=False,
                                      runtime_modules=actual_modules, BOX64_DYNAREC_SAFEFLAGS=2,
                                      limits=['experimental raw inventory; mixed API semantics not compiled',
                                              'caller/configuration/thread/replay qualification pending'])
        scenario_text = (out / 'scenario-input.log').read_text() if config.get('ordinary_exit') else text
        return_observation = None
        if config.get('career_flow') == 'dealer-vision':
            expected_return = [0, 128, 0] if result['flow']['career'].get('dealer_intro') is not None else [0]
            return_observation = native_return_evidence(out / 'game/dumps/native-return.jsonl', expected_return)
            result['native_return'] = return_observation
        result['scenario'] = (career_scenario_evidence(result['flow'], scenario_text, config['career_flow'], return_observation)
                              if config.get('career_flow') else scenario_evidence(result['flow'], scenario_text, consumed))
        if config.get('ordinary_exit'):
            result['shutdown'] = ordinary_exit_evidence(out, result['flow'])
            if not result['shutdown'].get('passed'):
                raise ValueError('ordinary shutdown was not observed')
            result['limits'].remove('scoped forced Wine shutdown; ordinary user quit not qualified')
        if config.get('career_flow'):
            career_module = module('stock_career_records', frozen / 'career_flow.py')
            result['career_save'] = career_module.saved_prefix(
                out / 'game/save/carrer/SaveProof-1/main',
                result.get('initial_career', {}).get('portrait_word'),
                result['flow']['career']['expected_money'] if config['career_flow'] == 'dealer-vision' else 20000)
            if config['career_flow'] == 'dealer-vision':
                result['career_vision'] = career_module.audit_vision(out, result['flow'])
        if config['record_controls'] and not config.get('input_observer'):
            compiler_module = module('stock_control_interval', frozen / 'control_interval.py')
            interval = compiler_module.compile_tape(out / 'game/dumps/stock-control-tape.jsonl')
            save(out / 'controls-interval.json', interval)
            if config['rng_observer']:
                # A format-5 tape with no RNG record is a missing observation, never a pass.
                if interval.get('format') != 'stock-api-interval-offline/3' or not interval.get('rng_calls'):
                    raise ValueError('RNG observer requested but no validated RNG calls were recorded')
            elif interval.get('format') == 'stock-api-interval-offline/3':
                raise ValueError('unexpected RNG records without an RNG observer request')
            if config.get('native_clock_observer'):
                if interval.get('format') != 'stock-api-native-return-interval-offline/2' or not interval.get('native_returns'):
                    raise ValueError('Native clock observer requested but no validated actual returns were recorded')
            elif interval.get('native_returns'):
                raise ValueError('unexpected Native clock returns without an observer request')
            result['controls'].update(interval_complete=True, calls=len(interval['calls']),
                                      shared_phases=len(interval.get('phase_order', [])),
                                      source_sha256=interval['source_sha256'],
                                      start_boundary='proxy_process_attach' if config['early_controls'] else 'first_Direct3DCreate9',
                                      early_startup='main_IAT_only' if config['early_controls'] else 'unobserved',
                                      rng_observer=config['rng_observer'], rng_calls=interval.get('rng_calls', 0),
                                      native_clock_observer=config.get('native_clock_observer', False),
                                      native_clock_returns=len(interval.get('native_returns', [])),
                                      omissions=['DirectInput consumption', 'stable lane bindings', 'OS scheduler',
                                                 'replay', 'exceptions/unwind', 'thread lifetime identity']
                                                + ([] if config['rng_observer'] else ['RNG']))
        result['probe32_exit'] = (out / 'probe32-exit.txt').read_text().strip()
        result['audio'] = audio.summarize(out / 'stock.wav', 's16le', 2, 48000, 'wav')
        result['audio'].update(path='stock.wav', route='pulse-null-monitor', stage='sink_monitor',
                               complete=True, physical_output=False)
        save(out / 'audio-summary.json', result['audio'])
        sinks = (out / 'audio-sinks.txt').read_text()
        result['private_sink'] = sinks.count('Name: stock\n') == 1 and sinks.count('Driver: module-null-sink.c') == 1
        result['passed'] = (result['container_exit'] == 0 and not result['timed_out'] and
                            result['scenario']['complete'] and result['private_sink'] and result['probe32_exit'] == '0' and
                            result['audio']['nonzero_samples'] > 0)
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, subprocess.SubprocessError, EOFError, wave.Error) as error:
        result['error'] = str(error)
        result['error_type'] = type(error).__name__
        result['timed_out'] |= isinstance(error, subprocess.TimeoutExpired)
    finally:
        try:
            cleanup = subprocess.run([docker, 'rm', '-f', config['container_name']], capture_output=True, timeout=15)
            result['cleanup_output'] = cleanup.stdout.decode(errors='replace') + cleanup.stderr.decode(errors='replace')
            remaining = subprocess.run([docker, 'ps', '-aq', '--filter', 'name=^/' + config['container_name'] + '$'],
                                       check=True, capture_output=True, timeout=10)
            result['container_removed'] = remaining.stdout.strip() == b''
            if process is not None:
                result.setdefault('container_exit', process.wait(timeout=5))
                result['timed_out'] |= result['container_exit'] == 124
        except (OSError, subprocess.SubprocessError) as error:
            result['cleanup_error'] = str(error)
            result['container_removed'] = False
        try:
            after = manifest(source)
            save(out / 'source-after.json', after)
            result['source_preserved'] = before is not None and before == after
            result['disc_preserved'] = json.loads((out / 'disc-before.json').read_text()) == manifest(disc)
            result['stock_preserved'] = digest(stock) == STOCK_SHA and digest(out / 'game/StreetLegal_Redline.exe') == STOCK_SHA
            result['texture_preserved'] = digest(texture) == config['texture_sha256']
            if config.get('save_fixture'):
                result['save_fixture_preserved'] = (json.loads((out / 'save-fixture-before.json').read_text()) ==
                                                     manifest(Path(config['save_fixture'])))
                result['passed'] &= result['save_fixture_preserved']
        except (OSError, ValueError) as error:
            result['preservation_error'] = str(error)
            result['source_preserved'] = False
        if config.get('prefix_seed'):
            try:
                preserved = audit_prefix_seed(config['prefix_seed'], out, 'prefix-seed-after.json')
                if preserved is not None:
                    result.setdefault('prefix_seed', {})['preserved'] = preserved
                    result['passed'] &= preserved
            except (OSError, ValueError) as error:
                result.update(passed=False, prefix_seed_audit_error=str(error))
        result['seconds'] = time.monotonic() - start
        result['passed'] &= all(result.get(k) is True for k in
                                ('container_removed', 'source_preserved', 'disc_preserved', 'stock_preserved', 'texture_preserved'))
        result['status'] = 'functional_scenario_captured' if result['passed'] else 'incomplete'
        save(out / 'worker-result.json', result)
    return 0 if result['passed'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'stock', 'texture-table', 'disc', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--image', default='slrr-private-stock-hangover1116-xtest-arm64-20261101')
    parser.add_argument('--focus-observer', type=Path, default=ROOT / 'build/parity_focus_observer')
    parser.add_argument('--timeout', type=float, default=130)
    parser.add_argument('--record-controls', action='store_true',
                        help='record-only main-image API interval; early startup/DirectInput/RNG/replay remain unqualified')
    parser.add_argument('--early-controls', action='store_true',
                        help='experimental record-only attach bootstrap; implies --record-controls, no late fallback')
    parser.add_argument('--rng-observer', action='store_true',
                        help='experimental: patch the guarded Stock CRT srand/rand tails (format 5); requires --early-controls')
    parser.add_argument('--input-observer', action='store_true',
                        help='experimental raw Stock consumption inventory; requires --early-controls and SAFEFLAGS2, no replay')
    parser.add_argument('--native-clock-observer', action='store_true',
                        help='experimental original stored script clock Float32 (format 9); requires --early-controls, no replay')
    parser.add_argument('--prefix-seed', type=Path,
                        help='stopped prefix; qualified ABI seed for input observer, audited ordinary Career seed for ephemeral mode')
    parser.add_argument('--prefix-seed-checks', type=Path,
                        help='ABI auditor report, or the ordinary Career capture result.json for ephemeral mode')
    parser.add_argument('--ephemeral-prefix', action='store_true',
                        help='experimental private tmpfs prefix; retain full manifests and changed bytes; no memory-budget increase')
    parser.add_argument('--ordinary-exit', action='store_true', help='experimental: exit through the guest menu and observe the actual game process handle')
    parser.add_argument('--max-artifact-mib', type=int, default=4096,
                        help='sampled logical output-tree limit, including game/prefix/PCM (default: 4096 MiB)')
    parser.add_argument('--artifact-budget-root', type=Path,
                        help='existing registered direct parent; preserve all previous evidence')
    parser.add_argument('--artifact-parent-max-mib', type=int,
                        help='exact existing parent reservation, never a new maximum')
    parser.add_argument('--career-flow', choices=('create', 'load', 'dealer-inspect', 'dealer-vision'),
                        help='experimental ordinary Career creation or fixture load; requires --ordinary-exit')
    parser.add_argument('--save-fixture', type=Path, help='immutable save copied into the private game; required for career load')
    args = parser.parse_args()
    if args.career_flow and not args.ordinary_exit:
        parser.error('--career-flow requires --ordinary-exit')
    if args.career_flow in ('load', 'dealer-inspect', 'dealer-vision') and not args.save_fixture:
        parser.error('--career-flow load/dealer-inspect requires --save-fixture')
    if args.save_fixture and not args.career_flow:
        parser.error('--save-fixture requires a declared --career-flow')
    if args.ordinary_exit and (args.record_controls or args.early_controls or args.rng_observer):
        parser.error('--ordinary-exit is not yet qualified with the interval recorder')
    if args.rng_observer and not args.early_controls:
        parser.error('--rng-observer requires --early-controls')
    if args.native_clock_observer and (not args.early_controls or args.rng_observer or args.input_observer or args.ordinary_exit or args.career_flow):
        parser.error('--native-clock-observer requires --early-controls without RNG, input, Career or ordinary-exit modes')
    if args.input_observer and (not args.early_controls or args.rng_observer or args.ordinary_exit or args.career_flow):
        parser.error('--input-observer currently requires --early-controls without RNG, Career or ordinary-exit modes')
    if args.input_observer and not args.prefix_seed:
        parser.error('--input-observer requires --prefix-seed')
    if args.ephemeral_prefix and (not args.prefix_seed or args.input_observer):
        parser.error('--ephemeral-prefix requires a qualified seed and currently excludes --input-observer')
    if bool(args.prefix_seed) != bool(args.prefix_seed_checks) or (args.prefix_seed and not (args.input_observer or args.ephemeral_prefix)):
        parser.error('prefix seed and qualification report require --input-observer or --ephemeral-prefix')
    if bool(args.artifact_budget_root) != (args.artifact_parent_max_mib is not None):
        parser.error('prepaid root and exact parent maximum are required together')
    if args.artifact_budget_root and not args.ephemeral_prefix:
        parser.error('prepaid Stock capture currently requires --ephemeral-prefix')
    artifact_budget = prepaid_budget = result = out = None
    try:
        source, stock, texture, disc, out = (p.resolve() for p in
                                            (args.source, args.stock, args.texture_table, args.disc, args.out))
        paths_valid(out, source, stock, texture, disc)
        save_fixture = args.save_fixture.resolve(strict=True) if args.save_fixture else None
        if save_fixture and (not save_fixture.is_dir() or save_fixture == out or
                             save_fixture in out.parents or out in save_fixture.parents):
            raise ValueError('invalid save fixture or output overlap')
        if out.exists():
            raise ValueError('output must be a new directory')
        if not math.isfinite(args.timeout) or args.timeout <= 0:
            raise ValueError('timeout must be finite and positive')
        if args.max_artifact_mib < 1:
            raise ValueError('artifact limit must be positive')
        observer = args.focus_observer.resolve(strict=True)
        if not observer.is_file() or not os.access(observer, os.X_OK):
            raise ValueError('executable parity_focus_observer required')
        commands = {k: shutil.which(k) for k in ('docker', 'i686-w64-mingw32-gcc', 'ffmpeg', 'tesseract')}
        if not all(commands.values()):
            raise ValueError('requires Docker, MinGW gcc, FFmpeg and Tesseract')
        # Four hard-limited guest CPUs; user-authorized20GiB guest plus3GiB host capture overhead.
        with pool.ResourcePool(pressure=pool.Pressure()).acquire({'cpu': 6, 'host_memory': 46, 'vm_memory': 40,
                                           'renderer': 1, 'stock': 1}) as lease:
            image = json.loads(subprocess.run([commands['docker'], 'image', 'inspect', args.image],
                                              check=True, capture_output=True, timeout=15).stdout)[0]
            if image['Architecture'] != 'arm64':
                raise ValueError('this adapter requires the ARM64 image')
            if (args.input_observer or args.ephemeral_prefix) and image['Id'] != 'sha256:c3105bb5c0fe829f89f6f782e79d7bfdb31708209ee4456e3ab4be5e7f94d88f':
                raise ValueError('seeded capture requires the exact qualified runtime image')
            # Wine dosdevices links are guest path metadata. Never follow them
            # from the host scanner; real container mounts/devices are checked
            # independently by isolated(), so link bytes are charged explicitly.
            if args.artifact_budget_root:
                prepaid_budget = prepaid.LeaseBudgetAdapter.admit(pool.ArtifactLimit,
                    args.artifact_budget_root, Path(args.out).absolute(), ROOT / 'local/storage-index',
                    args.artifact_parent_max_mib * 1024**2, child_max=args.max_artifact_mib * 1024**2)
                artifact_budget = prepaid_budget
            else:
                out.mkdir(parents=True, exist_ok=False)
                artifact_budget = pool.ArtifactBudget([out], args.max_artifact_mib * 1024 * 1024,
                                                       link_policy='count', retained=True)
            config = {'source': str(source), 'stock': str(stock), 'texture': str(texture), 'disc': str(disc),
                      'docker': commands['docker'], 'compiler': commands['i686-w64-mingw32-gcc'],
                      'container_name': 'slrr-parity-' + uuid.uuid4().hex, 'image_id': image['Id'],
                      'timeout': args.timeout, 'admission': lease.description,
                      'record_controls': args.record_controls or args.early_controls,
                      'early_controls': args.early_controls, 'rng_observer': args.rng_observer,
                      'input_observer': args.input_observer,
                      'native_clock_observer': args.native_clock_observer,
                      'ephemeral_prefix': args.ephemeral_prefix,
                      'guest_memory_bytes': 20 * 1024**3,
                      'prefix_seed': str(args.prefix_seed.resolve(strict=True)) if args.prefix_seed else None,
                      'prefix_seed_checks': str(args.prefix_seed_checks.resolve(strict=True)) if args.prefix_seed_checks else None,
                      'ordinary_exit': args.ordinary_exit, 'career_flow': args.career_flow,
                      'save_fixture': str(save_fixture) if save_fixture else None,
                      'artifact_budget': {'max_bytes': args.max_artifact_mib * 1024**2, 'roots': [str(out)],
                                          'link_policy': 'count', 'scope': 'sampled logical private output tree'},
                      'environment': {'image_id': image['Id'], 'architecture': image['Architecture'],
                                      'HODLL': 'wowbox64.dll', 'renderer': 'llvmpipe', 'LP_NUM_THREADS': 1,
                                      'display': 'private Xvfb 800x600x24', 'audio': 'private Pulse null-sink monitor'},
                      'producer_sha256': digest(__file__), 'observer_sha256': digest(observer),
                      'storage_sha256': digest(storage.__file__)}
            config['texture_sha256'] = digest(texture)
            config['tools'] = {key: {'path': path, 'sha256': digest(path)} for key, path in commands.items()}
            save(out / 'config.json', config)
            command = [str(observer), str(out / 'focus.jsonl'), sys.executable,
                       str(args.timeout + 120), str(Path(__file__).resolve()), '--worker', str(out / 'config.json')]
            save(out / 'execution.json', {'argv': command})
            result = {'schema': 1, 'passed': False}
            try:
                with (out / 'observer.stdout').open('wb') as stdout, (out / 'observer.stderr').open('wb') as stderr:
                    observed = lease.run_observer(command, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
                                                  timeout=args.timeout + 135, artifact_budget=artifact_budget)
                try:
                    record = json.loads((out / 'worker-result.json').read_text())
                    if not isinstance(record, dict) or record.get('schema') != 1 or type(record.get('passed')) is not bool:
                        raise ValueError('invalid worker verdict')
                    result.update(record)
                except (OSError, ValueError) as error:
                    result.update(passed=False, worker_error=str(error))
                result['observer_exit'] = observed.returncode
                events = focus.read_events(out / 'focus.jsonl')
                result['focus'] = focus.classify(events)
                owners = [e['root'] for e in events if e.get('kind') == 'child_started']
                result['execution_owner'] = owners[0] if len(owners) == 1 else None
                result['passed'] &= observed.returncode == 0 and result['focus']['status'] == 'no_owned_activation_observed'
            except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError, pool.ResourcePressure,
                    pool.ArtifactLimit) as error:
                result.update(passed=False, observer_error=str(error))
            finally:
                # Independent cleanup also runs if the observed worker failed.
                try:
                    subprocess.run([commands['docker'], 'rm', '-f', config['container_name']],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
                    remaining = subprocess.run([commands['docker'], 'ps', '-aq', '--filter',
                                                'name=^/' + config['container_name'] + '$'],
                                               check=True, capture_output=True, timeout=10)
                    result['outer_container_removed'] = remaining.stdout.strip() == b''
                    if not result['outer_container_removed']:
                        result['passed'] = False
                except (OSError, subprocess.SubprocessError) as error:
                    result.update(passed=False, outer_cleanup_error=str(error))
                # A signal can terminate Python before the worker's finally
                # block. Keep the missing verdict distinct and audit sources
                # independently rather than losing all failure evidence.
                if 'source_preserved' not in result and (out / 'source-before.json').is_file():
                    try:
                        after = manifest(source)
                        save(out / 'source-after-parent.json', after)
                        result['source_preserved'] = json.loads((out / 'source-before.json').read_text()) == after
                    except (OSError, ValueError) as error:
                        result['source_audit_error'] = str(error)
                if config.get('prefix_seed') and (out / 'prefix-seed-before.json').is_file():
                    try:
                        preserved = audit_prefix_seed(config['prefix_seed'], out, 'prefix-seed-after-parent.json')
                        result.setdefault('prefix_seed', {})['outer_preserved'] = preserved
                        result['passed'] &= preserved
                    except (OSError, ValueError) as error:
                        result.update(passed=False, prefix_seed_audit_error=str(error))
            result['producer_unchanged'] = digest(__file__) == config['producer_sha256']
            result['execution_pressure'] = lease.execution_pressure
            result['execution_artifacts'] = lease.execution_artifacts
            result['passed'] &= result['producer_unchanged']
            result['storage_source_unchanged'] = digest(storage.__file__) == config['storage_sha256']
            result['passed'] &= result['storage_source_unchanged']
            # Only a completed, drained capture is eligible. Failure evidence
            # stays untouched and remains charged by the cumulative allowance.
            if not prepaid_budget and result['passed'] and result.get('outer_container_removed') and result.get('observer_exit') == 0:
                artifact_budget.close()
                try:
                    result['storage_compaction'] = storage.compact([out], apply=True, seconds=120)
                except BlockingIOError as error:
                    result['storage_compaction'] = {'complete': False, 'status': 'deferred_busy', 'error': str(error)}
                except (OSError, ValueError, TimeoutError) as error:
                    result['storage_compaction'] = {'complete': False, 'error': str(error)}
                    result['passed'] = False
            if not result['passed']:
                result['status'] = 'incomplete'
            if prepaid_budget:
                result['artifact_finalization'] = 'pending'
            save(out / 'result.json', result)
            if not prepaid_budget:
                print(json.dumps({'output': str(out), 'passed': result['passed'],
                                  'status': result.get('status', 'incomplete'), 'error': result.get('error')}))
            return 0 if result['passed'] else 1
    except (OSError, ValueError, KeyError, subprocess.SubprocessError, pool.ResourcePressure, pool.ArtifactLimit) as error:
        parser.exit(2, f'run_stock: {error}\n')
    finally:
        if prepaid_budget:
            try:
                prepaid_budget.check('final')
                if result is not None:
                    result['artifact_finalization'] = 'verified'
                    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
                save(out / 'prepaid-audit.json', prepaid_budget.budget.report())
                prepaid_budget.check('final')
                if result is not None:
                    print(json.dumps({'output': str(out), 'passed': result['passed'],
                                      'artifact_finalization': result['artifact_finalization']}))
            except (OSError, ValueError, pool.ArtifactLimit) as error:
                failed = dict(result or {}, passed=False, artifact_finalization='failed', artifact_error=str(error))
                bound = prepaid_budget.budget.child_identity
                if out.is_dir() and not out.is_symlink() and (out.stat().st_dev, out.stat().st_ino) == bound:
                    (out / 'result.json').write_text(json.dumps(failed, indent=2) + '\n')
                print(f'run_stock: prepaid finalization: {error}', file=sys.stderr)
                raise SystemExit(1) from error
            finally:
                prepaid_budget.close()
        elif artifact_budget:
            artifact_budget.close()


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--worker':
        raise SystemExit(worker(sys.argv[2]))
    raise SystemExit(main())
