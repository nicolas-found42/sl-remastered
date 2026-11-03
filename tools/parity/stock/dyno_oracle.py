#!/usr/bin/env python3
"""Bounded original Dyno oracle; compile by default, Stock execution explicit.

This is a conditional pure-core experiment, never actual-game parity. Existing
qualified console isolation and prepaid preservation remain mandatory.
"""
from pathlib import Path
import argparse
import hashlib
import json
import runpy
import shutil
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/parity'))
import resource_pool as pool
import prepaid_budget as prepaid
import experiment_result as experiment

TEMPLATE = ROOT / 'local/verify/parity-goal-20261102-092437/factory-refusal-exact-env/issue53-original-initialization-v1'
STOCK_SHA = '7d77466a8c1a34d16a59d9ccf2e8059664dff1bfb421338074a461a932e8edde'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit(out):
    """Compare raw original outputs; no portable-model expected values."""
    captures = list(out.glob('capture-*/work'))
    assert len(captures) == 1
    work = captures[0]
    processes = []
    for name in ['first-0', 'repeat-0']:
        rows = [json.loads(line) for line in (work / (name + '.jsonl')).read_text().splitlines()]
        cases = [row for row in rows if 'case' in row]
        assert len(cases) == 7 and [row['case'] for row in cases] == list(range(7))
        assert len([row for row in rows if row.get('type') == 'patch']) == 3
        assert rows[-1] == {'type': 'scope', 'game_entry_executed': False, 'allocator_patches': 3}
        for row in cases:
            assert row['step'] == 437 and len(row['statistics']) == 6
            for field in ['statistics', 'torque', 'power', 'nitro_torque', 'nitro_power', 'state']:
                values = row[field]
                assert all(type(v) is int and 0 <= v <= 0xffffffff for v in values)
                assert all(v & 0x7f800000 != 0x7f800000 for v in values)
                expected = 116 if field == 'state' else 6 if field == 'statistics' else 16
                if field.startswith('nitro_') and row['case'] != 4:
                    expected = 0
                assert len(values) == expected
        processes.append(cases)
    assert processes[0] == processes[1], 'same-side original process repeat diverged'
    mutations = {str(i): any(processes[0][i][k] != processes[0][0][k]
        for k in ['statistics', 'torque', 'power', 'nitro_torque', 'nitro_power'])
        for i in range(2, 7)}
    assert all(mutations.values()), 'input mutation failed to change original output'
    return dict(schema=1, qualified_original_core_repeat=True,
        scope='Conditional mapped original pure-core oracle, not actual Stock gameplay or paired parity',
        stock_sha256=STOCK_SHA, mutations_observed=mutations,
        raw_files={name: sha(work / (name + '.jsonl')) for name in ['first-0', 'repeat-0']},
        cases=processes[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--parent-max-mib', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    parent, out = args.parent.resolve(), args.output.resolve()
    assert sha(ROOT / 'ref/raw/StreetLegal_Redline.bak.exe') == STOCK_SHA
    packet = ROOT / 'local/verify/issue53-car-refresh-20261102/dyno-core-rea.json'
    reads = json.loads(packet.read_text())['reads']
    rea = {}
    for key in ['dyno_wrapper', 'dyno_core_bytes', 'constants']:
        item = reads[key]['structuredContent']
        assert item['subject']['digest']['sha256'] == STOCK_SHA
        rea[key] = dict(evidence_id=item['evidence_id'], operation=item['operation'],
            parameters=item['parameters'])
    packet_hash = sha(packet)
    source = ROOT / 'tests/wine_original_dyno_test.c' 
    originals = {str(p): sha(p) for p in [source, Path(__file__).resolve(), TEMPLATE / 'capture.py', TEMPLATE / 'prefix_fixture.sh', TEMPLATE / 'prefix_shell.py', packet]}
    budget = prepaid.LeaseBudgetAdapter.admit(pool.ArtifactLimit, parent, out,
        ROOT / 'local/storage-index', args.parent_max_mib * 1024**2,
        child_max=8 * 1024**2)
    try:
        shutil.copy2(source, out / 'original-world.c')
        for name in ['prefix_fixture.sh', 'prefix_shell.py']:
            shutil.copy2(TEMPLATE / name, out / name)
        shutil.copy2(__file__, out / 'driver.py')
        command = ['i686-w64-mingw32-gcc', '-O0', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-function', '-fno-stack-protector', '-fno-omit-frame-pointer',
            '-mstackrealign', '-Wl,--image-base,0x10000000',
            str(out / 'original-world.c'), '-o', str(out / 'original-world.exe')]
        result = experiment.Experiment(['compile'])
        with pool.ResourcePool(pressure=pool.Pressure()).acquire(
                {'cpu': 1, 'host_memory': 2, 'build_writer': 1}, timeout=90) as lease:
            (out / 'admission.json').write_text(json.dumps(lease.description, indent=2))
            with (out / 'compile.log').open('xb') as log:
                result.run(lease, 'compile', command, timeout=60, output=log,
                    max_output_bytes=1024**2, artifact_budget=budget)
        assert result.finish(out / 'compile-result.json', budget) == 0
        build = dict(returncode=0, exe_sha256=sha(out / 'original-world.exe'),
            sources={'original-world.c': sha(out / 'original-world.c')},
            original_sources=originals, stock_sha256=STOCK_SHA,
            rea_packet=dict(path=str(packet), sha256=packet_hash, bindings=rea),
            scope='Original pure Dyno core, allocator-only substitution; no game entry')
        (out / 'build.json').write_text(json.dumps(build, indent=2))
        if args.execute:
            runner = (TEMPLATE / 'capture.py').read_text()
            runner = runner.replace("'scope':'Original constructors/finalizer plus40 original world steps with original empty-queue discovery; no body/prototype/geometry overwrites; no collision qualification'", "'scope':'Original pure Dyno core, seven input cases, allocator-only substitutions; no game entry or whole-game parity'")
            (out / 'capture.py').write_text(runner)
            sys.path.insert(0, str(out))
            try:
                runpy.run_path(str(out / 'capture.py'), init_globals={'PREPAID': budget}, run_name='__main__')
            except SystemExit as error:
                if error.code:
                    raise RuntimeError('original Dyno execution failed') from error
            (out / 'dyno-audit.json').write_text(json.dumps(audit(out), indent=2))
        assert all(sha(Path(path)) == digest for path, digest in originals.items())
        budget.check('final')
        (out / 'prepaid-audit.json').write_text(json.dumps(budget.budget.report(), indent=2))
        budget.check('final')
    finally:
        budget.close()


if __name__ == '__main__':
    main()
