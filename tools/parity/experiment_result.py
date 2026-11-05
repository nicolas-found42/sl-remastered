"""Fail-closed result contract for console experiments using ResourcePool leases.

Execution success is deliberately distinct from parity or review approval.
Expected nonzero exits must be declared before execution by the caller.
"""
import json
import argparse
import importlib.util
import sys
import subprocess
import datetime
import hashlib
import os
import time
import uuid
from pathlib import Path


class Experiment:
    def __init__(self, required_stages):
        self.required = tuple(required_stages)
        if not self.required or len(set(self.required)) != len(self.required):
            raise ValueError('unique required stages must be declared')
        if any(not isinstance(name, str) or not name for name in self.required):
            raise ValueError('stage names must be nonempty strings')
        self.stages = {}
        self.failures = []
        self.started = time.monotonic()
        self.context = {'attempt_id': uuid.uuid4().hex,
                        'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                        'pid': os.getpid(), 'python_version': list(sys.version_info[:3]),
                        'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
        self.journal = None
        self.telemetry_failures = 0

    def observe(self, event, stage=None, exit_code=None):
        if self.journal is None:
            return
        row = {'schema': 1, 'attempt_id': self.context['attempt_id'], 'event': event,
               'stage': stage, 'exit_code': exit_code,
               'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'elapsed_seconds': time.monotonic() - self.started}
        try:
            fd = os.open(self.journal, os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_NOFOLLOW | os.O_CLOEXEC, 0o600)
            try:
                raw = (json.dumps(row) + '\n').encode()
                if os.write(fd, raw) != len(raw):
                    raise OSError('short diagnostic journal write')
                os.fsync(fd)
            finally:
                os.close(fd)
        except OSError:
            self.telemetry_failures += 1

    def run(self, lease, name, command, *, expected_exit=0, **limits):
        if name not in self.required or name in self.stages:
            raise ValueError('unknown or repeated stage')
        if type(expected_exit) is not int:
            raise ValueError('expected exit must be an integer')
        row = {'command': list(command), 'expected_exit': expected_exit,
               'actual_exit': None, 'completed': False}
        self.stages[name] = row
        stage = self.required.index(name)
        self.observe('begin', stage)
        started = time.monotonic()
        try:
            result = lease.run(command, **limits)
            row.update(actual_exit=result.returncode, completed=True)
            self.observe('end', stage, result.returncode)
            return result
        except BaseException as error:
            row['error'] = type(error).__name__ + ': ' + str(error)
            self.observe('interrupted', stage)
            raise
        finally:
            row['elapsed_seconds'] = time.monotonic() - started

    def report(self):
        missing = [name for name in self.required if name not in self.stages]
        passed = not self.failures and not missing and all(
            row['completed'] and row['actual_exit'] == row['expected_exit']
            for row in self.stages.values())
        return {'schema': 1, **self.context, 'seconds': time.monotonic() - self.started,
                'telemetry_failures': self.telemetry_failures, 'required_stages': list(self.required),
                'stages': self.stages, 'missing_stages': missing,
                'supervision_failures': self.failures,
                'execution_passed': passed, 'parity_verified': False}

    def finish(self, destination, budget=None):
        """Retain a new report and return the wrapper's required exit status.

        Caller owns artifact admission and must use `raise SystemExit(finish(...))`.
        Exclusive creation protects previously retained reports.
        """
        report = self.report()
        destination = Path(destination)
        pending = destination.with_suffix('.pending.json')
        if destination.exists() or destination.is_symlink():
            raise FileExistsError(destination)
        with pending.open('x', encoding='utf-8') as output:
            json.dump(report, output, indent=2)
            output.write('\n')
        if budget is not None:
            # Scan with the complete report bytes present. A failed observation
            # retains the provisional report and publishes an explicit failure.
            try:
                budget.check()
            except BaseException as error:
                self.failures.append('final artifact observation: ' + str(error))
                with destination.open('x', encoding='utf-8') as output:
                    json.dump(self.report(), output, indent=2)
                raise
        # Same-directory rename does not add payload bytes. No previous attempt
        # is replaced: output directory was exclusively created by this runner.
        pending.rename(destination)
        return 0 if report['execution_passed'] else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--artifact-links', choices=('reject', 'count'), default='reject',
                        help='count link metadata without following targets; does not establish isolation')
    args = parser.parse_args(argv)
    budget = None
    experiment = None
    created = False
    try:
        with args.manifest.open('rb') as source:
            raw = source.read(4 * 1024 * 1024 + 1)
        if len(raw) > 4 * 1024 * 1024:
            raise ValueError('manifest exceeds size bound')
        manifest = json.loads(raw)
        if not isinstance(manifest, dict) or set(manifest) != {'stages'}:
            raise ValueError('manifest must contain only stages')
        stages = manifest['stages']
        if not isinstance(stages, list) or not 1 <= len(stages) <= 32:
            raise ValueError('one to 32 stages required')
        experiment = Experiment([stage['name'] for stage in stages])
        for stage in stages:
            if set(stage) - {'name', 'command', 'expected_exit'}:
                raise ValueError('unknown stage field')
            command = stage['command']
            if not isinstance(command, list) or not command or any(
                    not isinstance(part, str) or not part for part in command):
                raise ValueError('command must be a nonempty argv list')
            if type(stage.get('expected_exit', 0)) is not int:
                raise ValueError('expected exit must be an integer')
        args.output = args.output.absolute()
        if args.output.parent.resolve(strict=True) != args.output.parent:
            raise ValueError('canonical output parent required')
        if any(part in ('ref', '.git') for part in args.output.parts):
            raise ValueError('protected output path')
        args.output.mkdir(mode=0o700, parents=False, exist_ok=False)
        created = True
        spec = importlib.util.spec_from_file_location('experiment_pool',
                                                     Path(__file__).with_name('resource_pool.py'))
        pool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(pool)
        budget = pool.ArtifactBudget([args.output.resolve()], 8 * 1024 * 1024,
                                     link_policy=args.artifact_links)
        # Persist context before admission/execution: a killed launcher may never
        # reach finish(). Names, argv and environment stay out of this journal.
        with (args.output / 'attempt.json').open('x', encoding='utf-8') as stream:
            json.dump(experiment.context, stream)
            stream.flush()
            os.fsync(stream.fileno())
        experiment.journal = args.output / 'stages.jsonl'
        experiment.observe('attempt_started')
        with pool.ResourcePool(pressure=pool.Pressure()).acquire(
                {'cpu': 1, 'host_memory': 2, 'build_writer': 1}, timeout=30) as lease:
            for index, stage in enumerate(stages):
                with (args.output / f'{index}.log').open('xb') as output:
                    result = experiment.run(lease, stage['name'], stage['command'],
                                            expected_exit=stage.get('expected_exit', 0),
                                            timeout=args.timeout, output=output,
                                            max_output_bytes=1024 * 1024,
                                            artifact_budget=budget)
                if result.returncode != stage.get('expected_exit', 0):
                    break
        code = experiment.finish(args.output / 'result.json', budget)
        return code
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, TimeoutError,
            subprocess.TimeoutExpired) as error:
        print(f'experiment: {error}', file=sys.stderr)
        if experiment is not None:
            experiment.failures.append(type(error).__name__ + ': ' + str(error))
        if experiment is not None and created and not (args.output / 'result.json').exists():
            experiment.finish(args.output / 'result.json')
        return 2
    finally:
        if budget is not None:
            budget.close()


if __name__ == '__main__':
    raise SystemExit(main())
