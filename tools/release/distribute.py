#!/usr/bin/env python3
"""Explicit release phases; no credentials stored, no implicit submission/publication.

Run through shared admission/retention. Fresh outputs preserve failed attempts.
Receipts separate submission from waiting: a timeout never resubmits an artifact.
Publication checks bound declarations and bytes, not semantic truth of evidence.
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import stat
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / 'docs/specs/full-port-1.0.0.md'
MAP = ROOT / 'docs/research/full-port-1.0.0-requirements.md'
VERSION = '1.0.0'
REPOSITORY = 'nicolas-found42/sl-remastered'


def load_stage():
    spec = importlib.util.spec_from_file_location('distribution_stage', ROOT / 'tools/release/stage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def digest(path):
    path = Path(path)
    before = path.stat(follow_symlinks=False)
    if not stat.S_ISREG(before.st_mode):
        raise ValueError('ordinary file required: ' + str(path))
    value = hashlib.sha256()
    with os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW), 'rb') as stream:
        if os.fstat(stream.fileno()) != before:
            raise ValueError('file changed before hashing')
        while block := stream.read(1024 * 1024):
            value.update(block)
        after = os.fstat(stream.fileno())
    # Access time can change during reads; bind data/identity metadata instead.
    identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
    if identity(before) != identity(after) or identity(before) != identity(path.stat(follow_symlinks=False)):
        raise ValueError('file changed while hashing')
    return value.hexdigest()


def document(path):
    path = Path(path)
    if path.stat(follow_symlinks=False).st_size > 8 * 1024 * 1024:
        raise ValueError('document exceeds 8 MiB')
    expected = digest(path)
    value = load_stage().catalog.parse_json(path.read_bytes())
    if digest(path) != expected or not isinstance(value, dict):
        raise ValueError('invalid or changed document')
    return value


def write_new(path, value):
    with Path(path).open('x', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    descriptor = os.open(Path(path).parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def command(*argv):
    result = subprocess.run([str(x) for x in argv], capture_output=True, text=True, timeout=360)
    if len(result.stdout) + len(result.stderr) > 1024 * 1024:
        raise ValueError('release command output exceeded 1 MiB')
    if result.returncode:
        raise ValueError(f'{argv[0]} exited {result.returncode}: {result.stderr.strip()}')
    return result.stdout


def absent_output(path):
    supplied = Path(path)
    if '..' in supplied.parts or supplied.is_symlink():
        raise ValueError('output traversal or symlink refused')
    path = supplied.resolve()
    if path.exists() or not path.parent.is_dir():
        raise ValueError('fresh output with existing parent required')
    for protected in (ROOT / 'ref', ROOT / '.git'):
        if path == protected or protected in path.parents:
            raise ValueError('protected output refused')
    return path


def verify_developer_id(app, team, *, runtime=True):
    if not re.fullmatch(r'[A-Z0-9]{10}', team):
        raise ValueError('explicit ten-character signing team required')
    command('codesign', '--verify', '--strict', '--deep', app)
    # codesign display intentionally writes to stderr even on success.
    result = subprocess.run(['codesign', '-d', '--verbose=4', str(app)], capture_output=True, text=True, timeout=30)
    text = result.stdout + result.stderr
    if (result.returncode or f'TeamIdentifier={team}\n' not in text or
            'Authority=Developer ID Application:' not in text or (runtime and 'runtime' not in text)):
        raise ValueError('Developer ID team/hardened runtime verification failed')


def prepare(application, stage_report, output, identity, team):
    stage = load_stage()
    app = stage.ordinary_path(application, directory=True)
    out = absent_output(output)
    report = document(stage_report)
    if report.get('status') != 'developer_candidate' or report.get('source_bytes_preserved') is not True:
        raise ValueError('complete staging report required')
    before = stage.inventory(app)
    stage.require_same_files(report['output'], before, 'staged candidate')
    if app == out or app in out.parents or out in app.parents:
        raise ValueError('candidate input/output overlap')
    if not re.fullmatch(r'[0-9A-Fa-f]{40}', identity):
        raise ValueError('explicit signing-certificate SHA-1 required')
    out.mkdir()
    candidate = out / app.name
    shutil.copytree(app, candidate, symlinks=True)
    stage.require_same_files(before, stage.inventory(candidate), 'copied candidate')
    plist = candidate / 'Contents/Info.plist'
    info = plistlib.loads(plist.read_bytes())
    info['CFBundleShortVersionString'] = VERSION
    info['CFBundleVersion'] = VERSION
    plist.write_bytes(plistlib.dumps(info, sort_keys=True))
    executable = candidate / 'Contents/MacOS' / info['CFBundleExecutable']
    nested = sorted((candidate / 'Contents/Frameworks').glob('*.dylib'))
    for item in [*nested, executable, candidate]:
        command('codesign', '--force', '--sign', identity, '--options', 'runtime', '--timestamp', item)
    verify_developer_id(candidate, team)
    stage.require_same_files(before, stage.inventory(app), 'source staged application')
    archive = out / 'application.zip'
    command('ditto', '-c', '-k', '--keepParent', candidate, archive)
    result = {'schema': 1, 'status': 'signed_candidate_not_release_accepted',
              'version': VERSION, 'team': team, 'application': str(candidate),
              'application_inventory': stage.inventory(candidate),
              'archive': str(archive), 'archive_sha256': digest(archive),
              'stage_report_sha256': digest(stage_report), 'release_accepted': False}
    write_new(out / 'signed-candidate.json', result)
    return result


def writable_artifact(path):
    path = Path(path).resolve(strict=True)
    for protected in (ROOT / 'ref', ROOT / '.git'):
        if path == protected or protected in path.parents:
            raise ValueError('protected notarization target refused')
    return path


def submission_id(value):
    if not isinstance(value, str) or not re.fullmatch(r'[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}', value):
        raise ValueError('notary service returned no submission identity')
    return value.lower()


def notary_command(diagnostics, *argv):
    """Retain the observation even when an external action's outcome is unknown."""
    row = {'command': [str(x) for x in argv]}
    def text(value):
        return value.decode('utf-8', errors='replace') if isinstance(value, bytes) else (value or '')
    try:
        result = subprocess.run(row['command'], capture_output=True, text=True, timeout=360)
        row.update(stdout=text(result.stdout), stderr=text(result.stderr), returncode=result.returncode)
    except (OSError, subprocess.SubprocessError) as error:
        row.update(stdout=text(getattr(error, 'stdout', None)),
                   stderr=text(getattr(error, 'stderr', None)), error=type(error).__name__ + ': ' + str(error))
        for field in ('stdout', 'stderr'):
            row[field] = row[field][:1024 * 1024]
        write_new(diagnostics, row)
        raise
    oversized = len(row['stdout']) + len(row['stderr']) > 1024 * 1024
    if oversized:
        row['output_truncated'] = True
        row['stdout'], row['stderr'] = row['stdout'][:512 * 1024], row['stderr'][:512 * 1024]
    write_new(diagnostics, row)
    if oversized or result.returncode:
        raise ValueError('notary action failed; inspect retained diagnostics: ' + str(diagnostics))
    return row['stdout']


