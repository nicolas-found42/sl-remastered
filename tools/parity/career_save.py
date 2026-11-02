#!/usr/bin/env python3
"""Audit a real native Career create/save/ordinary-load round trip.

The supplied captures must come from run_native.py with normal intros and the
ordinary-career scripts. This does not launch a game, establish replay, verify
all saved fields, or compare against Stock. It rechecks retained raw artifacts;
producer status booleans alone cannot pass it.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import stat
import struct

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


snapshot = module('career_snapshot', HERE / 'save_snapshot.py')
audio = module('career_audio', HERE / 'audio_evidence.py')
focus = module('career_focus', HERE / 'focus_evidence.py')
sdat = module('career_sdat', ROOT / 'tools/sdat_census.py')
data = module('career_data', HERE / 'diagnostic-data.py')


def regular_file(path, maximum, boundary=None):
    if boundary is not None:
        current = boundary
        if current.is_symlink():
            raise ValueError('symlink capture directory')
        for part in path.relative_to(boundary).parts:
            current = current / part
            if current.is_symlink():
                raise ValueError('symlink in capture artifact path')
    size = path.lstat().st_size
    if not stat.S_ISREG(path.lstat().st_mode) or size > maximum:
        raise ValueError('unsafe or oversized artifact: ' + str(path))
    return size


def read_json(path):
    def unique(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate JSON key: ' + key)
            result[key] = value
        return result
    regular_file(path, 32 * 1024 * 1024)
    return json.loads(path.read_text(), object_pairs_hook=unique)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def career_prefix(raw):
    """Decode only the declared GameLogic.save/load prefix, not the whole Career.

    Installed GameLogic.class tree94 lines726..737 write ID, version, portrait,
    money, prestige, racer index, flags and name. ResourceRef's first value is its
    saved pack ordinal/local resource word. Keep that raw word diagnostic here;
    full resource rebinding is outside this prefix inspection.
    """
    if len(raw) > 16 * 1024 * 1024:
        raise ValueError('oversized Career')
    typ, flags, payload, _, packs = sdat.parse(raw)
    if typ != 2 or flags != 0 or payload < 32:
        raise ValueError('unsupported Career envelope')
    identifier, version, portrait, money, prestige, racer, state, length = struct.unpack_from('<IIIifiiI', raw, 8)
    if identifier != 0x87654321 or version != 21 or not 1 <= length <= 128 or 32 + length > payload:
        raise ValueError('unsupported Career prefix')
    name = raw[40:40 + length]
    if name[-1:] != b'\0' or b'\0' in name[:-1]:
        raise ValueError('invalid Career name terminator')
    return {'name': name[:-1].decode('ascii'), 'portrait_word': portrait,
            'money': money, 'prestige_bits': raw[24:28].hex(), 'racer_index': racer,
            'flags': state, 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest(),
            'packs': [[ordinal, path] for ordinal, path, _ in packs]}


def inspect_run(capture, mode):
    capture = Path(capture).resolve(strict=True)
    for name in ('metadata.json', 'result.json', 'script.txt', 'focus.jsonl',
                 'initial-save/manifest.json', 'game-data-manifest.json'):
        regular_file(capture / name, 32 * 1024 * 1024, capture)
    metadata = read_json(capture / 'metadata.json')
    status = read_json(capture / 'result.json')
    if (metadata.get('presentation') != 'background' or metadata.get('startup_movies') != 'normal'
            or type(status.get('exit_code')) is not int or type(status.get('observer_exit_code')) is not int
            or status.get('exit_code') != 0 or status.get('observer_exit_code') != 0
            or status.get('timed_out') is not False or status.get('probe_unchanged') is not True
            or status.get('source_save_unchanged') is not True):
        raise ValueError('capture did not complete the private normal-boot lane')
    roots = [p for p in (capture / 'run').iterdir() if p.is_dir() and not p.is_symlink()]
    if len(roots) != 1:
        raise ValueError('expected one physical Run directory')
    run = roots[0]
    for name in ('run.log', 'script.txt', 'audio-mix.raw.spec.jsonl'):
        regular_file(run / name, 64 * 1024 * 1024, capture)
    script = (capture / 'script.txt').read_bytes()
    expected = (ROOT / 'tools/scripts' / ('ordinary-career-' + mode + '.txt')).read_bytes()
    if script != expected or hashlib.sha256(script).hexdigest() != metadata.get('script_sha256'):
        raise ValueError('unexpected Career actions or script identity')
    if (run / 'script.txt').read_bytes() != script:
        raise ValueError('Run script differs from invocation')
    text = (run / 'run.log').read_text()
    required = ["text draw: 'SaveProof racing in Green Slip/20'",
                'name=EXIT GAME', 'name=YES', 'System.exit: ordinary frame-loop shutdown',
                'Config shutdown: stock finalizer completed save/game/options']
    positions = [text.find(value) for value in required]
    if any(pos < 0 for pos in positions) or positions != sorted(positions):
        raise ValueError('ordinary Garage/Exit/Yes/finalizer observations absent or reordered')
    if 'script quit at' in text or 'Invalid datafile saved:' in text:
        raise ValueError('forced quit or save-write failure in capture')
    if mode == 'load' and "text draw: 'SaveProof-1'" not in text:
        raise ValueError('normal LOAD CAREER filename was not observed')
    if (r'startup video data\fmv\Activision.avi finished' not in text or
            r'startup video data\fmv\Invictus.avi finished' not in text):
        raise ValueError('normal startup video was not observed')
    observed_focus = focus.classify(focus.read_events(capture / 'focus.jsonl'))
    if observed_focus['status'] != 'no_owned_activation_observed' or observed_focus.get('child_exit') != 0:
        raise ValueError('raw no-focus observation did not pass')
    observed_pcm = audio.native_capture(capture / 'run')
    if observed_pcm['frames'] <= 0 or observed_pcm['nonzero_samples'] <= 0:
        raise ValueError('empty or silent private mix')
    if observed_pcm != status.get('audio') or observed_focus != status.get('focus'):
        raise ValueError('producer focus/PCM summary contradicts raw artifacts')
    initial = read_json(capture / 'initial-save/manifest.json')
    if snapshot.manifest(capture / 'initial-save/save') != initial['files']:
        raise ValueError('retained initial save was changed')
    if snapshot.manifest(Path(initial['source'])) != initial['files']:
        raise ValueError('source save fixture was changed')
    manifest = read_json(capture / 'game-data-manifest.json')
    if (manifest.get('complete') is not True or
            manifest.get('game_root') != metadata.get('game') or
            digest(capture / 'game-data-manifest.json') != metadata.get('game_data', {}).get('sha256')):
        raise ValueError('source content identity is absent or changed')
    shots = ['garage.bmp', 'character-after.bmp'] if mode == 'create' else ['loaded-garage.bmp', 'load-list.bmp']
    shots += ['menu-before-exit.bmp', 'exit-confirmation.bmp']
    images = {}
    for name in shots:
        path = run / name
        size = regular_file(path, 64 * 1024 * 1024, capture)
        with path.open('rb') as stream:
            header = stream.read(54)
        if (len(header) != 54 or header[:2] != b'BM' or
                struct.unpack_from('<I', header, 2)[0] != size or
                struct.unpack_from('<I', header, 10)[0] >= size or
                struct.unpack_from('<ii', header, 18) != (800, 600)):
            raise ValueError('missing or unexpected rendered observation: ' + name)
        images[name] = {'sha256': digest(path), 'bytes': path.stat().st_size}
    return {'capture': str(capture), 'run': str(run), 'probe_sha256': metadata['probe_sha256'],
            'game_root': metadata['game'],
            'script_sha256': metadata['script_sha256'], 'focus': observed_focus, 'audio': observed_pcm,
            'initial_save': initial, 'images': images, 'log_sha256': digest(run / 'run.log')}


def audit(create, load, fixture, source_before):
    created, loaded = inspect_run(create, 'create'), inspect_run(load, 'load')
    if created['probe_sha256'] != loaded['probe_sha256']:
        raise ValueError('round trip used different probe builds')
    if created['initial_save']['files']:
        raise ValueError('creation did not start from an empty save fixture')
    relative = Path('carrer/SaveProof-1/main')
    create_save = Path(create).resolve() / 'data/save'
    fixture = Path(fixture).resolve(strict=True)
    if Path(loaded['initial_save']['source']).resolve() != fixture / 'save':
        raise ValueError('load invocation did not use the generated fixture')
    retained = read_json(fixture / 'manifest.json')
    if Path(retained['source']).resolve() != create_save:
        raise ValueError('fixture does not descend from the creation run')
    sources = [create_save / relative, fixture / 'save' / relative,
               Path(load).resolve() / 'initial-save/save' / relative,
               Path(load).resolve() / 'data/save' / relative]
    for path in sources:
        regular_file(path, 16 * 1024 * 1024, path.parents[3])
    records = [career_prefix(path.read_bytes()) for path in sources]
    if any(row != records[0] for row in records):
        raise ValueError('Career did not round trip byte-identically')
    if (records[0]['name'] != 'SaveProof' or records[0]['money'] != 20000 or
            records[0]['portrait_word'] != 0x100db or records[0]['racer_index'] != 0 or records[0]['flags'] != 0):
        raise ValueError('unexpected created Career identity or balance')
    if (snapshot.manifest(create_save) != retained['files'] or
            snapshot.manifest(fixture / 'save') != retained['files'] or
            snapshot.manifest(Path(load).resolve() / 'data/save') != retained['files'] or
            loaded['initial_save']['files'] != retained['files']):
        raise ValueError('fixture lineage differs from retained save manifest')
    before = read_json(Path(source_before).resolve(strict=True))
    source = Path(created['game_root']).resolve(strict=True)
    if (created['game_root'] != loaded['game_root'] or before.get('game_root') != str(source)
            or before.get('complete') is not True or before.get('exclusions')):
        raise ValueError('invalid or mismatched complete source baseline')
    current = data.collect(source, exclude_runtime=False)
    if not current['complete'] or current['exclusions'] or current['files'] != before['files']:
        raise ValueError('actual source tree differs from complete retained baseline')
    return {'schema': 1, 'status': 'native_ordinary_career_roundtrip_observed',
            'created': created, 'loaded': loaded, 'career': records[0],
            'file_lineage': [str(path) for path in sources],
            'source_preservation': {'baseline': str(Path(source_before).resolve()),
                                    'baseline_sha256': digest(Path(source_before)),
                                    'root': str(source), 'files': len(current['files']),
                                    'current_files_sha256': hashlib.sha256(json.dumps(current['files'], sort_keys=True).encode()).hexdigest(),
                                    'scope': 'current complete tree equals retained baseline; not continuous write monitoring'},
            'limits': ['functional native evidence only; no Stock comparison or replay',
                       'prefix decoder does not validate every saved field or resource rebinding',
                       'focus notification/poll coverage has the observer limits retained above',
                       'screenshots are retained for independent inspection; no pixel equivalence claim']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--create', type=Path, required=True)
    parser.add_argument('--load', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--source-before-manifest', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        output = args.out.resolve()
        protected = [ROOT / 'ref', args.create.resolve(), args.load.resolve(), args.fixture.resolve()]
        metadata = read_json(args.create.resolve() / 'metadata.json')
        initial = read_json(args.create.resolve() / 'initial-save/manifest.json')
        protected += [Path(metadata['game']).resolve(), Path(metadata['reference_root']).resolve(),
                      Path(initial['source']).resolve()]
        if any(output == p or p in output.parents for p in protected):
            raise ValueError('output overlaps Reference Folder or retained input evidence')
        if args.out.exists() or args.out.is_symlink():
            raise ValueError('output must not exist')
        value = audit(args.create, args.load, args.fixture, args.source_before_manifest)
        with args.out.open('x') as stream:
            json.dump(value, stream, indent=2, allow_nan=False)
            stream.write('\n')
        print(value['status'])
    except (OSError, ValueError, KeyError, TypeError, struct.error, sdat.Bad) as error:
        parser.exit(2, f'career_save: {error}\n')


if __name__ == '__main__':
    main()
