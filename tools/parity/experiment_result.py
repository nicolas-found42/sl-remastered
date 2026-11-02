"""Fail-closed result contract for console experiments using ResourcePool leases.

Execution success is deliberately distinct from parity or review approval.
Expected nonzero exits must be declared before execution by the caller.
"""
import json
import argparse
import importlib.util
import sys
import subprocess
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

    def run(self, lease, name, command, *, expected_exit=0, **limits):
        if name not in self.required or name in self.stages:
            raise ValueError('unknown or repeated stage')
        if type(expected_exit) is not int:
            raise ValueError('expected exit must be an integer')
        row = {'command': list(command), 'expected_exit': expected_exit,
               'actual_exit': None, 'completed': False}
        self.stages[name] = row
        try:
            result = lease.run(command, **limits)
            row.update(actual_exit=result.returncode, completed=True)
            return result
        except BaseException as error:
            row['error'] = type(error).__name__ + ': ' + str(error)
            raise

    def report(self):
        missing = [name for name in self.required if name not in self.stages]
        passed = not self.failures and not missing and all(
            row['completed'] and row['actual_exit'] == row['expected_exit']
            for row in self.stages.values())
        return {'schema': 1, 'required_stages': list(self.required),
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
        args.output.mkdir(parents=False, exist_ok=False)
        created = True
        spec = importlib.util.spec_from_file_location('experiment_pool',
                                                     Path(__file__).with_name('resource_pool.py'))
        pool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(pool)
        budget = pool.ArtifactBudget([args.output.resolve()], 8 * 1024 * 1024,
                                     link_policy=args.artifact_links)
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