def write_notary_receipt(path, value):
    # A killed writer may leave its pending attempt, but never a truncated
    # canonical handle. Hard-link installation is atomic and refuses clobbering.
    path = Path(path)
    pending = Path(str(path) + '.pending-' + uuid.uuid4().hex)
    write_new(pending, value)
    os.link(pending, path, follow_symlinks=False)
    descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
        pending.unlink()
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def target_inventory(target):
    return load_stage().inventory(target) if target.is_dir() else {'sha256': digest(target)}


def check_notary_inputs(row):
    artifact, target = Path(row['artifact']), writable_artifact(row['staple_target'])
    if digest(artifact) != row['artifact_sha256']:
        raise ValueError('submitted artifact changed')
    if target_inventory(target) != row['target_before_staple']:
        raise ValueError('staple target changed after submission')
    return artifact, target


def receipt_value(intent, result):
    return dict(intent, submission_id=submission_id(result.get('id')), response=result)


def submit(artifact, staple_target, profile, receipt):
    artifact = Path(artifact).resolve(strict=True)
    target = writable_artifact(staple_target)
    receipt = absent_output(receipt)
    intent_path = absent_output(str(receipt) + '.intent.json')
    service_output = absent_output(str(receipt) + '.service.json')
    if target.is_dir() and target in receipt.parents:
        raise ValueError('submission receipt overlaps staple target')
    if not isinstance(profile, str) or not profile.strip():
        raise ValueError('existing keychain profile required')
    intent = {'schema': 2, 'artifact': str(artifact), 'artifact_sha256': digest(artifact),
              'staple_target': str(target), 'target_before_staple': target_inventory(target),
              'profile': profile, 'receipt': str(receipt),
              'started_at': datetime.now(timezone.utc).isoformat(), 'release_accepted': False}
    # Exclusive, flushed intent precedes the only upload. Its existence refuses
    # a second submit even when the process dies before receiving a handle.
    write_new(intent_path, intent)
    try:
        raw = notary_command(service_output, 'xcrun', 'notarytool', 'submit', artifact,
                             '--keychain-profile', profile, '--output-format', 'json')
    except (OSError, ValueError, subprocess.SubprocessError):
        # A timeout/nonzero exit can still contain a complete service handle.
        # Keep it durably before propagating the failed observation.
        if service_output.exists():
            try:
                value = receipt_value(intent, response(document(service_output)['stdout']))
            except (ValueError, KeyError, TypeError):
                pass
            else:
                write_notary_receipt(receipt, value)
        raise
    value = receipt_value(intent, response(raw))
    write_notary_receipt(receipt, value)
    check_notary_inputs(intent)
    return value


