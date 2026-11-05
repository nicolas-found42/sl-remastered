#!/usr/bin/env python3
"""Developer entry point for the Parity workflow.

Lanes: targeted feedback (test), failure reproduction (repro), corpus/unit checks
(corpus), catalog coverage (coverage), performance timing (perf), the strict proof
evaluator (proof) and the complete integration gate (gate).

Every command that builds or runs a game-adjacent process goes through the shared
cooperative resource pool. This tool schedules and reports; it never converts a red
result to green and never reports unfinished catalog rows as verified.
"""
import argparse
import hashlib
import importlib.util
import json
import math
import re
import shlex
import statistics
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
POOL = HERE / 'resource_pool.py'
CATALOG = HERE / 'catalog.json'
# Same budgets tools/check.sh requests, so interactive work and the gate agree.
BUILD = {'cpu': 6, 'host-memory': 12, 'build-writer': 1}
RUN = {'cpu': 6, 'host-memory': 12, 'renderer': 2}


def pooled(resources, argv):
    flags = []
    for key, value in resources.items():
        flags += [f'--{key}', str(value)]
    return [sys.executable, '-I', str(POOL), *flags, '--', *map(str, argv)]


def check_pattern(pattern):
    if not pattern or pattern.startswith('-'):
        raise ValueError('a non-empty test regular expression is required')
    return pattern


def check_test_name(test):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+', test or ''):
        raise ValueError('an exact test name is required')
    return test


def plan_test(pattern, build_dir='build', build=True):
    check_pattern(pattern)
    steps = []
    if build:
        steps.append(pooled(BUILD, ['cmake', '--build', build_dir, '--parallel', 6]))
    steps.append(pooled(RUN, ['ctest', '--test-dir', build_dir, '-R', pattern, '--no-tests=error', '--output-on-failure']))
    return steps


def plan_configure(preset):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+', preset):
        raise ValueError('a CMake preset name is required')
    return [pooled({'cpu': 1, 'host-memory': 2, 'build-writer': 1},
                   ['cmake', '--preset', preset])]


def plan_setup(skip_brew=False):
    """Explicit installation only; doctor and lint never install dependencies."""
    environment = ROOT / '.venv'
    steps = []
    if not skip_brew:
        steps.append(['brew', 'bundle', '--file', str(ROOT / 'Brewfile')])
    steps += [[sys.executable, '-m', 'venv', str(environment)],
              [str(environment / 'bin/python'), '-m', 'pip', 'install',
               '--requirement', str(ROOT / 'requirements-dev.txt')],
              ['git', 'config', '--local', 'core.hooksPath', '.githooks']]
    return steps


def corpus_commands(check_sh):
    """Python suites exactly as the integration gate invokes them (no drift)."""
    found = re.compile(r'(?:tests|tools/parity)/[^\s]*test[^\s]*\.py')
    commands = []
    for line in Path(check_sh).read_text().replace('\\\n', ' ').splitlines():
        if not line.startswith('python3 -I '):
            continue
        words = shlex.split(line, comments=True)
        child = words
        wrapped = words[:3] == ['python3', '-I', 'tools/parity/resource_pool.py']
        if wrapped:
            if '--' not in words:
                raise ValueError('pooled gate command has no child separator')
            split = words.index('--')
            child = words[split + 1:]
        if len(child) < 3 or child[:2] != ['python3', '-I'] or not found.fullmatch(child[2]):
            continue
        if any(re.search(r'[;&|<>`$]', part) for part in child[3:]):
            raise ValueError('unsupported Python suite command in gate')
        command = [sys.executable, '-I', *child[2:]]
        if wrapped:
            command = [sys.executable, '-I', words[2], *words[3:split], '--', *command]
        commands.append(command)
    if not commands:
        raise ValueError('no corpus suites found in check script')
    return commands


