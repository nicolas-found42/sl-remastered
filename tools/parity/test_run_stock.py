"""Counterfactual evidence checks, not fabricated Stock execution evidence."""
import copy
import contextlib
import json
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('run_stock', Path(__file__).with_name('run_stock.py'))
stock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stock)


class StockEvidenceTest(unittest.TestCase):
    @unittest.skipUnless(sys.platform == 'darwin', 'APFS clone adapter requires Darwin cp -c')
    def test_bounded_clone_keeps_bytes_and_private_inodes(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve()
            source = root/'source'
            source.mkdir()
            (source/'nested').mkdir()
            for i in range(35):
                (source/'nested'/str(i)).write_bytes(str(i).encode())
            before = stock.manifest(source)
            with (root/'log').open('wb') as log:
                stock.clone_preparation(source, root/'copy', before, None, log)
            self.assertEqual(stock.manifest(root/'copy'), before)
            self.assertEqual(stock.manifest(source), before)
            for path in (source/'nested').iterdir():
                self.assertNotEqual(path.stat().st_ino, (root/'copy/nested'/path.name).stat().st_ino)

    def test_clone_does_not_mutate_after_wait_consumes_copy_deadline(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve()
            source = root/'source'
            source.mkdir()
            (source/'file').write_bytes(b'fixture')
            before = stock.manifest(source)
            clock, limits = [0.0], []
            class DelayedWriter:
                def __init__(self, binding):
                    pass
                def __enter__(self):
                    return self
                def __exit__(self, *args):
                    pass
                @contextlib.contextmanager
                def batch(self, *, deadline):
                    limits.append(deadline)
                    clock[0] = 30.0
                    yield
            with mock.patch.object(stock.prepaid, 'PreparationWriter', DelayedWriter), \
                    mock.patch.object(stock.time, 'monotonic', lambda: clock[0]), \
                    (root/'log').open('wb') as log:
                with self.assertRaisesRegex(TimeoutError, 'clone preparation deadline'):
                    stock.clone_preparation(source, root/'copy', before, {'child': str(root)}, log)
            self.assertEqual(limits, [30.0])
            self.assertFalse((root/'copy').exists())
            self.assertEqual(stock.manifest(source), before)

    def test_clone_refuses_aliases_and_failure_without_fallback(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name).resolve()
            source = root/'source'
            source.mkdir()
            (source/'file').write_bytes(b'fixture')
            before = stock.manifest(source)
            with (root/'log').open('wb') as log:
                with mock.patch.object(stock.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'cp')) as run:
                    with self.assertRaises(subprocess.CalledProcessError):
                        stock.clone_preparation(source, root/'failed', before, None, log)
                    self.assertEqual(run.call_count, 1)
                    self.assertEqual(run.call_args.args[0][:3], ['cp', '-c', '-p'])
                (source/'alias').symlink_to(source/'file')
                with self.assertRaisesRegex(ValueError, 'alias or special'):
                    stock.clone_preparation(source, root/'alias-copy', before, None, log)
                self.assertFalse((root/'alias-copy').exists())

    def test_native_return_witness_refuses_loss_reordering_injection_and_wrong_key(self):
        rows = [{'sequence': n, 'source': 'native', 'device': 1, 'offset': 28,
                 'data': value, 'state_call': n * 10, 'tick': n * 100}
                for n, value in enumerate((0, 128, 0), 1)]
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / 'return.jsonl'
            def write(values):
                path.write_text(''.join(json.dumps(row) + '\n' for row in values))
            write(rows)
            self.assertTrue(stock.native_return_evidence(path, [0, 128, 0])['passed'])
            for values in (rows[:2], rows + [rows[-1]], rows[::-1],
                           [dict(row, source='injected') for row in rows],
                           [dict(row, offset=1) for row in rows],
                           [dict(row, sequence=1) for row in rows],
                           [dict(row, state_call=True) for row in rows],
                           [dict(row, device=n) for n, row in enumerate(rows)]):
                write(values)
                with self.assertRaises(ValueError):
                    stock.native_return_evidence(path, [0, 128, 0])
            write(rows)
            with self.assertRaises(ValueError):
                stock.native_return_evidence(path, [0])
            path.unlink()
            target = Path(name) / 'actual'
            target.write_text('fixture')
            path.symlink_to(target)
            with self.assertRaises(ValueError):
                stock.native_return_evidence(path, [0, 128, 0])

    def test_stop_requires_unique_success_not_merely_a_matching_marker(self):
        good='[ 123 ms] control_tape_stop result=12\n'
        self.assertTrue(stock.control_stop_evidence(good)['passed'])
        self.assertTrue(stock.control_stop_evidence(good.replace('=12','=0'))['passed'])
        for text in ('',good.replace('=12','=-1'),good.replace('=12','=-0'),
                     good.replace('=12','=65537'),good+good,good+'control_tape_stop result=12\n',
                     good.replace('=12','=True'),good.rstrip()+' partial\n'):
            with self.subTest(text=text):
                self.assertFalse(stock.control_stop_evidence(text)['passed'])

    def test_career_frontiers_do_not_pass_from_outcome_booleans_or_missing_input(self):
        menu = dict.fromkeys(('MAIN MENU', 'FREE RIDE', 'OPTIONS'), True)
        flow = {'passed': True, 'before': {'menu': menu},
                'after': {'menu': menu, 'dialog': {'name_title': False, 'cancel_button': False}},
                'actions': [{'label': name} for name in
                            ('New Career', 'Type Career name', 'Accept name', 'Next portrait', 'Accept character', 'Garage Main Menu')],
                'career': {'mode': 'create', 'name': 'SaveProof', 'passed': True, 'welcome_dismissed': False,
                           'name_dialog': {'dialog': {'name_title': True, 'cancel_button': True}},
                           'character_before': {'career': {'character': True}},
                           'character_after': {'career': {'character': True}},
                           'garage': {'career': {'identity': True, 'money': True, 'welcome': False}}}}
        flow['actions'][1]['text'] = 'SaveProof'
        text = ''.join(f'input_poll: kind=1 offset=12 data={n} source=native\n'
                       for n in [0] + [value for _ in range(5) for value in (128, 0)])
        self.assertTrue(stock.career_scenario_evidence(flow, text, 'create')['complete'])
        for mutation in ('initial-up', 'portrait-frontier', 'name-dialog', 'garage-money', 'typed-name', 'welcome-branch'):
            bad, log = copy.deepcopy(flow), text
            if mutation == 'initial-up':
                log = log.split('\n', 1)[1]
            elif mutation == 'portrait-frontier':
                del bad['career']['character_after']
            elif mutation == 'name-dialog':
                del bad['career']['name_dialog']
            elif mutation == 'garage-money':
                bad['career']['garage']['career']['money'] = False
            elif mutation == 'typed-name':
                bad['actions'][1]['text'] = 'WrongName'
            else:
                bad['career']['welcome_dismissed'] = True
            self.assertFalse(stock.career_scenario_evidence(bad, log, 'create')['complete'], mutation)

    def test_ordinary_exit_requires_signaled_same_process_and_observed_confirmation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            start = {'pid': 308, 'tick': 1000}
            end = {'pid': 308, 'tick': 60000, 'wait_result': 0, 'exit_code': 0}
            flow = {'observations': [{'image': 'exit.png'}], 'shutdown': {
                'process_start': start, 'process_exit': end,
                'actions': [{'label': 'Exit Game'}, {'label': 'Confirm Yes'}],
                'confirmation': {'image': 'exit.png', 'yes_center': [320, 470],
                                 'labels': dict.fromkeys(('title', 'question', 'yes', 'no'), True)}}}
            def write(a, b):
                (root / 'game-process-start.json').write_text(json.dumps(a))
                (root / 'game-process-exit.json').write_text(json.dumps(b))
            write(start, end)
            (root / 'ordinary-exit-observed-time.txt').write_text('12345\n')
            self.assertTrue(stock.ordinary_exit_evidence(root, flow)['passed'])
            for key, value in [('pid', 999), ('exit_code', 1), ('wait_result', 258),
                               ('tick', 1000), ('tick', True)]:
                bad = dict(end, **{key: value})
                write(start, bad)
                # Even a controller repeating corrupt values cannot establish exit.
                claimed = copy.deepcopy(flow)
                claimed['shutdown']['process_exit'] = bad
                self.assertFalse(stock.ordinary_exit_evidence(root, claimed)['passed'])
            write(start, end)
            for mutation in ('confirmation', 'actions', 'observation'):
                bad = copy.deepcopy(flow)
                if mutation == 'confirmation':
                    bad['shutdown']['confirmation']['labels']['no'] = False
                elif mutation == 'actions':
                    bad['shutdown']['actions'].reverse()
                else:
                    bad['observations'] = []
                self.assertFalse(stock.ordinary_exit_evidence(root, bad)['passed'])
            (root / 'ordinary-exit-error.txt').touch()
            self.assertFalse(stock.ordinary_exit_evidence(root, flow)['passed'])

    def test_host_socket_devices_network_and_resource_escapes_are_rejected(self):
        out, disc = Path('/private/capture'), Path('/private/disc')
        value = {'Image': 'sha256:fixture', 'HostConfig': {'NetworkMode': 'none', 'Privileged': False,
                 'CapDrop': ['ALL'], 'SecurityOpt': ['no-new-privileges'],
                 'NanoCpus': 4_000_000_000, 'Memory': 3 * 1024**3, 'PidsLimit': 256},
                 'Mounts': [{'Destination': '/work', 'Source': str(out), 'RW': True},
                            {'Destination': '/disc', 'Source': str(disc), 'RW': False}]}
        self.assertTrue(stock.isolated(value, out, disc, 'sha256:fixture'))
        raised = copy.deepcopy(value)
        raised['HostConfig']['Memory'] = 20 * 1024**3
        self.assertFalse(stock.isolated(raised, out, disc, 'sha256:fixture'))
        self.assertTrue(stock.isolated(raised, out, disc, 'sha256:fixture', memory_limit_bytes=20 * 1024**3))
        self.assertFalse(stock.isolated(value, out, disc, 'sha256:fixture', memory_limit_bytes=20 * 1024**3))
        cases = [('NetworkMode', 'host'), ('Privileged', True), ('Devices', ['/dev/snd']),
                 ('DeviceRequests', ['gpu']), ('CapAdd', ['SYS_ADMIN']), ('CapDrop', []),
                 ('PidMode', 'host'), ('IpcMode', 'host'), ('SecurityOpt', []),
                 ('NanoCpus', 0), ('Memory', 0), ('PidsLimit', 0)]
        for key, replacement in cases:
            bad = copy.deepcopy(value)
            bad['HostConfig'][key] = replacement
            with self.subTest(key=key):
                self.assertFalse(stock.isolated(bad, out, disc, 'sha256:fixture'))
        ephemeral = copy.deepcopy(value)
        seed = Path('/private/seed')
        ephemeral['Mounts'].append({'Destination': '/seed', 'Source': str(seed), 'RW': False})
        ephemeral['Mounts'].append({'Destination': '/work/instrumentation-source',
                                   'Source': str(out / 'instrumentation-source'), 'RW': False})
        ephemeral['HostConfig'].update(Tmpfs={'/privateprefix': 'rw,exec,nosuid,nodev,size=3g'}, MemorySwap=3 * 1024**3)
        self.assertTrue(stock.isolated(ephemeral, out, disc, 'sha256:fixture', seed))
        for mutation in ('writable-seed', 'writable-instrumentation', 'larger-memory', 'unbounded-tmpfs', 'noexec', 'swap'):
            bad = copy.deepcopy(ephemeral)
            if mutation == 'writable-seed':
                bad['Mounts'][-2]['RW'] = True
            elif mutation == 'writable-instrumentation':
                bad['Mounts'][-1]['RW'] = True
            elif mutation == 'noexec':
                bad['HostConfig']['Tmpfs']['/privateprefix'] = 'rw,noexec,nosuid,nodev,size=3g'
            elif mutation == 'larger-memory':
                bad['HostConfig']['Memory'] *= 2
            elif mutation == 'swap':
                bad['HostConfig']['MemorySwap'] = -1
            else:
                bad['HostConfig']['Tmpfs']['/privateprefix'] = 'rw'
            self.assertFalse(stock.isolated(bad, out, disc, 'sha256:fixture', seed), mutation)
        for mutation in ('host-audio-socket', 'writable-disc', 'different-image'):
            bad = copy.deepcopy(value)
            if mutation == 'host-audio-socket':
                bad['Mounts'].append({'Destination': '/audio', 'Source': '/host/audio.sock', 'RW': True})
            elif mutation == 'writable-disc':
                bad['Mounts'][1]['RW'] = True
            else:
                bad['Image'] = 'sha256:different'
            with self.subTest(mutation=mutation):
                self.assertFalse(stock.isolated(bad, out, disc, 'sha256:fixture'))

    def test_loading_frozen_helper_does_not_add_bytecode_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            helper = root / 'frozen_helper.py'
            helper.write_text('value = 42\n')
            before = stock.manifest(root)
            self.assertEqual(stock.module('owned_frozen_helper', helper).value, 42)
            self.assertEqual(stock.manifest(root), before)

    def test_exit_and_boolean_claims_do_not_replace_ordered_scenario_evidence(self):
        menu = {'MAIN MENU': True, 'FREE RIDE': True, 'OPTIONS': True}
        flow = {'passed': True, 'actions': [{'label': 'New Career'}, {'label': 'Cancel button'}],
                'before': {'menu': menu},
                'name_dialog': {'dialog': {'name_title': True, 'cancel_button': True}},
                'after': {'menu': menu, 'dialog': {'name_title': False, 'cancel_button': False}}}
        text = 'GetDeviceState calls=20\n' + ''.join(
            f'input_poll: kind=1 offset=12 data={v} source=native\n' for v in (0, 128, 0, 128, 0))
        self.assertTrue(stock.scenario_evidence(flow, text)['complete'])
        for mutation in ('missing-dialog', 'dialog-still-open', 'missing-label', 'retry', 'wrong-button-order',
                         'no-polling', 'queue-drop', 'boolean-only'):
            bad, log = copy.deepcopy(flow), text
            if mutation == 'missing-dialog':
                del bad['name_dialog']
            elif mutation == 'dialog-still-open':
                bad['after']['dialog']['name_title'] = True
            elif mutation == 'missing-label':
                del bad['after']['menu']['OPTIONS']
            elif mutation == 'retry':
                bad['actions'].insert(1, {'label': 'New Career retry'})
            elif mutation == 'wrong-button-order':
                log = text.replace('data=128', 'data=0', 1)
            elif mutation == 'no-polling':
                log = text.replace('GetDeviceState calls=20', '')
            elif mutation == 'queue-drop':
                log += 'input_queue: dropped '
            else:
                bad = {'passed': True}
            with self.subTest(mutation=mutation):
                self.assertFalse(stock.scenario_evidence(bad, log)['complete'])

    def test_wrong_stock_or_protected_output_stops_before_creating_attempt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('source', 'disc'):
                (root / name).mkdir()
            (root / 'wrong.exe').write_bytes(b'not the verified Stock')
            (root / 'texture.dat').write_bytes(b'table')
            output = root / 'new-output'
            result = subprocess.run([sys.executable, '-I', str(Path(stock.__file__)),
                                     '--source', str(root / 'source'), '--stock', str(root / 'wrong.exe'),
                                     '--texture-table', str(root / 'texture.dat'), '--disc', str(root / 'disc'),
                                     '--out', str(output)], capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 2)
            self.assertFalse(output.exists())
            with self.assertRaises(ValueError):
                stock.paths_valid((root / 'source/new').resolve(), (root / 'source').resolve(),
                                  root / 'wrong.exe', root / 'texture.dat', root / 'disc')


if __name__ == '__main__':
    unittest.main()