def resume_submission(receipt):
    receipt = writable_artifact(receipt) if Path(receipt).exists() else absent_output(receipt)
    intent = document(str(receipt) + '.intent.json')
    if intent.get('schema') != 2 or intent.get('receipt') != str(receipt):
        raise ValueError('submission intent does not identify this receipt')
    artifact, target = check_notary_inputs(intent)
    if target.is_dir() and target in receipt.parents:
        raise ValueError('submission receipt overlaps staple target')
    if receipt.exists():
        row = document(receipt)
        if any(row.get(key) != value for key, value in intent.items()):
            raise ValueError('receipt differs from submission intent')
        submission_id(row['submission_id'])
        return row
    # A process can die after retaining the service response but before receipt
    # installation. Preserve that known handle even while its job is pending.
    service_path = Path(str(receipt) + '.service.json')
    if service_path.exists():
        try:
            observed = document(service_path)
            expected_command = ['xcrun', 'notarytool', 'submit', str(artifact),
                                '--keychain-profile', intent['profile'], '--output-format', 'json']
            if observed.get('command') != expected_command:
                raise ValueError('retained response belongs to another submission')
            recovered = receipt_value(intent, response(observed['stdout']))
        except (OSError, ValueError, KeyError, TypeError):
            pass
        else:
            check_notary_inputs(intent)
            write_notary_receipt(receipt, recovered)
            return recovered
    attempt = Path(str(receipt) + '.recovery-' + uuid.uuid4().hex)
    attempt.mkdir(mode=0o700)
    profile = intent['profile']
    history = response(notary_command(attempt / 'history.json', 'xcrun', 'notarytool',
        'history', '--keychain-profile', profile, '--output-format', 'json')).get('history')
    if not isinstance(history, list) or len(history) > 1000:
        raise ValueError('bounded notary history unavailable; never resubmit unknown outcome')
    earliest = datetime.fromisoformat(intent['started_at']) - timedelta(minutes=2)
    candidates = []
    for item in history:
        if not isinstance(item, dict) or item.get('name') != artifact.name:
            continue
        created = datetime.fromisoformat(item['createdDate'].replace('Z', '+00:00'))
        if created >= earliest:
            candidates.append(submission_id(item.get('id')))
    if len(candidates) > 20 or len(set(candidates)) != len(candidates):
        raise ValueError('ambiguous or excessive notary history candidates')
    matches = []
    for index, identifier in enumerate(candidates):
        info = response(notary_command(attempt / f'info-{index}.json', 'xcrun', 'notarytool',
            'info', identifier, '--keychain-profile', profile, '--output-format', 'json'))
        if submission_id(info.get('id')) != identifier or info.get('name') != artifact.name:
            raise ValueError('notary history/info identity differs')
        # History names/timestamps alone cannot bind uploaded bytes. Logs exist
        # only for completed jobs; unavailable logs leave the outcome unresolved.
        log = response(notary_command(attempt / f'log-{index}.json', 'xcrun', 'notarytool',
            'log', identifier, '--keychain-profile', profile))
        if (submission_id(log.get('jobId')) != identifier or
                log.get('archiveFilename') != artifact.name):
            raise ValueError('notary log identity differs')
        if log.get('sha256') == intent['artifact_sha256']:
            matches.append(info)
    if len(matches) != 1:
        raise ValueError('submission outcome unknown or ambiguous; retain intent, never resubmit')
    check_notary_inputs(intent)
    value = receipt_value(intent, matches[0])
    write_notary_receipt(receipt, value)
    return value


