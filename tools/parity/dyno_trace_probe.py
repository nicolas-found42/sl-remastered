#!/usr/bin/env python3
"""Build and run the bounded production-model numerical observer probe."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/parity'))
import experiment_result as experiment
import prepaid_budget as prepaid
import resource_pool as pool


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--parent-max-mib', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tables', action='store_true')
    parser.add_argument('--model-source', type=Path)
    parser.add_argument('--expect-differences', action='store_true')
    args = parser.parse_args()
    parent, out = args.parent.resolve(), args.output.resolve()
    model = args.model_source.resolve() if args.model_source else ROOT / 'src/dyno_model.cpp'
    probe = ROOT / ('tests/dyno_model_probe.cpp' if args.tables else 'tests/dyno_step_probe.cpp')
    sources = [model, ROOT / 'src/dyno_model.hpp', probe, Path(__file__).resolve()]
    if args.expect_differences:
        assert args.tables and args.model_source
        assert sha(model) == 'ad53cd65b77be453a58f11d5c1edee54f51cdebe4013d8ffdb52b17ac24065f6'
    expected_outcome = True
    before = {str(path): sha(path) for path in sources}
    budget = prepaid.LeaseBudgetAdapter.admit(pool.ArtifactLimit, parent, out,
        ROOT / 'local/storage-index', args.parent_max_mib * 1024**2,
        child_max=8 * 1024**2)
    try:
        for path in sources:
            shutil.copy2(path, out / path.name)
        executable = out / 'dyno-step-probe'
        command = ['clang++', '-std=c++20', '-O0', '-Wall', '-Wextra',
            '-Wshorten-64-to-32', '-ffp-contract=off', '-fsanitize=address,undefined',
            '-fno-sanitize-recover=undefined', '-I', str(out),
            str(out / 'dyno_model.cpp'), str(out / probe.name),
            '-o', str(executable)]
        exp = experiment.Experiment(['compile', 'first', 'repeat'])
        with pool.ResourcePool(pressure=pool.Pressure()).acquire(
                {'cpu': 1, 'host_memory': 2, 'build_writer': 1}, timeout=120) as lease:
            (out / 'admission.json').write_text(json.dumps(lease.description, indent=2))
            for stage, argv in [('compile', command), ('first', [str(executable)]),
                                ('repeat', [str(executable)])]:
                with (out / (stage + '.log')).open('xb') as log:
                    done = exp.run(lease, stage, argv, timeout=60, output=log,
                        max_output_bytes=1024**2, artifact_budget=budget)
                if done.returncode:
                    break
        assert exp.finish(out / 'result.json', budget) == 0
        assert before == {str(path): sha(path) for path in sources}
        assert (out / 'first.log').read_bytes() == (out / 'repeat.log').read_bytes()
        rows = [json.loads(line) for line in (out / 'first.log').read_text().splitlines()]
        if args.tables:
            reference = ROOT / 'local/verify/issue53-native-clock-abi-v3-20261102/dyno-original-step-v2/dyno-audit.json'
            assert sha(reference) == '2dfd8b92055f37a9ab6f468a8af6aa64a2e913fd6d29a783f069c92cb81f6015'
            original = json.loads(reference.read_text())['cases']
            assert [row['case'] for row in rows] == [row['case'] for row in original]
            mismatches = []
            for port, stock in zip(rows, original):
                if port['step'] != stock['step']:
                    mismatches.append(dict(case=port['case'], field='step', port=port['step'], stock=stock['step']))
                for field in ['statistics', 'torque', 'power', 'nitro_torque', 'nitro_power']:
                    assert len(port[field]) == len(stock[field])
                    for index, (actual, expected) in enumerate(zip(port[field], stock[field])):
                        if actual != expected:
                            mismatches.append(dict(case=port['case'], field=field, index=index, port=actual, stock=expected))
            expected_outcome = bool(mismatches) == args.expect_differences
            (out / 'comparison.json').write_text(json.dumps(dict(reference=str(reference),
                reference_sha256=sha(reference), port_sha256=sha(out / 'first.log'),
                raw_word_parity=not mismatches, expected_differences=args.expect_differences,
                mismatch_count=len(mismatches), mismatches=mismatches,
                scope='Nine conditional original/new native model cases; no actual game parity'), indent=2))
        else:
            assert rows[-1]['observer_output_unchanged'] is True
        (out / 'bindings.json').write_text(json.dumps(dict(sources=before,
            executable_sha256=sha(executable), trace_sha256=sha(out / 'first.log'),
            records=len(rows), source_unchanged=True, same_side_exact_repeat=True,
            scope='Native production-model observer, no actual game or Stock parity'), indent=2))
        budget.check('final')
        (out / 'prepaid-audit.json').write_text(json.dumps(budget.budget.report(), indent=2))
        budget.check('final')
    finally:
        budget.close()
    assert expected_outcome, 'conditional original raw-word comparison failed'


if __name__ == '__main__':
    main()
