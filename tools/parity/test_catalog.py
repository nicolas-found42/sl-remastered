import copy
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
import zlib

spec = importlib.util.spec_from_file_location('catalog', Path(__file__).with_name('catalog.py'))
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)


class CandidateCatalogTest(unittest.TestCase):
    def setUp(self):
        self.value = catalog.read_json(Path(__file__).with_name('catalog.json'))

    def test_dependency_graph_orders_predecessors_before_consumers(self):
        ordered = catalog.validate(self.value)
        for rung in self.value['rungs']:
            for dependency in rung['depends_on']:
                self.assertLess(ordered.index(dependency), ordered.index(rung['id']))

    def test_structural_branches_preserve_backward_edges_and_terminal_fallthrough(self):
        nodes = [(0, 1, None), (0x15, 2, 2), (0x14, 3, 0xfffffffe), (0x16, 4, 0xffffffff)]
        rows = catalog.branch_data.tree_branches(nodes)
        self.assertEqual([r['relative_target'] for r in rows], [3, 0, 2])
        self.assertEqual([len(r['successors']) for r in rows], [2, 1, 2])
        self.assertEqual(rows[-1]['successors'][1],
                         {'edge': 'fallthrough', 'node': 4, 'exits_tree': True, 'runtime_evidence': []})
        self.assertTrue(all(r['semantic_assertion'] == 'pending' and not r['paired_proofs'] for r in rows))

    def test_structural_branch_bad_target_or_operand_is_refused(self):
        for operand in (None, True, -1, 0x100000000, 1, 0xffffffff):
            with self.subTest(operand=operand), self.assertRaises(ValueError):
                catalog.branch_data.tree_branches([(0x15, 3, operand)])

    def test_actual_script_inventory_is_hash_bound_and_does_not_promote_runtime_coverage(self):
        source = self.value['sources']['menu']
        game = catalog.REPO / '.scratch/2005-extract/Program Files/Street Legal Racing - Redline'
        path = game / source['path']
        manifest = {'files': {source['path']: {'size': path.stat().st_size, 'sha256': source['sha256']}}}
        report = catalog.branch_data.inventory(game, manifest, catalog.tree_data.load, catalog.tree_data.t.Bad)
        self.assertTrue(report['explicit_branch_decode_complete'])
        self.assertFalse(report['semantic_branch_inventory_complete'])
        self.assertEqual(report['summary']['decoded_files'], 1)
        self.assertGreater(report['summary']['conditional_branches'], 0)
        self.assertEqual(report['summary']['runtime_covered_edges'], 0)
        manifest['files'][source['path']]['sha256'] = '0' * 64
        invalid = catalog.branch_data.inventory(game, manifest, catalog.tree_data.load, catalog.tree_data.t.Bad)
        self.assertFalse(invalid['explicit_branch_decode_complete'])
        self.assertEqual(invalid['summary']['decoded_files'], 0)
        self.assertEqual(invalid['files'][0]['classes'], [])

    def test_script_decoder_failure_retains_file_identity_as_inconclusive(self):
        import hashlib
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            raw = b'not a TUFA class'
            (root / 'broken.class').write_bytes(raw)
            identity = {'size': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}
            report = catalog.branch_data.inventory(root, {'files': {'broken.class': identity}},
                                                    catalog.tree_data.load, catalog.tree_data.t.Bad)
            self.assertEqual(report['summary']['decode_failures'], 1)
            self.assertEqual(report['files'][0]['sha256'], identity['sha256'])
            self.assertEqual(report['files'][0]['status'], 'inconclusive')

    def test_content_script_bindings_reject_omitted_duplicate_and_changed_files(self):
        manifest = self.manifest()
        inventory = {'files': [{'path': p, **v, 'status': 'inconclusive'}
                               for p, v in manifest['files'].items() if Path(p).suffix == '.class']}
        report = catalog.content_coverage(self.value, manifest, script_inventory=inventory)
        self.assertEqual(sum(r['script_structure'] is not None for r in report['content']), len(inventory['files']))
        for mutation in ('omitted', 'duplicate', 'changed'):
            bad = copy.deepcopy(inventory)
            if mutation == 'omitted': bad['files'].pop()
            elif mutation == 'duplicate': bad['files'].append(bad['files'][0])
            else: bad['files'][0]['sha256'] = '0' * 64
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                catalog.content_coverage(self.value, manifest, script_inventory=bad)

    def test_missing_cyclic_and_duplicate_dependencies_are_rejected(self):
        for mutation in ('missing', 'cycle', 'duplicate-id', 'unknown-journey'):
            value = copy.deepcopy(self.value)
            if mutation == 'missing':
                value['rungs'][0]['depends_on'] = ['absent']
            elif mutation == 'cycle':
                value['rungs'][0]['depends_on'] = [value['rungs'][-1]['id']]
            elif mutation == 'duplicate-id':
                value['rungs'].append(value['rungs'][0])
            else:
                value['journeys'][0]['rungs'].append('absent')
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                catalog.validate(value)

    def test_candidate_cannot_claim_exhaustive_branches_or_verified_assertions(self):
        for field, replacement in (('variant_inventory_complete', True),
                                    ('assertion_status', 'verified')):
            value = copy.deepcopy(self.value)
            value['rungs'][0][field] = replacement
            with self.assertRaises(ValueError):
                catalog.validate(value)

    def test_full_inventory_accounts_for_previously_excluded_runtime_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'save').mkdir()
            (root / 'save/options').write_bytes(b'options')
            (root / 'StreetLegal_Redline.exe').write_bytes(b'exe')
            (root / '.hidden').write_bytes(b'hidden')
            (root / 'd3d9.dll').write_bytes(b'proxy')
            inventory = catalog.data.collect(root, exclude_runtime=False)
            self.assertTrue(inventory['complete'])
            self.assertEqual(set(inventory['files']),
                             {'save/options', 'StreetLegal_Redline.exe', '.hidden', 'd3d9.dll'})
            self.assertFalse(inventory['exclusions'])
            self.assertEqual(inventory['hashed_bytes'], 21)

    def test_missing_changed_or_incomplete_sources_cannot_validate_inventory(self):
        files = {s['path']: {'sha256': s['sha256'], 'size': 1}
                 for s in self.value['sources'].values()}
        manifest = {'complete': True, 'files': files, 'hashed_bytes': len(files), 'exclusions': []}
        result = catalog.inspect(self.value, manifest)
        self.assertEqual(result['status'], 'candidate_inventory')
        self.assertGreater(result['variants_pending'], 0)
        for mutation in ('missing', 'changed', 'incomplete'):
            bad = copy.deepcopy(manifest)
            first = next(iter(files))
            if mutation == 'missing':
                del bad['files'][first]
            elif mutation == 'changed':
                bad['files'][first]['sha256'] = '0' * 64
            else:
                bad['complete'] = False
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                catalog.inspect(self.value, bad)

    def test_authority_trees_exist_in_identified_installed_game(self):
        game = catalog.REPO / '.scratch/2005-extract/Program Files/Street Legal Racing - Redline'
        self.assertGreater(catalog.validate_tree_anchors(self.value, game), 0)
        bad = copy.deepcopy(self.value)
        target = next(r for r in bad['rungs'] if r['authority'])
        target['authority'][0]['trees'] = [1_000_000]
        with self.assertRaises(ValueError):
            catalog.validate_tree_anchors(bad, game)

    def manifest(self):
        files = {s['path']: {'sha256': s['sha256'], 'size': 1}
                 for s in self.value['sources'].values()}
        files['unknown/new-content.whatever'] = {'size': 1, 'sha256': '1' * 64}
        return {'complete': True, 'files': files, 'hashed_bytes': len(files), 'exclusions': []}

    def test_every_exact_content_identity_and_named_variant_stays_visible_and_unverified(self):
        manifest = self.manifest()
        report = catalog.content_coverage(self.value, manifest)
        self.assertEqual({r['path'] for r in report['content']}, set(manifest['files']))
        self.assertEqual(len(report['content']), len(manifest['files']))
        for row in report['content']:
            self.assertEqual(row['sha256'], manifest['files'][row['path']]['sha256'])
            self.assertEqual(row['semantic_coverage'], 'unverified')
            self.assertEqual(row['paired_proofs'], [])
        unknown = next(r for r in report['content'] if r['path'].startswith('unknown/'))
        self.assertEqual(unknown['assignment'], 'unassigned')
        menu = next(r for r in report['content'] if r['path'] == self.value['sources']['menu']['path'])
        self.assertTrue(any(a['rung'] == 'B-boot-menu' and a['trees'] == [0, 1]
                            for a in menu['candidate_contracts']))
        variants = {(r['rung'], r['variant']) for r in report['variants']}
        self.assertEqual(variants, {(r['id'], v) for r in self.value['rungs'] for v in r['variants']})
        self.assertTrue(all(r['assertion_status'] == 'pending' and not r['branch_inventory_complete']
                            for r in report['variants']))
        self.assertEqual(report['summary']['paired_proofs'], 0)

    def test_excluded_invalid_and_miscounted_inventory_cannot_claim_complete_accounting(self):
        for mutation in ('excluded', 'errors', 'traversal', 'bad-hash', 'bool-size', 'wrong-total'):
            manifest = self.manifest()
            key = next(iter(manifest['files']))
            if mutation == 'excluded': manifest['exclusions'] = [{'path': 'save/'}]
            elif mutation == 'errors': manifest['errors'] = [{'reason': 'unreadable'}]
            elif mutation == 'traversal': manifest['files']['../escape'] = manifest['files'].pop(key)
            elif mutation == 'bad-hash': manifest['files'][key]['sha256'] = 'bad'
            elif mutation == 'bool-size': manifest['files'][key]['size'] = True
            else: manifest['hashed_bytes'] += 1
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                catalog.content_coverage(self.value, manifest)

    def test_frontier_declarations_require_a_real_candidate_variant_and_source_anchor(self):
        for field, value in [('id', ''), ('rung', 'absent'), ('variant', 'invented'),
                             ('stage', 'internal-script-state'), ('source', 'city')]:
            bad = copy.deepcopy(self.value)
            bad['stock_frontiers'][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                catalog.validate(bad)


class StockFrontierBindingTest(unittest.TestCase):
    """Synthetic validator fixtures, never actual Stock execution evidence."""
    def setUp(self):
        CandidateCatalogTest.setUp(self)
        self.temp = tempfile.TemporaryDirectory(prefix='sl-catalog-fixture-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.runner = catalog.module('catalog_test_runner', catalog.HERE / 'run_stock.py')
        self.inventory = self.manifest()
        self.fixture()

    manifest = CandidateCatalogTest.manifest

    def write(self, name, value):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value))

    def mutate(self, name, fn):
        path = self.root / name
        value = json.loads(path.read_text())
        fn(value)
        self.write(name, value)

    def fixture(self):
        root, runner = self.root, self.runner
        before = self.inventory['files']
        self.write('source-before.json', before)
        self.write('source-after.json', before)
        private = copy.deepcopy(before)
        for name in ('d3d9.dll', 'dinput8.dll', 'mcicda.dll'):
            path = root / 'game' / name
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(name.encode())
            private[name] = {'size': path.stat().st_size, 'sha256': runner.digest(path)}
        exe = root / 'game/StreetLegal_Redline.exe'
        exe.write_bytes((catalog.REPO / 'ref/raw/StreetLegal_Redline.bak.exe').read_bytes())
        private[exe.name] = {'size': exe.stat().st_size, 'sha256': runner.digest(exe)}
        self.write('private-initial.json', private)
        producer = root / 'instrumentation-source/runner.py'
        producer.parent.mkdir()
        identities = {}
        for name in ('runner.py', 'container.sh', 'state_flow.py', 'menu_ocr.py', 'probe32.c',
                     'd3d9_proxy.c', 'dinput8_proxy.c', 'walk_trace.c', 'synthetic_cd.c', 'synthetic_metadata.py'):
            p = producer.parent / name
            p.write_bytes(b'synthetic validator fixture, never a producer\n')
            identities[name] = {'size': p.stat().st_size, 'sha256': runner.digest(p)}
        self.write('instrumentation-source.json', identities)
        image_id = 'sha256:fixture'
        self.write('config.json', {'disc': str(root / 'disc'), 'image_id': image_id,
                                  'producer_sha256': runner.digest(producer)})
        inspected = {'Image': image_id, 'HostConfig': {'NetworkMode': 'none', 'Privileged': False,
                     'CapDrop': ['ALL'], 'SecurityOpt': ['no-new-privileges'], 'NanoCpus': 4_000_000_000,
                     'Memory': 3 * 1024**3, 'PidsLimit': 256},
                     'Mounts': [{'Destination': '/work', 'Source': str(root), 'RW': True},
                                {'Destination': '/disc', 'Source': str(root / 'disc'), 'RW': False}]}
        self.write('container-inspect.json', inspected)
        menu = {'MAIN MENU': True, 'FREE RIDE': True, 'OPTIONS': True}
        absent = {'name_title': False, 'cancel_button': False}
        shown = dict.fromkeys(absent, True)
        stages = [('before', menu, absent), ('name_dialog', {k: False for k in menu}, shown),
                  ('after', menu, absent)]
        flow = {'passed': True, 'observations': [], 'actions': [
            {'label': label, 'button_down_request_wall_time_ns': n * 100 + 50,
             'released_wall_time_ns': n * 100 + 75}
            for n, label in enumerate(('New Career', 'Cancel button'), 1)]}
        for n, (stage, labels, dialog) in enumerate(stages, 1):
            image = root / (stage + '.png')
            def chunk(kind, body):
                return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
            # Valid blank synthetic image: OCR/pixel agreement is explicitly outside this text validator.
            image.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 800, 600, 8, 2, 0, 0, 0)) +
                              chunk(b'IDAT', zlib.compress((b'\x00' + b'\x00' * 2400) * 600)) + chunk(b'IEND', b''))
            row = {'stage': stage, 'image': image.name, 'wall_time_ns': n * 100,
                   'menu': labels, 'dialog': dialog}
            flow[stage] = row
            flow['observations'].append(row)
            roi = image.stem + '-ocr/'
            self.write(roi + 'region-ocr.json', {'source': str(image), 'labels': labels,
                       'texts': {'header': 'MAIN MENU' if stage != 'name_dialog' else '',
                                 'choices': 'FREE RIDE OPTIONS' if stage != 'name_dialog' else ''}})
            self.write(roi + 'dialog-ocr.json', {'source': str(image), 'labels': dialog,
                       'texts': {'dialog': 'ENTER PLAYER NAME' if stage == 'name_dialog' else '',
                                 'cancel': 'CANCEL' if stage == 'name_dialog' else ''}})
        self.write('flow.json', flow)
        log = root / 'game/dumps/dinput8proxy.log'
        log.parent.mkdir()
        log.write_text('GetDeviceState calls=20\n' + ''.join(
            f'input_poll: kind=1 offset=12 data={v} source=native\n' for v in (0, 128, 0, 128, 0)))
        owner = {'pid': 100, 'pgid': 100, 'ppid': 90, 'start_sec': 20, 'start_usec': 0,
                 'read_ok': True, 'exe': '/fixture'}
        observer = dict(owner, pid=90, pgid=90, ppid=1, start_sec=10, exe='/observer')
        external = dict(owner, pid=200, pgid=200, ppid=1, start_sec=5, exe='/user-app')
        events = [{'kind': 'observer_ready', 'observer': observer, 'requested_executable': '/fixture'},
                  {'kind': 'child_started', 'spawned_pid': 100, 'root': owner},
                  {'kind': 'foreground', 'chain': [external], 'end_reason': 'terminal'},
                  {'kind': 'observer_finished', 'child_exit': 0, 'timed_out': False,
                   'polls': 4, 'max_poll_gap_seconds': .02}]
        events = [dict(e, schema=1, sequence=n, monotonic=n * .02) for n, e in enumerate(events)]
        (root / 'focus.jsonl').write_text(''.join(json.dumps(e) + '\n' for e in events))
        with wave.open(str(root / 'stock.wav'), 'wb') as wav:
            wav.setnchannels(2); wav.setsampwidth(2); wav.setframerate(48000)
            wav.writeframes(struct.pack('<4h', 50, 50, 100, 100))
        audio = runner.audio.summarize(root / 'stock.wav', 's16le', 2, 48000, 'wav')
        audio.update(path='stock.wav', route='pulse-null-monitor', stage='sink_monitor',
                     complete=True, physical_output=False)
        self.write('audio-summary.json', audio)
        (root / 'audio-sinks.txt').write_text('Name: stock\nDriver: module-null-sink.c\n')
        self.write('result.json', {'schema': 1, 'status': 'functional_scenario_captured', 'passed': True,
            'timed_out': False, 'container_exit': 0, 'observer_exit': 0, 'flow': flow,
            'scenario': runner.scenario_evidence(flow, log.read_text()), 'audio': audio,
            'focus': runner.focus.classify(events), 'execution_owner': owner,
            **dict.fromkeys(('source_preserved', 'disc_preserved', 'stock_preserved', 'texture_preserved',
                             'container_removed', 'producer_unchanged'), True)})

    def bindings(self):
        return catalog.stock_frontiers(self.value, self.inventory, self.root)

    def test_valid_frontiers_bind_artifacts_but_never_promote_content_or_variants(self):
        bindings = self.bindings()
        self.assertEqual(len(bindings), 3)
        self.assertTrue(all(not b['paired_proof'] and b['artifacts']['stock.wav']['sha256']
                            for b in bindings))
        report = catalog.content_coverage(self.value, self.inventory, bindings)
        self.assertEqual(report['summary']['stock_frontier_bindings'], 3)
        self.assertEqual(report['summary']['paired_proofs'], 0)
        self.assertTrue(all(r['semantic_coverage'] == 'unverified' for r in report['content']))
        self.assertTrue(all(r['assertion_status'] == 'pending' for r in report['variants']))

    def test_result_true_cannot_replace_raw_native_input(self):
        log = self.root / 'game/dumps/dinput8proxy.log'
        log.write_text(log.read_text().replace('data=128', 'data=0', 1))
        with self.assertRaisesRegex(ValueError, 'native input'):
            self.bindings()

    def test_result_true_cannot_replace_pcm_or_private_sink(self):
        (self.root / 'stock.wav').write_bytes(b'not PCM')
        with self.assertRaises((ValueError, EOFError, wave.Error)):
            self.bindings()

    def test_result_true_cannot_replace_ocr_transcript(self):
        self.mutate('name_dialog-ocr/dialog-ocr.json', lambda d: d['texts'].update(cancel='NO'))
        with self.assertRaisesRegex(ValueError, 'OCR'):
            self.bindings()

    def test_result_true_cannot_replace_source_or_private_executable_identity(self):
        self.mutate('private-initial.json', lambda d: d['StreetLegal_Redline.exe'].update(sha256='0' * 64))
        with self.assertRaisesRegex(ValueError, 'private copy'):
            self.bindings()

    def test_mutated_source_manifest_is_rejected_even_if_both_preservation_claims_agree(self):
        for name in ('source-before.json', 'source-after.json'):
            self.mutate(name, lambda d: d[next(iter(d))].update(sha256='0' * 64))
        with self.assertRaisesRegex(ValueError, 'corpus differs'):
            self.bindings()

    def test_changed_frozen_producer_is_rejected(self):
        (self.root / 'instrumentation-source/runner.py').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'frozen producer'):
            self.bindings()

    def test_changed_isolation_is_rejected(self):
        self.mutate('container-inspect.json', lambda d: d['HostConfig'].update(NetworkMode='host'))
        with self.assertRaisesRegex(ValueError, 'isolation'):
            self.bindings()

    def test_owned_activation_and_omitted_lifecycle_are_rejected(self):
        focus = self.root / 'focus.jsonl'
        rows = [json.loads(line) for line in focus.read_text().splitlines()]
        rows[2]['chain'] = [rows[1]['root'], rows[0]['observer']]
        focus.write_text(''.join(json.dumps(r) + '\n' for r in rows))
        with self.assertRaisesRegex(ValueError, 'foreground ownership'):
            self.bindings()

    def test_symlink_artifacts_are_rejected(self):
        path = self.root / 'flow.json'
        moved = self.root / 'moved-flow.json'
        path.rename(moved); path.symlink_to(moved)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            self.bindings()

    def test_missing_ordered_frontier_is_rejected_even_if_result_matches(self):
        def change(value): value['observations'].reverse()
        self.mutate('flow.json', change)
        self.mutate('result.json', lambda d: change(d['flow']))
        with self.assertRaisesRegex(ValueError, 'reordered'):
            self.bindings()

    def test_cancel_capture_cannot_be_relabelled_as_accept_or_internal_branch(self):
        bad = copy.deepcopy(self.value)
        bad['stock_frontiers'][2]['variant'] = 'accept'
        with self.assertRaises(ValueError):
            catalog.validate(bad)

    def test_boolean_success_and_numeric_ocr_claims_are_not_interchangeable(self):
        self.mutate('result.json', lambda d: d.update(container_exit=False))
        with self.assertRaisesRegex(ValueError, 'incomplete Stock'):
            self.bindings()

    def test_nonimage_frontier_artifact_is_rejected(self):
        (self.root / 'after.png').write_bytes(b'not a screenshot')
        with self.assertRaisesRegex(ValueError, 'PNG'):
            self.bindings()