def finish(receipt, output):
    # Each failed attempt is retained. Retry the same output against an unchanged
    # intent, but never reuse bytes a partial stapler action may have modified.
    supplied = Path(output)
    if supplied.exists():
        raise ValueError('completed finish output already exists')
    output = absent_output(output)
    row = document(receipt)
    submission_id(row['submission_id'])
    artifact, target = check_notary_inputs(row)
    if target.is_dir() and target in output.parents:
        raise ValueError('finish output overlaps staple target')
    expected = {'receipt': str(Path(receipt).resolve(strict=True)), 'receipt_sha256': digest(receipt)}
    intent_path = Path(str(output) + '.intent.json')
    if intent_path.exists():
        if document(intent_path) != expected:
            raise ValueError('finish intent differs from existing receipt')
    else:
        write_new(intent_path, expected)
    attempt = Path(str(output) + '.attempt-' + uuid.uuid4().hex)
    attempt.mkdir(mode=0o700)
    raw = notary_command(attempt / 'wait-command.json', 'xcrun', 'notarytool', 'wait', row['submission_id'],
                         '--keychain-profile', row['profile'], '--timeout', '5m', '--output-format', 'json')
    observed = {'submission_id': row['submission_id'], 'response': raw}
    write_new(attempt / 'wait-response.json', observed)
    legacy_wait = Path(str(output) + '.wait.json')
    if not legacy_wait.exists():
        write_new(legacy_wait, observed)
    result = response(raw)
    if result.get('status') != 'Accepted' or submission_id(result.get('id')) != row['submission_id'].lower():
        raise ValueError('notarization not accepted; retain receipt and inspect existing submission')
    check_notary_inputs(row)
    payload = attempt / 'target'
    payload.mkdir(mode=0o700)
    candidate = payload / target.name
    if target.is_dir():
        shutil.copytree(target, candidate, symlinks=True)
    else:
        shutil.copy2(target, candidate, follow_symlinks=False)
    copied = target_inventory(candidate)
    if target.is_dir():
        copied['game_root'] = str(target)
    if copied != row['target_before_staple']:
        raise ValueError('staple copy differs from submitted target')
    notary_command(attempt / 'staple-command.json', 'xcrun', 'stapler', 'staple', candidate)
    notary_command(attempt / 'validate-command.json', 'xcrun', 'stapler', 'validate', candidate)
    check_notary_inputs(row)
    value = {'schema': 2, 'status': 'notarized_and_stapled', 'submission': row,
             'response': result, 'staple_target': str(candidate),
             'target_after_staple': target_inventory(candidate), 'release_accepted': False}
    write_new(output, value)
    return value


def create_dmg(application, output, identity, team):
    stage = load_stage()
    app = stage.ordinary_path(application, directory=True)
    out = absent_output(output)
    if out.suffix != '.dmg' or not re.fullmatch(r'[0-9A-Fa-f]{40}', identity):
        raise ValueError('fresh .dmg and explicit signing-certificate SHA-1 required')
    verify_developer_id(app, team)
    command('xcrun', 'stapler', 'validate', app)
    before = stage.inventory(app)
    # Keep the volume source for reproducibility/inspection; never mutate inputs.
    payload = absent_output(out.with_suffix('.payload'))
    payload.mkdir()
    shutil.copytree(app, payload / app.name, symlinks=True)
    (payload / 'Applications').symlink_to('/Applications')
    stage.require_same_files(before, stage.inventory(payload / app.name), 'DMG payload app')
    command('hdiutil', 'create', '-srcfolder', payload, '-volname',
            'Street Legal Racing 1.0.0', '-fs', 'HFS+', '-format', 'UDZO',
            '-imagekey', 'zlib-level=9', out)
    command('hdiutil', 'verify', out)
    command('codesign', '--sign', identity, '--timestamp', out)
    verify_developer_id(out, team, runtime=False)
    stage.require_same_files(before, stage.inventory(app), 'source application')
    value = {'schema': 1, 'status': 'signed_dmg_not_yet_notarized', 'version': VERSION,
             'dmg': str(out), 'sha256': digest(out), 'application_inventory': before,
             'payload': str(payload), 'release_accepted': False,
             'reproducibility': 'fixed recipe and exact source bytes; signing timestamps, filesystem metadata and notarization tickets are not byte-deterministic'}
    write_new(out.with_suffix('.json'), value)
    return value


