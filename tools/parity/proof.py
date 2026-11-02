#!/usr/bin/env python3
"""Evaluate bounded Parity evidence; missing evidence never passes.

Records are run-local JSON manifests plus hashed JSONL transition tapes. This
module evaluates supplied evidence; it does not create Stock observations or
claim coverage outside a named contract. See docs/specs/full-port-1.0.0.md for the full goal.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import uuid
import importlib.util


def sibling(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


focus_module = sibling('focus_evidence')
audio_module = sibling('audio_evidence')


class InvalidEvidence(ValueError):
    pass


def read_json(text):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise InvalidEvidence(f'duplicate JSON key: {key}')
            result[key] = value
        return result
    def constant(value):
        raise InvalidEvidence(f'nonfinite JSON number: {value}')
    return json.loads(text, object_pairs_hook=pairs, parse_constant=constant)


def require(condition, message):
    if not condition:
        raise InvalidEvidence(message)


def sha(value):
    return hashlib.sha256(value).hexdigest()


def canonical(value):
    # Preserve JSON types and IEEE signed zero; Python's 1 == True is unsuitable.
    if isinstance(value, float):
        require(math.isfinite(value), 'nonfinite observation')
        return ['float64', struct.pack('>d', value).hex()]
    if isinstance(value, dict):
        return ['object', [[key, canonical(value[key])] for key in sorted(value)]]
    if isinstance(value, list):
        return ['array', [canonical(item) for item in value]]
    return [type(value).__name__, value]


def encoded(value):
    return json.dumps(canonical(value), separators=(',', ':'), ensure_ascii=True)


def digest(value):
    require(isinstance(value, str) and len(value) == 64 and
            all(c in '0123456789abcdef' for c in value), 'invalid SHA-256')
    return value


def artifact_path(root, descriptor):
    require(isinstance(descriptor, dict), 'missing artifact descriptor')
    relative = descriptor.get('path')
    require(isinstance(relative, str) and relative, 'missing artifact path')
    path = Path(relative)
    require(not path.is_absolute() and '..' not in path.parts,
            'artifact must be run-local')
    resolved = (root / path).resolve(strict=True)
    require(root in resolved.parents, 'artifact escapes run directory')
    require(resolved.is_file(), 'artifact must be a regular file')
    # Refuse all symlinks, including aliases that still resolve inside the run.
    require(not any((root / Path(*path.parts[:n])).is_symlink()
                    for n in range(1, len(path.parts) + 1)), 'symlink artifact')
    hashed = hashlib.sha256()
    with resolved.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            hashed.update(block)
    require(hashed.hexdigest() == digest(descriptor.get('sha256')), 'artifact hash mismatch')
    return resolved


def artifact(root, descriptor):
    data = artifact_path(root, descriptor).read_bytes()
    require(sha(data) == descriptor['sha256'], 'artifact changed after verification')
    return data


def validate_capability(name, data, root, manifest, contract):
    if name == 'focus_preserved':
        events = [read_json(line) for line in data.decode('utf-8').splitlines() if line.strip()]
        result = focus_module.classify(events)
        require(result['status'] == 'no_owned_activation_observed', 'focus evidence is not complete')
        require(result['child_exit'] == 0, 'observed execution failed')
        owner = focus_module.key(manifest.get('execution_owner'))
        roots = [e.get('root') for e in events if e.get('kind') == 'child_started']
        require(owner is not None and len(roots) == 1 and owner == focus_module.key(roots[0]),
                'focus evidence belongs to another execution')
    elif name == 'private_audio':
        report = read_json(data.decode('utf-8'))
        require(type(report.get('schema')) is int and report['schema'] == 1 and
                report.get('complete') is True and report.get('physical_output') is False,
                'incomplete audio isolation evidence')
        stages = {'sdl-disk': 'physical_final_mix', 'pulse-null-monitor': 'sink_monitor'}
        require(report.get('route') in stages and report.get('stage') == stages[report['route']],
                'unverified private audio route')
        capture = artifact_path(root, report)
        actual = audio_module.summarize(capture, report['encoding'], report['channels'],
                                        report['rate'], report.get('container', 'raw'))
        require(type(report.get('frames')) is int and type(report.get('nonzero_samples')) is int,
                'audio counts must be integers')
        for metric in ('frames', 'nonzero_samples', 'sha256'):
            require(report.get(metric) == actual[metric], 'audio metric differs from captured PCM')
        minimum = contract.get('minimum_audio_nonzero_samples', 0)
        require(type(minimum) is int and minimum >= 0, 'invalid audio signal requirement')
        require(actual['nonzero_samples'] >= minimum, 'required game audio signal is absent')
    else:
        raise InvalidEvidence('unsupported capability verifier: ' + name)


def load_run(path, contract):
    path = Path(path).resolve(strict=True)
    manifest = read_json(path.read_text())
    require(isinstance(manifest, dict) and type(manifest.get('schema')) is int and manifest['schema'] == 1,
            'unsupported run schema')
    encoded(manifest)
    require(manifest.get('contract') == contract['id'], 'wrong contract')
    require(manifest.get('implementation') in ('stock', 'port'), 'unknown implementation')
    require(str(uuid.UUID(manifest.get('run_id', ''))) == manifest['run_id'], 'invalid run id')
    identity = manifest.get('identity', {})
    for key in ('executable', 'game', 'fixture', 'scenario'):
        digest(identity.get(key))
    require({key: identity[key] for key in ('game', 'fixture', 'scenario')} == contract['inputs'],
            'run inputs do not match contract')
    if manifest['implementation'] == 'stock':
        require(identity['executable'] == contract['stock_executable'], 'wrong Stock Executable')
    require(isinstance(identity.get('environment'), dict) and identity['environment'],
            'missing execution environment')
    require(manifest.get('complete') is True and type(manifest.get('dropped_records')) is int and
            manifest['dropped_records'] == 0,
            'incomplete or dropped run records')
    require(type(manifest.get('exit_code')) is int and manifest['exit_code'] == 0 and manifest.get('timed_out') is False,
            'unsuccessful run')
    require(manifest.get('source_preserved') is True, 'unverified source preservation')
    # Each capability names its own retained observation, not a launcher intent.
    observations = manifest.get('capabilities', {})
    for capability in contract['capabilities']:
        evidence = observations.get(capability, {})
        require(evidence.get('verified') is True, f'unverified capability: {capability}')
        validate_capability(capability, artifact(path.parent, evidence.get('artifact')),
                            path.parent, manifest, contract)
    coverage = manifest.get('coverage', [])
    require(isinstance(coverage, list) and len(coverage) == len(set(coverage)),
            'invalid coverage declarations')
    require(set(coverage) == set(contract['observations']), 'incomplete observation coverage')
    tape = artifact(path.parent, manifest.get('tape'))
    frames = [read_json(line) for line in tape.decode('utf-8').splitlines() if line.strip()]
    require(bool(frames), 'empty transition tape')
    previous_boundary = None
    for index, frame in enumerate(frames):
        require(type(frame.get('sequence')) is int and frame['sequence'] == index, 'missing or reordered transition')
        boundary = frame.get('boundary')
        require(isinstance(boundary, str) and boundary and boundary != previous_boundary,
                'missing or repeated transition boundary')
        previous_boundary = boundary
        require(isinstance(frame.get('stimulus'), dict) and frame['stimulus'],
                'missing consumed stimulus')
        state = frame.get('observations')
        require(isinstance(state, dict) and set(state) == set(contract['observations']),
                'incomplete transition observations')
        encoded(frame)  # rejects overflow-to-infinity as well as explicit NaN
    require(frames[-1]['boundary'] == contract['terminal_boundary'],
            'scenario stopped before terminal boundary')
    require([frame['boundary'] for frame in frames] == contract['boundaries'],
            'missing or unexpected observation boundary')
    stimuli = [{'boundary': f['boundary'], 'stimulus': f['stimulus']} for f in frames]
    require(sha(encoded(stimuli).encode()) == contract['stimulus_sha256'],
            'consumed stimulus differs from contract')
    return {'manifest': manifest, 'frames': frames, 'record': str(path),
            'manifest_sha256': sha(path.read_bytes())}


def validate_contract(contract):
    encoded(contract)
    require(type(contract.get('schema')) is int and contract['schema'] == 1, 'unsupported contract schema')
    inputs = contract.get('inputs', {})
    require(set(inputs) == {'game', 'fixture', 'scenario'}, 'missing contract input identities')
    for value in inputs.values():
        digest(value)
    digest(contract.get('stock_executable'))
    for key in ('id', 'terminal_boundary', 'authority'):
        require(isinstance(contract.get(key), str) and contract[key], f'missing {key}')
    for key in ('observations', 'capabilities'):
        values = contract.get(key)
        require(isinstance(values, list) and values and
                all(isinstance(x, str) and x for x in values) and
                len(values) == len(set(values)), f'invalid {key}')
    boundaries = contract.get('boundaries')
    require(isinstance(boundaries, list) and boundaries and
            all(isinstance(b, str) and b for b in boundaries) and
            len(set(boundaries)) == len(boundaries) and
            boundaries[-1] == contract['terminal_boundary'], 'invalid boundary schedule')
    digest(contract.get('stimulus_sha256'))
    require('focus_preserved' in contract['capabilities'] and
            'private_audio' in contract['capabilities'], 'missing user-interference contract')
    rules = contract.get('tolerances', {})
    require(isinstance(rules, dict), 'invalid tolerance policy')
    for field, rule in rules.items():
        require(field in contract['observations'], 'tolerance names undeclared observation')
        require(isinstance(rule, dict) and isinstance(rule.get('evidence'), str) and
                rule['evidence'] and rule.get('kind') == 'numeric',
                'tolerance requires numeric field and evidence')
        for key in ('absolute', 'relative'):
            value = rule.get(key)
            require(type(value) in (int, float) and math.isfinite(value) and value >= 0,
                    'invalid numerical tolerance')


def first_difference(left, right, tolerances):
    if len(left) != len(right):
        return {'reason': 'transition_count', 'left': len(left), 'right': len(right)}
    for a, b in zip(left, right):
        for key in ('sequence', 'boundary', 'stimulus'):
            if encoded(a[key]) != encoded(b[key]):
                return {'sequence': a['sequence'], 'field': key, 'reason': 'stimulus_or_schedule',
                        'left': a[key], 'right': b[key]}
        for field, value in a['observations'].items():
            other = b['observations'][field]
            if encoded(value) == encoded(other):
                continue
            rule = tolerances.get(field)
            if rule and type(value) is float and type(other) is float:
                if abs(value - other) <= max(rule['absolute'],
                                             rule['relative'] * max(abs(value), abs(other))):
                    continue
            return {'sequence': a['sequence'], 'boundary': a['boundary'], 'field': field,
                    'reason': 'observation', 'left': value, 'right': other}
    return None


def evaluate(contract, paths, mode='parity'):
    """Require two or more independent Runs per side before comparing sides."""
    try:
        validate_contract(contract)
        require(mode in ('parity', 'repeatability'), 'unknown comparison mode')
        runs = [load_run(path, contract) for path in paths]
        require(len({r['record'] for r in runs}) == len(runs), 'duplicate run record')
        require(len({r['manifest']['run_id'] for r in runs}) == len(runs), 'duplicate run id')
        require(len({focus_module.key(r['manifest'].get('execution_owner')) for r in runs}) == len(runs),
                'reused execution identity')
        groups = {}
        for run in runs:
            groups.setdefault(run['manifest']['implementation'], []).append(run)
        require(set(groups) == {'stock', 'port'} if mode == 'parity' else len(groups) == 1,
                'wrong implementation set for comparison mode')
        shared = None
        for side, samples in groups.items():
            require(len(samples) >= 2, f'missing repeated {side} observations')
            reference = samples[0]
            identity = reference['manifest']['identity']
            common = {k: identity[k] for k in ('game', 'fixture', 'scenario')}
            require(shared is None or shared == common, 'different game/fixture/scenario')
            shared = common
            for sample in samples[1:]:
                require(sample['manifest']['identity'] == identity,
                        f'different {side} executable/environment/fixture')
                difference = first_difference(reference['frames'], sample['frames'], {})
                if difference:
                    return {'status': 'inconclusive', 'reason': 'not_repeatable',
                            'implementation': side, 'difference': difference}
        if mode == 'repeatability':
            return {'status': 'repeatable', 'contract': contract['id'],
                    'scope': 'self-comparison only; no Stock/Port Parity claim'}
        stock, port = groups['stock'][0], groups['port'][0]
        difference = first_difference(stock['frames'], port['frames'], contract.get('tolerances', {}))
        if difference:
            return {'status': 'diverged' if difference['reason'] == 'observation' else 'inconclusive',
                    'contract': contract['id'], 'difference': difference}
        return {'status': 'matched', 'contract': contract['id'],
                'observations': contract['observations'],
                'tolerances': contract.get('tolerances', {}),
                'records': [{'path': r['record'], 'sha256': r['manifest_sha256']} for r in runs],
                'scope': 'named contract and supplied observations only'}
    except (InvalidEvidence, OSError, ValueError, TypeError, KeyError, AttributeError,
            UnicodeError, OverflowError) as error:
        return {'status': 'inconclusive', 'reason': str(error)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('contract', type=Path)
    parser.add_argument('runs', type=Path, nargs='+')
    parser.add_argument('--mode', choices=('parity', 'repeatability'), default='parity')
    args = parser.parse_args()
    try:
        result = evaluate(read_json(args.contract.read_text()), args.runs, args.mode)
    except (OSError, ValueError) as error:
        result = {'status': 'inconclusive', 'reason': str(error)}
    print(json.dumps(result, indent=2, allow_nan=False))
    return 0 if result['status'] in ('matched', 'repeatable') else 1


if __name__ == '__main__':
    raise SystemExit(main())
