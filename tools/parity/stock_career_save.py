#!/usr/bin/env python3
"""Inspect retained ordinary Stock Career persistence, with exact divergences.

This never launches the game or changes a capture. A current-time re-save is
reported exactly, not masked or granted a comparator tolerance. This tool checks
functional profile persistence; deterministic clock/replay remains unqualified.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import struct

HERE = Path(__file__).resolve().parent


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


native = module('stock_career_sdat', HERE / 'career_save.py')
stock = module('stock_career_capture', HERE / 'run_stock.py')


def inspect_capture(capture, mode):
    capture = Path(capture).resolve(strict=True)
    read = lambda name: native.read_json(capture / name)
    config, result, flow = read('config.json'), read('result.json'), read('flow.json')
    if (config.get('career_flow') != mode or config.get('ordinary_exit') is not True
            or any(config.get(key) for key in ('record_controls', 'early_controls', 'rng_observer'))
            or result.get('passed') is not True or result.get('container_exit') != 0
            or any(result.get(key) is not True for key in ('container_removed', 'outer_container_removed',
                                                         'source_preserved', 'disc_preserved', 'stock_preserved', 'texture_preserved'))):
        raise ValueError('incomplete ordinary private Stock capture')
    if config['producer_sha256'] != native.digest(capture / 'instrumentation-source/runner.py'):
        raise ValueError('producer identity differs from frozen source')
    frozen_before = read('instrumentation-source.json')
    frozen_now = stock.manifest(capture / 'instrumentation-source')
    derived = {name: value for name, value in frozen_now.items() if name not in frozen_before}
    if (any(frozen_now.get(name) != value for name, value in frozen_before.items()) or
            any(not name.startswith('__pycache__/') or not name.endswith('.pyc') for name in derived)):
        raise ValueError('instrumentation source was changed')
    ephemeral = config.get('ephemeral_prefix', False)
    seed = Path(config['prefix_seed']).resolve(strict=True) if ephemeral else None
    if not stock.isolated(read('container-inspect.json'), capture, Path(config['disc']), config['image_id'],
                          seed, memory_limit_bytes=config.get('guest_memory_bytes', 3 * 1024**3)):
        raise ValueError('actual guest isolation failed reinspection')
    prefix_audit = None
    if ephemeral:
        before_prefix = stock.prefix_archive.parse_manifest(capture / 'instrumentation-source/expected-prefix.tsv')
        if read('prefix-seed-before.json') != before_prefix or stock.prefix_manifest(seed) != before_prefix:
            raise ValueError('retained prefix seed differs from capture identity')
        prepared, finished, final_prefix = stock.prefix_archive.validate_capture(
            capture, before_prefix, write_reports=False)
        if read('prefix-after.json') != final_prefix or result.get('prefix_archive') != {
                'prepared': prepared, 'finished': finished}:
            raise ValueError('retained prefix report contradicts actual delta')
        stock.audit_runtime_modules(capture, '/privateprefix', final_prefix)
        prefix_audit = {'seed': str(seed), 'seed_manifest_sha256': native.digest(capture / 'prefix-seed-before.json'),
                        'prepared': prepared, 'finished': finished,
                        'final_manifest_sha256': native.digest(capture / 'prefix-after.json')}

    shutdown = stock.ordinary_exit_evidence(capture, flow)
    if not shutdown['passed']:
        raise ValueError('ordinary process-handle exit evidence failed')
    scenario = stock.career_scenario_evidence(flow, (capture / 'scenario-input.log').read_text(), mode)
    if not scenario['complete']:
        raise ValueError('Career UI/input frontier evidence failed')
    observed_focus = stock.focus.classify(stock.focus.read_events(capture / 'focus.jsonl'))
    if observed_focus['status'] != 'no_owned_activation_observed' or observed_focus.get('child_exit') != 0:
        raise ValueError('raw focus evidence was incomplete')
    if result.get('focus') != observed_focus:
        raise ValueError('producer focus summary contradicts raw records')
    pcm = stock.audio.summarize(capture / 'stock.wav', 's16le', 2, 48000, 'wav')
    if pcm['frames'] <= 0 or pcm['nonzero_samples'] <= 0:
        raise ValueError('empty or silent private mix')
    if any(result.get('audio', {}).get(key) != value for key, value in pcm.items()):
        raise ValueError('producer audio summary contradicts actual PCM')
    saved = capture / 'game/save/carrer/SaveProof-1/main'
    native.regular_file(saved, 16 * 1024 * 1024, capture)
    if (native.digest(saved) != result.get('career_save', {}).get('sha256') or
            saved.stat().st_size != result.get('career_save', {}).get('bytes')):
        raise ValueError('saved Career bytes contradict producer identity')
    sink = (capture / 'audio-sinks.txt').read_text()
    if sink.count('Name: stock\n') != 1 or sink.count('Driver: module-null-sink.c') != 1:
        raise ValueError('private null sink evidence was not observed')
    before, after = read('source-before.json'), read('source-after.json')
    if before != after or after != stock.manifest(Path(config['source'])):
        raise ValueError('actual full source tree differs from retained identity')
    if stock.digest(Path(config['stock'])) != stock.STOCK_SHA or stock.digest(capture / 'game/StreetLegal_Redline.exe') != stock.STOCK_SHA:
        raise ValueError('authoritative or private executable changed')
    if read('disc-before.json') != stock.manifest(Path(config['disc'])) or stock.digest(Path(config['texture'])) != config['texture_sha256']:
        raise ValueError('disc/texture source identity changed')
    images = {}
    rows = [flow['before'], flow['after'], flow['career']['garage']]
    rows += ([flow['career']['character_before'], flow['career']['character_after']]
             if mode == 'create' else [flow['career']['load_list'], flow['career']['load_selected']])
    for row in rows:
        path = capture / row['image']
        native.regular_file(path, 16 * 1024 * 1024, capture)
        with path.open('rb') as stream:
            header = stream.read(24)
        if header[:8] != b'\x89PNG\r\n\x1a\n' or struct.unpack_from('>II', header, 16) != (800, 600):
            raise ValueError('missing or invalid guest frame')
        images[path.name] = native.digest(path)
        if 'career' in row:
            ocr = read(path.stem + '-ocr/career-ocr.json')
            if ocr['labels'] != row['career']:
                raise ValueError('Career row differs from retained raw OCR')
    if mode == 'load':
        fixture = Path(config['save_fixture'])
        if result.get('save_fixture_preserved') is not True or read('save-fixture-before.json') != stock.manifest(fixture):
            raise ValueError('source save fixture changed')
    else:
        if config.get('save_fixture') or any(name.startswith('save/carrer/') for name in read('private-initial.json')):
            raise ValueError('creation did not begin without a Career fixture')
    return {'capture': str(capture), 'mode': mode, 'source_files': len(after), 'scenario': scenario,
            'shutdown': shutdown, 'container_exit': result['container_exit'],
            'focus': observed_focus, 'audio': pcm, 'images': images,
            'config_sha256': native.digest(capture / 'config.json'),
            'source_before_sha256': native.digest(capture / 'source-before.json'),
            'source_sha256': native.digest(capture / 'source-after.json'),
            'image_id': config['image_id'], 'stock_sha256': stock.STOCK_SHA,
            'derived_bytecode': derived, 'prefix_archive': prefix_audit,
            'limits': ['external frontiers and profile persistence, not internal callback attribution',
                       'mouse holds can trigger repeated UI callbacks; planned clicks are not consumed-state tapes']}


def compare_careers(left, right):
    a, b = native.career_prefix(left), native.career_prefix(right)
    keys = ('name', 'portrait_word', 'money', 'prestige_bits', 'racer_index', 'flags', 'packs')
    if any(a[key] != b[key] for key in keys):
        raise ValueError('profile, assets or pack bindings did not persist exactly')
    if a['name'] != 'SaveProof' or a['money'] != 20000 or not a['portrait_word']:
        raise ValueError('unexpected saved profile')
    if dict(a['packs']).get(a['portrait_word'] >> 16) != 'frontend.rpk':
        raise ValueError('saved portrait is not bound to the observed frontend pack')
    differences = [i for i, (x, y) in enumerate(zip(left, right)) if x != y]
    if len(left) != len(right):
        differences.append(min(len(left), len(right)))
    # Shipped GameLogic.save tree94 lines801..810 ends with getTime(), day,
    # seven reserved ints; load tree96 lines937..946 reads the same tail.
    end_a = 8 + native.sdat.parse(left)[2]
    end_b = 8 + native.sdat.parse(right)[2]
    if end_a != end_b or end_a < 40:
        raise ValueError('Career payload extent changed')
    clock = end_a - 36
    before_time, after_time = struct.unpack_from('<f', left, clock)[0], struct.unpack_from('<f', right, clock)[0]
    if not all(math.isfinite(v) and 0 <= v < 86400 for v in (before_time, after_time)):
        raise ValueError('invalid observed saved time')
    outside_clock = [i for i in differences if not clock <= i < clock + 4]
    if outside_clock:
        raise ValueError('unexplained exact re-save divergence at byte ' + str(outside_clock[0]))
    return {'profile': a, 'left_sha256': hashlib.sha256(left).hexdigest(),
            'right_sha256': hashlib.sha256(right).hexdigest(), 'bytes': len(left),
            'exact_resave': left == right, 'changed_byte_offsets': differences,
            'first_divergence': differences[0] if differences else None,
            'serialized_current_time': {'offset': clock, 'before': before_time, 'after': after_time,
                                        'before_bits': left[clock:clock + 4].hex(), 'after_bits': right[clock:clock + 4].hex(),
                                        'scope': 'exact observed current-time words; no tolerance or replay-equivalence rule'},
            'all_other_file_bytes_exact': not outside_clock}


def audit(create, load, fixture):
    create, load, fixture = (Path(path).resolve(strict=True) for path in (create, load, fixture))
    made, restored = inspect_capture(create, 'create'), inspect_capture(load, 'load')
    if made['stock_sha256'] != restored['stock_sha256'] or made['image_id'] != restored['image_id']:
        raise ValueError('different Stock/runtime identities')
    initial = native.read_json(fixture / 'manifest.json')
    if Path(initial['source']).resolve() != create / 'game/save':
        raise ValueError('fixture did not descend from actual creation')
    if (native.snapshot.manifest(fixture / 'save') != initial['files'] or
            native.snapshot.manifest(create / 'game/save') != initial['files']):
        raise ValueError('generated fixture lineage was changed')
    config = native.read_json(load / 'config.json')
    if Path(config['save_fixture']).resolve() != fixture / 'save':
        raise ValueError('ordinary load did not use the generated fixture')
    rel = Path('carrer/SaveProof-1/main')
    source, copied, final = create / 'game/save' / rel, fixture / 'save' / rel, load / 'game/save' / rel
    for path in (source, copied, final):
        native.regular_file(path, 16 * 1024 * 1024, path.parents[3])
    left, clone, right = source.read_bytes(), copied.read_bytes(), final.read_bytes()
    if left != clone:
        raise ValueError('snapshot changed actual Career bytes')
    loaded_initial = native.read_json(load / 'private-initial.json')
    if any(loaded_initial.get('save/' + name, {}).get('sha256') != value
           for name, value in initial['files'].items()):
        raise ValueError('private load input was not the actual generated Career')
    comparison = compare_careers(left, right)
    final_tree = native.snapshot.manifest(load / 'game/save')
    changed = [name for name in sorted(initial['files'].keys() | final_tree.keys())
               if initial['files'].get(name) != final_tree.get(name)]
    if initial['files'].keys() != final_tree.keys() or any(name != rel.as_posix() for name in changed):
        raise ValueError('unexplained divergence outside the Career file')
    return {'schema': 1, 'status': 'ordinary_stock_career_profile_persistence_observed',
            'created': made, 'loaded': restored, 'comparison': comparison,
            'save_tree': {'files': len(final_tree), 'changed_files': changed,
                          'all_other_save_files_exact': True},
            'clock_authority': {'script': 'sl/Scripts/game/GameLogic.class',
                                'sha256': stock.manifest(Path(config['source']))['sl/Scripts/game/GameLogic.class']['sha256'],
                                'save_tree': 94, 'save_lines': '801..810', 'load_tree': 96, 'load_lines': '937..946'},
            'limits': ['not byte-identical when the exact current-time word changes; first divergence remains explicit',
                       'no numeric tolerance, deterministic repeatability, Port equivalence or paired proof is established',
                       'load-selected frame can already show loading before the scheduled lower-button action; cause attribution remains bounded',
                       'synthetic CD metadata remains disclosed; authentic CD validation was excluded by the user']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('create', 'load', 'fixture', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        output = args.out.resolve()
        config = native.read_json(args.create.resolve() / 'config.json')
        protected = (args.create, args.load, args.fixture, HERE.parents[1] / 'ref',
                     Path(config['source']), Path(config['disc']))
        if args.out.exists() or args.out.is_symlink() or any(p.resolve() in output.parents for p in protected):
            raise ValueError('new output must be outside input evidence and ref/')
        value = audit(args.create, args.load, args.fixture)
        with args.out.open('x') as stream:
            json.dump(value, stream, indent=2, allow_nan=False)
            stream.write('\n')
        print(value['status'])
    except (OSError, ValueError, KeyError, TypeError, struct.error, native.sdat.Bad) as error:
        parser.exit(2, f'stock_career_save: {error}\n')


if __name__ == '__main__':
    main()