def bound(binding):
    if not isinstance(binding, dict) or set(binding) != {'path', 'sha256'}:
        raise ValueError('exact path/SHA-256 evidence binding required')
    path = Path(binding['path']).resolve(strict=True)
    if digest(path) != binding['sha256']:
        raise ValueError('evidence bytes changed: ' + str(path))
    return path


def validate_audit(audit_path, dmg, source_commit, *, requirement_map=None, specification=None):
    requirement_map = MAP if requirement_map is None else Path(requirement_map)
    specification = SPEC if specification is None else Path(specification)
    audit = document(audit_path)
    if (type(audit.get('schema')) is not int or audit.get('schema') != 1 or audit.get('version') != VERSION or
            audit.get('source_commit') != source_commit or
            not isinstance(audit.get('team'), str) or not re.fullmatch(r'[A-Z0-9]{10}', audit['team']) or
            not re.fullmatch(r'[a-f0-9]{40}', source_commit)):
        raise ValueError('exact version/source audit required')
    if bound(audit['specification']) != specification.resolve() or bound(audit['requirement_map']) != requirement_map.resolve():
        raise ValueError('canonical specification and requirement map required')
    ids = set()
    for line in requirement_map.read_text().splitlines():
        match = re.match(r'^\| ([A-Z]+[0-9]+)\s+[—–]', line)
        if not match:
            continue
        identifier = match[1]
        if identifier in ids:
            raise ValueError('duplicate mapped requirement')
        ids.add(identifier)
        cells = line.split('|')
        if len(cells) < 8 or not re.search(r'(?:^|;)\s*verified\s*$', cells[-3].strip()):
            raise ValueError('requirement map is incomplete: ' + identifier)
    if not {*(f'US{i:02}' for i in range(1,47)), *(f'D{i}' for i in range(1,9))} <= ids:
        raise ValueError('required product/deliverable identifiers absent')
    requirements = audit.get('requirements')
    if not isinstance(requirements, dict) or set(requirements) != ids:
        raise ValueError('audit must cover every mapped requirement exactly')
    for identifier, row in requirements.items():
        if not isinstance(row, dict) or row.get('status') != 'verified' or not row.get('evidence'):
            raise ValueError('unverified requirement: ' + identifier)
        for item in row['evidence']:
            bound(item)
    for name in ('standards_review', 'specification_review', 'jev_completion', 'integration_gate'):
        record = document(bound(audit[name]))
        if record.get('source_commit') != source_commit or record.get('accepted') is not True or record.get('unresolved') != []:
            raise ValueError('incomplete source-bound release check: ' + name)
    clean = document(bound(audit['clean_installation']))
    checks = {'signature','notarization','gatekeeper','dependency_closure','first_launch',
              'input','audio','settings','save_reload','replacement_preserves_progress','sustained_play'}
    if (clean.get('dmg_sha256') != digest(dmg) or clean.get('source_commit') != source_commit or
            clean.get('architecture') != 'arm64' or clean.get('clean_machine') is not True or
            not clean.get('hardware') or not clean.get('macos') or not clean.get('tested_limits') or
            set(clean.get('checks', {})) != checks or
            any(value is not True for value in clean['checks'].values()) or not clean.get('evidence')):
        raise ValueError('exact-DMG clean Apple Silicon acceptance incomplete')
    for item in clean['evidence']:
        bound(item)
    return audit


