"""Release phases qualify refusal/identity/handle boundaries without credentials."""
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import subprocess
import signal
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


class ReleaseService:
    """Explicit GitHub/signing process boundary; never contacts a service."""
    def __init__(self, case):
        self.case = case
        self.calls = []
        self.bytes = {}
        self.row = None
        self.remote_commit = case.commit
        self.corrupt_download = False
        self.after_assessment = None
        self.fail_download_once = False
        self.publish_timeout_once = False
        self.create_timeout_once = False
        self.asset_id = 100

    def __call__(self, *args):
        self.calls.append(args)
        if args[0] == 'git':
            if 'rev-parse' in args:
                return self.case.commit + '\n'
            if 'status' in args:
                return ''
            if 'show' in args:
                return (self.case.root / args[-1].split(':', 1)[1]).read_text()
            if 'ls-remote' in args:
                # A fork origin deliberately has the accepted tag; only querying
                # the actual publication repository exposes a wrong upstream tag.
                commit = self.case.commit if 'origin' in args else self.remote_commit
                return commit + '\trefs/tags/1.0.0\n'
        if args[0] == 'spctl':
            if self.after_assessment:
                self.after_assessment(args[-1])
            return ''
        if args[0] in ('xcrun', 'hdiutil'):
            return ''
        if args[:2] == ('gh', 'api'):
            if 'POST' in args:
                payload = json.loads(Path(args[args.index('--input') + 1]).read_text())
                self.row = dict(payload, id=37, assets=[])
                if self.create_timeout_once:
                    self.create_timeout_once = False
                    raise ValueError('creation observation timeout')
            elif 'PATCH' in args:
                self.row['draft'] = False
                if self.publish_timeout_once:
                    self.publish_timeout_once = False
                    raise ValueError('publication observation timeout')
            return json.dumps(self.row)
        if args[:3] == ('gh', 'release', 'upload'):
            path = Path(args[4]); data = path.read_bytes()
            self.bytes[path.name] = data
            self.asset_id += 1
            self.row['assets'].append({'id': self.asset_id, 'name': path.name,
                'state': 'uploaded', 'size': len(data), 'digest': 'sha256:' + hashlib.sha256(data).hexdigest()})
            return ''
        if args[:3] == ('gh', 'release', 'download'):
            if self.fail_download_once:
                self.fail_download_once = False
                raise ValueError('download observation timeout')
            name = args[args.index('--pattern') + 1]
            destination = Path(args[args.index('--dir') + 1])
            (destination / name).write_bytes(b'corrupt download' if self.corrupt_download else self.bytes[name])
            return ''
        if args[:3] == ('gh', 'release', 'view'):
            return json.dumps({'databaseId': self.row['id']})
        raise AssertionError('unexpected command: ' + repr(args))

    def count(self, method):
        return sum(call[:2] == ('gh', 'api') and method in call for call in self.calls)


class NotaryService:
    """Simulates durable server jobs across failed client observations."""
    def __init__(self, case):
        self.case = case
        self.calls = []
        self.jobs = []
        self.submit_timeout = False
        self.timeout_handle = False
        self.wait_timeout = False
        self.staple_timeout = False
        self.validate_failure = False
        self.staple_inputs = []
        self.pending_log = False

    def __call__(self, argv, **kwargs):
        self.calls.append(argv)
        operation = argv[2]
        if argv[1] == 'notarytool':
            if operation == 'submit':
                # This check runs at the service boundary, before acceptance.
                intent = self.case.root / 'notary.json.intent.json'
                assert intent.exists(), 'upload has no durable intent'
                artifact = Path(argv[3])
                identifier = '12345678-1234-1234-1234-123456789abc'
                row = {'id': identifier, 'name': artifact.name,
                       'createdDate': json.loads(intent.read_text())['started_at'],
                       'status': 'Accepted', 'sha256': self.case.release.digest(artifact)}
                self.jobs.append(row)
                if self.submit_timeout:
                    raise subprocess.TimeoutExpired(argv, 360,
                        output=json.dumps({'id': identifier}).encode() if self.timeout_handle else b'upload accepted; connection lost',
                        stderr=b'fixture transport timeout')
                value = {'id': identifier}
            elif operation == 'history':
                value = {'history': [{k:v for k,v in row.items() if k != 'sha256'} for row in self.jobs]}
            elif operation == 'info':
                value = next(row for row in self.jobs if row['id'] == argv[3])
            elif operation == 'log':
                row = next(row for row in self.jobs if row['id'] == argv[3])
                if self.pending_log:
                    return subprocess.CompletedProcess(argv, 1, '', 'job not complete')
                value = {'jobId': row['id'], 'archiveFilename': row['name'], 'sha256': row['sha256']}
            elif operation == 'wait':
                if self.wait_timeout:
                    self.wait_timeout = False
                    raise subprocess.TimeoutExpired(argv, 360, output=b'pending', stderr=b'fixture wait timeout')
                value = {'id': argv[3], 'status': 'Accepted'}
            else:
                raise AssertionError(argv)
            return subprocess.CompletedProcess(argv, 0, json.dumps(value), '')
        if argv[1] == 'stapler':
            target = Path(argv[-1])
            if target.is_dir():
                target = target / 'Contents/data'
            if operation == 'staple':
                self.staple_inputs.append(target.read_bytes())
                target.write_bytes(target.read_bytes() + b' stapled ticket')
                if self.staple_timeout:
                    self.staple_timeout = False
                    raise subprocess.TimeoutExpired(argv, 360, output=b'partial staple', stderr=b'lost observation')
            elif operation == 'validate' and self.validate_failure:
                self.validate_failure = False
                return subprocess.CompletedProcess(argv, 1, '', 'temporary validate failure')
            return subprocess.CompletedProcess(argv, 0, '', '')
        raise AssertionError(argv)


class DistributionTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('distribution', ROOT / 'tools/release/distribute.py')
        self.release = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.release)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.artifact = self.root / 'candidate.dmg'
        self.artifact.write_bytes(b'exact candidate fixture')
        self.commit = 'a' * 40

    def write(self, name, value):
        path = self.root / name
        path.write_text(json.dumps(value))
        return path

    def bind(self, path):
        return {'path': str(path), 'sha256': self.release.digest(path)}

    def audit_fixture(self):
        identifiers = [*(f'US{i:02}' for i in range(1, 47)), *(f'D{i}' for i in range(1, 9))]
        mapping = self.root / 'requirements.md'
        mapping.write_text('\n'.join(f'| {identifier} — fixture | implementation | reference | acceptance | fixture; verified | done |' for identifier in identifiers))
        specification = self.root / 'specification.md'
        specification.write_text('bound specification fixture')
        evidence = self.write('evidence.json', {'fixture': True})
        checks = {'signature','notarization','gatekeeper','dependency_closure','first_launch',
                  'input','audio','settings','save_reload','replacement_preserves_progress','sustained_play'}
        clean = self.write('clean.json', {'dmg_sha256': self.release.digest(self.artifact),
            'source_commit': self.commit, 'architecture': 'arm64', 'clean_machine': True,
            'hardware': 'fixture only', 'macos': 'fixture only', 'tested_limits': 'fixture only',
            'checks': {name: True for name in checks}, 'evidence': [self.bind(evidence)]})
        audit = {'schema': 1, 'version': '1.0.0', 'source_commit': self.commit, 'team': 'ABCDEFGHIJ',
                 'specification': self.bind(specification), 'requirement_map': self.bind(mapping),
                 'requirements': {name: {'status': 'verified', 'evidence': [self.bind(evidence)]} for name in identifiers},
                 'clean_installation': self.bind(clean)}
        for name in ('standards_review','specification_review','jev_completion','integration_gate'):
            record = self.write(name+'.json', {'source_commit': self.commit, 'accepted': True, 'unresolved': []})
            audit[name] = self.bind(record)
        return self.write('audit.json', audit), mapping, specification

    def test_complete_fixture_audit_binds_every_requirement_and_candidate(self):
        audit, mapping, specification = self.audit_fixture()
        self.release.validate_audit(audit, self.artifact, self.commit,
                                    requirement_map=mapping, specification=specification)
        self.artifact.write_bytes(b'changed after acceptance')
        with self.assertRaisesRegex(ValueError, 'exact-DMG'):
            self.release.validate_audit(audit, self.artifact, self.commit,
                                        requirement_map=mapping, specification=specification)

    def test_unverified_map_refuses_even_if_audit_claims_verified(self):
        audit, mapping, specification = self.audit_fixture()
        mapping.write_text(mapping.read_text().replace('fixture; verified', 'fixture; implemented but unverified', 1))
        value = json.loads(audit.read_text()); value['requirement_map'] = self.bind(mapping)
        audit.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, 'map is incomplete'):
            self.release.validate_audit(audit, self.artifact, self.commit,
                                        requirement_map=mapping, specification=specification)

    def test_missing_requirement_corrupt_evidence_and_unresolved_review_refuse(self):
        audit, mapping, specification = self.audit_fixture()
        original = json.loads(audit.read_text())
        variants = []
        missing = json.loads(json.dumps(original)); missing['requirements'].pop('US22'); variants.append(missing)
        corrupt = json.loads(json.dumps(original)); corrupt['requirements']['D8']['evidence'][0]['sha256'] = '0'*64; variants.append(corrupt)
        review = self.write('bad-review.json', {'source_commit': self.commit, 'accepted': True, 'unresolved': ['missing parity']})
        unresolved = json.loads(json.dumps(original)); unresolved['jev_completion'] = self.bind(review); variants.append(unresolved)
        for value in variants:
            with self.subTest(value=value):
                audit.write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    self.release.validate_audit(audit, self.artifact, self.commit,
                                                requirement_map=mapping, specification=specification)

    def test_current_project_map_cannot_authorize_publication(self):
        audit, _, _ = self.audit_fixture()
        value = json.loads(audit.read_text())
        value['requirement_map'] = self.bind(self.release.MAP)
        value['specification'] = self.bind(self.release.SPEC)
        audit.write_text(json.dumps(value))
        notes = self.root / 'notes.md'; notes.write_text('fixture')
        calls = []
        def command(*args):
            calls.append(args)
            if args[0] == 'git' and 'rev-parse' in args:
                return self.commit+'\n'
            self.fail('incomplete product reached a publication command: '+repr(args))
        with mock.patch.object(self.release, 'command', side_effect=command):
            with self.assertRaisesRegex(ValueError, 'map is incomplete'):
                self.release.publish(audit, self.artifact, notes, self.root/'download')
        self.assertEqual(len(calls), 1)
        self.assertTrue((self.root/'download/inputs').is_dir())
        self.assertFalse((self.root/'download/draft-handle.json').exists())

    def publication(self):
        audit, mapping, specification = self.audit_fixture()
        notes = self.root / 'notes.md'; notes.write_text('final accepted notes')
        service = ReleaseService(self)
        stage = self.release.load_stage()
        patches = mock.patch.multiple(self.release, ROOT=self.root, MAP=mapping, SPEC=specification)
        return audit, notes, service, stage, patches

    def publish_fixture(self, *, mutation=None, corrupt=False, wrong_tag=False):
        audit, notes, service, stage, patches = self.publication()
        service.after_assessment = mutation
        service.corrupt_download = corrupt
        if wrong_tag:
            service.remote_commit = 'b' * 40
        destination = self.root / 'publication'
        with patches, mock.patch.object(self.release, 'load_stage', return_value=stage), mock.patch.object(self.release, 'command', side_effect=service), mock.patch.object(self.release, 'verify_developer_id'):
            result = self.release.publish(audit, self.artifact, notes, destination)
        return result, service, destination

    def test_publication_requires_matching_download_and_keeps_installation_unverified(self):
        result, service, destination = self.publish_fixture()
        self.assertTrue(result['download_verified'])
        self.assertFalse(result['downloaded_installation_verified'])
        self.assertEqual((service.count('POST'), service.count('PATCH')), (1, 1))
        self.assertFalse(service.row['draft'])
        patch_index = next(i for i, call in enumerate(service.calls) if call[:2] == ('gh', 'api') and 'PATCH' in call)
        downloads = [i for i, call in enumerate(service.calls) if call[:3] == ('gh', 'release', 'download')]
        self.assertEqual(sum(i < patch_index for i in downloads), 2)
        self.assertEqual(sum(i > patch_index for i in downloads), 2)
        self.assertTrue((destination / 'draft-handle.json').exists())
        self.assertTrue((destination / 'publish-request.json').exists())

    def test_changed_download_keeps_release_private_and_retains_handle(self):
        audit, notes, service, stage, patches = self.publication()
        service.corrupt_download = True
        destination = self.root / 'bad-download'
        with patches, mock.patch.object(self.release, 'load_stage', return_value=stage), mock.patch.object(self.release, 'command', side_effect=service), mock.patch.object(self.release, 'verify_developer_id'):
            with self.assertRaisesRegex(ValueError, 'identity differs'):
                self.release.publish(audit, self.artifact, notes, destination)
        self.assertTrue(service.row['draft'])
        self.assertEqual(service.count('PATCH'), 0)
        self.assertTrue((destination / 'draft-handle.json').exists())
        self.assertTrue(list(destination.glob('draft-download-*')))

    def test_publication_never_uploads_mutated_original_inputs_after_acceptance(self):
        original = self.artifact.read_bytes()
        def change_original(_):
            self.artifact.write_bytes(b'changed original after acceptance')
        result, service, _ = self.publish_fixture(mutation=change_original)
        self.assertEqual(service.bytes[self.artifact.name], original)
        self.assertEqual(result['dmg_sha256'], hashlib.sha256(original).hexdigest())

    def test_changed_frozen_input_refuses_before_draft_creation(self):
        audit, notes, service, stage, patches = self.publication()
        def corrupt_frozen(path):
            Path(path).chmod(0o600) # Deliberate fault injection bypasses the read-only snapshot.
            Path(path).write_bytes(b'changed frozen input')
        service.after_assessment = corrupt_frozen
        with patches, mock.patch.object(self.release, 'load_stage', return_value=stage), mock.patch.object(self.release, 'command', side_effect=service), mock.patch.object(self.release, 'verify_developer_id'):
            with self.assertRaisesRegex(ValueError, 'frozen publication input changed'):
                self.release.publish(audit, self.artifact, notes, self.root / 'changed-frozen')
        self.assertEqual(service.count('POST'), 0)

    def test_origin_tag_cannot_authorize_another_publication_repository(self):
        audit, notes, service, stage, patches = self.publication()
        service.remote_commit = 'b' * 40
        with patches, mock.patch.object(self.release, 'load_stage', return_value=stage), mock.patch.object(self.release, 'command', side_effect=service), mock.patch.object(self.release, 'verify_developer_id'):
            with self.assertRaisesRegex(ValueError, 'publication repository tag'):
                self.release.publish(audit, self.artifact, notes, self.root / 'wrong-tag')
        self.assertEqual(service.count('POST'), 0)

    def test_draft_download_timeout_resumes_same_handle_without_upload_or_create(self):
        audit, notes, service, stage, patches = self.publication()
        service.fail_download_once = True
        destination = self.root / 'resume-draft'
        with patches, mock.patch.object(self.release, 'load_stage', return_value=stage), mock.patch.object(self.release, 'command', side_effect=service), mock.patch.object(self.release, 'verify_developer_id'):
            with self.assertRaisesRegex(ValueError, 'download observation timeout'):
                self.release.publish(audit, self.artifact, notes, destination)
            self.assertTrue(service.row['draft'])
            uploads = sum(call[:3] == ('gh', 'release', 'upload') for call in service.calls)
            result = self.release.continue_publication(destination)
            self.assertTrue(result['download_verified'])
        self.assertEqual((service.count('POST'), service.count('PATCH')), (1, 1))
        self.assertEqual(sum(call[:3] == ('gh', 'release', 'upload') for call in service.calls), uploads)
        self.assertEqual(len(list(destination.glob('attempt-*'))), 2)

    def test_publication_timeout_observes_existing_public_release_without_republishing(self):
        audit, notes, service, stage, patches = self.publication()
        service.publish_timeout_once = True
        destination = self.root / 'resume-public'
        with patches, mock.patch.object(self.release, 'load_stage', return_value=stage), mock.patch.object(self.release, 'command', side_effect=service), mock.patch.object(self.release, 'verify_developer_id'):
            with self.assertRaisesRegex(ValueError, 'publication observation timeout'):
                self.release.publish(audit, self.artifact, notes, destination)
            self.assertFalse(service.row['draft'])
            result = self.release.continue_publication(destination)
        self.assertTrue(result['download_verified'])
        self.assertEqual((service.count('POST'), service.count('PATCH')), (1, 1))

    def test_creation_timeout_recovers_draft_by_tag_without_another_create(self):
        audit, notes, service, stage, patches = self.publication()
        service.create_timeout_once = True
        destination = self.root / 'resume-creation'
        with patches, mock.patch.object(self.release, 'load_stage', return_value=stage), mock.patch.object(self.release, 'command', side_effect=service), mock.patch.object(self.release, 'verify_developer_id'):
            with self.assertRaisesRegex(ValueError, 'creation observation timeout'):
                self.release.publish(audit, self.artifact, notes, destination)
            self.assertTrue((destination / 'publication-request.json').exists())
            self.assertFalse((destination / 'draft-handle.json').exists())
            result = self.release.continue_publication(destination)
        self.assertTrue(result['download_verified'])
        self.assertEqual((service.count('POST'), service.count('PATCH')), (1, 1))

    def test_resume_after_preflight_failure_cannot_create_or_adopt_a_draft(self):
        audit, notes, service, stage, patches = self.publication()
        destination = self.root / 'failed-preflight'
        with patches, mock.patch.object(self.release, 'load_stage', return_value=stage), mock.patch.object(self.release, 'command', side_effect=service), mock.patch.object(self.release, 'verify_developer_id', side_effect=ValueError('invalid signature')):
            with self.assertRaisesRegex(ValueError, 'invalid signature'):
                self.release.publish(audit, self.artifact, notes, destination)
            with self.assertRaises(FileNotFoundError):
                self.release.continue_publication(destination)
        self.assertEqual(service.count('POST'), 0)
        self.assertFalse(any(call[:3] == ('gh', 'release', 'view') for call in service.calls))

    def test_strict_documents_reject_duplicate_keys_and_nonfinite_values(self):
        document=self.root/'invalid.json'
        for text in ('{"schema":1,"schema":2}', '{"value":NaN}'):
            document.write_text(text)
            with self.assertRaises(ValueError):
                self.release.document(document)

    def test_submission_receipt_preserves_existing_handle_across_wait_timeout(self):
        receipt = self.root / 'receipt.json'
        identifier = '12345678-1234-1234-1234-123456789abc'
        with mock.patch.object(self.release, 'notary_command', return_value=json.dumps({'id': identifier})) as run:
            self.release.submit(self.artifact, self.artifact, 'stored-profile', receipt)
        self.assertEqual(run.call_count, 1)
        with mock.patch.object(self.release, 'notary_command', side_effect=ValueError('observation timeout')) as run:
            with self.assertRaisesRegex(ValueError, 'timeout'):
                self.release.finish(receipt, self.root/'finished.json')
        self.assertEqual(run.call_args.args[2:5], ('notarytool','wait',identifier))
        self.assertEqual(json.loads(receipt.read_text())['submission_id'], identifier)
        calls=[]
        def accepted(*args):
            calls.append(args)
            return json.dumps({'id':identifier,'status':'Accepted'}) if 'wait' in args else ''
        with mock.patch.object(self.release, 'notary_command', side_effect=accepted):
            result=self.release.finish(receipt, self.root/'finished.json')
        self.assertEqual(result['status'],'notarized_and_stapled')
        self.assertFalse(any('submit' in call for call in calls))
        self.assertFalse(result['release_accepted'])

    def test_changed_submitted_artifact_refuses_before_wait(self):
        receipt=self.root/'receipt.json'
        with mock.patch.object(self.release,'notary_command',return_value=json.dumps({'id':'12345678-1234-1234-1234-123456789abc'})):
            self.release.submit(self.artifact,self.artifact,'profile',receipt)
        self.artifact.write_bytes(b'corrupt')
        with mock.patch.object(self.release,'notary_command') as run:
            with self.assertRaisesRegex(ValueError,'submitted artifact changed'):
                self.release.finish(receipt,self.root/'finished.json')
            run.assert_not_called()

    def test_rejected_notarization_never_staples(self):
        receipt=self.root/'receipt.json'
        identifier='12345678-1234-1234-1234-123456789abc'
        with mock.patch.object(self.release,'notary_command',return_value=json.dumps({'id':identifier})):
            self.release.submit(self.artifact,self.artifact,'profile',receipt)
        with mock.patch.object(self.release,'notary_command',return_value=json.dumps({'id':identifier,'status':'Invalid'})) as run:
            with self.assertRaisesRegex(ValueError,'notarization not accepted'):
                self.release.finish(receipt,self.root/'finished.json')
            self.assertEqual(run.call_count,1)
        observation=json.loads((self.root/'finished.json.wait.json').read_text())
        self.assertEqual(json.loads(observation['response'])['status'],'Invalid')

    def test_process_death_during_receipt_write_recovers_without_another_upload(self):
        child = self.root / 'interrupted-receipt.py'
        child.write_text(r'''import importlib.util,json,os,signal,sys
from pathlib import Path
from types import SimpleNamespace
from unittest import mock
root=Path(sys.argv[1]);fixture=Path(sys.argv[2])
spec=importlib.util.spec_from_file_location('distribution_test_fixture',root/'tests/test_release_distribution.py')
tests=importlib.util.module_from_spec(spec);spec.loader.exec_module(tests)
spec=importlib.util.spec_from_file_location('interrupted_distribution',Path(sys.argv[3]))
release=importlib.util.module_from_spec(spec);spec.loader.exec_module(release);release.ROOT=root
case=SimpleNamespace(root=fixture,release=release)
service=tests.NotaryService(case)
original_write=release.write_new

def server(argv,**kwargs):
 result=service(argv,**kwargs)
 if argv[2]=='submit':
  with (fixture/'server-jobs.json').open('w') as stream:
   json.dump(service.jobs,stream);stream.flush();os.fsync(stream.fileno())
 return result

def interrupted(path,value):
 if isinstance(value,dict) and 'submission_id' in value:
  with Path(path).open('x') as stream:
   stream.write('{"schema":2,');stream.flush();os.fsync(stream.fileno())
  with (fixture/'interrupted-path.txt').open('w') as stream:
   stream.write(str(path));stream.flush();os.fsync(stream.fileno())
  os.kill(os.getpid(),signal.SIGKILL)
 return original_write(path,value)

with mock.patch.object(release.subprocess,'run',side_effect=server),mock.patch.object(release,'write_new',side_effect=interrupted):
 release.submit(fixture/'candidate.dmg',fixture/'candidate.dmg','profile',fixture/'notary.json')
''')
        done = subprocess.run([sys.executable, '-I', str(child), str(ROOT), str(self.root), self.release.__file__],
                              capture_output=True, text=True, timeout=20)
        self.assertEqual(done.returncode, -signal.SIGKILL, done.stderr)
        receipt = self.root / 'notary.json'
        self.assertFalse(receipt.exists(), 'process death exposed a partial canonical receipt')
        partial = Path((self.root / 'interrupted-path.txt').read_text())
        self.assertEqual(partial.read_text(), '{"schema":2,')
        self.assertTrue(Path(str(receipt) + '.intent.json').exists())
        self.assertTrue(Path(str(receipt) + '.service.json').exists())
        service = NotaryService(self)
        service.jobs = json.loads((self.root / 'server-jobs.json').read_text())
        service.pending_log = True # The returned upload handle is usable before completion.
        with mock.patch.object(self.release.subprocess, 'run', side_effect=service):
            result = self.release.resume_submission(receipt)
        self.assertEqual(result['submission_id'], service.jobs[0]['id'])
        self.assertEqual(self.release.document(receipt)['submission_id'], service.jobs[0]['id'])
        self.assertEqual(len(service.jobs), 1)
        self.assertEqual(service.calls, [], 'known upload handle should not require completed history/logs')
        self.assertEqual(partial.read_text(), '{"schema":2,')

    def test_receipt_installation_never_clobbers_a_concurrent_handle(self):
        receipt = self.root / 'notary.json'
        service = NotaryService(self)
        existing = b'{"existing":"retained concurrent receipt"}'
        def concurrent_handle(argv, **kwargs):
            result = service(argv, **kwargs)
            if argv[2] == 'submit':
                receipt.write_bytes(existing)
            return result
        with mock.patch.object(self.release.subprocess, 'run', side_effect=concurrent_handle):
            with self.assertRaises(FileExistsError):
                self.release.submit(self.artifact, self.artifact, 'profile', receipt)
        self.assertEqual(receipt.read_bytes(), existing)
        pending = list(self.root.glob('notary.json.pending-*'))
        self.assertEqual(len(pending), 1)
        self.assertEqual(self.release.document(pending[0])['submission_id'], service.jobs[0]['id'])
        self.assertEqual(len(service.jobs), 1)

    def test_submit_timeout_retains_intent_diagnostics_and_recovers_exact_existing_job(self):
        receipt = self.root / 'notary.json'
        service = NotaryService(self); service.submit_timeout = True
        with mock.patch.object(self.release.subprocess, 'run', side_effect=service):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.release.submit(self.artifact, self.artifact, 'profile', receipt)
            self.assertFalse(receipt.exists())
            observation = self.release.document(str(receipt) + '.service.json')
            self.assertIn('transport timeout', observation['stderr'])
            with self.assertRaises(ValueError):
                self.release.submit(self.artifact, self.artifact, 'profile', receipt)
            result = self.release.resume_submission(receipt)
        self.assertEqual(result['submission_id'], service.jobs[0]['id'])
        self.assertEqual(sum(call[2] == 'submit' for call in service.calls), 1)
        self.assertEqual(sum(call[2] == 'log' for call in service.calls), 1)

    def test_submit_timeout_with_complete_handle_keeps_polling_handle(self):
        receipt = self.root / 'notary.json'
        service = NotaryService(self); service.submit_timeout = True; service.timeout_handle = True
        with mock.patch.object(self.release.subprocess, 'run', side_effect=service):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.release.submit(self.artifact, self.artifact, 'profile', receipt)
            self.assertEqual(self.release.resume_submission(receipt)['submission_id'], service.jobs[0]['id'])
        self.assertEqual(len(service.calls), 1)

    def test_saved_handle_from_another_command_cannot_replace_missing_receipt(self):
        receipt = self.root / 'notary.json'
        service = NotaryService(self)
        service.submit_timeout = True; service.timeout_handle = True
        with mock.patch.object(self.release.subprocess, 'run', side_effect=service):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.release.submit(self.artifact, self.artifact, 'profile', receipt)
            receipt.rename(self.root / 'retained-original-receipt.json')
            diagnostic = Path(str(receipt) + '.service.json')
            value = self.release.document(diagnostic)
            value['command'][5] = 'another-profile'
            diagnostic.write_text(json.dumps(value))
            service.pending_log = True
            with self.assertRaisesRegex(ValueError, 'notary action failed'):
                self.release.resume_submission(receipt)
        self.assertFalse(receipt.exists())
        self.assertEqual(sum(call[2] == 'submit' for call in service.calls), 1)

    def test_history_name_without_exact_hash_or_completed_log_never_recovers(self):
        receipt = self.root / 'notary.json'
        service = NotaryService(self); service.submit_timeout = True
        with mock.patch.object(self.release.subprocess, 'run', side_effect=service):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.release.submit(self.artifact, self.artifact, 'profile', receipt)
            service.jobs[0]['sha256'] = '0' * 64
            with self.assertRaisesRegex(ValueError, 'unknown or ambiguous'):
                self.release.resume_submission(receipt)
            service.pending_log = True
            with self.assertRaisesRegex(ValueError, 'notary action failed'):
                self.release.resume_submission(receipt)
        self.assertFalse(receipt.exists())
        self.assertEqual(sum(call[2] == 'submit' for call in service.calls), 1)

    def test_multiple_matching_jobs_refuse_and_changed_inputs_do_not_query_service(self):
        receipt = self.root / 'notary.json'
        service = NotaryService(self); service.submit_timeout = True
        with mock.patch.object(self.release.subprocess, 'run', side_effect=service):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.release.submit(self.artifact, self.artifact, 'profile', receipt)
            duplicate = dict(service.jobs[0], id='22345678-1234-1234-1234-123456789abc')
            service.jobs.append(duplicate)
            with self.assertRaisesRegex(ValueError, 'unknown or ambiguous'):
                self.release.resume_submission(receipt)
            count = len(service.calls)
            self.artifact.write_bytes(b'changed input')
            with self.assertRaisesRegex(ValueError, 'submitted artifact changed'):
                self.release.resume_submission(receipt)
            self.assertEqual(len(service.calls), count)
        self.assertFalse(receipt.exists())

    def test_finish_retries_wait_and_partial_staple_on_fresh_copies_without_resubmission(self):
        for failure in ('wait_timeout', 'staple_timeout', 'validate_failure'):
            with self.subTest(failure=failure):
                receipt = self.root / 'notary.json'
                if receipt.exists():
                    # Each subcase has its own paths and fixture intent.
                    for path in self.root.glob('notary.json*'):
                        if path.is_file(): path.unlink()
                service = NotaryService(self)
                original = self.artifact.read_bytes()
                output = self.root / ('finished-' + failure + '.json')
                with mock.patch.object(self.release.subprocess, 'run', side_effect=service):
                    self.release.submit(self.artifact, self.artifact, 'profile', receipt)
                    setattr(service, failure, True)
                    with self.assertRaises((subprocess.TimeoutExpired, ValueError)):
                        self.release.finish(receipt, output)
                    self.assertEqual(self.artifact.read_bytes(), original)
                    result = self.release.finish(receipt, output)
                self.assertEqual(result['status'], 'notarized_and_stapled')
                self.assertEqual(Path(result['staple_target']).read_bytes(), original + b' stapled ticket')
                self.assertTrue(all(data == original for data in service.staple_inputs))
                self.assertEqual(sum(call[2] == 'submit' for call in service.calls), 1)
                self.assertEqual(len(list(self.root.glob(output.name + '.attempt-*'))), 2)

    def test_finish_app_copy_preserves_original_and_target_content_binding(self):
        app = self.root / 'Game.app'
        (app / 'Contents').mkdir(parents=True)
        (app / 'Contents/data').write_bytes(b'app fixture')
        receipt = self.root / 'notary.json'
        service = NotaryService(self)
        with mock.patch.object(self.release.subprocess, 'run', side_effect=service):
            self.release.submit(self.artifact, app, 'profile', receipt)
            result = self.release.finish(receipt, self.root / 'app-finished.json')
        self.assertEqual((app / 'Contents/data').read_bytes(), b'app fixture')
        self.assertEqual((Path(result['staple_target']) / 'Contents/data').read_bytes(), b'app fixture stapled ticket')
        self.assertEqual(result['target_after_staple']['game_root'], result['staple_target'])

    def test_finish_refuses_changed_target_before_service_and_overlapping_outputs(self):
        app = self.root / 'Game.app'
        (app / 'Contents').mkdir(parents=True)
        payload = app / 'Contents/data'; payload.write_bytes(b'app fixture')
        receipt = self.root / 'notary.json'
        service = NotaryService(self)
        with mock.patch.object(self.release.subprocess, 'run', side_effect=service):
            with self.assertRaisesRegex(ValueError, 'overlaps'):
                self.release.submit(self.artifact, app, 'profile', app / 'receipt.json')
            self.assertFalse(service.calls)
            self.release.submit(self.artifact, app, 'profile', receipt)
            count = len(service.calls)
            with self.assertRaisesRegex(ValueError, 'overlaps'):
                self.release.finish(receipt, app / 'finish.json')
            payload.write_bytes(b'changed app fixture')
            with self.assertRaisesRegex(ValueError, 'target changed'):
                self.release.finish(receipt, self.root / 'changed-target.json')
            self.assertEqual(len(service.calls), count)

    def test_prepare_signs_nested_code_first_and_preserves_staged_input(self):
        stage=self.release.load_stage()
        app=self.root/'Staged.app';(app/'Contents/MacOS').mkdir(parents=True)
        (app/'Contents/Frameworks').mkdir()
        (app/'Contents/Frameworks/nested.dylib').write_bytes(b'nested fixture')
        (app/'Contents/MacOS/Game').write_bytes(b'game fixture')
        (app/'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable':'Game'}))
        before=stage.inventory(app)
        report=self.write('stage.json',{'status':'developer_candidate','source_bytes_preserved':True,'output':before})
        calls=[]
        def local(*args):
            calls.append(args)
            if args[0]=='ditto':
                Path(args[-1]).write_bytes(b'zip fixture')
            return ''
        with mock.patch.object(self.release,'command',side_effect=local), mock.patch.object(self.release,'verify_developer_id'):
            result=self.release.prepare(app,report,self.root/'signed','a'*40,'ABCDEFGHIJ')
        self.assertEqual(before['files'],stage.inventory(app)['files'])
        signed=Path(result['application'])
        self.assertEqual(plistlib.loads((signed/'Contents/Info.plist').read_bytes())['CFBundleShortVersionString'],'1.0.0')
        signatures=[Path(call[-1]) for call in calls if call[0]=='codesign']
        self.assertEqual(signatures,[signed/'Contents/Frameworks/nested.dylib',signed/'Contents/MacOS/Game',signed])
        self.assertFalse(result['release_accepted'])

    def test_reference_target_refused_before_submission(self):
        reference=self.root/'ref';reference.mkdir()
        target=reference/'protected.dmg';target.write_bytes(b'reference fixture')
        with mock.patch.object(self.release,'ROOT',self.root), mock.patch.object(self.release,'command') as run:
            with self.assertRaisesRegex(ValueError,'protected'):
                self.release.submit(target,target,'profile',self.root/'receipt.json')
            run.assert_not_called()
        self.assertEqual(target.read_bytes(),b'reference fixture')

    def test_real_dmg_contains_application_and_applications_link(self):
        app=self.root/'Game.app';(app/'Contents').mkdir(parents=True)
        (app/'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable':'Game'}))
        (app/'Contents/data').write_bytes(b'complete tiny payload fixture')
        out=self.root/'fixture.dmg'
        real=self.release.command
        def local_only(*args):
            if args[0] in ('codesign','xcrun'):
                return '' # No signing identity or notary access used by this control.
            return real(*args)
        with mock.patch.object(self.release,'verify_developer_id'), mock.patch.object(self.release,'command',side_effect=local_only):
            result=self.release.create_dmg(app,out,'a'*40,'ABCDEFGHIJ')
        self.assertEqual(result['sha256'],hashlib.sha256(out.read_bytes()).hexdigest())
        self.assertFalse(result['release_accepted'])
        mount=self.root/'mounted';mount.mkdir()
        attached=False
        try:
            subprocess.run(['hdiutil','attach','-readonly','-nobrowse','-mountpoint',str(mount),str(out)],check=True,capture_output=True,timeout=30)
            attached=True
            self.assertEqual((mount/'Game.app/Contents/data').read_bytes(),b'complete tiny payload fixture')
            self.assertTrue((mount/'Applications').is_symlink())
            self.assertEqual((mount/'Applications').readlink(),Path('/Applications'))
        finally:
            if attached:
                subprocess.run(['hdiutil','detach',str(mount)],check=True,capture_output=True,timeout=30)


if __name__ == '__main__':
    unittest.main()