def coverage(catalog_path=CATALOG):
    """Catalog coverage. A Rung counts as verified only if its own record is consistent:
    complete variant inventory, known and verified dependencies, and a unique id."""
    catalog = json.loads(Path(catalog_path).read_text())
    rungs = catalog['rungs']
    ids = [r['id'] for r in rungs]
    duplicates = sorted({i for i in ids if ids.count(i) > 1})
    known = set(ids)
    declared = {r['id'] for r in rungs if r.get('assertion_status') == 'verified'}
    problems = []
    verified = set()
    for rung in rungs:
        if rung['id'] not in declared or rung['id'] in duplicates:
            continue
        reasons = []
        if not rung.get('variant_inventory_complete'):
            reasons.append('variant inventory incomplete')
        missing = [d for d in rung.get('depends_on', []) if d not in known]
        unverified = [d for d in rung.get('depends_on', []) if d in known and d not in declared]
        if missing:
            reasons.append(f'unknown dependencies {missing}')
        if unverified:
            reasons.append(f'unverified dependencies {unverified}')
        if reasons:
            problems.append({'rung': rung['id'], 'declared_verified_but': reasons})
        else:
            verified.add(rung['id'])
    by_status = {}
    for rung in rungs:
        by_status.setdefault(rung.get('assertion_status', 'unknown'), []).append(rung['id'])
    unknown_deps = sorted({d for r in rungs for d in r.get('depends_on', []) if d not in known})
    journeys = {j['id']: [r for r in j['rungs'] if r not in known] for j in catalog['journeys']}
    return {
        'catalog_status': catalog.get('status'),
        'rungs': len(rungs),
        'variants': sum(len(r.get('variants', [])) for r in rungs),
        'journeys': len(catalog['journeys']),
        'verified_rungs': len(verified),
        'pending_rungs': len(rungs) - len(verified),
        'assertion_status': {k: len(v) for k, v in sorted(by_status.items())},
        'variant_inventory_incomplete': sum(1 for r in rungs if not r.get('variant_inventory_complete')),
        'unknown_dependencies': unknown_deps,
        'journey_unknown_rungs': {k: v for k, v in journeys.items() if v},
        'duplicate_ids': duplicates,
        'inconsistent_verified_claims': problems,
        'scope': catalog.get('scope'),
    }


def coverage_consistent(report):
    return not (report['duplicate_ids'] or report['inconsistent_verified_claims'] or
                report['unknown_dependencies'] or report['journey_unknown_rungs'])


def signature(output):
    """Stable verdict signature: output with timing/pid noise removed."""
    lines = [re.sub(r'\d+(\.\d+)? sec', 'N sec', line) for line in output.splitlines()
             if 'Total Test time' not in line]
    return hashlib.sha256('\n'.join(lines).encode()).hexdigest()[:16]


def run_capture(argv, cwd=ROOT):
    started = time.monotonic()
    done = subprocess.run(argv, cwd=cwd, capture_output=True, text=True)
    return done.returncode, done.stdout + done.stderr, time.monotonic() - started


def repro(test, runs, runner=run_capture, build_dir='build'):
    check_test_name(test)
    if runs < 1:
        raise ValueError('a positive run count is required')
    argv = pooled(RUN, ['ctest', '--test-dir', build_dir, '-R', f'^{re.escape(test)}$', '--no-tests=error', '--output-on-failure'])
    results = [runner(argv) for _ in range(runs)]
    codes = sorted({code for code, _, _ in results})
    signatures = sorted({signature(out) for _, out, _ in results})
    return {'test': test, 'runs': runs, 'exit_codes': codes, 'distinct_output_signatures': len(signatures),
            'deterministic': len(codes) == 1 and len(signatures) == 1,
            'red': any(code != 0 for code in codes),
            'seconds_max': round(max(sec for _, _, sec in results), 3),
            'last_output_tail': results[-1][1].strip().splitlines()[-12:]}


def perf(pattern, runs, runner=run_capture, build_dir='build'):
    check_pattern(pattern)
    if runs < 1:
        raise ValueError('a positive run count is required')
    argv = pooled(RUN, ['ctest', '--test-dir', build_dir, '-R', pattern, '--no-tests=error'])
    times, codes = [], []
    for _ in range(runs):
        code, _, seconds = runner(argv)
        codes.append(code)
        times.append(seconds)
    return {'pattern': pattern, 'runs': runs, 'exit_codes': sorted(set(codes)), 'all_passed': set(codes) == {0},
            'seconds': {'min': round(min(times), 3), 'median': round(statistics.median(times), 3),
                        'max': round(max(times), 3)},
            'note': 'wall-clock including pool admission and process start; not a representative game benchmark'}


