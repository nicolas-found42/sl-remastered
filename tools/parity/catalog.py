#!/usr/bin/env python3
"""Inventory content and bind bounded Stock frontiers without promoting Parity."""
import argparse
import collections
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import stat
import struct

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
spec = importlib.util.spec_from_file_location('diagnostic_data', HERE / 'diagnostic-data.py')
data = importlib.util.module_from_spec(spec)
spec.loader.exec_module(data)
tree_spec = importlib.util.spec_from_file_location('tufa_dump', REPO / 'tools/tufa_dump.py')
tree_data = importlib.util.module_from_spec(tree_spec)
tree_spec.loader.exec_module(tree_data)
branch_spec = importlib.util.spec_from_file_location('script_branches', HERE / 'script_branches.py')
branch_data = importlib.util.module_from_spec(branch_spec)
branch_spec.loader.exec_module(branch_data)


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def relative_path(value):
    return (isinstance(value, str) and bool(value) and '\\' not in value and
            not Path(value).is_absolute() and '..' not in Path(value).parts and
            Path(value).as_posix() == value and value != '.')


def sha256(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


def same_json(left, right):
    """JSON identity preserves booleans, integers and floating-point value types."""
    return json.dumps(left, sort_keys=True, allow_nan=False) == json.dumps(right, sort_keys=True, allow_nan=False)


STOCK_FRONTIER_TARGETS = {'before': ('B-boot-menu', 'normal', 'menu'),
                          'name_dialog': ('B-dialog-focus', 'show', 'dialog'),
                          'after': ('B-dialog-focus', 'cancel', 'dialog')}


def parse_json(raw):
    def pairs(values):
        result = {}
        for key, value in values:
            if key in result:
                raise ValueError(f'duplicate key: {key}')
            result[key] = value
        return result
    def invalid(value):
        raise ValueError(f'nonfinite value: {value}')
    return json.loads(raw, object_pairs_hook=pairs, parse_constant=invalid)


def read_json(path):
    return parse_json(Path(path).read_text())


def names(values):
    return (isinstance(values, list) and bool(values) and
            all(isinstance(x, str) and x.strip() for x in values) and len(set(values)) == len(values))


def validate(catalog):
    if type(catalog.get('schema')) is not int or catalog['schema'] != 1 or catalog.get('status') != 'candidates':
        raise ValueError('expected schema-1 candidate catalog')
    sources = catalog['sources']
    if not isinstance(sources, dict) or not sources:
        raise ValueError('catalog requires authoritative source identities')
    for key, source in sources.items():
        path = Path(source['path'])
        digest = source['sha256']
        if (not isinstance(key, str) or not key or path.is_absolute() or '..' in path.parts or
                not path.parts or not isinstance(digest, str) or len(digest) != 64 or
                any(c not in '0123456789abcdef' for c in digest)):
            raise ValueError('invalid source identity')
    rungs = {}
    for rung in catalog['rungs']:
        identifier = rung['id']
        if not isinstance(identifier, str) or not identifier or identifier in rungs:
            raise ValueError('duplicate/invalid Rung id')
        if rung.get('assertion_status') != 'candidate' or rung.get('variant_inventory_complete') is not False:
            raise ValueError('candidate catalog cannot assert completed branch inventory or proof')
        if not names(rung['observations']) or not names(rung['variants']):
            raise ValueError('Rung needs unique observation and variant names')
        if not isinstance(rung['question'], str) or not rung['question'].strip():
            raise ValueError('Rung needs a bounded question')
        if not isinstance(rung['depends_on'], list) or len(set(rung['depends_on'])) != len(rung['depends_on']):
            raise ValueError('invalid dependencies')
        if rung['kind'] not in ('harness', 'gameplay', 'kernel', 'content'):
            raise ValueError('unknown Rung kind')
        if rung['kind'] != 'harness' and not rung['authority']:
            raise ValueError('Stock behavior candidate requires an authority anchor')
        for anchor in rung['authority']:
            if anchor['source'] not in sources or type(anchor['class_index']) is not int or anchor['class_index'] < 0:
                raise ValueError('invalid authority anchor')
            trees = anchor['trees']
            if not isinstance(trees, list) or not trees or any(type(t) is not int or t < 0 for t in trees):
                raise ValueError('invalid tree references')
        rungs[identifier] = rung
    if not rungs:
        raise ValueError('empty Rung catalog')
    ordered, visiting, visited = [], set(), set()
    def visit(identifier):
        if identifier not in rungs:
            raise ValueError(f'unknown dependency: {identifier}')
        if identifier in visiting:
            raise ValueError(f'cyclic Rung dependency: {identifier}')
        if identifier in visited:
            return
        visiting.add(identifier)
        for dependency in rungs[identifier]['depends_on']:
            visit(dependency)
        visiting.remove(identifier)
        visited.add(identifier)
        ordered.append(identifier)
    for identifier in rungs:
        visit(identifier)
    journeys = set()
    for journey in catalog['journeys']:
        if journey['id'] in journeys or not names(journey['rungs']):
            raise ValueError('invalid Journey')
        journeys.add(journey['id'])
        if any(identifier not in rungs for identifier in journey['rungs']):
            raise ValueError('Journey refers to an unknown Rung')
    frontiers = set()
    for frontier in catalog.get('stock_frontiers', []):
        if (set(frontier) != {'id', 'rung', 'variant', 'stage', 'source'} or
                not isinstance(frontier['id'], str) or not frontier['id'] or frontier['id'] in frontiers or
                frontier['stage'] not in ('before', 'name_dialog', 'after') or frontier['rung'] not in rungs):
            raise ValueError('invalid Stock frontier declaration')
        rung = rungs[frontier['rung']]
        if (frontier['variant'] not in rung['variants'] or
                frontier['source'] not in {a['source'] for a in rung['authority']} or
                (frontier['rung'], frontier['variant'], frontier['source']) != STOCK_FRONTIER_TARGETS[frontier['stage']]):
            raise ValueError('Stock frontier lacks a matching candidate variant/source anchor')
        frontiers.add(frontier['id'])
    return ordered


def validate_inventory(manifest):
    if (manifest.get('complete') is not True or manifest.get('exclusions') != [] or
            manifest.get('errors', []) != [] or not isinstance(manifest.get('files'), dict)):
        raise ValueError('incomplete or excluded corpus inventory')
    total = 0
    for path, record in manifest['files'].items():
        if (not relative_path(path) or not isinstance(record, dict) or
                type(record.get('size')) is not int or record['size'] < 0 or not sha256(record.get('sha256'))):
            raise ValueError('invalid corpus file identity')
        total += record['size']
    if type(manifest.get('hashed_bytes')) is not int or manifest['hashed_bytes'] != total:
        raise ValueError('corpus byte count differs from file identities')


def reference_difference_inventory(path):
    """Compare two explicitly bound recorded trees; do not infer official provenance.

    The reference labels and older/newer roles are caller declarations. File
    identity says nothing about whether a Resource is official, reachable, or
    semantically equivalent; even equal bytes retain pending behavior rows.
    """
    def document(source, expected=None):
        source = Path(source).absolute()
        if source.resolve(strict=True) != source:
            raise ValueError('reference identity source must be canonical and not a symlink')
        before = source.stat()
        if not stat.S_ISREG(before.st_mode) or before.st_size > 32 * 1024**2:
            raise ValueError('reference identity document missing or exceeds 32 MiB')
        # A replaced FIFO must not block before its descriptor can be checked.
        descriptor = os.open(source, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        try:
            opened = os.fstat(descriptor)
            if not stat.S_ISREG(opened.st_mode):
                raise ValueError('reference identity source is not a regular file')
            if data.fingerprint(opened) != data.fingerprint(before):
                raise ValueError('reference identity source changed before reading')
            with os.fdopen(descriptor, 'rb') as stream:
                descriptor = None
                raw = stream.read(before.st_size + 1)
                after = os.fstat(stream.fileno())
        finally:
            if descriptor is not None:
                os.close(descriptor)
        if (len(raw) != before.st_size or data.fingerprint(before) != data.fingerprint(after) or
                data.fingerprint(before) != data.fingerprint(source.stat())):
            raise ValueError('reference identity source changed during reading')
        digest = hashlib.sha256(raw).hexdigest()
        if expected is not None and digest != expected:
            raise ValueError('reference inventory hash differs from its identity binding')
        identity = {'path': str(source), 'size': len(raw), 'sha256': digest}
        return parse_json(raw), identity, data.fingerprint(before)

    declaration, source_identity, source_fingerprint = document(path)
    bindings = [(Path(source_identity['path']), source_fingerprint, source_identity['sha256'])]
    if (not isinstance(declaration, dict) or set(declaration) != {'schema', 'references'} or
            type(declaration['schema']) is not int or declaration['schema'] != 1 or
            not isinstance(declaration['references'], list) or len(declaration['references']) != 2):
        raise ValueError('expected schema-1 identities for exactly two references')
    references, inventories = {}, {}
    identifiers, inventory_scopes = set(), set()
    for reference in declaration['references']:
        if (not isinstance(reference, dict) or
                set(reference) != {'id', 'role', 'label', 'inventory', 'executable'} or
                not isinstance(reference['id'], str) or not re.fullmatch('[A-Za-z0-9][A-Za-z0-9_.-]*', reference['id']) or
                reference['id'] in identifiers or reference['role'] not in ('older', 'newer') or
                reference['role'] in references or not isinstance(reference['label'], str) or not reference['label'].strip()):
            raise ValueError('references require distinct ids and explicit older/newer roles')
        binding, executable = reference['inventory'], reference['executable']
        if (not isinstance(binding, dict) or not {'path', 'sha256'} <= set(binding) or
                set(binding) - {'path', 'sha256', 'prefix'} or
                not relative_path(binding['path']) or not sha256(binding['sha256']) or
                ('prefix' in binding and not relative_path(binding['prefix'])) or
                not isinstance(executable, dict) or set(executable) != {'path', 'size', 'sha256'} or
                not relative_path(executable['path']) or type(executable['size']) is not int or
                executable['size'] < 0 or not sha256(executable['sha256'])):
            raise ValueError('invalid reference inventory or executable identity')
        inventory_path = Path(source_identity['path']).parent / binding['path']
        prefix = binding.get('prefix', '')
        for previous_path, previous_prefix in inventory_scopes:
            if (inventory_path == previous_path and
                    (not prefix or not previous_prefix or prefix == previous_prefix or
                     prefix.startswith(previous_prefix + '/') or previous_prefix.startswith(prefix + '/'))):
                raise ValueError('reference inventory scopes overlap in one recorded collection')
        inventory, inventory_identity, fingerprint = document(inventory_path, binding['sha256'])
        bindings.append((inventory_path, fingerprint, binding['sha256']))
        if not isinstance(inventory, dict):
            raise ValueError('reference inventory must be an object')
        validate_inventory(inventory)
        if any(set(identity) != {'size', 'sha256'} for identity in inventory['files'].values()):
            raise ValueError('reference comparison supports only recorded regular-file size/hash identities')
        files = inventory['files']
        if prefix:
            prefix += '/'
            files = {name[len(prefix):]: identity for name, identity in files.items() if name.startswith(prefix)}
        inventory_identity['prefix'] = binding.get('prefix', '')
        expected_executable = {key: executable[key] for key in ('size', 'sha256')}
        if files.get(executable['path']) != expected_executable:
            raise ValueError('reference executable missing or changed in its recorded inventory')
        role = reference['role']
        references[role] = {'id': reference['id'], 'declared_label': reference['label'],
                            'executable': executable, 'inventory': inventory_identity,
                            'files': len(files), 'bytes': sum(identity['size'] for identity in files.values()),
                            'provenance': 'unknown'}
        inventories[role] = files
        identifiers.add(reference['id'])
        inventory_scopes.add((inventory_path, binding.get('prefix', '')))
    rows = []
    for name in sorted(set(inventories['older']) | set(inventories['newer'])):
        sides = {}
        for role in ('older', 'newer'):
            identity = inventories[role].get(name)
            sides[role] = ({'reference': references[role]['id'], 'identity': identity,
                            'provenance': 'unknown'} if identity is not None else None)
        if sides['older'] is None:
            relation = 'newer_only'
        elif sides['newer'] is None:
            relation = 'older_only'
        else:
            relation = 'same_bytes' if same_json(sides['older']['identity'], sides['newer']['identity']) else 'changed_bytes'
        rows.append({'requirement_id': 'D-content-' + hashlib.sha256(name.encode('utf-8')).hexdigest(),
                     'path': name, 'relation': relation, **sides,
                     'semantic_coverage': 'unverified', 'observed_behavior': [],
                     'affected_interfaces': None, 'affected_saves': None,
                     'selected_behavior': None, 'rationale': None, 'decision_status': 'pending',
                     'depends_on': [], 'dependency_inventory_complete': False,
                     'evidence': [], 'paired_proofs': []})
    for source, fingerprint, digest in bindings:
        _, _, after = document(source, digest)
        if after != fingerprint:
            raise ValueError('reference identity source changed during comparison')
    return {'schema': 1, 'status': 'reference_file_differences_pending', 'source': source_identity,
            'references': references, 'matching': 'exact root-relative POSIX path; case preserved',
            'content': rows,
            'summary': {'paths': len(rows), 'relations': dict(sorted(collections.Counter(r['relation'] for r in rows).items())),
                        'unclassified_file_occurrences': sum(len(files) for files in inventories.values()),
                        'semantic_pending_paths': len(rows), 'decisions_pending_paths': len(rows), 'paired_proofs': 0},
            'limits': ['bound recorded inventories, not a new scan or proof of current game files',
                       'only recorded regular-file size/hash identities are supported; other entry kinds refuse',
                       'reference labels and chronology are declared, not verified executable version metadata',
                       'input fingerprints are rechecked; this is not an atomic filesystem snapshot',
                       'no official/modification/backup/research classification is inferred',
                       'path/case changes, renames, packed Resources and semantic equivalence remain unresolved',
                       'file relations never establish behavior, feature coverage, reconciliation or Parity']}


def content_coverage(catalog, manifest, frontiers=(), script_inventory=None):
    """Every file and named variant gets a row; anchors never imply semantic coverage."""
    inspect(catalog, manifest)
    by_path = collections.defaultdict(list)
    for rung in catalog['rungs']:
        for anchor in rung['authority']:
            path = catalog['sources'][anchor['source']]['path']
            by_path[path].append({'rung': rung['id'], **anchor})
    rows = []
    script_rows = {}
    if script_inventory is not None:
        for index, record in enumerate(script_inventory['files']):
            path = record['path']
            if path in script_rows or manifest['files'].get(path) != {k: record[k] for k in ('size', 'sha256')}:
                raise ValueError('script inventory differs from content identities')
            script_rows[path] = {'artifact': 'script-branches.json', 'file_index': index,
                                 'status': record['status'], 'sha256': record['sha256']}
        if set(script_rows) != {p for p in manifest['files'] if Path(p).suffix.lower() == '.class'}:
            raise ValueError('script inventory omitted or added a class file')
    for path, identity in sorted(manifest['files'].items()):
        rows.append({'path': path, **identity,
                     'assignment': 'candidate_anchor' if by_path[path] else 'unassigned',
                     'candidate_contracts': by_path[path], 'semantic_coverage': 'unverified',
                     'script_structure': script_rows.get(path),
                     'stock_frontiers': [f['id'] for f in frontiers if f['content']['path'] == path],
                     'paired_proofs': []})
    branches = [{'rung': r['id'], 'variant': variant, 'assertion_status': 'pending',
                 'branch_inventory_complete': False, 'required_observations': r['observations'],
                 'stock_frontiers': [f['id'] for f in frontiers
                                     if f['rung'] == r['id'] and f['variant'] == variant],
                 'paired_proofs': []}
                for r in catalog['rungs'] for variant in r['variants']]
    return {'schema': 1, 'status': 'explicit_pending_coverage', 'content': rows, 'variants': branches,
            'stock_frontiers': list(frontiers),
            'summary': {'files': len(rows), 'candidate_anchored_files': sum(bool(r['candidate_contracts']) for r in rows),
                        'unassigned_files': sum(not r['candidate_contracts'] for r in rows),
                        'semantically_unverified_files': len(rows), 'named_variants_pending': len(branches),
                        'stock_frontier_bindings': len(frontiers), 'paired_proofs': 0},
            'limits': ['exact file accounting and candidate anchors, not content semantics or runtime file-access evidence',
                       'named variants are a partial inventory; unenumerated branches remain unknown',
                       'Stock frontiers are external functional observations, not internal branch or paired proof']}


def stock_frontiers(catalog, manifest, capture):
    """Bind only the retained menu/dialog frontier, rechecking raw supporting artifacts.

    This does not authenticate an honest producer or independently read pixels.
    No observer, OCR tool, game, sound device or subprocess is launched here.
    """
    inspect(catalog, manifest)
    capture = Path(capture).resolve(strict=True)
    artifacts = {}

    def artifact(name, limit=16 * 1024**2):
        if not relative_path(name):
            raise ValueError('invalid Stock artifact path')
        path = capture / name
        if any(p.is_symlink() for p in [path, *path.parents] if p != capture.parent):
            raise ValueError('symlink Stock artifact')
        if not path.is_file() or path.stat().st_size > limit:
            raise ValueError('missing or oversized Stock artifact')
        before = data.fingerprint(path.stat())
        digest = hashlib.sha256()
        with path.open('rb') as stream:
            for chunk in iter(lambda: stream.read(1024**2), b''):
                digest.update(chunk)
        if before != data.fingerprint(path.stat()):
            raise ValueError('Stock artifact changed during inspection')
        artifacts[name] = {'path': name, 'sha256': digest.hexdigest(), 'size': before[2]}
        return path

    def document(name):
        return read_json(artifact(name))

    runner = module('catalog_stock_evidence', HERE / 'run_stock.py')
    result, flow, config = (document(n) for n in ('result.json', 'flow.json', 'config.json'))
    if (type(result.get('schema')) is not int or result['schema'] != 1 or
            result.get('status') != 'functional_scenario_captured' or result.get('timed_out') is not False or
            type(result.get('container_exit')) is not int or result['container_exit'] != 0 or
            type(result.get('observer_exit')) is not int or result['observer_exit'] != 0 or
            not same_json(result.get('flow'), flow) or
            not all(result.get(k) is True for k in ('passed', 'source_preserved', 'disc_preserved',
                    'stock_preserved', 'texture_preserved', 'container_removed', 'producer_unchanged'))):
        raise ValueError('incomplete Stock functional capture')
    if config.get('ordinary_exit'):
        raise ValueError('ordinary-exit capture is not a qualified frontier-binding producer yet')
    before, after, private = (document(n) for n in ('source-before.json', 'source-after.json', 'private-initial.json'))
    if not same_json(before, after) or not same_json(before, manifest['files']):
        raise ValueError('Stock capture corpus differs from authoritative inventory')
    changed = {p for p in set(before) | set(private) if before.get(p) != private.get(p)}
    allowed = {'StreetLegal_Redline.exe', 'd3d9.dll', 'dinput8.dll', 'mcicda.dll'}
    if changed != allowed or private['StreetLegal_Redline.exe']['sha256'] != runner.STOCK_SHA:
        raise ValueError('Stock private copy differs outside the qualified instrumentation')
    for path in allowed:
        actual = artifact('game/' + path)
        if (artifacts['game/' + path]['sha256'] != private[path]['sha256'] or
                actual.stat().st_size != private[path]['size']):
            raise ValueError('Stock private executable/proxy changed')
    producer = document('instrumentation-source.json')
    required = {'runner.py', 'container.sh', 'state_flow.py', 'menu_ocr.py', 'probe32.c',
                'd3d9_proxy.c', 'dinput8_proxy.c', 'walk_trace.c', 'synthetic_cd.c', 'synthetic_metadata.py'}
    if (not required <= producer.keys() or len(producer) > 32 or
            config.get('producer_sha256') != producer['runner.py']['sha256']):
        raise ValueError('Stock producer identity differs')
    for name, identity in producer.items():
        artifact('instrumentation-source/' + name)
        if artifacts['instrumentation-source/' + name] != {'path': 'instrumentation-source/' + name,
                                                         'sha256': identity['sha256'], 'size': identity['size']}:
            raise ValueError('Stock frozen producer changed')
    inspected = document('container-inspect.json')
    if not runner.isolated(inspected, capture, Path(config['disc']).resolve(), config['image_id']):
        raise ValueError('Stock isolation artifacts fail validation')
    native_input = artifact('game/dumps/dinput8proxy.log').read_text()
    scenario = runner.scenario_evidence(flow, native_input)
    if not scenario['complete'] or not same_json(result.get('scenario'), scenario):
        raise ValueError('Stock native input/scenario evidence differs')
    events = runner.focus.read_events(artifact('focus.jsonl'))
    focus = runner.focus.classify(events)
    roots = [e['root'] for e in events if e['kind'] == 'child_started']
    if (focus['status'] != 'no_owned_activation_observed' or focus['child_exit'] != 0 or
            not same_json(roots, [result.get('execution_owner')]) or not same_json(result.get('focus'), focus)):
        raise ValueError('Stock foreground ownership evidence differs')
    summary = document('audio-summary.json')
    pcm = artifact('stock.wav', 256 * 1024**2)
    audio = runner.audio.summarize(pcm, 's16le', 2, 48000, 'wav')
    audio.update(path='stock.wav', route='pulse-null-monitor', stage='sink_monitor',
                 complete=True, physical_output=False)
    if not same_json(summary, audio) or not same_json(result.get('audio'), audio) or audio['nonzero_samples'] <= 0:
        raise ValueError('Stock PCM bytes differ from final-mix evidence')
    sinks = artifact('audio-sinks.txt').read_text()
    if sinks.count('Name: stock\n') != 1 or sinks.count('Driver: module-null-sink.c') != 1:
        raise ValueError('Stock private null-sink evidence differs')
    stages = ('before', 'name_dialog', 'after')
    indices, times = [], []
    for stage in stages:
        row = flow[stage]
        if row not in flow['observations']:
            raise ValueError('Stock frontier is absent from ordered observations')
        indices.append(flow['observations'].index(row))
        when = row['wall_time_ns']
        if type(when) is not int or when <= 0:
            raise ValueError('invalid Stock observation time')
        times.append(when)
        image = artifact(row['image'])
        header = image.read_bytes()[:24]
        if (image.suffix != '.png' or len(header) < 24 or header[:8] != b'\x89PNG\r\n\x1a\n' or
                header[12:16] != b'IHDR' or struct.unpack('>II', header[16:24]) != (800, 600)):
            raise ValueError('Stock frontier lacks an800x600 PNG artifact')
        stem = image.stem + '-ocr/'
        menu, dialog = (document(stem + n) for n in ('region-ocr.json', 'dialog-ocr.json'))
        joined = '\n'.join(menu['texts'].values()).upper()
        menu_labels = {name: name in joined for name in ('MAIN MENU', 'FREE RIDE', 'OPTIONS')}
        dialog_labels = {'name_title': 'ENTER PLAYER NAME' in dialog['texts']['dialog'].upper(),
                         'cancel_button': 'CANCEL' in dialog['texts']['cancel'].upper()}
        if (not same_json(menu['labels'], menu_labels) or not same_json(row['menu'], menu_labels) or
                not same_json(dialog['labels'], dialog_labels) or not same_json(row['dialog'], dialog_labels) or
                Path(menu['source']).resolve() != image or Path(dialog['source']).resolve() != image):
            raise ValueError('Stock OCR transcript/frontier mismatch')
    if indices != sorted(set(indices)) or times != sorted(set(times)):
        raise ValueError('Stock frontiers are reordered')
    actions = flow['actions']
    for n in range(2):
        pressed, released = (actions[n][k] for k in ('button_down_request_wall_time_ns', 'released_wall_time_ns'))
        if (type(pressed) is not int or type(released) is not int or
                not times[n] < pressed < released < times[n + 1]):
            raise ValueError('Stock action/observation ordering differs')
    # Rehash after cross-artifact checks, not only before each parser reads.
    retained = dict(artifacts)
    for name, identity in retained.items():
        artifact(name, 256 * 1024**2 if name == 'stock.wav' else 16 * 1024**2)
        if artifacts[name] != identity:
            raise ValueError('Stock artifact changed during binding')
    bindings = []
    for frontier in catalog.get('stock_frontiers', []):
        source = catalog['sources'][frontier['source']]
        row = flow[frontier['stage']]
        bindings.append({**frontier, 'status': 'stock_external_frontier_observed', 'paired_proof': False,
                         'content': source, 'capture_root': str(capture),
                         'stock_executable_sha256': runner.STOCK_SHA,
                         'expected_from_stock': {'menu': row['menu'], 'dialog': row['dialog']},
                         'observation': row, 'artifacts': retained,
                         'limits': ['OCR transcript with retained image; pixels are not independently recognized here',
                                    'content identity does not prove this class/tree executed or all branches were observed',
                                    'no script state, input owner, scheduling, repetition or paired comparison proof']})
    return bindings


def inspect(catalog, game_manifest, reference_manifest=None):
    ordered = validate(catalog)
    validate_inventory(game_manifest)
    if reference_manifest is not None:
        validate_inventory(reference_manifest)
    for source in catalog['sources'].values():
        actual = game_manifest['files'].get(source['path'])
        if actual is None or actual['sha256'] != source['sha256']:
            raise ValueError(f'authoritative source missing or changed: {source["path"]}')
    def summary(manifest):
        return {'files': len(manifest['files']), 'bytes': manifest['hashed_bytes'],
                'suffixes': dict(sorted(collections.Counter(Path(p).suffix.lower() or '(none)'
                                      for p in manifest['files']).items())),
                'excluded': len(manifest['exclusions'])}
    return {'schema': 1, 'status': 'candidate_inventory', 'topological_order': ordered,
            'rungs': len(catalog['rungs']), 'journeys': len(catalog['journeys']),
            'variants_pending': sum(len(r['variants']) for r in catalog['rungs']),
            'stock_source_identities_checked': len(catalog['sources']),
            'game': summary(game_manifest),
            'reference': summary(reference_manifest) if reference_manifest else None,
            'limits': ['file identity and candidate graph only; no behavior or parser coverage claim',
                       'variant enumeration is partial; unenumerated branches remain unknown',
                       'per-content semantic coverage and paired runtime proof remain pending']}


def validate_tree_anchors(catalog, game):
    classes, checked = {}, 0
    for rung in catalog['rungs']:
        for anchor in rung['authority']:
            identifier = anchor['source']
            source = catalog['sources'][identifier]
            path = game / source['path']
            if identifier not in classes:
                if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != source['sha256']:
                    raise ValueError('source changed before tree validation')
                classes[identifier] = tree_data.load(path)
                if hashlib.sha256(path.read_bytes()).hexdigest() != source['sha256']:
                    raise ValueError('source changed during tree validation')
            group = classes[identifier]
            if anchor['class_index'] >= len(group):
                raise ValueError('authority class does not exist')
            trees = group[anchor['class_index']]['trees']
            if any(number >= len(trees) for number in anchor['trees']):
                raise ValueError(f'authority tree does not exist: {rung["id"]}')
            checked += 1
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path, default=HERE / 'catalog.json')
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument('--game', type=Path)
    inputs.add_argument('--references', type=Path,
                        help='two explicit reference identities bound to recorded complete inventories; no new scan')
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--stock-capture', type=Path,
                        help='optional retained qualified Stock menu/dialog capture; read-only artifact binding')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.references:
            if args.reference or args.stock_capture:
                raise ValueError('--reference and --stock-capture apply only with --game')
            report = reference_difference_inventory(args.references)
            out = args.out.resolve()
            protected = [(REPO / 'ref').resolve(), Path(report['source']['path']),
                         *(Path(r['inventory']['path']) for r in report['references'].values())]
            if any(out == p or p in out.parents or out in p.parents for p in protected):
                raise ValueError('output overlaps protected reference identity sources')
            out.mkdir(parents=True, exist_ok=False)
            differences = out / 'reference-differences.json'
            differences.write_text(json.dumps(report, indent=2) + '\n')
            result = {'schema': 1, 'status': report['status'], **report['summary'],
                      'differences': {'path': differences.name,
                                      'sha256': hashlib.sha256(differences.read_bytes()).hexdigest()},
                      'limits': report['limits']}
            (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(result))
            return 0
        catalog_digest = hashlib.sha256(args.catalog.read_bytes()).hexdigest()
        catalog = read_json(args.catalog)
        validate(catalog)
        out = args.out.resolve()
        protected = [args.game.resolve(), (REPO / 'ref').resolve(), args.catalog.resolve()]
        if args.reference:
            protected.append(args.reference.resolve())
        if args.stock_capture:
            protected.append(args.stock_capture.resolve())
        if any(out == p or p in out.parents or out in p.parents for p in protected):
            raise ValueError('output overlaps protected corpus/catalog')
        out.mkdir(parents=True, exist_ok=False)
        game = data.collect(args.game, exclude_runtime=False)
        reference = data.collect(args.reference, exclude_runtime=False) if args.reference else None
        # Retain incomplete scans too; inspection fails closed afterwards.
        for label, value in (('game', game), ('reference', reference)):
            if value is not None:
                (out / (label + '-manifest.json')).write_text(json.dumps(value, indent=2) + '\n')
        result = inspect(catalog, game, reference)
        result['tree_anchors_checked'] = validate_tree_anchors(catalog, args.game.resolve())
        branches = branch_data.inventory(args.game, game, tree_data.load, tree_data.t.Bad)
        branch_path = out / 'script-branches.json'
        branch_path.write_text(json.dumps(branches, indent=2) + '\n')
        result['script_branches'] = {**branches['summary'], 'path': branch_path.name,
                                    'sha256': hashlib.sha256(branch_path.read_bytes()).hexdigest(),
                                    'explicit_branch_decode_complete': branches['explicit_branch_decode_complete'],
                                    'semantic_branch_inventory_complete': False}
        frontiers = stock_frontiers(catalog, game, args.stock_capture) if args.stock_capture else []
        coverage = content_coverage(catalog, game, frontiers, branches)
        coverage_path = out / 'content-coverage.json'
        coverage_path.write_text(json.dumps(coverage, indent=2) + '\n')
        result['content_coverage'] = {**coverage['summary'], 'path': coverage_path.name,
                                      'sha256': hashlib.sha256(coverage_path.read_bytes()).hexdigest()}
        if hashlib.sha256(args.catalog.read_bytes()).hexdigest() != catalog_digest:
            raise ValueError('catalog changed during inspection')
        result['catalog_sha256'] = catalog_digest
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(2, f'catalog: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
