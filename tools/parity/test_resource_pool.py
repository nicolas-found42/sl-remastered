import importlib.util
import fcntl
import hashlib
import json
import io
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

TOOL = Path(__file__).with_name('resource_pool.py')
spec = importlib.util.spec_from_file_location('resource_pool', TOOL)
pool_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pool_module)


class AdmissionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.capacity = {'cpu': 2, 'memory': 2, 'writer': 1}
        self.pool = pool_module.ResourcePool(self.root / 'pool', self.capacity)

    def holder(self, request):
        ready = self.root / ('ready-' + str(time.monotonic_ns()))
        script = ('import importlib.util,json,sys,time\nfrom pathlib import Path\n'
                  's=importlib.util.spec_from_file_location("pool",sys.argv[1])\n'
                  'm=importlib.util.module_from_spec(s);s.loader.exec_module(m)\n'
                  'p=m.ResourcePool(sys.argv[2],json.loads(sys.argv[3]))\n'
                  'with p.acquire(json.loads(sys.argv[4]),2):\n'
                  ' Path(sys.argv[5]).touch()\n time.sleep(30)\n')
        process = subprocess.Popen([sys.executable, '-I', '-c', script, str(TOOL),
                                    str(self.pool.directory), json.dumps(self.capacity),
                                    json.dumps(request), str(ready)],
                                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE)
        def cleanup():
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=3)
        self.addCleanup(cleanup)
        deadline = time.monotonic() + 3
        while not ready.exists():
            if process.poll() is not None:
                self.fail(process.communicate()[1].decode())
            if time.monotonic() > deadline:
                self.fail('holder did not acquire resources')
            time.sleep(.01)
        return process

    def test_independent_processes_share_aggregate_budget(self):
        self.holder({'cpu': 1})
        with self.pool.acquire({'cpu': 1}, .2):
            with self.assertRaises(TimeoutError):
                with self.pool.acquire({'cpu': 1}, .1):
                    self.fail('oversubscribed CPU budget')
            # A different resource remains usable while CPU permits are full.
            with self.pool.acquire({'memory': 2}, .1):
                pass

    def test_failed_multiresource_admission_rolls_back_all_permits(self):
        process = self.holder({'writer': 1})
        with self.assertRaises(TimeoutError):
            with self.pool.acquire({'cpu': 2, 'memory': 2, 'writer': 1}, .1):
                self.fail('exclusive writer admitted twice')
        with self.pool.acquire({'cpu': 2, 'memory': 2}, .1):
            pass
        process.kill()
        process.communicate(timeout=3)
        with self.pool.acquire({'cpu': 2, 'memory': 2, 'writer': 1}, .2):
            pass

    def test_exception_releases_lease(self):
        with self.assertRaisesRegex(RuntimeError, 'attempt failed'):
            with self.pool.acquire({'cpu': 2, 'writer': 1}, .1):
                raise RuntimeError('attempt failed')
        with self.pool.acquire({'cpu': 2, 'writer': 1}, .1):
            pass

    def test_stalled_admission_mutex_obeys_request_deadline(self):
        with (self.pool.directory / 'admission.lock').open('rb') as gate:
            fcntl.flock(gate, fcntl.LOCK_EX)
            started = time.monotonic()
            with self.assertRaises(TimeoutError):
                with self.pool.acquire({'cpu': 1}, .05):
                    pass
            self.assertLess(time.monotonic() - started, .5)

    def test_guardian_cleans_root_and_releases_permits_after_launcher_is_killed(self):
        directory = self.root / 'default-pool'
        ready = self.root / 'root-ready'
        code = ('from pathlib import Path;import os,signal,sys,time;'
                'signal.signal(signal.SIGTERM,signal.SIG_IGN);'
                'Path(sys.argv[1]).write_text(str(os.getpid()));time.sleep(30)')
        process = subprocess.Popen([sys.executable, '-I', str(TOOL), '--pool', str(directory),
                                    '--cpu', '12', '--', sys.executable, '-I', '-c', code, str(ready)],
                                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, start_new_session=True)
        def cleanup():
            if ready.exists():
                try:
                    os.killpg(int(ready.read_text()), signal.SIGKILL)
                except ProcessLookupError:
                    pass
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.communicate(timeout=3)
        self.addCleanup(cleanup)
        deadline = time.monotonic() + 3
        while not ready.exists():
            if process.poll() is not None:
                self.fail(process.communicate()[1].decode())
            if time.monotonic() > deadline:
                self.fail('supervised root was not started')
            time.sleep(.01)
        process.kill()
        process.wait(timeout=3)
        pool = pool_module.ResourcePool(directory)
        # The guardian owns cleanup even when the Python launcher cannot run
        # finally. Its leases remain held until its root has been reaped.
        with self.assertRaises(TimeoutError):
            with pool.acquire({'cpu': 1}, .1):
                self.fail('stubborn live root lost its reservation during cleanup')
        with pool.acquire({'cpu': 12}, 4):
            with self.assertRaises(ProcessLookupError):
                os.kill(int(ready.read_text()), 0)
            pass

    def test_rejects_incompatible_configuration_and_impossible_requests(self):
        with self.assertRaises(ValueError):
            pool_module.ResourcePool(self.pool.directory, {'cpu': 3})
        for request in ({}, {'cpu': 0}, {'cpu': 3}, {'cpu': True}, {'unknown': 1}):
            with self.assertRaises(ValueError):
                with self.pool.acquire(request, .1):
                    pass
        for timeout in (0, float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                with self.pool.acquire({'cpu': 1}, timeout):
                    pass

    def test_root_retains_lease_if_guardian_and_launcher_are_killed(self):
        directory = self.root / 'guardian-death-pool'
        ready = self.root / 'orphan.json'
        code = ('import json,os,sys,time;from pathlib import Path;'
                'Path(sys.argv[1]).write_text(json.dumps({"root":os.getpid(),"guardian":os.getppid()}));'
                'time.sleep(30)')
        process = subprocess.Popen([sys.executable, '-I', str(TOOL), '--pool', str(directory),
                                    '--cpu', '12', '--', sys.executable, '-I', '-c', code, str(ready)],
                                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE)
        def cleanup():
            if ready.exists():
                for pid in json.loads(ready.read_text()).values():
                    try:
                        os.kill(pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=3)
        self.addCleanup(cleanup)
        deadline = time.monotonic() + 3
        while not ready.exists():
            if process.poll() is not None:
                self.fail(process.communicate()[1].decode())
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)
        owned = json.loads(ready.read_text())
        os.kill(owned['guardian'], signal.SIGKILL)
        process.kill()
        process.wait(timeout=2)
        os.kill(owned['root'], 0)
        pool = pool_module.ResourcePool(directory)
        with self.assertRaises(TimeoutError):
            with pool.acquire({'cpu': 1}, .1):
                self.fail('surviving root lost its lease after guardian destruction')
        os.kill(owned['root'], signal.SIGKILL)
        with pool.acquire({'cpu': 12}, 2):
            pass

    def test_symlink_slot_does_not_touch_target(self):
        target = self.root / 'untouched'
        target.write_bytes(b'preserve')
        (self.pool.directory / 'cpu-0.lock').symlink_to(target)
        with self.assertRaises(OSError):
            with self.pool.acquire({'cpu': 1}, .1):
                pass
        self.assertEqual(target.read_bytes(), b'preserve')

    def queued(self, request, timeout=5):
        """Start a contender and wait for its live queue entry, not a timing guess."""
        acquired = self.root / ('acquired-' + str(time.monotonic_ns()))
        before = set(self.pool.directory.glob('wait-*.lock'))
        code = ('import importlib.util,json,sys,time\nfrom pathlib import Path\n'
                's=importlib.util.spec_from_file_location("pool",sys.argv[1])\n'
                'm=importlib.util.module_from_spec(s);s.loader.exec_module(m)\n'
                'p=m.ResourcePool(sys.argv[2],json.loads(sys.argv[3]))\n'
                'with p.acquire(json.loads(sys.argv[4]),float(sys.argv[6])):\n'
                ' Path(sys.argv[5]).touch()\n time.sleep(30)\n')
        process = subprocess.Popen([sys.executable, '-I', '-c', code, str(TOOL),
                                    str(self.pool.directory), json.dumps(self.capacity),
                                    json.dumps(request), str(acquired), str(timeout)],
                                   stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        def cleanup():
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=3)
        self.addCleanup(cleanup)
        deadline = time.monotonic() + 3
        while not (set(self.pool.directory.glob('wait-*.lock')) - before):
            if acquired.exists():
                self.fail('contender bypassed the blocked head of the queue')
            if process.poll() is not None:
                self.fail(process.communicate()[1].decode())
            if time.monotonic() >= deadline:
                self.fail('contender did not queue')
            time.sleep(.01)
        return process, acquired

    def await_acquired(self, process, acquired):
        deadline = time.monotonic() + 3
        while not acquired.exists():
            if process.poll() is not None:
                self.fail(process.communicate()[1].decode())
            if time.monotonic() >= deadline:
                self.fail('queued contender was not admitted')
            time.sleep(.01)

    def test_later_small_requests_cannot_starve_a_large_request(self):
        holder = self.holder({'cpu': 1})
        large, acquired = self.queued({'cpu': 2})
        # One permit is free; an unfair pool would admit every small contender.
        for _ in range(3):
            with self.assertRaises(TimeoutError):
                with self.pool.acquire({'cpu': 1}, .1):
                    self.fail('later request overtook the large queued request')
        holder.kill()
        holder.communicate(timeout=3)
        self.await_acquired(large, acquired)

    def test_crashed_queue_head_is_pruned_and_next_contender_proceeds(self):
        self.holder({'cpu': 1})
        first, first_acquired = self.queued({'cpu': 2})
        second, second_acquired = self.queued({'cpu': 1})
        first.kill()
        first.communicate(timeout=3)
        self.await_acquired(second, second_acquired)
        self.assertFalse(first_acquired.exists())

    def test_expired_queue_head_does_not_block_later_contenders(self):
        self.holder({'cpu': 1})
        first, first_acquired = self.queued({'cpu': 2}, timeout=.5)
        second, second_acquired = self.queued({'cpu': 1})
        _, error = first.communicate(timeout=3)
        self.assertNotEqual(first.returncode, 0)
        self.assertIn(b'TimeoutError', error)
        self.await_acquired(second, second_acquired)
        self.assertFalse(first_acquired.exists())

    def test_fifo_order_across_multiple_live_contenders(self):
        holder = self.holder({'cpu': 2})
        waiters = [self.queued({'cpu': 2}) for _ in range(3)]
        holder.kill()
        holder.communicate(timeout=3)
        for i, (process, acquired) in enumerate(waiters):
            self.await_acquired(process, acquired)
            self.assertTrue(all(not later.exists() for _, later in waiters[i + 1:]))
            process.kill()
            process.communicate(timeout=3)
        with self.pool.acquire({'cpu': 2}, 1):
            pass


class PressureTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name) / 'pool'

    def pool(self, sampler, **kwargs):
        pressure = pool_module.Pressure(sampler=sampler, **kwargs)
        return pool_module.ResourcePool(self.dir, {'cpu': 2}, pressure=pressure)

    @staticmethod
    def sample(disk_gib=50, memory_gib=8):
        return lambda path: {'free_disk_bytes': int(disk_gib * pool_module.GIB),
                             'available_memory_bytes': int(memory_gib * pool_module.GIB)}

    def test_low_disk_or_memory_refuses_at_the_deadline_with_the_reason(self):
        for kwargs, text in (({'disk_gib': 5}, 'free disk'), ({'memory_gib': .5}, 'available memory')):
            with self.subTest(kwargs=kwargs):
                start = time.monotonic()
                with self.assertRaises(pool_module.ResourcePressure) as caught:
                    with self.pool(self.sample(**kwargs)).acquire({'cpu': 1}, timeout=.6):
                        self.fail('admitted under pressure')
                self.assertIn(text, str(caught.exception))
                self.assertGreaterEqual(time.monotonic() - start, .5)

    def test_refusal_holds_no_permits(self):
        pool = self.pool(self.sample(disk_gib=1))
        with self.assertRaises(pool_module.ResourcePressure):
            with pool.acquire({'cpu': 2}, timeout=.3):
                pass
        healthy = pool_module.ResourcePool(self.dir, {'cpu': 2})
        with healthy.acquire({'cpu': 2}, timeout=1):
            pass

    def test_admission_proceeds_when_pressure_lifts_and_records_the_sample(self):
        readings = iter([self.sample(disk_gib=1)(None), self.sample(disk_gib=1)(None)])
        def flip(path):
            try:
                return next(readings)
            except StopIteration:
                return self.sample()(path)
        with self.pool(flip).acquire({'cpu': 1}, timeout=5) as lease:
            self.assertGreaterEqual(lease.description['wait_seconds'], .4)
            self.assertEqual(lease.description['pressure_sample']['free_disk_bytes'], 50 * pool_module.GIB)

    def test_unreadable_or_invalid_samples_refuse_instead_of_passing(self):
        def broken(path):
            raise OSError('no counters')
        for sampler in (broken, lambda path: {}, lambda path: {'free_disk_bytes': True, 'available_memory_bytes': 1}):
            with self.subTest(sampler=sampler), self.assertRaises(pool_module.ResourcePressure):
                with self.pool(sampler).acquire({'cpu': 1}, timeout=.3):
                    pass

    def test_zero_thresholds_disable_the_gate_and_negatives_are_refused(self):
        pressure = pool_module.Pressure(0, 0, sampler=lambda path: 1 / 0)
        self.assertEqual(pressure.check(), (True, {}, []))
        with self.assertRaises(ValueError):
            pool_module.Pressure(-1, 0)

    def test_pressure_is_off_by_default_for_library_callers(self):
        lib = pool_module.ResourcePool(self.dir, {'cpu': 1})
        with lib.acquire({'cpu': 1}, timeout=1) as lease:
            self.assertNotIn('pressure_sample', lease.description)

    def test_cli_refuses_under_impossible_thresholds_and_runs_when_disabled(self):
        base = [sys.executable, '-I', str(TOOL), '--pool', str(self.dir), '--wait', '1', '--cpu', '1']
        low = subprocess.run(base + ['--min-free-disk-gib', '100000000', '--', sys.executable, '-c', 'print(1)'],
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(low.returncode, 2)
        self.assertIn('free disk', low.stderr)
        off = subprocess.run(base + ['--min-free-disk-gib', '0', '--min-available-memory-gib', '0', '--',
                                     sys.executable, '-c', 'print(7)'], capture_output=True, text=True, timeout=30)
        self.assertEqual((off.returncode, off.stdout.strip()), (0, '7'))


class CommandSupervisionTest(unittest.TestCase):
    """Real console roots/children exercise the CLI execution boundary."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.ready = self.root / 'child.pid'
        self.output = io.BytesIO()

    def command(self):
        child = 'import os,sys,time\nfrom pathlib import Path\n'
        child += 'Path(sys.argv[1]).write_text(str(os.getpid()))\ntime.sleep(30)\n'
        root = ('import signal,subprocess,sys\n'
                'p=subprocess.Popen([sys.executable,"-I","-c",sys.argv[1],sys.argv[2]])\n'
                'signal.signal(signal.SIGTERM,lambda *_: None)\n'
                'p.wait()\n')
        return [sys.executable, '-I', '-c', root, child, str(self.ready)]

    def healthy(self):
        return {'free_disk_bytes': 50 * pool_module.GIB,
                'available_memory_bytes': 8 * pool_module.GIB}

    def assert_child_gone(self):
        pid = int(self.ready.read_text())
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                return
            time.sleep(.01)
        self.fail(f'owned child {pid} remains after cleanup')

    def test_pressure_and_lost_counters_stop_real_root_and_child(self):
        for field in ('free_disk_bytes', 'available_memory_bytes', 'unavailable'):
            with self.subTest(field=field):
                self.ready.unlink(missing_ok=True)
                def sampler(path):
                    sample = self.healthy()
                    if self.ready.exists():
                        if field == 'unavailable':
                            raise OSError('counter lost')
                        sample[field] = 0
                    return sample
                pool = pool_module.ResourcePool(self.root / 'pool', {'cpu': 1},
                                                pool_module.Pressure(sampler=sampler))
                with pool.acquire({'cpu': 1}) as lease:
                    with self.assertRaises(pool_module.ResourcePressure):
                        lease.run(self.command(), timeout=5, output=self.output)
                    self.assertEqual(lease.execution['stop_reason'], 'pressure')
                    self.assertTrue(lease.execution_pressure['stopped'])
                    self.assert_child_gone()
                with pool_module.ResourcePool(self.root / 'pool', {'cpu': 1}).acquire({'cpu': 1}, .2):
                    pass

    def test_deadline_stops_root_and_child(self):
        lease = pool_module.Lease({}, ())
        with self.assertRaises(subprocess.TimeoutExpired):
            lease.run(self.command(), timeout=.5, output=self.output)
        self.assertEqual(lease.execution['stop_reason'], 'timeout')
        self.assert_child_gone()

    def test_stubborn_root_is_killed_after_bounded_grace(self):
        lease = pool_module.Lease({}, ())
        code = 'import signal,time;signal.signal(signal.SIGTERM,signal.SIG_IGN);time.sleep(30)'
        start = time.monotonic()
        with self.assertRaises(subprocess.TimeoutExpired):
            lease.run([sys.executable, '-I', '-c', code], timeout=.5, output=self.output)
        self.assertTrue(lease.execution['kill_required'])
        self.assertEqual(lease.execution['returncode'], -signal.SIGKILL)
        self.assertLess(time.monotonic() - start, 4)

    def test_output_flood_is_refused_and_retained_output_is_capped(self):
        lease = pool_module.Lease({}, ())
        code = 'import os;\nwhile True: os.write(1,b"x"*65536)'
        with self.assertRaises(pool_module.OutputLimit):
            lease.run([sys.executable, '-I', '-c', code], timeout=5,
                      max_output_bytes=100000, output=self.output)
        self.assertEqual(lease.execution['stop_reason'], 'output_limit')
        self.assertEqual(len(self.output.getvalue()), 100000)
        self.assertEqual(lease.execution['retained_output_bytes'], 100000)

    def test_nonzero_result_and_both_streams_are_preserved(self):
        lease = pool_module.Lease({}, ())
        code = 'import os;os.write(1,b"out\\n");os.write(2,b"err\\n");raise SystemExit(7)'
        result = lease.run([sys.executable, '-I', '-c', code], output=self.output)
        self.assertEqual(result.returncode, 7)
        self.assertEqual(self.output.getvalue(), b'out\nerr\n')
        self.assertIsNone(lease.execution['stop_reason'])

    def test_invalid_bounds_never_launch(self):
        lease = pool_module.Lease({}, ())
        for kwargs in ({'timeout': 0}, {'timeout': float('inf')},
                       {'max_output_bytes': 0}, {'max_output_bytes': True}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                lease.run(['nonexistent-command'], output=self.output, **kwargs)

    def test_guardian_retains_root_pid_through_every_process_group_signal(self):
        """Actual child exit remains waitable at both TERM and KILL boundaries."""
        read_end, write_end = os.pipe()
        os.write(write_end, b'G')
        real_killpg = os.killpg
        checked = []
        def inspect_then_signal(pid, signum):
            try:
                info = os.waitid(os.P_PID, pid, os.WEXITED | os.WNOHANG | os.WNOWAIT)
            except ChildProcessError:
                self.fail('root was reaped before its final process-group signal')
            checked.append((pid, signum))
            return real_killpg(pid, signum)
        try:
            with tempfile.TemporaryFile() as config, tempfile.TemporaryFile() as status:
                config.write(json.dumps({'command': [sys.executable, '-I', '-c', 'raise SystemExit(0)'],
                                          'cwd': None, 'env': None, 'lease_descriptors': []}).encode())
                config.seek(0)
                # Guardian owns/consumes these descriptors, not the file objects.
                with mock.patch.object(pool_module.os, 'killpg', inspect_then_signal):
                    result = pool_module._console_guardian(os.dup(config.fileno()), read_end, os.dup(status.fileno()))
                read_end = -1
                status.seek(0)
                final = json.loads(status.read())
                self.assertEqual((result, final['returncode']), (0, 0))
                self.assertEqual([s for _, s in checked], [signal.SIGTERM, signal.SIGKILL])
                with self.assertRaises(ChildProcessError):
                    os.waitid(os.P_PID, final['root_pid'], os.WEXITED | os.WNOHANG | os.WNOWAIT)
        finally:
            os.close(write_end)
            if read_end >= 0:
                try: os.close(read_end)
                except OSError: pass

    @unittest.skipUnless(sys.platform == 'darwin', 'Darwin zombie-only EPERM contract')
    def test_zombie_permission_exception_never_hides_live_or_unreadable_group(self):
        process = subprocess.Popen([sys.executable, '-I', '-c', 'raise SystemExit(0)'],
                                   start_new_session=True)
        self.addCleanup(lambda: process.wait(timeout=3))
        deadline = time.monotonic() + 3
        while not pool_module._root_exited_unreaped(process):
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)
        cases = ([{'pid': process.pid, 'state': 'Z'}, {'pid': 123, 'state': 'S'}], [])
        for members in cases:
            with mock.patch.object(pool_module.os, 'killpg', side_effect=PermissionError('owned refusal')):
                with mock.patch.object(pool_module, '_group_snapshot', return_value=members):
                    with self.assertRaises(PermissionError):
                        pool_module._signal_owned_group(process, signal.SIGTERM, {})
        with mock.patch.object(pool_module.os, 'killpg', side_effect=PermissionError('owned refusal')):
            with mock.patch.object(pool_module, '_group_snapshot', side_effect=OSError('census lost')):
                with self.assertRaises(OSError):
                    pool_module._signal_owned_group(process, signal.SIGTERM, {})

    def test_cleanup_refusal_retains_structured_failure_despite_zero_root_exit(self):
        read_end, write_end = os.pipe()
        os.write(write_end, b'G')
        try:
            with tempfile.TemporaryFile() as config, tempfile.TemporaryFile() as status:
                config.write(json.dumps({'command': [sys.executable, '-I', '-c', 'raise SystemExit(0)'],
                                          'cwd': None, 'env': None, 'lease_descriptors': []}).encode())
                config.seek(0)
                with mock.patch.object(pool_module, '_signal_owned_group', side_effect=PermissionError('owned refusal')):
                    result = pool_module._console_guardian(os.dup(config.fileno()), read_end, os.dup(status.fileno()))
                read_end = -1
                status.seek(0)
                final = json.loads(status.read())
                self.assertEqual((result, final['returncode']), (2, 0))
                self.assertTrue(final['cleanup_incomplete'])
                self.assertIn('owned refusal', final['cleanup_error'])
        finally:
            os.close(write_end)
            if read_end >= 0:
                try: os.close(read_end)
                except OSError: pass

    def test_lost_root_wait_identity_refuses_before_any_group_signal(self):
        process = subprocess.Popen([sys.executable, '-I', '-c', 'raise SystemExit(0)'],
                                   start_new_session=True)
        process.wait(timeout=3)
        with mock.patch.object(pool_module.os, 'killpg') as send:
            with self.assertRaisesRegex(RuntimeError, 'no longer reserved'):
                pool_module._signal_owned_group(process, signal.SIGTERM, {})
            send.assert_not_called()

    def test_artifact_growth_stops_real_root_and_child_and_releases_budget(self):
        artifacts = (self.root / 'artifacts')
        artifacts.mkdir()
        budget = pool_module.ArtifactBudget([artifacts.resolve()], 1000)
        command = self.command()
        command[3] = command[3].replace('p=subprocess.Popen',
            'from pathlib import Path\nPath(sys.argv[3]).write_bytes(b"x"*2000)\np=subprocess.Popen')
        command.append(str(artifacts / 'capture.raw'))
        pool = pool_module.ResourcePool(self.root / 'pool', {'cpu': 1})
        with pool.acquire({'cpu': 1}) as lease:
            with self.assertRaisesRegex(pool_module.ArtifactLimit, 'artifact bytes exceeded'):
                lease.run(command, timeout=5, output=self.output, artifact_budget=budget)
            self.assertEqual(lease.execution['stop_reason'], 'artifact_limit')
            self.assertTrue(lease.execution_artifacts['stopped'])
            self.assert_child_gone()
        self.assertEqual((artifacts / 'capture.raw').stat().st_size, 2000)
        with pool.acquire({'cpu': 1}, .2):
            pass

    def test_fast_success_cannot_skip_final_artifact_check(self):
        artifacts = self.root / 'artifacts'
        artifacts.mkdir()
        budget = pool_module.ArtifactBudget([artifacts.resolve()], 1000)
        lease = pool_module.Lease({}, ())
        code = 'from pathlib import Path;import sys;Path(sys.argv[1]).write_bytes(b"x"*2000)'
        with self.assertRaisesRegex(pool_module.ArtifactLimit, 'artifact bytes exceeded'):
            lease.run([sys.executable, '-I', '-c', code, str(artifacts / 'fast.raw')],
                      output=self.output, artifact_budget=budget)
        self.assertEqual(lease.execution['returncode'], 0)
        self.assertEqual(lease.execution['stop_reason'], 'artifact_limit')

    def test_preexisting_artifact_overflow_never_launches_and_healthy_cli_runs(self):
        artifacts = self.root / 'artifacts'
        artifacts.mkdir()
        (artifacts / 'retained').write_bytes(b'x' * 2000)
        lease = pool_module.Lease({}, ())
        with self.assertRaises(pool_module.ArtifactLimit):
            lease.run(['nonexistent-command'], output=self.output,
                      artifact_budget=pool_module.ArtifactBudget([artifacts.resolve()], 1000))
        cli = [sys.executable, '-I', str(TOOL), '--pool', str(self.root / 'pool'), '--cpu', '1',
               '--min-free-disk-gib', '0', '--min-available-memory-gib', '0',
               '--artifact-dir', str(artifacts.resolve()), '--max-artifact-mib', '1', '--']
        success = subprocess.run(cli + [sys.executable, '-I', '-c', 'print(7)'], capture_output=True, timeout=5)
        self.assertEqual((success.returncode, success.stdout.strip()), (0, b'7'))
        failure = subprocess.run(cli + [sys.executable, '-I', '-c',
            'from pathlib import Path;import sys;Path(sys.argv[1]).write_bytes(b"x"*2097152)',
            str(artifacts / 'flood')], capture_output=True, timeout=5)
        self.assertEqual(failure.returncode, 2)
        self.assertIn(b'artifact bytes exceeded', failure.stderr)

    def test_command_launch_error_is_retained_and_releases_reservation(self):
        pool = pool_module.ResourcePool(self.root / 'launch-error-pool', {'cpu': 1})
        with pool.acquire({'cpu': 1}) as lease:
            with self.assertRaisesRegex(RuntimeError, 'FileNotFoundError'):
                lease.run([str(self.root / 'absent-executable')], output=self.output)
            self.assertIsNone(lease.execution['returncode'])
            self.assertIn('FileNotFoundError', lease.execution['error'])
        with pool.acquire({'cpu': 1}, .2):
            pass

    def test_cli_timeout_and_output_limit_are_failures(self):
        base = [sys.executable, '-I', str(TOOL), '--pool', str(self.root / 'pool'),
                '--cpu', '1', '--min-free-disk-gib', '0', '--min-available-memory-gib', '0']
        timed = subprocess.run(base + ['--timeout', '.2', '--', sys.executable, '-I', '-c',
                                      'import time;time.sleep(30)'], capture_output=True, timeout=5)
        self.assertEqual(timed.returncode, 2)
        self.assertIn(b'timed out', timed.stderr)
        flooded = subprocess.run(base + ['--max-output-mib', '1', '--', sys.executable, '-I', '-c',
                                         'import os;\nwhile True: os.write(2,b"x"*65536)'],
                                 capture_output=True, timeout=5)
        self.assertEqual(flooded.returncode, 2)
        self.assertEqual(len(flooded.stdout), 1024 * 1024)
        self.assertIn(b'output exceeded', flooded.stderr)

    def test_sigkill_during_guardian_launch_handshake_and_command_spawn(self):
        """Pause the real guardian at each boundary; kill its lifeline owner.

        The test's Popen interposer delays execution only, and then calls the
        original Popen. Production has no test switches or alternate cleanup.
        """
        wrapper = self.root / 'guardian.py'
        wrapper.write_text(
            'import importlib.util,os,sys,time\nfrom pathlib import Path\n'
            's=importlib.util.spec_from_file_location("pool",sys.argv[1])\n'
            'm=importlib.util.module_from_spec(s);s.loader.exec_module(m)\n'
            'mode,ready,release=sys.argv[2:5]\n'
            'original=m.subprocess.Popen\n'
            'def wait():\n'
            ' Path(ready).write_text(str(os.getpid()))\n'
            ' while not Path(release).exists(): time.sleep(.01)\n'
            'if mode=="before_go": wait()\n'
            'else:\n'
            ' def delayed(*args,**kwargs):\n'
            '  wait()\n'
            '  return original(*args,**kwargs)\n'
            ' m.subprocess.Popen=delayed\n'
            'raise SystemExit(m._console_guardian(*(int(x) for x in sys.argv[5:])))\n')
        launcher = (
            'import json,os,subprocess,sys,tempfile,time\n'
            'from pathlib import Path\n'
            'r,w=os.pipe()\n'
            'with tempfile.TemporaryFile() as cfg, tempfile.TemporaryFile() as status:\n'
            ' cfg.write(json.dumps({"command":[sys.executable,"-I","-c",'
            '"import os,sys,time;from pathlib import Path;Path(sys.argv[1]).write_text(str(os.getpid()));time.sleep(30)",sys.argv[6]],'
            '"cwd":None,"env":None,"lease_descriptors":[]}).encode());cfg.seek(0)\n'
            ' p=subprocess.Popen([sys.executable,"-I",sys.argv[1],sys.argv[2],*sys.argv[3:6],str(cfg.fileno()),str(r),str(status.fileno())],'
            'pass_fds=(r,cfg.fileno(),status.fileno()))\n'
            ' os.close(r)\n'
            ' if sys.argv[3]!="before_go": os.write(w,b"G")\n'
            ' time.sleep(30)\n')
        for mode in ('before_go', 'during_spawn'):
            with self.subTest(mode=mode):
                ready, release, child = [self.root / (mode + suffix)
                                          for suffix in ('.ready', '.release', '.child')]
                parent = subprocess.Popen([sys.executable, '-I', '-c', launcher,
                                           str(wrapper), str(TOOL), mode, str(ready), str(release), str(child)],
                                          stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                          stderr=subprocess.PIPE)
                self.addCleanup(lambda p=parent: p.poll() is None and p.kill())
                deadline = time.monotonic() + 3
                while not ready.exists():
                    if parent.poll() is not None:
                        self.fail(parent.communicate()[1].decode())
                    self.assertLess(time.monotonic(), deadline)
                    time.sleep(.01)
                guardian = int(ready.read_text())
                parent.kill()
                parent.wait(timeout=2)
                release.touch()
                # The guardian is now orphaned; on macOS launchd reaps it. Linux
                # containers may retain an inert zombie, so require no live code.
                deadline = time.monotonic() + 4
                while time.monotonic() < deadline:
                    status = subprocess.run(['ps', '-o', 'stat=', '-p', str(guardian)],
                                            capture_output=True, text=True).stdout.strip()
                    if not status or status.startswith('Z'):
                        break
                    time.sleep(.02)
                else:
                    os.kill(guardian, signal.SIGKILL)
                    self.fail('guardian did not finish after launcher SIGKILL')
                parent.communicate(timeout=2)
                if mode == 'before_go':
                    self.assertFalse(child.exists(), 'EOF before GO launched a command')
                elif child.exists():
                    with self.assertRaises(ProcessLookupError):
                        os.kill(int(child.read_text()), 0)


class ExecutionPressureTest(unittest.TestCase):
    """Use the real focus supervisor with an owned console child, never gameplay."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.ready = self.root / 'child.json'
        self.observer = pool_module.ROOT / 'build/parity_focus_observer'
        self.assertTrue(self.observer.is_file(), 'build parity_focus_observer before this suite')

    def command(self, sleep=30):
        code = ('import json,os,sys,time\nfrom pathlib import Path\n'
                'Path(sys.argv[1]).write_text(json.dumps({"pid":os.getpid(),"pgid":os.getpgrp()}))\n'
                f'time.sleep({sleep})\n')
        return [str(self.observer), str(self.root / 'focus.jsonl'), str(Path(sys.executable).resolve()),
                '10', '-I', '-c', code, str(self.ready)]

    def sample(self, path):
        return {'free_disk_bytes': 50 * pool_module.GIB, 'available_memory_bytes': 8 * pool_module.GIB}

    def assert_child_cleaned(self):
        child = json.loads(self.ready.read_text())
        self.assertEqual(child['pid'], child['pgid'])
        with self.assertRaises(ProcessLookupError):
            os.kill(child['pid'], 0)
        events = [json.loads(line) for line in (self.root / 'focus.jsonl').read_text().splitlines()]
        self.assertTrue(any(row.get('kind') == 'observer_finished' for row in events))

    def run_observed(self, lease, command, timeout):
        with (self.root / 'observer.stdout').open('wb') as stdout, (self.root / 'observer.stderr').open('wb') as stderr:
            return lease.run_observer(command, timeout=timeout, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr)

    def check_pressure_stop(self, field, reason):
        def sampler(path):
            sample = self.sample(path)
            if self.ready.exists():
                sample[field] = 0
            return sample
        pool = pool_module.ResourcePool(self.root / 'pool', {'cpu': 1},
                                        pool_module.Pressure(sampler=sampler))
        with pool.acquire({'cpu': 1}) as lease:
            with self.assertRaisesRegex(pool_module.ResourcePressure, reason):
                self.run_observed(lease, self.command(), 5)
            self.assertTrue(lease.execution_pressure['stopped'])
            self.assertGreaterEqual(lease.execution_pressure['samples'], 2)
            self.assertNotIn('observer_kill_required', lease.execution_pressure)
            self.assert_child_cleaned()
        healthy_pool = pool_module.ResourcePool(self.root / 'pool', {'cpu': 1})
        with healthy_pool.acquire({'cpu': 1}, .2):
            pass

    def test_disk_pressure_after_admission_stops_and_drains_the_real_observer_child(self):
        self.check_pressure_stop('free_disk_bytes', 'free disk')

    def test_memory_pressure_after_admission_stops_and_drains_the_real_observer_child(self):
        self.check_pressure_stop('available_memory_bytes', 'available memory')

    def test_lost_pressure_observation_during_execution_also_stops(self):
        def sampler(path):
            if self.ready.exists():
                raise OSError('counter lost during execution')
            return self.sample(path)
        lease = pool_module.Lease({}, (), pool_module.Pressure(sampler=sampler))
        with self.assertRaisesRegex(pool_module.ResourcePressure, 'counter lost during execution'):
            self.run_observed(lease, self.command(), 5)
        self.assertTrue(lease.execution_pressure['stopped'])
        self.assert_child_cleaned()

    def test_healthy_execution_retains_the_successful_verdict_and_sample(self):
        lease = pool_module.Lease({}, (), pool_module.Pressure(sampler=self.sample))
        result = self.run_observed(lease, self.command(sleep=.05), 5)
        self.assertEqual(result.returncode, 0)
        self.assertFalse(lease.execution_pressure['stopped'])
        self.assertGreaterEqual(lease.execution_pressure['samples'], 1)
        self.assert_child_cleaned()

    def test_outer_deadline_gives_the_observer_time_to_drain_its_child(self):
        lease = pool_module.Lease({}, (), pool_module.Pressure(sampler=self.sample))
        with self.assertRaises(subprocess.TimeoutExpired):
            self.run_observed(lease, self.command(), .8)
        self.assertNotIn('observer_kill_required', lease.execution_pressure)
        self.assert_child_cleaned()

    def test_artifact_overflow_stops_and_drains_the_real_observer_child(self):
        lease = pool_module.Lease({}, ())
        budget = pool_module.ArtifactBudget([self.root.resolve()], 1)
        with (self.root / 'observer.stdout').open('wb') as stdout, (self.root / 'observer.stderr').open('wb') as stderr:
            with self.assertRaises(pool_module.ArtifactLimit):
                lease.run_observer(self.command(), timeout=5, artifact_budget=budget,
                                   stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr)
        self.assertTrue(lease.execution_artifacts['stopped'])
        self.assert_child_cleaned()


class ArtifactBudgetTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()

    def test_sparse_hidden_and_nested_files_count_logical_bytes(self):
        (self.root / '.hidden').write_bytes(b'x' * 7)
        (self.root / 'nested').mkdir()
        with (self.root / 'nested/sparse').open('wb') as stream:
            stream.truncate(1000000)
        sample = pool_module.ArtifactBudget([self.root], 1000007).check()
        self.assertEqual(sample['logical_bytes'], 1000007)
        self.assertEqual(sample['entries'], 3)
        self.assertTrue(sample['complete'])
        with self.assertRaises(pool_module.ArtifactLimit):
            pool_module.ArtifactBudget([self.root], 1000006).check()

    def test_replaced_missing_root_and_symlinks_refuse_instead_of_empty(self):
        child = self.root / 'capture'
        child.mkdir()
        budget = pool_module.ArtifactBudget([child], 100)
        child.rename(self.root / 'retained')
        with self.assertRaises(pool_module.ArtifactLimit):
            budget.check()
        child.mkdir()
        with self.assertRaisesRegex(pool_module.ArtifactLimit, 'identity changed'):
            budget.check()
        (child / 'external').symlink_to(self.root / 'retained')
        with self.assertRaisesRegex(pool_module.ArtifactLimit, 'symlink'):
            pool_module.ArtifactBudget([child], 100).check()

    def test_scan_and_entry_bounds_refuse_incomplete_observations(self):
        for name in ('a', 'b'):
            (self.root / name).touch()
        for kwargs in ({'max_entries': 1}, {'scan_seconds': 1e-12}):
            budget = pool_module.ArtifactBudget([self.root], 100, **kwargs)
            with self.assertRaises(pool_module.ArtifactLimit):
                budget.check()
            self.assertFalse(budget.last_sample['complete'])

    def test_invalid_bounds_overlapping_and_reference_roots_refused(self):
        for value in (0, -1, True):
            with self.assertRaises(ValueError):
                pool_module.ArtifactBudget([self.root], value)
        for roots in ([], [self.root, self.root], [self.root, self.root.parent],
                      [pool_module.ROOT / 'ref']):
            with self.assertRaises(ValueError):
                pool_module.ArtifactBudget(roots, 100)

    def test_expired_final_empty_directory_observation_cannot_be_complete(self):
        budget = pool_module.ArtifactBudget([self.root], 100, scan_seconds=1)
        calls = 0
        def clock():
            nonlocal calls
            calls += 1
            return 0 if calls == 1 else 2
        with mock.patch.object(pool_module.time, 'monotonic', clock):
            with self.assertRaisesRegex(pool_module.ArtifactLimit, 'time bound exceeded'):
                budget.check()
        self.assertFalse(budget.last_sample['complete'])

    def test_count_links_charges_metadata_without_traversing_external_targets(self):
        artifacts = self.root / 'artifacts'
        artifacts.mkdir()
        outside = self.root / 'private-authority'
        outside.write_bytes(b'unchanged outside authority')
        (artifacts / 'z:').symlink_to(outside)
        (artifacts / 'missing').symlink_to(self.root / 'absent')
        before = outside.read_bytes()
        expected = sum(p.lstat().st_size for p in artifacts.iterdir())
        sample = pool_module.ArtifactBudget([artifacts], expected, link_policy='count').check()
        self.assertEqual(sample['logical_bytes'], expected)
        self.assertEqual(sample['symlink_bytes'], expected)
        self.assertEqual(sample['symlinks'], 2)
        self.assertEqual(outside.read_bytes(), before)
        with self.assertRaises(pool_module.ArtifactLimit):
            pool_module.ArtifactBudget([artifacts], expected - 1, link_policy='count').check()
        with self.assertRaises(ValueError):
            pool_module.ArtifactBudget([artifacts], 100, link_policy='follow')

    def test_unix_socket_default_refuses_and_explicit_count_never_connects(self):
        endpoint = self.root / 'peer.sock'
        with socket.socket(socket.AF_UNIX) as server:
            server.bind(str(endpoint))
            server.listen(1)
            server.settimeout(.02)
            with self.assertRaises(pool_module.ArtifactLimit):
                pool_module.ArtifactBudget([self.root], 1000).check()
            sample = pool_module.ArtifactBudget([self.root], 1000, socket_policy='count').check()
            self.assertEqual(sample['unix_sockets'], 1)
            self.assertEqual(sample['unix_socket_bytes'], endpoint.lstat().st_size)
            self.assertEqual(sample['logical_bytes'], endpoint.lstat().st_size)
            self.assertTrue(sample['complete'])
            with self.assertRaises(TimeoutError):
                server.accept()

    def test_socket_count_preserves_file_byte_entry_and_other_special_refusals(self):
        with socket.socket(socket.AF_UNIX) as server:
            server.bind(str(self.root / 'peer.sock'))
            payload = self.root / 'payload'
            payload.write_bytes(b'xx')
            with self.assertRaises(pool_module.ArtifactLimit):
                pool_module.ArtifactBudget([self.root], 1, socket_policy='count').check()
            with self.assertRaises(pool_module.ArtifactLimit):
                pool_module.ArtifactBudget([self.root], 1000, max_entries=1, socket_policy='count').check()
            payload.unlink()
            os.mkfifo(self.root / 'fifo')
            with self.assertRaises(pool_module.ArtifactLimit):
                pool_module.ArtifactBudget([self.root], 1000, socket_policy='count').check()
            with self.assertRaises(ValueError):
                pool_module.ArtifactBudget([self.root], 1000, socket_policy='connect')


class StockIntentAdmissionTest(unittest.TestCase):
    """Stock creation may outlive every process holding its permits."""
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.capacity = {'cpu': 2, 'stock': 1}

    def specification(self):
        return {'container_name': 'slrr-parity-'+'a'*32, 'image_id': 'sha256:'+'b'*64,
                'configuration_sha256': 'c'*64}

    def test_idle_migration_preserves_capacity_mutex_and_fences_cached_legacy_client(self):
        directory = self.root / 'legacy'
        cached = pool_module.ResourcePool(directory, self.capacity)
        with cached.acquire({'cpu': 1}, .2):pass
        gate = (directory / 'admission.lock').stat()
        old = (directory / 'cpu-0.lock').stat()
        modern = pool_module.ResourcePool.migrate_stock_intents(directory, self.capacity)
        self.assertEqual(modern.capacity, self.capacity)
        self.assertEqual((directory / 'admission.lock').stat().st_ino, gate.st_ino)
        self.assertEqual((directory / 'legacy-v1/cpu-0.lock').stat().st_ino, old.st_ino)
        with self.assertRaises(ValueError):pool_module.ResourcePool(directory, self.capacity)
        # The already constructed client never rereads capacity.json at _try.
        with self.assertRaises(OSError):
            with cached.acquire({'cpu': 1}, .2):self.fail('cached legacy client bypassed new protocol')
        with modern.acquire({'cpu': 2, 'stock': 1}, .2):pass

    def test_healthy_stock_intent_keeps_other_capacity_usable(self):
        pool = pool_module.ResourcePool(self.root / 'pool', self.capacity, protocol=2)
        with pool.acquire({'cpu': 1, 'stock': 1}, .2) as lease:
            lease.begin_stock_creation(self.specification())
            with pool.acquire({'cpu': 1}, .2):pass
            with self.assertRaises(TimeoutError):
                with pool.acquire({'cpu': 2}, .05):pass
        with self.assertRaises(pool_module.StockOrphanPending):
            with pool.acquire({'cpu': 1}, .2):pass

    def test_registration_requires_active_protocol_and_valid_spec_before_writes(self):
        legacy = pool_module.ResourcePool(self.root / 'legacy', self.capacity)
        with legacy.acquire({'stock': 1}, .2) as lease:
            with self.assertRaises(ValueError):lease.begin_stock_creation(self.specification())
        pool = pool_module.ResourcePool(self.root / 'pool', self.capacity, protocol=2)
        with pool.acquire({'stock': 1}, .2) as lease:
            with self.assertRaises(ValueError):lease.begin_stock_creation({'container_name': 'unknown'})
        with pool.acquire({'cpu': 1}, .2) as lease:
            with self.assertRaises(ValueError):lease.begin_stock_creation(self.specification())
        with self.assertRaises(ValueError):lease.begin_stock_creation(self.specification())
        with pool.acquire({'stock': 1}, .2):pass

    def test_one_permit_set_cannot_authorize_two_unretired_creations(self):
        pool=pool_module.ResourcePool(self.root/'pool',self.capacity,protocol=2)
        with pool.acquire({'stock':1},.2) as lease:
            lease.begin_stock_creation(self.specification())
            with self.assertRaises(ValueError):lease.begin_stock_creation(self.specification())

    def test_cached_v2_client_rereads_intents_after_holder_exits(self):
        pool=pool_module.ResourcePool(self.root/'pool',self.capacity,protocol=2)
        cached=pool_module.ResourcePool(pool.directory,self.capacity,protocol=2)
        with pool.acquire({'stock':1},.2) as lease:lease.begin_stock_creation(self.specification())
        with self.assertRaises(pool_module.StockOrphanPending):
            with cached.acquire({'cpu':1},.2):self.fail('cached v2 client ignored committed orphan')

    def test_live_intent_owner_without_its_permit_is_not_healthy(self):
        pool=pool_module.ResourcePool(self.root/'pool',self.capacity,protocol=2)
        with pool.acquire({'cpu':1,'stock':1},.2) as lease:
            lease.begin_stock_creation(self.specification())
            # Keep the owner alive while independently releasing a permit.
            # Replace that FD with /dev/null so the Lease can still close it.
            with open(os.devnull,'rb') as dummy:os.dup2(dummy.fileno(),lease.descriptors[0])
            with self.assertRaises(pool_module.StockOrphanPending):
                with pool.acquire({'cpu':1},.2):self.fail('owner lock hid a dropped resource permit')

    def test_malformed_or_replaced_record_and_lock_refuse_even_with_live_owner(self):
        for mutation in ('json', 'duplicate_key', 'nonfinite', 'extra_key', 'replace_record', 'replace_owner', 'replace_permit', 'extra_file'):
            with self.subTest(mutation=mutation):
                pool = pool_module.ResourcePool(self.root / mutation, self.capacity, protocol=2)
                with pool.acquire({'cpu': 1, 'stock': 1}, .2) as lease:
                    intent = lease.begin_stock_creation(self.specification())
                    root = pool.directory / 'stock-intents-v2'
                    record = root / (intent.identifier+'.json')
                    owner = root / (intent.identifier+'.owner')
                    if mutation == 'json':record.write_text('{')
                    elif mutation == 'duplicate_key':record.write_text(record.read_text().replace('"schema": 1','"schema": 1, "schema": 1'))
                    elif mutation == 'nonfinite':record.write_text(record.read_text().replace('"schema": 1','"schema": NaN'))
                    elif mutation == 'extra_key':
                        value=json.loads(record.read_text());value['cleared']=True;record.write_text(json.dumps(value))
                    elif mutation == 'replace_record':
                        raw=record.read_bytes();record.rename(root/'saved-record');record.write_bytes(raw);record.chmod(0o600)
                        (root/'saved-record').unlink()
                    elif mutation == 'replace_owner':
                        owner.rename(root/'saved-owner');owner.touch(mode=0o600);(root/'saved-owner').unlink()
                    elif mutation == 'replace_permit':
                        path=pool.directory/'permits-v2/cpu-0.lock';path.rename(pool.directory/'saved-permit')
                        path.touch(mode=0o600);(pool.directory/'saved-permit').unlink()
                    else:(root/'unknown').touch()
                    with self.assertRaises(pool_module.StockOrphanPending):
                        with pool.acquire({'cpu': 1}, .2):self.fail('corrupt Stock state admitted work')

    def test_protocol_two_wire_integer_fields_reject_bool_and_float_equivalence(self):
        for mutation in ('capacity_bool','capacity_float','pool_identity','admission_identity',
                         'layout_identity','record_identity','owner_identity','permit_identity'):
            with self.subTest(mutation=mutation):
                pool=pool_module.ResourcePool(self.root/mutation,self.capacity,protocol=2)
                with pool.acquire({'cpu':1,'stock':1},.2) as lease:
                    intent=lease.begin_stock_creation(self.specification())
                    config_path=pool.directory/'capacity.json'
                    config=json.loads(config_path.read_text())
                    if mutation.startswith('capacity_'):
                        config['capacity']['stock']=True if mutation=='capacity_bool' else 1.0
                    elif mutation in ('pool_identity','admission_identity'):
                        config[mutation]=[float(n) for n in config[mutation]]
                    elif mutation=='layout_identity':
                        config['layout']['permits-v2']=[float(n) for n in config['layout']['permits-v2']]
                    else:
                        path=pool.directory/'stock-intents-v2'/(intent.identifier+'.json')
                        record=json.loads(path.read_text())
                        if mutation=='permit_identity':record['permits'][0]['identity']=[float(n) for n in record['permits'][0]['identity']]
                        else:record[mutation]=[float(n) for n in record[mutation]]
                        path.write_text(json.dumps(record)+'\n')
                        # Keep the byte binding valid to isolate typed schema
                        # refusal from the independent corruption/hash guard.
                        config['intents'][intent.identifier]['sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
                    config_path.write_text(json.dumps(config)+'\n')
                    with self.assertRaises(pool_module.StockOrphanPending):
                        with pool.acquire({'cpu':1},.2):self.fail('untyped wire integer admitted work')

    def test_registration_durability_failure_leaves_unresolved_barrier(self):
        pool = pool_module.ResourcePool(self.root / 'pool', self.capacity, protocol=2)
        with pool.acquire({'stock': 1}, .2) as lease:
            with mock.patch.object(pool_module, '_stock_sync_directory', side_effect=OSError('fsync refused')):
                with self.assertRaises(OSError):lease.begin_stock_creation(self.specification())
            with self.assertRaises(pool_module.StockOrphanPending):
                with pool.acquire({'cpu': 1}, .2):pass

    def test_uncommitted_intent_never_returns_a_creation_handle(self):
        pool=pool_module.ResourcePool(self.root/'pool',self.capacity,protocol=2)
        with pool.acquire({'stock':1},.2) as lease:
            with mock.patch.object(pool_module.os,'replace',side_effect=OSError('registry commit refused')):
                with self.assertRaises(OSError):lease.begin_stock_creation(self.specification())
        with self.assertRaises(pool_module.StockOrphanPending):
            pool_module.ResourcePool(pool.directory,self.capacity,protocol=2)

    def test_removing_entire_intent_pair_does_not_erase_committed_orphan(self):
        pool=pool_module.ResourcePool(self.root/'pool',self.capacity,protocol=2)
        with pool.acquire({'stock':1},.2) as lease:
            intent=lease.begin_stock_creation(self.specification())
        for suffix in ('.json','.owner'):
            (pool.directory/'stock-intents-v2'/(intent.identifier+suffix)).unlink()
        reopened=pool_module.ResourcePool(pool.directory,self.capacity,protocol=2)
        with self.assertRaises(pool_module.StockOrphanPending):
            with reopened.acquire({'cpu':1},.2):self.fail('deleted pair erased durable orphan')

    def test_migration_refuses_live_locks_and_unknown_or_partial_state(self):
        directory = self.root / 'legacy'
        legacy = pool_module.ResourcePool(directory, self.capacity)
        with legacy.acquire({'cpu': 1}, .2):
            with self.assertRaises(BlockingIOError):pool_module.ResourcePool.migrate_stock_intents(directory, self.capacity)
        waiter=directory/('wait-'+'0'*20+'-'+'a'*32+'.lock')
        waiter.touch(mode=0o600)
        with waiter.open('r+b') as stream:
            fcntl.flock(stream.fileno(),fcntl.LOCK_EX)
            with self.assertRaises(BlockingIOError):pool_module.ResourcePool.migrate_stock_intents(directory,self.capacity)
        waiter.unlink()
        unknown=directory/'unknown';unknown.touch()
        with self.assertRaises(ValueError):pool_module.ResourcePool.migrate_stock_intents(directory,self.capacity)
        unknown.unlink()
        (directory/'protocol-v2.pending').touch()
        with self.assertRaises(ValueError):pool_module.ResourcePool.migrate_stock_intents(directory,self.capacity)
        with self.assertRaises(pool_module.StockOrphanPending):pool_module.ResourcePool(directory,self.capacity,protocol=2)

    def test_partial_migration_never_activates_v2_and_retains_failure(self):
        directory = self.root/'legacy'
        legacy=pool_module.ResourcePool(directory,self.capacity)
        with legacy.acquire({'cpu':1},.2):pass
        original=pool_module.os.replace
        def refuse_commit(source,destination):
            if Path(destination).name=='capacity.json':raise OSError('commit refused')
            return original(source,destination)
        with mock.patch.object(pool_module.os,'replace',side_effect=refuse_commit):
            with self.assertRaises(OSError):pool_module.ResourcePool.migrate_stock_intents(directory,self.capacity)
        self.assertEqual(json.loads((directory/'capacity.json').read_text()),self.capacity)
        with self.assertRaises(pool_module.StockOrphanPending):pool_module.ResourcePool(directory,self.capacity,protocol=2)
        with self.assertRaises(ValueError):pool_module.ResourcePool(directory,self.capacity)

    def test_v2_waiter_and_mutex_deadlines_remain_bounded(self):
        pool=pool_module.ResourcePool(self.root/'pool',self.capacity,protocol=2)
        with (pool.directory/'admission.lock').open('r+b') as stream:
            fcntl.flock(stream.fileno(),fcntl.LOCK_EX)
            started=time.monotonic()
            with self.assertRaises(TimeoutError):
                with pool.acquire({'cpu':1},.05):pass
            self.assertLess(time.monotonic()-started,.5)
        with pool.acquire({'cpu':2},.2):
            with self.assertRaises(TimeoutError):
                with pool.acquire({'cpu':1},.05):pass
            with pool.acquire({'stock':1},.2):pass

    def test_v2_fifo_waiter_cannot_be_overtaken_by_unrelated_resource(self):
        pool=pool_module.ResourcePool(self.root/'pool',self.capacity,protocol=2)
        sentinel=self.root/'head-admitted'
        script='''import importlib.util,json,sys
from pathlib import Path
s=importlib.util.spec_from_file_location('pool',sys.argv[1]);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
with m.ResourcePool(sys.argv[2],json.loads(sys.argv[3]),protocol=2).acquire({'cpu':2,'stock':1},2):Path(sys.argv[4]).touch()
'''
        process=None
        with pool.acquire({'cpu':2},.2):
            process=subprocess.Popen([sys.executable,'-I','-c',script,str(TOOL),str(pool.directory),
                                      json.dumps(self.capacity),str(sentinel)],stderr=subprocess.PIPE)
            def cleanup():
                if process.poll() is None:process.kill()
                process.communicate(timeout=3)
            self.addCleanup(cleanup)
            deadline=time.monotonic()+2
            while not list(pool.directory.glob('wait-*.lock')):
                if process.poll() is not None:self.fail(process.communicate()[1].decode())
                self.assertLess(time.monotonic(),deadline);time.sleep(.01)
            with self.assertRaises(TimeoutError):
                with pool.acquire({'stock':1},.05):self.fail('later v2 waiter overtook blocked head')
        _,error=process.communicate(timeout=3)
        self.assertEqual(process.returncode,0,error.decode());self.assertTrue(sentinel.exists())

    def stock_intent_holder(self, directory):
        ready = self.root / 'holders.json'
        script = '''import importlib.util,json,os,sys,time
from pathlib import Path
s=importlib.util.spec_from_file_location('pool',sys.argv[1]);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
p=m.ResourcePool(sys.argv[2],json.loads(sys.argv[3]),protocol=2)
with p.acquire({'cpu':1,'stock':1},2) as lease:
 intent=lease.begin_stock_creation({'container_name':'slrr-parity-'+'a'*32,'image_id':'sha256:'+'b'*64,'configuration_sha256':'c'*64})
 child=os.fork()
 if child==0:
  time.sleep(30);os._exit(0)
 Path(sys.argv[4]).write_text(json.dumps({'owner':os.getpid(),'backup':child,'intent':intent.identifier}))
 time.sleep(30)
'''
        holder = subprocess.Popen([sys.executable, '-I', '-c', script, str(TOOL),
                                   str(directory), json.dumps(self.capacity), str(ready)],
                                  stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                  start_new_session=True)
        def cleanup():
            if holder.returncode is None:
                pool_module._signal_owned_group(holder, signal.SIGKILL, {})
            return holder.communicate(timeout=3)
        self.addCleanup(cleanup)
        deadline = time.monotonic()+3
        while not ready.exists():
            if pool_module._root_exited_unreaped(holder):self.fail(cleanup()[1].decode())
            self.assertLess(time.monotonic(), deadline)
            time.sleep(.01)
        return holder

    def test_creation_intent_survives_all_holders_and_blocks_new_command(self):
        directory = self.root / 'pool'
        pool_module.ResourcePool(directory, self.capacity, protocol=2)
        holder=self.stock_intent_holder(directory)
        pool_module._signal_owned_group(holder, signal.SIGKILL, {})
        holder.communicate(timeout=3)
        # This is an independent kernel-lock observation, not a pool verdict.
        deadline = time.monotonic()+2
        permits=sorted((directory / 'permits-v2').glob('*.lock'))
        self.assertEqual(len(permits),2)
        for path in permits:
            with path.open('r+b') as stream:
                while True:
                    try:fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB);break
                    except BlockingIOError:
                        self.assertLess(time.monotonic(), deadline);time.sleep(.01)
        sentinel = self.root / 'incorrectly-admitted'
        command = '''import importlib.util,json,sys
from pathlib import Path
s=importlib.util.spec_from_file_location('pool',sys.argv[1]);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
try:
 with m.ResourcePool(sys.argv[2],json.loads(sys.argv[3]),protocol=2).acquire({'cpu':1},.2):Path(sys.argv[4]).touch()
except m.StockOrphanPending:sys.exit(7)
'''
        result = subprocess.run([sys.executable, '-I', '-c', command, str(TOOL), str(directory),
                                 json.dumps(self.capacity), str(sentinel)], capture_output=True, timeout=3)
        self.assertEqual(result.returncode, 7, result.stderr.decode()+f' sentinel_exists={sentinel.exists()}')
        self.assertFalse(sentinel.exists())

    def test_holder_death_after_healthy_validation_cannot_reuse_committed_stock_slot(self):
        directory=self.root/'pool'
        pool_module.ResourcePool(directory,self.capacity,protocol=2)
        holder=self.stock_intent_holder(directory)
        validated, release, sentinel, slot_acquired=(self.root/name for name in
                                                   ('validated','continue','incorrectly-admitted','reserved-slot-acquired'))
        # The request runs in another real process. Its test-only hook pauses
        # immediately after successful validation, under the admission mutex.
        script='''import importlib.util,json,os,sys,time
from pathlib import Path
s=importlib.util.spec_from_file_location('pool',sys.argv[1]);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
p=m.ResourcePool(sys.argv[2],json.loads(sys.argv[3]),protocol=2)
stock=os.stat(Path(sys.argv[2])/'permits-v2/stock-0.lock')
stock_identity=(stock.st_dev,stock.st_ino)
real_flock=m.fcntl.flock
def observed_flock(fd,mode):
 result=real_flock(fd,mode)
 info=os.fstat(fd)
 if Path(sys.argv[5]).exists() and (info.st_dev,info.st_ino)==stock_identity:Path(sys.argv[7]).touch()
 return result
m.fcntl.flock=observed_flock
original=p._check_stock_intents
once=[]
def after_validation(deadline):
 result=original(deadline)
 if not once:
  once.append(True);Path(sys.argv[4]).touch()
  while not Path(sys.argv[5]).exists():
   if time.monotonic()>=deadline:raise TimeoutError('test validation handshake deadline')
   time.sleep(.005)
 return result
p._check_stock_intents=after_validation
try:
 with p.acquire({'stock':1},2):Path(sys.argv[6]).touch()
except m.StockOrphanPending:sys.exit(7)
'''
        request=subprocess.Popen([sys.executable,'-I','-c',script,str(TOOL),str(directory),
                                  json.dumps(self.capacity),str(validated),str(release),str(sentinel),str(slot_acquired)],
                                 stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        def cleanup():
            if request.poll() is None:request.kill()
            request.communicate(timeout=3)
        self.addCleanup(cleanup)
        deadline=time.monotonic()+2
        while not validated.exists():
            if request.poll() is not None:self.fail(request.communicate()[1].decode())
            self.assertLess(time.monotonic(),deadline);time.sleep(.005)
        pool_module._signal_owned_group(holder,signal.SIGKILL,{})
        holder.communicate(timeout=3)
        with (directory/'permits-v2/stock-0.lock').open('r+b') as stream:
            fcntl.flock(stream.fileno(),fcntl.LOCK_EX | fcntl.LOCK_NB)
        release.touch()
        _,error=request.communicate(timeout=3)
        self.assertEqual(request.returncode,7,error.decode()+f' sentinel_exists={sentinel.exists()}')
        self.assertFalse(sentinel.exists())
        self.assertFalse(slot_acquired.exists(),'committed Stock slot was temporarily reacquired before final recheck')


if __name__ == '__main__':
    unittest.main()