def mix_timing(group):
    """Validate complete declared work before calculating any throughput."""
    def finite(value):
        return type(value) in (float, int) and math.isfinite(value)
    start, end = group.get('started'), group.get('finished')
    if not finite(start) or not finite(end) or end <= start or group.get('passed') is not True:
        raise ValueError('incomplete or invalid mixed-run interval')
    tasks = group.get('tasks', [])
    if sorted(t.get('name', '') for t in tasks) != ['cpu', 'native', 'stock']:
        raise ValueError('mixed run requires exactly one native, Stock and CPU task')
    for task in tasks:
        a, b = task.get('started'), task.get('finished')
        if (not finite(a) or not finite(b) or not start <= a < b <= end or
                type(task.get('exit_code')) is not int or task['exit_code'] != 0 or task.get('incomplete')):
            raise ValueError('failed, dropped or invalid mixed task')
    beats = group.get('heartbeat', [])
    gaps = [beat.get('gap_seconds') for beat in beats]
    pressure = group.get('pressure', [])
    if not gaps or any(not finite(g) or g < 0 for g in gaps) or not pressure:
        raise ValueError('missing mixed-run responsiveness observations')
    previous = start
    for beat in beats:
        at = beat.get('at')
        if (not finite(at) or not previous <= at <= end or
                abs((at - previous) - beat['gap_seconds']) > 1e-8):
            raise ValueError('missing or inconsistent mixed heartbeat record')
        previous = at
    if end - previous > .25:
        raise ValueError('mixed heartbeat observation ends before the work finishes')
    if any(p.get('ok') is not True for p in pressure):
        raise ValueError('mixed run lost pressure observations or crossed a threshold')
    if any(type(p.get('sample', {}).get(key)) is not int or p['sample'][key] < 0
           for p in pressure for key in ('available_memory_bytes', 'free_disk_bytes')):
        raise ValueError('invalid mixed pressure counters')
    previous = start
    for sample in pressure:
        at = sample.get('at')
        if not finite(at) or not previous <= at <= end or at - previous > 1.25:
            raise ValueError('missing or late mixed pressure observation')
        if (sample['sample']['available_memory_bytes'] < 1024**3 or
                sample['sample']['free_disk_bytes'] < 10 * 1024**3):
            raise ValueError('mixed pressure counters crossed the declared envelope')
        previous = at
    if end - previous > 1.25:
        raise ValueError('mixed pressure observation ends before the work finishes')
    if max(gaps) > .25:
        raise ValueError('mixed heartbeat gap exceeds the 250ms observation envelope')
    ordered = sorted(gaps)
    return {'wall_seconds': end - start, 'completed_tasks': len(tasks),
            'tasks_per_minute': len(tasks) * 60 / (end - start),
            'heartbeat_seconds': {'p95': ordered[min(len(ordered)-1, math.ceil(len(ordered)*.95)-1)],
                                  'max': max(gaps), 'samples': len(gaps)},
            'pressure_samples': len(pressure),
            'min_available_memory_bytes': min(p['sample']['available_memory_bytes'] for p in pressure),
            'min_free_disk_bytes': min(p['sample']['free_disk_bytes'] for p in pressure),
            'task_seconds': {t['name']: t['finished']-t['started'] for t in tasks}}