def source_acceptance(audit_path, dmg, commit):
    audit = validate_audit(audit_path, dmg, commit)
    if command('git', '-C', ROOT, 'rev-parse', 'HEAD').strip() != commit:
        raise ValueError('accepted source commit changed')
    if command('git', '-C', ROOT, 'status', '--porcelain', '--untracked-files=normal').strip():
        raise ValueError('clean accepted source checkout required')
    for path in (MAP, SPEC):
        committed = command('git', '-C', ROOT, 'show', f'{commit}:{path.relative_to(ROOT)}')
        if hashlib.sha256(committed.encode()).hexdigest() != digest(path):
            raise ValueError('requirement/specification bytes are not in accepted source commit')
    return audit


def remote_source(commit):
    # Bind the same repository as the release API, independently of local origin.
    repository = f'https://github.com/{REPOSITORY}.git'
    remote = command('git', '-C', ROOT, 'ls-remote', repository,
                     f'refs/tags/{VERSION}', f'refs/tags/{VERSION}^{{}}')
    refs = {}
    for line in remote.splitlines():
        fields = line.split()
        if len(fields) != 2 or fields[1] in refs or not re.fullmatch(r'[a-f0-9]{40}', fields[0]):
            raise ValueError('invalid publication-repository tag response')
        refs[fields[1]] = fields[0]
    if refs.get(f'refs/tags/{VERSION}^{{}}', refs.get(f'refs/tags/{VERSION}')) != commit:
        raise ValueError('publication repository tag does not identify accepted source')
    return {'repository': REPOSITORY, 'url': repository, 'refs': refs, 'source_commit': commit}


def freeze_input(source, destination):
    source = Path(source).resolve(strict=True)
    before = digest(source)
    shutil.copyfile(source, destination, follow_symlinks=False)
    if digest(source) != before or digest(destination) != before:
        raise ValueError('publication input changed while freezing: ' + str(source))
    destination.chmod(0o400)
    return {'path': str(destination), 'sha256': before,
            'source': str(source), 'size': destination.stat().st_size}


def frozen_inputs(request, output):
    result = {}
    for name in ('dmg', 'audit', 'notes'):
        item = request['inputs'][name]
        path = Path(item['path'])
        if path.parent != output / 'inputs' or path.resolve(strict=True) != path or digest(path) != item['sha256']:
            raise ValueError('frozen publication input changed: ' + name)
        if path.stat().st_size != item['size']:
            raise ValueError('frozen publication input extent changed: ' + name)
        result[name] = path
    return result


def response(raw):
    value = load_stage().catalog.parse_json(raw.encode())
    if not isinstance(value, dict):
        raise ValueError('release service returned a non-object')
    return value


def release_identity(row, request):
    identifier = row.get('id')
    if (type(identifier) is not int or identifier <= 0 or
            row.get('tag_name') != VERSION or row.get('prerelease') is not False or
            type(row.get('draft')) is not bool or row.get('name') != 'Full Port 1.0.0' or
            row.get('body') != Path(request['inputs']['notes']['path']).read_text()):
        raise ValueError('release handle or metadata differs from publication request')
    return identifier


def remote_assets(row, request, *, complete):
    expected = {Path(request['inputs'][name]['path']).name: request['inputs'][name]
                for name in ('dmg', 'audit')}
    assets = {}
    for item in row.get('assets', []):
        name = item.get('name')
        if name not in expected or name in assets:
            raise ValueError('release contains unexpected or duplicated assets')
        wanted = expected[name]
        if (type(item.get('id')) is not int or item['id'] <= 0 or
                item.get('state') != 'uploaded' or item.get('size') != wanted['size'] or
                item.get('digest') != 'sha256:' + wanted['sha256']):
            raise ValueError('release asset identity differs from accepted input: ' + name)
        assets[name] = item
    if complete and set(assets) != set(expected):
        raise ValueError('release is missing an accepted asset')
    return assets


def download_assets(output, request, label):
    directory = output / (label + '-' + uuid.uuid4().hex)
    directory.mkdir()
    for name in ('dmg', 'audit'):
        item = request['inputs'][name]
        filename = Path(item['path']).name
        command('gh', 'release', 'download', VERSION, '--repo', REPOSITORY,
                '--pattern', filename, '--dir', directory)
        if digest(directory / filename) != item['sha256']:
            raise ValueError('published/downloaded asset identity differs: ' + filename)
    return directory