class ReferenceDifferenceInventoryTest(unittest.TestCase):
    """Recorded inventory fixtures; no official-content or runtime authority claim."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sl-reference-inventory-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.references = {'schema': 1, 'references': []}
        self.inventories = {}
        for role, files in (
                ('older', {'Stock.exe': b'older executable', 'shared.cfg': b'same',
                           'changed.class': b'old script', 'older-only.scx': b'old mesh'}),
                ('newer', {'Stock.exe': b'newer executable', 'shared.cfg': b'same',
                           'changed.class': b'new script', 'newer-only.wav': b'new audio'})):
            identities = {path: self.identity(raw) for path, raw in files.items()}
            inventory = {'complete': True, 'exclusions': [], 'errors': [], 'files': identities,
                         'hashed_bytes': sum(len(raw) for raw in files.values())}
            self.inventories[role] = inventory
            inventory_path = self.root / (role + '.json')
            inventory_path.write_text(json.dumps(inventory))
            self.references['references'].append({
                'id': role + '-reference', 'role': role, 'label': role + ' declared build',
                'inventory': {'path': inventory_path.name,
                              'sha256': self.identity(inventory_path.read_bytes())['sha256']},
                'executable': {'path': 'Stock.exe', **identities['Stock.exe']}})
        self.manifest = self.root / 'references.json'
        self.manifest.write_text(json.dumps(self.references))

    @staticmethod
    def identity(raw):
        import hashlib
        return {'size': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}

    def rebind(self, role, inventory):
        path = self.root / (role + '.json')
        path.write_text(json.dumps(inventory))
        reference = next(r for r in self.references['references'] if r['role'] == role)
        reference['inventory']['sha256'] = self.identity(path.read_bytes())['sha256']
        self.manifest.write_text(json.dumps(self.references))

    def test_two_bound_references_account_for_shared_changed_and_unique_content_without_promoting_parity(self):
        report = catalog.reference_difference_inventory(self.manifest)
        rows = {row['path']: row for row in report['content']}
        self.assertEqual({path: row['relation'] for path, row in rows.items()}, {
            'Stock.exe': 'changed_bytes', 'shared.cfg': 'same_bytes',
            'changed.class': 'changed_bytes', 'older-only.scx': 'older_only',
            'newer-only.wav': 'newer_only'})
        self.assertEqual(rows['changed.class']['older']['identity'],
                         self.inventories['older']['files']['changed.class'])
        self.assertEqual(rows['changed.class']['newer']['identity'],
                         self.inventories['newer']['files']['changed.class'])
        self.assertIsNone(rows['older-only.scx']['newer'])
        self.assertIsNone(rows['newer-only.wav']['older'])
        for row in rows.values():
            self.assertEqual(row['semantic_coverage'], 'unverified')
            self.assertEqual(row['decision_status'], 'pending')
            self.assertIsNone(row['selected_behavior'])
            self.assertIsNone(row['affected_interfaces'])
            self.assertIsNone(row['affected_saves'])
            self.assertEqual(row['paired_proofs'], [])
            for role in ('older', 'newer'):
                if row[role] is not None:
                    self.assertEqual(row[role]['provenance'], 'unknown')
        self.assertEqual(report['summary']['unclassified_file_occurrences'], 8)
        self.assertEqual(report['summary']['semantic_pending_paths'], 5)
        self.assertEqual(report['summary']['paired_proofs'], 0)

    def test_cli_retains_difference_artifact_and_refuses_to_replace_prior_output(self):
        output = self.root / 'comparison'
        command = [sys.executable, '-I', str(catalog.HERE / 'catalog.py'),
                   '--references', str(self.manifest), '--out', str(output)]
        first = subprocess.run(command, capture_output=True, text=True, timeout=5)
        self.assertEqual(first.returncode, 0, first.stderr)
        result = json.loads((output / 'result.json').read_text())
        artifact = output / result['differences']['path']
        self.assertEqual(result['differences']['sha256'], self.identity(artifact.read_bytes())['sha256'])
        self.assertEqual(json.loads(artifact.read_text())['summary']['paths'], 5)
        before = {p.name: p.read_bytes() for p in output.iterdir()}
        repeated = subprocess.run(command, capture_output=True, text=True, timeout=5)
        self.assertNotEqual(repeated.returncode, 0)
        self.assertEqual({p.name: p.read_bytes() for p in output.iterdir()}, before)

    def test_unsupported_file_kinds_cannot_be_compared_as_regular_file_bytes(self):
        inventory = copy.deepcopy(self.inventories['older'])
        inventory['files']['shared.cfg']['kind'] = 'symlink'
        self.rebind('older', inventory)
        with self.assertRaisesRegex(ValueError, 'regular-file'):
            catalog.reference_difference_inventory(self.manifest)

    def test_explicit_prefixes_reuse_a_bound_collection_without_counting_other_collections(self):
        collection = {'complete': True, 'exclusions': [], 'errors': [], 'files': {}, 'hashed_bytes': 0}
        for role, inventory in self.inventories.items():
            collection['files'].update({role + '/' + path: identity for path, identity in inventory['files'].items()})
        collection['files']['research/lead.cpp'] = self.identity(b'research lead')
        collection['hashed_bytes'] = sum(record['size'] for record in collection['files'].values())
        path = self.root / 'collection.json'
        path.write_text(json.dumps(collection))
        for reference in self.references['references']:
            reference['inventory'] = {'path': path.name, 'sha256': self.identity(path.read_bytes())['sha256'],
                                      'prefix': reference['role']}
        self.manifest.write_text(json.dumps(self.references))
        report = catalog.reference_difference_inventory(self.manifest)
        self.assertEqual(report['summary']['paths'], 5)
        self.assertEqual(report['summary']['unclassified_file_occurrences'], 8)
        self.assertNotIn('research/lead.cpp', {row['path'] for row in report['content']})
        for role in ('older', 'newer'):
            self.assertEqual(report['references'][role]['inventory']['prefix'], role)

    def test_row_ids_survive_reference_order_file_order_content_changes_and_unrelated_additions(self):
        before = {row['path']: row['requirement_id'] for row in catalog.reference_difference_inventory(self.manifest)['content']}
        self.references['references'].reverse()
        inventory = copy.deepcopy(self.inventories['newer'])
        inventory['files']['changed.class'] = self.identity(b'a subsequent recorded script')
        inventory['files']['unrelated.cfg'] = self.identity(b'another file')
        inventory['files'] = dict(reversed(list(inventory['files'].items())))
        inventory['hashed_bytes'] = sum(record['size'] for record in inventory['files'].values())
        self.rebind('newer', inventory)
        after = {row['path']: row['requirement_id'] for row in catalog.reference_difference_inventory(self.manifest)['content']}
        self.assertEqual({path: after[path] for path in before}, before)
        self.assertEqual(len(set(after.values())), 6)

    def test_corrupt_incomplete_and_mismatched_recorded_inventories_refuse(self):
        for mutation in ('changed-binding', 'incomplete', 'excluded', 'error', 'bool-size',
                         'wrong-total', 'bad-path', 'executable-changed', 'executable-missing'):
            inventory = copy.deepcopy(self.inventories['older'])
            if mutation == 'changed-binding':
                self.rebind('older', inventory)
                with (self.root / 'older.json').open('a') as stream:
                    stream.write('\n')
            else:
                if mutation == 'incomplete': inventory['complete'] = False
                elif mutation == 'excluded': inventory['exclusions'] = [{'path': 'save/'}]
                elif mutation == 'error': inventory['errors'] = [{'path': 'unknown.class'}]
                elif mutation == 'bool-size': inventory['files']['shared.cfg']['size'] = True
                elif mutation == 'wrong-total': inventory['hashed_bytes'] += 1
                elif mutation == 'bad-path': inventory['files']['../escape'] = inventory['files'].pop('shared.cfg')
                elif mutation == 'executable-changed': inventory['files']['Stock.exe']['sha256'] = '0' * 64
                else:
                    removed = inventory['files'].pop('Stock.exe')
                    inventory['hashed_bytes'] -= removed['size']
                self.rebind('older', inventory)
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                catalog.reference_difference_inventory(self.manifest)

    def test_duplicate_authorities_ambiguous_roles_and_unsafe_bindings_refuse(self):
        original = copy.deepcopy(self.references)
        for mutation in ('id', 'role', 'scope', 'third', 'path', 'prefix', 'official'):
            self.references = copy.deepcopy(original)
            first, second = self.references['references']
            if mutation == 'id': second['id'] = first['id']
            elif mutation == 'role': second['role'] = first['role']
            elif mutation == 'scope': second['inventory'] = first['inventory']
            elif mutation == 'third': self.references['references'].append(copy.deepcopy(first))
            elif mutation == 'path': first['inventory']['path'] = '../escape.json'
            elif mutation == 'prefix': first['inventory']['prefix'] = 'older/../newer'
            else: first['official'] = True
            self.manifest.write_text(json.dumps(self.references))
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                catalog.reference_difference_inventory(self.manifest)

    def test_duplicate_file_paths_and_symlink_inventory_sources_refuse(self):
        path = self.root / 'older.json'
        raw = path.read_text().replace('"shared.cfg": ', '"shared.cfg": {}, "shared.cfg": ', 1)
        path.write_text(raw)
        self.references['references'][0]['inventory']['sha256'] = self.identity(path.read_bytes())['sha256']
        self.manifest.write_text(json.dumps(self.references))
        with self.assertRaisesRegex(ValueError, 'duplicate key'):
            catalog.reference_difference_inventory(self.manifest)
        self.rebind('older', self.inventories['older'])
        moved = path.with_name('original-older.json')
        path.rename(moved)
        path.symlink_to(moved)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            catalog.reference_difference_inventory(self.manifest)

    def test_overlapping_prefixes_cannot_count_one_collection_twice(self):
        for prefix in ('', 'group'):
            files = {('group/' if prefix else '') + name: identity
                     for name, identity in self.inventories['older']['files'].items()}
            files.update({'group/sub/' + name: identity
                          for name, identity in self.inventories['newer']['files'].items()})
            collection = {'complete': True, 'exclusions': [], 'errors': [], 'files': files,
                          'hashed_bytes': sum(record['size'] for record in files.values())}
            path = self.root / 'overlap.json'
            path.write_text(json.dumps(collection))
            for reference in self.references['references']:
                selected = prefix if reference['role'] == 'older' else 'group/sub'
                reference['inventory'] = {'path': path.name, 'sha256': self.identity(path.read_bytes())['sha256']}
                if selected:
                    reference['inventory']['prefix'] = selected
            self.manifest.write_text(json.dumps(self.references))
            with self.subTest(prefix=prefix), self.assertRaisesRegex(ValueError, 'overlap'):
                catalog.reference_difference_inventory(self.manifest)

    def test_oversized_identity_document_refuses_before_parsing(self):
        with self.manifest.open('r+b') as stream:
            stream.truncate(32 * 1024**2 + 1)
        with self.assertRaisesRegex(ValueError, '32 MiB'):
            catalog.reference_difference_inventory(self.manifest)

    def test_fifo_substitution_between_stat_and_open_refuses_without_blocking(self):
        # Substitute an actual FIFO at the open boundary, then call the real
        # OS open with the reader's flags. The child deadline bounds old code
        # that blocks waiting for a FIFO writer; no descriptor/result is faked.
        script = '''
import importlib.util
import os
from pathlib import Path
import stat
import sys

spec = importlib.util.spec_from_file_location('catalog_fifo_race', sys.argv[1])
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)
source = Path(sys.argv[2])
original_open = os.open
substituted = False
def substitute(path, flags, *args, **kwargs):
    global substituted
    if Path(path) == source and not substituted:
        substituted = True
        source.rename(source.with_name('original-identities.json'))
        os.mkfifo(source)
        assert stat.S_ISFIFO(source.stat().st_mode)
        print('FIFO substituted', flush=True)
    return original_open(path, flags, *args, **kwargs)
catalog.os.open = substitute
try:
    catalog.reference_difference_inventory(source)
except ValueError as error:
    assert substituted and stat.S_ISFIFO(source.stat().st_mode)
    print('refused:', error)
else:
    raise AssertionError('FIFO was accepted as an identity document')
'''
        try:
            result = subprocess.run([sys.executable, '-I', '-c', script,
                                     str(catalog.HERE / 'catalog.py'), str(self.manifest)],
                                    capture_output=True, text=True, timeout=2)
        except subprocess.TimeoutExpired as error:
            self.assertIn(b'FIFO substituted', error.stdout or b'')
            self.fail('identity reader blocked after its source was replaced with a FIFO')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('FIFO substituted', result.stdout)
        self.assertIn('refused:', result.stdout)


if __name__ == '__main__':
    unittest.main()