def perf_captures(directory):
    """Audit a retained sequential/concurrent experiment, including raw PCM/focus.

    This command does not launch arbitrary commands supplied by a manifest.
    The experiment producer, frozen tools, binaries and authority inputs remain
    inspectable beside its records. A missing workload is never a speedup.
    """
    directory = Path(directory).resolve(strict=True)
    modules = {}
    for name in ('focus_evidence', 'audio_evidence'):
        spec = importlib.util.spec_from_file_location('mix_' + name, HERE / (name + '.py'))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        modules[name] = module
    def read(path):
        def invalid(value):
            raise ValueError('nonfinite mixed evidence value: ' + value)
        return json.loads(Path(path).read_text(), parse_constant=invalid)
    binding = read(directory / 'experiment-identity.json')
    manifest_path = Path(binding['frozen_manifest'])
    if hashlib.sha256(manifest_path.read_bytes()).hexdigest() != binding['frozen_manifest_sha256']:
        raise ValueError('mixed frozen workload manifest changed')
    manifest = read(manifest_path)['files']
    producer = binding['producer']
    if hashlib.sha256(Path(producer['path']).read_bytes()).hexdigest() != producer['sha256']:
        raise ValueError('mixed retained producer source changed')
    binaries = {}
    for item in binding['cpu_binaries']:
        name = item['name']
        if (name in binaries or not re.fullmatch(r'[A-Za-z0-9_.-]+', name) or
                manifest.get('bin/' + name) != item['sha256'] or
                hashlib.sha256(Path(item['path']).read_bytes()).hexdigest() != item['sha256']):
            raise ValueError('mixed CPU executable identity differs from the predeclared frozen bytes')
        binaries[name] = item
    if not binaries:
        raise ValueError('missing mixed CPU executable identities')
    reports, expected_cpu, workload_identity, authority_identity = {}, None, {}, {}
    for mode in ('sequential', 'concurrent'):
        base = directory / mode
        group = read(base / 'group.json')
        report = mix_timing(group)
        if group.get('mode') != mode:
            raise ValueError('mixed evidence mode differs from its record')
        if mode == 'sequential':
            tasks = sorted(group['tasks'], key=lambda t: t['started'])
            if any(a['finished'] > b['started'] for a, b in zip(tasks, tasks[1:])):
                raise ValueError('sequential baseline contains overlapping tasks')
        details, execution_intervals = {}, {}
        for task in group['tasks']:
            kind = task['name']
            out = base / kind
            if Path(task['out']).resolve() != out.resolve():
                raise ValueError('mixed task evidence escaped its declared output')
            focus = modules['focus_evidence'].classify(modules['focus_evidence'].read_events(out / 'focus.jsonl'))
            if focus['status'] != 'no_owned_activation_observed' or focus['child_exit'] != 0:
                raise ValueError('mixed task lacks complete no-owned-activation evidence')
            raw_events = modules['focus_evidence'].read_events(out / 'focus.jsonl')
            finished = [e for e in raw_events if e['kind'] == 'observer_finished'][0]
            started = [e for e in raw_events if e['kind'] == 'child_started'][0]
            execution_intervals[kind] = (started['monotonic'], finished['monotonic'])
            details[kind] = {'focus': focus['status'], 'focus_max_poll_gap_seconds': finished['max_poll_gap_seconds']}
            if kind == 'cpu':
                cpu = read(out / 'cpu.json')
                expected, completed = cpu['expected_jobs'], cpu['completed']
                ids = [row['id'] for row in completed]
                if (not expected or len(set(expected)) != len(expected) or sorted(ids) != sorted(expected) or
                        cpu.get('all_passed') is not True or
                        any(r.get('complete') is not True or type(r.get('exit_code')) is not int or
                            r['exit_code'] != 0 for r in completed)):
                    raise ValueError('mixed CPU work is missing, duplicated or failed')
                actual_out = str(Path(task['out']).resolve())
                argv = [str(value).replace(str(out), '<OUT>').replace(actual_out, '<OUT>')
                        for value in task['argv']]
                worker_index = argv.index('--cpu-worker')
                if Path(argv[worker_index - 1]).resolve() != Path(producer['path']).resolve():
                    raise ValueError('mixed CPU command does not bind the retained producer')
                commands = [(r['id'], r['name'], binaries[r['name']]['sha256']) for r in completed]
                identity = {'jobs': expected, 'commands': sorted(commands), 'producer_argv': argv}
                if expected_cpu is not None and expected_cpu != identity:
                    raise ValueError('baseline and concurrent CPU work differ')
                expected_cpu = identity
                for row in completed:
                    if not re.fullmatch(r'[A-Za-z0-9_.-]+', row['id']):
                        raise ValueError('invalid mixed CPU execution identity')
                    log = out / (row['id'] + '.log')
                    if hashlib.sha256(log.read_bytes()).hexdigest() != row['stdout_sha256']:
                        raise ValueError('mixed CPU output identity changed')
                    if (row.get('execution', {}).get('returncode') != 0 or
                            row['execution'].get('stop_reason') is not None or
                            row['execution'].get('guardian_cleanup_missing') or
                            row.get('pressure', {}).get('stopped')):
                        raise ValueError('mixed CPU execution supervision was incomplete')
                details[kind]['completed_executions'] = len(expected)
                details[kind]['executable_sha256'] = {name: item['sha256'] for name, item in binaries.items()}
                details[kind]['admission_wait_seconds'] = cpu['admission']['wait_seconds']
                # CPU job timestamps use Python's monotonic implementation;
                # compare only the three native observer CLOCK_MONOTONIC
                # intervals. They describe supervised root lifetimes, not
                # an instruction-level trace or cross-clock projection.
                continue
            result = read(out / 'result.json')
            config = read(out / ('metadata.json' if kind == 'native' else 'config.json'))
            keys = (('script_sha256', 'probe_sha256', 'startup_movies', 'presentation', 'game', 'reference_root')
                    if kind == 'native' else
                    ('source', 'stock', 'texture', 'disc', 'image_id', 'ordinary_exit', 'record_controls',
                     'early_controls', 'rng_observer', 'producer_sha256', 'observer_sha256', 'texture_sha256', 'tools'))
            identity = {key: config[key] for key in keys}
            if kind == 'native' and (identity['startup_movies'] != 'normal' or identity['presentation'] != 'background'):
                raise ValueError('mixed native workload bypassed the qualified complete pipeline')
            if kind in workload_identity and workload_identity[kind] != identity:
                raise ValueError('baseline and concurrent game workloads differ')
            workload_identity[kind] = identity
            details[kind]['admission_wait_seconds'] = config['admission']['wait_seconds']
            required = (('probe_unchanged', 'source_save_unchanged', 'source_game_unchanged') if kind == 'native' else
                        ('passed', 'source_preserved', 'stock_preserved', 'disc_preserved', 'texture_preserved',
                         'container_removed', 'outer_container_removed', 'producer_unchanged'))
            if kind == 'stock':
                required += ('private_sink', 'container_isolated')
            if (any(result.get(key) is not True for key in required) or result.get('timed_out') or
                    result.get('resource_pressure') or result.get('artifact_error') or
                    (kind == 'native' and result.get('exit_code') != 0)):
                raise ValueError('mixed game task failed its source/lifecycle checks')
            before_path, after_path = (('source-game-before.json', 'source-game-after.json') if kind == 'native'
                                       else ('source-before.json', 'source-after.json'))
            before, after = read(out / before_path), read(out / after_path)
            if kind == 'native':
                if before.get('complete') is not True or after.get('complete') is not True or before['files'] != after['files']:
                    raise ValueError('mixed native source manifests differ or are incomplete')
            elif before != after:
                raise ValueError('mixed Stock source manifests differ')
            content = before['files'] if kind == 'native' else before
            if kind in authority_identity and authority_identity[kind] != content:
                raise ValueError('baseline and concurrent authoritative source bytes differ')
            authority_identity[kind] = content
            artifacts = result.get('execution_artifacts', {})
            if not artifacts.get('samples') or artifacts.get('stopped') or not artifacts.get('last_sample', {}).get('complete'):
                raise ValueError('mixed game task lacks complete artifact accounting')
            pcm = (modules['audio_evidence'].native_capture(out / 'run') if kind == 'native' else
                   modules['audio_evidence'].summarize(out / 'stock.wav', 's16le', 2, 48000, 'wav'))
            if pcm['nonzero_samples'] <= 0 or any(result['audio'].get(k) != v for k, v in pcm.items()):
                raise ValueError('mixed task PCM is empty or changed')
            details[kind].update(pcm_frames=pcm['frames'], artifact_samples=artifacts['samples'])
        report['tasks'] = details
        overlap = min(b for a, b in execution_intervals.values()) - max(a for a, b in execution_intervals.values())
        if mode == 'concurrent' and overlap <= 0:
            raise ValueError('concurrent mix has no observed three-class root overlap')
        report['three_class_observed_root_overlap_seconds'] = max(0.0, overlap)
        reports[mode] = report
    reports['elapsed_ratio'] = reports['sequential']['wall_seconds'] / reports['concurrent']['wall_seconds']
    reports['all_declared_work_verified'] = True
    reports['scope'] = ('one retained normal-boot/menu/dialog/ordinary-exit mix per mode plus identical CPU work; '
                        'wall time includes admission; heartbeat/focus polling are responsiveness proxies, '
                        'not human input-latency measurements or a maximum-capacity claim')
    reports['cpu_identity_scope'] = ('predeclared frozen executable bytes rehashed with the same retained producer/argv binding; '
                                     'not a per-call runtime execution tape. Producer hash audited after the captures.')
    return reports


