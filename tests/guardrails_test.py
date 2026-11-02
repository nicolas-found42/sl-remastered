"""Exercise real formatter and linter, including deliberate violations."""
import importlib.util
from pathlib import Path
import subprocess
import shutil
import json
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('guardrails', ROOT / 'tools/guardrails.py')
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


class GuardrailTest(unittest.TestCase):
    def test_installed_hook_checks_staged_bytes_in_isolated_repository(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            for name in ('tools/guardrails.py', 'tools/parity/resource_pool.py',
                         '.githooks/pre-commit', '.clang-format', '.clang-tidy'):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / name, target)
            def git(*args):
                return subprocess.run(['git', *args], cwd=root, check=True, capture_output=True)
            git('init', '-q')
            git('config', 'core.hooksPath', '.githooks')
            source = root / 'control.py'
            source.write_text('def broken(:\n')
            git('add', 'control.py')
            source.write_text('value = 1\n')
            def hook():
                return subprocess.run(['git', 'hook', 'run', 'pre-commit'], cwd=root,
                                      capture_output=True, text=True, timeout=160)
            self.assertEqual(hook().returncode, 1, 'working content must not hide bad staged bytes')
            git('add', 'control.py')
            self.assertEqual(hook().returncode, 0)
            cpp = root / 'narrow.cpp'
            cpp.write_text('int narrow(double value) { return value; }\n')
            git('add', 'narrow.cpp')
            (root / 'build').mkdir()
            (root / 'build/compile_commands.json').write_text(json.dumps([
                {'directory': str(root), 'file': str(cpp),
                 'arguments': ['clang++', '-std=c++20', '-c', str(cpp)]}]))
            rejected = hook()
            self.assertEqual(rejected.returncode, 1, rejected.stdout + rejected.stderr)
            self.assertIn('bugprone-narrowing-conversions', rejected.stdout + rejected.stderr)
            cpp.write_text('int narrow(double value) { return static_cast<int>(value); }\n')
            git('add', 'narrow.cpp')
            accepted = hook()
            self.assertEqual(accepted.returncode, 0, accepted.stdout + accepted.stderr)

    def test_format_violation_and_clean_control(self):
        path = ROOT / 'tests/guardrail_fixture.cpp'
        guard.check_file(path, b'int main() { return 0; }\n', tidy=False)
        with self.assertRaises(ValueError):
            guard.check_file(path, b'int main(){return 0;}\n', tidy=False)

    def test_python_syntax_violation_and_clean_control(self):
        guard.check_file(Path('control.py'), b'x = 1\n')
        with self.assertRaises(SyntaxError):
            guard.check_file(Path('control.py'), b'def broken(:\n')

    def test_real_tidy_narrowing_and_explicit_cast(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'narrow.cpp'
            for expression, expected in [('value', 1), ('static_cast<int>(value)', 0)]:
                source.write_text('int narrow(double value) { return ' + expression + '; }\n')
                command = [guard.tool('clang-tidy'), str(source),
                           '--config-file=' + str(ROOT / '.clang-tidy'), '--warnings-as-errors=*',
                           '--', '-std=c++20']
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                if expected:
                    self.assertIn('bugprone-narrowing-conversions', result.stdout)


if __name__ == '__main__':
    unittest.main()
