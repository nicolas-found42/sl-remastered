"""Developer entry point controls: command planning and honest reporting (no game runs)."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('dev', Path(__file__).with_name('dev.py'))
dev = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dev)


def fixture_catalog(statuses, chain=False):
    rungs = [{'id': f'R{i}', 'assertion_status': s, 'variant_inventory_complete': s == 'verified',
              'depends_on': [f'R{i-1}'] if chain and i else [], 'variants': ['a', 'b']} for i, s in enumerate(statuses)]
    return {'status': 'candidates', 'scope': 'fixture', 'rungs': rungs,
            'journeys': [{'id': 'J', 'rungs': ['R0', 'R-missing']}]}


class DevTest(unittest.TestCase):
    def mixed(self):
        return {'started': 0.0, 'finished': 10.0, 'passed': True,
                'tasks': [{'name': name, 'started': float(i*3), 'finished': float(i*3+2), 'exit_code': 0}
                          for i, name in enumerate(('native', 'stock', 'cpu'))],
                'heartbeat': [{'at': i*.05, 'gap_seconds': (i*.05-(i-1)*.05) if i else 0.0}
                              for i in range(200)],
                'pressure': [{'at': float(i), 'ok': True,
                              'sample': {'available_memory_bytes': 4*1024**3, 'free_disk_bytes': 20*1024**3}}
                             for i in range(10)]}

    def test_mix_reports_only_completed_equal_work_and_observed_responsiveness(self):
        report = dev.mix_timing(self.mixed())
        self.assertEqual(report['completed_tasks'], 3)
        self.assertEqual(report['wall_seconds'], 10)
        self.assertEqual(report['tasks_per_minute'], 18)
        self.assertLess(report['heartbeat_seconds']['max'], .051)

    def test_dropped_failed_duplicate_or_boolean_work_cannot_be_a_speedup(self):
        for mutation in ('missing', 'duplicate', 'failed', 'boolean', 'incomplete', 'negative', 'nan'):
            group = self.mixed()
            if mutation == 'missing': group['tasks'].pop()
            elif mutation == 'duplicate': group['tasks'][0]['name'] = 'cpu'
            elif mutation == 'failed': group['tasks'][0]['exit_code'] = 1
            elif mutation == 'boolean': group['tasks'][0]['exit_code'] = False
            elif mutation == 'incomplete': group['tasks'][0]['incomplete'] = True
            elif mutation == 'negative': group['finished'] = -1
            else: group['tasks'][0]['finished'] = float('nan')
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                dev.mix_timing(group)

    def test_mix_refuses_missing_pressure_counter_and_responsiveness_gaps(self):
        for mutation in ('no-heartbeat', 'lost-pressure', 'bad-counter', 'gap', 'dropped-beat', 'early-end',
                         'missing-pressure', 'late-pressure', 'low-pressure'):
            group = self.mixed()
            if mutation == 'no-heartbeat': group['heartbeat'] = []
            elif mutation == 'lost-pressure': group['pressure'][0]['ok'] = False
            elif mutation == 'bad-counter': group['pressure'][0]['sample']['free_disk_bytes'] = True
            elif mutation == 'gap': group['heartbeat'][10]['gap_seconds'] = .5
            elif mutation == 'dropped-beat': del group['heartbeat'][10]
            elif mutation == 'early-end': group['heartbeat'] = group['heartbeat'][:20]
            elif mutation == 'missing-pressure': group['pressure'] = group['pressure'][:1]
            elif mutation == 'late-pressure': del group['pressure'][4]
            else: group['pressure'][0]['sample']['available_memory_bytes'] = 0
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                dev.mix_timing(group)

    def test_perf_capture_audit_never_launches_missing_or_incomplete_work(self):
        with self.assertRaises(FileNotFoundError):
            dev.perf_captures(Path(self.temp.name) / 'missing')
        planned = subprocess.run([sys.executable, '-I', dev.__file__, '--dry-run', 'perf-captures', self.temp.name],
                                 capture_output=True, text=True, timeout=30)
        self.assertEqual(planned.returncode, 0)
        self.assertIn('read-only evidence audit', planned.stdout)
    def test_test_lane_builds_through_the_pool_then_runs_ctest_under_renderer_lock(self):
        build, run = dev.plan_test('object_world')
        self.assertIn('--build-writer', build)
        self.assertEqual(build[build.index('--') + 1:build.index('--') + 3], ['cmake', '--build'])
        self.assertIn('--renderer', run)
        self.assertEqual(run[run.index('--') + 1], 'ctest')
        self.assertIn('--output-on-failure', run)
        self.assertEqual(len(dev.plan_test('x', build=False)), 1)

    def test_empty_or_option_like_patterns_are_refused(self):
        for bad in ('', '--help', '-j8'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                dev.plan_test(bad)

    def test_coverage_never_reports_candidates_as_verified(self):
        report = dev.coverage(self.write(fixture_catalog(['candidate', 'candidate', 'verified'])))
        self.assertEqual((report['rungs'], report['verified_rungs'], report['pending_rungs']), (3, 1, 2))
        self.assertEqual(report['assertion_status'], {'candidate': 2, 'verified': 1})
        self.assertEqual(report['variant_inventory_incomplete'], 2)
        self.assertEqual(report['journey_unknown_rungs'], {'J': ['R-missing']})
        none = dev.coverage(self.write(fixture_catalog(['candidate'] * 4)))
        self.assertEqual((none['verified_rungs'], none['pending_rungs']), (0, 4))

    def test_real_catalog_reports_zero_verified(self):
        report = dev.coverage()
        self.assertEqual(report['verified_rungs'], 0)
        self.assertEqual(report['pending_rungs'], report['rungs'])

    def test_corpus_lane_is_derived_from_the_gate_script(self):
        script = Path(self.temp.name) / 'check.sh'
        script.write_text('echo hi\npython3 -I tests/a_test.py\npython3 -I tools/parity/test_b.py\n'
                          'python3 -I tools/other.py\n  python3 -I tests/indented_test.py\n')
        commands = dev.corpus_commands(script)
        self.assertEqual([c[-1] for c in commands], ['tests/a_test.py', 'tools/parity/test_b.py'])
        script.write_text('echo nothing\n')
        with self.assertRaises(ValueError):
            dev.corpus_commands(script)

    def test_real_gate_script_yields_this_tool_and_other_parity_suites(self):
        names = [c[-1] for c in dev.corpus_commands(dev.ROOT / 'tools/check.sh')]
        self.assertIn('tools/parity/test_control_interval.py', names)
        self.assertIn('tools/parity/stock/test_durable_wire.py', names)
        self.assertIn('tools/parity/stock/test_stock_input_tape.py', names)
        self.assertIn('tools/parity/test_storage.py', names)

    def test_corpus_preserves_pool_limits_and_interpreter(self):
        script = Path(self.temp.name) / 'check.sh'
        script.write_text('python3 -I tools/parity/resource_pool.py --wait 5 --cpu 1 '
                          '--host-memory 1 --timeout 30 -- \\\n'
                          '  python3 -I tools/parity/stock/test_durable_wire.py # retained limits\n')
        commands = dev.corpus_commands(script)
        self.assertEqual(commands, [[sys.executable, '-I', 'tools/parity/resource_pool.py',
                                    '--wait', '5', '--cpu', '1', '--host-memory', '1', '--timeout', '30',
                                    '--', sys.executable, '-I', 'tools/parity/stock/test_durable_wire.py']])
        script.write_text('python3 -I tests/a_test.py ; echo ignored\n')
        with self.assertRaisesRegex(ValueError, 'unsupported Python suite'):
            dev.corpus_commands(script)

    def test_repro_reports_deterministic_red_and_flaky_differently(self):
        red = lambda argv: (6, 'FAILED  0.18 sec\nassert x\n', 0.2)
        report = dev.repro('object_world_step_unit', 3, runner=red)
        self.assertTrue(report['red'] and report['deterministic'])
        self.assertEqual(report['distinct_output_signatures'], 1)
        flip = iter([(0, 'ok', 0.1), (6, 'boom', 0.1), (0, 'ok', 0.1)])
        flaky = dev.repro('t', 3, runner=lambda argv: next(flip))
        self.assertFalse(flaky['deterministic'])
        self.assertTrue(flaky['red'])
        green = dev.repro('t', 2, runner=lambda argv: (0, 'ok', 0.1))
        self.assertFalse(green['red'])

    def test_repro_rejects_regex_or_traversal_names(self):
        for bad in ('a.*', 'a b', '../x', ''):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                dev.repro(bad, 1, runner=lambda argv: (0, '', 0))
        with self.assertRaises(ValueError):
            dev.repro('ok', 0, runner=lambda argv: (0, '', 0))

    def test_timing_noise_does_not_change_the_signature(self):
        self.assertEqual(dev.signature('Test #1: x ... 0.18 sec\n'), dev.signature('Test #1: x ... 0.21 sec\n'))
        self.assertNotEqual(dev.signature('a'), dev.signature('b'))

    def test_perf_reports_failures_instead_of_timing_only(self):
        report = dev.perf('x', 2, runner=lambda argv: (1, '', 0.5))
        self.assertFalse(report['all_passed'])
        self.assertEqual(report['seconds']['median'], 0.5)

    def test_ctest_selection_fails_when_no_test_matches(self):
        run = dev.plan_test('x', build=False)[0]
        self.assertIn('--no-tests=error', run)
        self.assertIn('--no-tests=error', dev.pooled(dev.RUN, ['ctest', '--no-tests=error']))
        with open(dev.HERE / 'dev.py') as source:
            text = source.read()
        # every ctest -R selection in the tool carries the flag
        import re
        selections = re.findall(r"\['ctest'[^\]]*'-R'[^\]]*\]", text)
        self.assertEqual(len(selections), 3)
        self.assertTrue(all('--no-tests=error' in sel for sel in selections), selections)

    def test_nonexistent_test_name_is_a_failure_through_the_real_ctest(self):
        build = dev.ROOT / 'build'
        if not (build / 'CTestTestfile.cmake').exists():
            self.skipTest('no configured build tree')
        result = subprocess.run([sys.executable, '-I', dev.__file__, 'test', '--no-build', 'object_wrold_typo_xyz'],
                                capture_output=True, text=True, timeout=120)
        self.assertNotEqual(result.returncode, 0, result.stdout[-300:])

    def test_coverage_refuses_to_count_inconsistent_verified_claims(self):
        ghost = fixture_catalog(['verified'])
        ghost['rungs'][0].update(variant_inventory_complete=False, depends_on=['ghost'])
        report = dev.coverage(self.write(ghost))
        self.assertEqual((report['verified_rungs'], report['pending_rungs']), (0, 1))
        reasons = report['inconsistent_verified_claims'][0]['declared_verified_but']
        self.assertTrue(any('inventory' in r for r in reasons) and any('ghost' in r for r in reasons))
        self.assertFalse(dev.coverage_consistent(report))
        dup = fixture_catalog(['verified', 'verified'])
        dup['rungs'][1]['id'] = 'R0'
        report = dev.coverage(self.write(dup))
        self.assertEqual(report['verified_rungs'], 0)
        self.assertEqual(report['duplicate_ids'], ['R0'])
        chained = fixture_catalog(['candidate', 'verified'], chain=True)
        report = dev.coverage(self.write(chained))
        self.assertEqual(report['verified_rungs'], 0)
        self.assertIn('unverified dependencies', str(report['inconsistent_verified_claims']))
        good = dev.coverage(self.write(fixture_catalog(['verified', 'verified'], chain=True)))
        self.assertEqual(good['verified_rungs'], 2)

    def test_dry_run_and_perf_validate_names_like_the_real_run(self):
        run = lambda *a: subprocess.run([sys.executable, '-I', dev.__file__, *a], capture_output=True, text=True, timeout=30)
        self.assertEqual(run('--dry-run', 'repro', 'a b').returncode, 2)
        self.assertEqual(run('--dry-run', 'perf', '').returncode, 2)
        self.assertEqual(run('--dry-run', 'repro', 'ok_test', '--runs', '0').returncode, 2)
        self.assertEqual(run('--dry-run', 'repro', 'ok_test').returncode, 0)
        with self.assertRaises(ValueError):
            dev.perf('', 1, runner=lambda argv: (0, '', 0))
        with self.assertRaises(ValueError):
            dev.perf('-N', 1, runner=lambda argv: (0, '', 0))

    def test_repro_and_perf_build_first(self):
        planned = json.loads(subprocess.run([sys.executable, '-I', dev.__file__, '--dry-run', 'repro', 'ok_test'],
                                            capture_output=True, text=True, timeout=30).stdout)
        self.assertIn('cmake', planned['steps'][0])
        self.assertIn('--build-writer', planned['steps'][0])

    def test_cli_dry_run_and_exit_codes(self):
        run = lambda *a: subprocess.run([sys.executable, '-I', dev.__file__, *a], capture_output=True, text=True, timeout=30)
        planned = run('--dry-run', 'test', 'foo')
        self.assertEqual(planned.returncode, 0)
        self.assertEqual(len(json.loads(planned.stdout)), 2)
        self.assertEqual(run('--dry-run', 'test', '-j8').returncode, 2)
        self.assertEqual(run('--dry-run', 'test', '--', '-j8').returncode, 2)
        self.assertEqual(run('coverage', '--json').returncode, 0)
        self.assertEqual(run('--dry-run', 'gate').returncode, 0)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)

    def write(self, obj):
        path = Path(self.temp.name) / 'catalog.json'
        path.write_text(json.dumps(obj))
        return path


if __name__ == '__main__':
    unittest.main()
