"""No Wine execution: CLI derivation/refusal and diagnostic scope controls."""
import ast
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace


SCRIPT = Path(__file__).with_name('qualify_factory.py')


class DiscoveryQualifierCLITest(unittest.TestCase):
    def invoke(self, root, *extra):
        return subprocess.run(
            [sys.executable, '-I', str(SCRIPT), '--out', str(root / 'new-output'),
             '--prefix-seed', str(root / 'unused-seed'), '--discovery-abi',
             '--print-runner', *extra],
            capture_output=True, text=True, timeout=10)

    def test_print_runner_contains_only_discovery_cases_and_no_writes(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            result = self.invoke(root, '--memory-prefix')
            self.assertEqual(result.returncode, 0, result.stderr)
            tree = ast.parse(result.stdout)
            functions = [node for node in ast.walk(tree) if isinstance(node, ast.FunctionDef)]
            self.assertTrue(any(node.name == 'run' for node in functions))
            self.assertIn("cases = ('discovery','discovery-no-fx')", result.stdout)
            self.assertIn("'discovery-no-fx.exe',discovery_fault", result.stdout)
            self.assertNotIn('input-proof.exe', result.stdout)
            self.assertNotIn('factory-proof.exe', result.stdout)
            self.assertIn('4 * 1024**2', result.stdout)
            self.assertEqual(list(root.iterdir()), [])

    def test_incompatible_flags_refuse_before_derivation_or_writes(self):
        options = [('--native-clock',), ('--native-clock-default-profile',),
                   ('--native-clock-legacy-fault',), ('--control-writer-reference', '/unused'),
                   ('--control-writer-reference-sha256', 'unused'), ('--real-construction',),
                   ('--device-raw',), ('--bulk-owned',), ('--bulk-overflow',),
                   ('--lazy',), ('--depth',), ('--setup-failure', 'all'),
                   ('--fault', 'fx'), ('--fault', 'edx'), ('--fault', 'device-fx'),
                   ('--fault', 'device-edx'), ('--fault', 'bulk-bytes')]
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            for flags in options:
                with self.subTest(flags=flags):
                    result = self.invoke(root, '--memory-prefix', *flags)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('--discovery-abi requires', result.stderr)
                    self.assertEqual(result.stdout, '')
                    self.assertEqual(list(root.iterdir()), [])
            result = self.invoke(root)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('--memory-prefix', result.stderr)
            self.assertEqual(list(root.iterdir()), [])

    def test_independent_state_audit_rejects_cleanup_argument_and_fp_corruption(self):
        tree = ast.parse(SCRIPT.read_text())
        node = next(node for node in tree.body
                    if isinstance(node, ast.FunctionDef) and node.name == 'audit_discovery_states')
        namespace = {}
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(SCRIPT), 'exec'), namespace)
        audit = namespace['audit_discovery_states']
        raw = bytearray(384 * 576)
        arena, index = 0x12340000, 0
        def put(base, offset, value, size=4):
            raw[base + offset:base + offset + size] = value.to_bytes(size, 'little')
        for cleanup in (4, 0, 4, 0, 0, 48, 16, 4):
            for mode in range(4):
                for flags in (0x202, 0x203, 0x246, 0x697):
                    for behavior in range(3):
                        base = index * 576
                        cw, mxcsr = 0x37f + mode * 1024, 0x1f80 + mode * 8192
                        if behavior == 2:
                            cw, mxcsr = cw ^ 0x400, mxcsr ^ 0x2000
                        digest = 0x811c9dc5
                        for argument in range(12):
                            digest = ((digest ^ (0xabc00000 + index * 16 + argument)) * 16777619) % 2**32
                        for offset, value in ((0, 0x66666666), (4, 0x55555555),
                                (12, arena + cleanup), (16, 0x44444444),
                                (20, 0xd3d3d3d3 if behavior else 0x33333333),
                                (24, 0xc2c2c2c2 if behavior else 0x22222222),
                                (28, 0xa1a1a1a1 if behavior else 0x11111111),
                                (32, flags ^ (1 if behavior == 1 else 0x40 if behavior == 2 else 0)),
                                (36, 0x55667788 if behavior == 2 else 0x55aa55aa),
                                (40, arena + 4 + cleanup), (44, digest), (72, mxcsr),
                                (568, 0x12345678), (572, 0x9abcdef0)):
                            put(base, offset, value)
                        put(base, 564, cw, 2)
                        put(base, 566, 7 << 11 if behavior == 2 else 0, 2)
                        xmm = bytes([0 if behavior == 2 else 255]) * 16
                        raw[base + 208:base + 224] = xmm
                        raw[base + 320:base + 336] = xmm
                        index += 1
        self.assertTrue(audit(raw))
        self.assertFalse(audit(raw[:-1]))
        for case in (1, 49, 241, 289, 337):
            for offset in (12, 20, 24, 28, 32, 36, 40, 44, 72, 208, 320, 564, 568, 572):
                with self.subTest(case=case, offset=offset):
                    corrupt = bytearray(raw)
                    corrupt[case * 576 + offset] ^= 1
                    self.assertFalse(audit(corrupt))

    def test_discovery_metadata_is_explicit_and_excludes_game_claims(self):
        # Load only the pure metadata function; importing the CLI would parse args.
        source = SCRIPT.read_text()
        self.assertIn("'mode':result['factory_mode'],'scope':result['scope']", source)
        tree = ast.parse(source)
        node = next(node for node in tree.body
                    if isinstance(node, ast.FunctionDef) and node.name == 'qualification_metadata')
        namespace = {}
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(SCRIPT), 'exec'), namespace)
        args = SimpleNamespace(lazy=False, setup_failure=None, depth=False, fault=None,
                               memory_prefix=True, device_raw=False, real_construction=False,
                               discovery_abi=True, native_clock=False)
        mode, scope = namespace['qualification_metadata'](args)
        self.assertTrue(mode['discovery_abi'])
        self.assertFalse(mode['native_clock'])
        self.assertIn('owned actual discovery entry/return stubs only', scope)
        self.assertIn('no Stock graph', scope)
        args.discovery_abi = False
        self.assertFalse(namespace['qualification_metadata'](args)[0]['discovery_abi'])
        self.assertIn('owned factory mode', namespace['qualification_metadata'](args)[1])


if __name__ == '__main__':
    unittest.main()