def continue_publication(output):
    output = Path(output).resolve(strict=True)
    request = document(output / 'publication-request.json')
    if (request.get('schema') != 1 or request.get('repository') != REPOSITORY or
            request.get('version') != VERSION or request.get('output') != str(output)):
        raise ValueError('publication request does not belong to this output/repository')
    inputs = frozen_inputs(request, output)
    commit = request['source_commit']
    intended = document(output / 'create-draft.json')
    if intended != {'tag_name': VERSION, 'target_commitish': commit,
                    'name': 'Full Port 1.0.0', 'body': inputs['notes'].read_text(),
                    'draft': True, 'prerelease': False}:
        raise ValueError('saved draft creation intent differs from accepted inputs')
    audit = source_acceptance(inputs['audit'], inputs['dmg'], commit)
    remote_source(commit)
    command('xcrun', 'stapler', 'validate', inputs['dmg'])
    command('hdiutil', 'verify', inputs['dmg'])
    verify_developer_id(inputs['dmg'], audit['team'], runtime=False)
    command('spctl', '--assess', '--type', 'open', '--context', 'context:primary-signature', inputs['dmg'])
    frozen_inputs(request, output)
    attempt = output / ('attempt-' + uuid.uuid4().hex)
    attempt.mkdir()
    handle_path = output / 'draft-handle.json'
    if handle_path.exists():
        handle = document(handle_path)
        identifier = handle['release_id']
        if type(identifier) is not int or identifier <= 0:
            raise ValueError('invalid saved release handle')
        row = response(command('gh', 'api', f'repos/{REPOSITORY}/releases/{identifier}'))
    else:
        # A creation timeout may have created the draft. Inspect its known tag;
        # resume never issues another create request.
        recovered = response(command('gh', 'release', 'view', VERSION, '--repo', REPOSITORY,
            '--json', 'databaseId,isDraft,isPrerelease,tagName,name,body'))
        identifier = recovered.get('databaseId')
        if type(identifier) is not int or identifier <= 0:
            raise ValueError('creation outcome unknown; inspect existing draft, do not create again')
        row = response(command('gh', 'api', f'repos/{REPOSITORY}/releases/{identifier}'))
        release_identity(row, request)
        if not row['draft']:
            raise ValueError('unrecorded published release cannot be resumed')
        write_new(handle_path, {'release_id': identifier, 'repository': REPOSITORY,
                                'version': VERSION, 'recovered_after_creation': True})
    if release_identity(row, request) != identifier:
        raise ValueError('release service changed the saved release handle')
    write_new(attempt / 'observed-release.json', row)
    if row['draft']:
        assets = remote_assets(row, request, complete=False)
        for name in ('dmg', 'audit'):
            item = request['inputs'][name]
            path = inputs[name]
            if path.name not in assets:
                frozen_inputs(request, output)
                command('gh', 'release', 'upload', VERSION, path, '--repo', REPOSITORY)
        row = response(command('gh', 'api', f'repos/{REPOSITORY}/releases/{identifier}'))
        if release_identity(row, request) != identifier or not row['draft']:
            raise ValueError('draft changed during asset upload')
        assets = remote_assets(row, request, complete=True)
        downloaded = download_assets(output, request, 'draft-download')
        write_new(attempt / 'draft-verified.json', {'release_id': identifier,
                  'assets': assets, 'download': str(downloaded)})
        frozen_inputs(request, output)
        source_acceptance(inputs['audit'], inputs['dmg'], commit)
        tag = remote_source(commit)
        final = response(command('gh', 'api', f'repos/{REPOSITORY}/releases/{identifier}'))
        if (release_identity(final, request) != identifier or not final['draft'] or
                remote_assets(final, request, complete=True) != assets):
            raise ValueError('draft changed after download verification')
        # Record the handle and verified assets before the only public mutation.
        publish_request = output / 'publish-request.json'
        expected = {'release_id': identifier, 'repository': REPOSITORY,
                    'version': VERSION, 'assets': assets, 'tag': tag}
        if publish_request.exists():
            if document(publish_request) != expected:
                raise ValueError('saved publication request no longer matches verified draft')
        else:
            write_new(publish_request, expected)
        row = response(command('gh', 'api', '--method', 'PATCH',
            f'repos/{REPOSITORY}/releases/{identifier}', '-F', 'draft=false'))
        write_new(attempt / 'publish-response.json', row)
    else:
        saved = document(output / 'publish-request.json')
        if saved.get('release_id') != identifier or remote_assets(row, request, complete=True) != saved.get('assets'):
            raise ValueError('published release differs from the recorded publication request')
    if release_identity(row, request) != identifier or row['draft']:
        raise ValueError('release publication was not observed')
    remote_assets(row, request, complete=True)
    frozen_inputs(request, output)
    remote_source(commit)
    downloaded = download_assets(output, request, 'public-download')
    value = {'schema': 1, 'source_commit': commit,
             'dmg_sha256': request['inputs']['dmg']['sha256'],
             'audit_sha256': request['inputs']['audit']['sha256'], 'download_verified': True,
             'release_id': identifier, 'release': f'https://github.com/{REPOSITORY}/releases/tag/{VERSION}',
             'download': str(downloaded), 'downloaded_installation_verified': False,
             'scope': 'draft assets verified before publication; exact public downloads verified; clean downloaded installation/launch remains required'}
    write_new(attempt / 'publication.json', value)
    return value