def execute(steps, dry_run):
    if dry_run:
        print(json.dumps(steps))
        return 0
    for step in steps:
        code = subprocess.run(step, cwd=ROOT).returncode
        if code:
            return code
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--dry-run', action='store_true', help='print the commands instead of running them')
    sub = parser.add_subparsers(dest='lane', required=True)
    p = sub.add_parser('setup', help='explicitly install development dependencies and the local hook')
    p.add_argument('--skip-brew', action='store_true', help='install Python dependencies and hook only')
    p = sub.add_parser('doctor', help='read-only tool versions, dependencies and database diagnosis')
    p.add_argument('--build-dir', default='build')
    p.add_argument('--json', action='store_true')
    p = sub.add_parser('configure', help='configure a CMake preset under shared build admission')
    p.add_argument('--preset', default='ninja-release')
    p = sub.add_parser('lint', help='check staged, committed or explicit working source without rewriting')
    selection = p.add_mutually_exclusive_group()
    selection.add_argument('--staged', action='store_true')
    selection.add_argument('--base')
    selection.add_argument('--paths', nargs='+')
    p.add_argument('--tidy', action='store_true', help='also run deep native analysis with exact compile commands')
    p.add_argument('--build-dir', default='build')
    p.add_argument('--translation-unit', action='append', default=[])
    p = sub.add_parser('format', help='explicitly format selected working files; never stage files')
    p.add_argument('paths', nargs='+')
    p = sub.add_parser('portable', help='pooled, asset-free native CI tests; does not replace the full gate')
    p.add_argument('--build-dir', default='build')
    p.add_argument('--no-build', action='store_true')
    p.add_argument('--test-label', default='^portable$')
    p = sub.add_parser('test', help='build once then run tests matching a regular expression')
    p.add_argument('pattern')
    p.add_argument('--no-build', action='store_true')
    p = sub.add_parser('repro', help='run one test N times and report determinism of the verdict')
    p.add_argument('test')
    p.add_argument('--runs', type=int, default=3)
    sub.add_parser('corpus', help='run every Python suite exactly as the gate does')
    p = sub.add_parser('coverage', help='report catalog coverage; pending rows stay pending')
    p.add_argument('--json', action='store_true')
    p = sub.add_parser('perf', help='time tests matching a regular expression')
    p.add_argument('pattern')
    p.add_argument('--runs', type=int, default=3)
    p = sub.add_parser('perf-captures', help='audit retained sequential/concurrent native/Stock/CPU performance evidence')
    p.add_argument('directory', type=Path)
    p = sub.add_parser('proof', help='delegate to the strict proof evaluator')
    p.add_argument('args', nargs=argparse.REMAINDER)
    sub.add_parser('gate', help='the complete integration gate (tools/check.sh)')
    for lane in ('evidence-index', 'trace-inspect'):
        p = sub.add_parser(lane, help='inspect retained evidence without executing targets')
        p.add_argument('args', nargs=argparse.REMAINDER)
    p = sub.add_parser('incident', help='read bounded historical records by time/run/operation without launching targets')
    p.add_argument('args', nargs=argparse.REMAINDER)
    p = sub.add_parser('jev-packet', help='build a bounded request from hashed evidence excerpts')
    p.add_argument('manifest', type=Path)
    p = sub.add_parser('capacity', help='read-only retention and existing-parent planning')
    p.add_argument('--parent', type=Path)
    p = sub.add_parser('experiment', help='run declared console stages with failure propagation')
    p.add_argument('manifest', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--artifact-links', choices=('reject', 'count'), default='reject')
    args = parser.parse_args(argv)
    try:
        if args.lane == 'setup':
            return execute(plan_setup(args.skip_brew), args.dry_run)
        if args.lane == 'doctor':
            command = [sys.executable, '-I', str(ROOT / 'tools/devtools.py'),
                       '--build-dir', args.build_dir]
            if args.json:
                command.append('--json')
            return execute([command], args.dry_run)
        if args.lane == 'configure':
            return execute(plan_configure(args.preset), args.dry_run)
        if args.lane in ('lint', 'format'):
            command = [sys.executable, '-I', str(ROOT / 'tools/guardrails.py')]
            if args.lane == 'format':
                command += ['--format', '--paths', *args.paths]
            else:
                if args.paths:
                    command += ['--paths', *args.paths]
                elif args.base:
                    command += ['--base', args.base]
                else:
                    command.append('--staged')
                command += ['--build-dir', args.build_dir]
                if args.tidy:
                    command.append('--tidy')
                for unit in args.translation_unit:
                    command += ['--translation-unit', unit]
            return execute([command], args.dry_run)
        if args.lane == 'portable':
            check_pattern(args.test_label)
            command = [sys.executable, '-I', str(ROOT / 'tools/portable_checks.py'),
                       '--build-dir', args.build_dir, '--test-label', args.test_label]
            if args.no_build:
                command.append('--no-build')
            return execute([command], args.dry_run)
        if args.lane == 'incident':
            forwarded = args.args[1:] if args.args[:1] == ['--'] else args.args
            return execute([[sys.executable, '-I', str(ROOT / 'tools/incident.py'), *forwarded]], args.dry_run)
        if args.lane in ('evidence-index', 'trace-inspect'):
            script = args.lane.replace('-', '_') + '.py'
            return execute([[sys.executable, '-I', str(HERE / script), *args.args]], args.dry_run)
        if args.lane == 'jev-packet':
            return execute([[sys.executable, '-I', str(HERE / 'jev_packet.py'),
                             str(args.manifest.resolve())]], args.dry_run)
        if args.lane == 'capacity':
            command = [sys.executable, '-I', str(HERE / 'capacity.py')]
            if args.parent is not None:
                command += ['--parent', str(args.parent.absolute())]
            return execute([command], args.dry_run)
        if args.lane == 'experiment':
            return execute([[sys.executable, '-I', str(HERE / 'experiment_result.py'),
                             str(args.manifest.resolve()), str(args.output.absolute()),
                             '--artifact-links', args.artifact_links]], args.dry_run)
        if args.lane == 'test':
            return execute(plan_test(args.pattern, build=not args.no_build), args.dry_run)
        if args.lane == 'corpus':
            return execute(corpus_commands(ROOT / 'tools/check.sh'), args.dry_run)
        if args.lane == 'proof':
            return execute([[sys.executable, '-I', str(HERE / 'proof.py'), *args.args]], args.dry_run)
        if args.lane == 'gate':
            return execute([['bash', str(ROOT / 'tools/check.sh')]], args.dry_run)
        if args.lane == 'coverage':
            report = coverage()
            if args.json:
                print(json.dumps(report, indent=2))
            else:
                for key, value in report.items():
                    print(f'{key}: {value}')
            # An inconsistent catalog is a failure signal even though it prints.
            return 0 if coverage_consistent(report) else 1
        if args.lane == 'perf-captures':
            if args.dry_run:
                print(json.dumps({'lane': args.lane, 'directory': str(args.directory), 'note': 'read-only evidence audit'}))
                return 0
            print(json.dumps(perf_captures(args.directory), indent=2))
            return 0
        if args.lane == 'repro':
            check_test_name(args.test)
        else:
            check_pattern(args.pattern)
        if args.runs < 1:
            raise ValueError('a positive run count is required')
        if args.dry_run:
            print(json.dumps({'lane': args.lane, 'steps': [pooled(BUILD, ['cmake', '--build', 'build', '--parallel', 6])],
                              'note': 'dry run executes nothing'}))
            return 0
        # Never time or reproduce against a stale binary.
        built = execute([pooled(BUILD, ['cmake', '--build', 'build', '--parallel', 6])], False)
        if built:
            return built
        report = repro(args.test, args.runs) if args.lane == 'repro' else perf(args.pattern, args.runs)
        print(json.dumps(report, indent=2))
        # A red or nondeterministic reproduction is a failure signal, never a pass.
        return 1 if report.get('red') or report.get('deterministic') is False or report.get('all_passed') is False else 0
    except (OSError, ValueError, KeyError) as error:
        print(f'dev: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