def publish(audit_path, dmg, notes, output):
    output = absent_output(output)
    dmg = Path(dmg).resolve(strict=True)
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._ -]*\.dmg', dmg.name):
        raise ValueError('unambiguous .dmg asset filename required')
    output.mkdir(mode=0o700)
    inputs = output / 'inputs'; inputs.mkdir(mode=0o700)
    snapshots = {'dmg': freeze_input(dmg, inputs / dmg.name),
                 'audit': freeze_input(audit_path, inputs / 'release-acceptance.json'),
                 'notes': freeze_input(notes, inputs / 'release-notes.md')}
    commit = command('git', '-C', ROOT, 'rev-parse', 'HEAD').strip()
    request = {'schema': 1, 'repository': REPOSITORY, 'version': VERSION,
               'output': str(output), 'source_commit': commit, 'inputs': snapshots}
    write_new(output / 'publication-request.json', request)
    audit = source_acceptance(Path(snapshots['audit']['path']), Path(snapshots['dmg']['path']), commit)
    write_new(output / 'remote-source.json', remote_source(commit))
    candidate = Path(snapshots['dmg']['path'])
    command('xcrun', 'stapler', 'validate', candidate)
    command('hdiutil', 'verify', candidate)
    verify_developer_id(candidate, audit['team'], runtime=False)
    command('spctl', '--assess', '--type', 'open', '--context', 'context:primary-signature', candidate)
    frozen_inputs(request, output)
    body = Path(snapshots['notes']['path']).read_text()
    payload = output / 'create-draft.json'
    write_new(payload, {'tag_name': VERSION, 'target_commitish': commit,
                       'name': 'Full Port 1.0.0', 'body': body, 'draft': True, 'prerelease': False})
    # A persisted intent precedes the call. A timeout is resumed by inspection,
    # not by creating another release or replacing its assets.
    raw = command('gh', 'api', '--method', 'POST', f'repos/{REPOSITORY}/releases', '--input', payload)
    write_new(output / 'create-draft-response.json', {'response': raw})
    row = response(raw)
    identifier = release_identity(row, request)
    if not row['draft']:
        raise ValueError('release service did not create a private draft')
    write_new(output / 'draft-handle.json', {'release_id': identifier,
              'repository': REPOSITORY, 'version': VERSION})
    return continue_publication(output)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='phase', required=True)
    fields = {'prepare': ('application','stage-report','output','identity','team'),
              'submit': ('artifact','staple-target','profile','receipt'),
              'finish': ('receipt','output'), 'dmg': ('application','output','identity','team'),
              'publish': ('audit-path','dmg','notes','output'),
              'resume-publication': ('output',), 'resume-submission': ('receipt',)}
    for name, names in fields.items():
        item = sub.add_parser(name)
        for field in names:
            item.add_argument('--'+field, required=True)
    args = vars(parser.parse_args()); phase = args.pop('phase')
    try:
        result = {'prepare':prepare,'submit':submit,'finish':finish,'dmg':create_dmg,'publish':publish,'resume-publication':continue_publication,'resume-submission':resume_submission}[phase](**args)
        print(json.dumps(result,indent=2))
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.exit(1, f'distribute: {error}\n')


if __name__ == '__main__':
    main()
