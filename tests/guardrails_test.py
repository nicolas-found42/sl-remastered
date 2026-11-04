"""Run actual checkers against selected Git blobs and real compilation databases."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('guardrails', ROOT / 'tools/guardrails.py')
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


class GuardrailTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        for name in (
            'tools/guardrails.py',
            'tools/parity/resource_pool.py',
            '.githooks/pre-commit',
            '.clang-format',
            '.clang-tidy',
            'ruff.toml',
            'tools/devtools.json',
        ):
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, target)
        self.git('init', '-q')
        self.git('config', 'user.email', 'guardrails@example.invalid')
        self.git('config', 'user.name', 'Guardrail control')
        self.git('config', 'core.hooksPath', '.githooks')
        self.git('add', '.')
        self.git('-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'control baseline')

    def git(self, *args):
        return subprocess.run(['git', *args], cwd=self.root, check=True, capture_output=True)

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
        return path

    def check(self, *arguments, env=None):
        return subprocess.run(
            [sys.executable, '-I', str(self.root / 'tools/guardrails.py'), *arguments],
            cwd=self.root,
            capture_output=True,
            text=True,
            timeout=180,
            env=env,
        )

    def hook(self):
        return subprocess.run(
            ['git', 'hook', 'run', 'pre-commit'],
            cwd=self.root,
            capture_output=True,
            text=True,
            timeout=180,
        )

    def assert_pass(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_hook_uses_staged_python_including_unicode_space_path(self):
        path = self.write('tools/control café.py', 'value = unknown_name\n')
        self.git('add', 'tools/control café.py')
        path.write_text('value = 1\n')
        before = self.git('show', ':tools/control café.py').stdout
        rejected = self.hook()
        self.assertEqual(rejected.returncode, 1, rejected.stdout + rejected.stderr)
        self.assertIn('F821', rejected.stdout + rejected.stderr)
        self.assertEqual(self.git('show', ':tools/control café.py').stdout, before)
        self.assertEqual(path.read_text(), 'value = 1\n')
        self.git('add', 'tools/control café.py')
        path.write_text('value = unknown_name\n')
        self.assert_pass(self.hook())
        path.write_text('def broken(:\n')
        self.git('add', 'tools/control café.py')
        self.assertEqual(self.hook().returncode, 1)

    def test_modified_cpp_checks_only_edited_range_without_database(self):
        path = self.write(
            'legacy space.cpp', 'int legacy(){return 1;}\n\nint fresh() { return 2; }\n'
        )
        self.git('add', 'legacy space.cpp')
        self.git('-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'legacy formatting debt')
        path.write_text('int legacy(){return 1;}\n\nint fresh() { return 3; }\n')
        self.git('add', 'legacy space.cpp')
        self.assert_pass(self.hook())
        self.assertFalse((self.root / 'build').exists())
        path.write_text('int legacy(){return 1;}\n\nint fresh(){return 3;}\n')
        self.git('add', 'legacy space.cpp')
        self.assertEqual(self.hook().returncode, 1)
        self.assertIn('clang-format', self.hook().stderr)

    def test_new_cpp_and_rename_are_fully_formatted(self):
        path = self.write('new.cpp', 'int main(){return 0;}\n')
        self.git('add', 'new.cpp')
        self.assertEqual(self.hook().returncode, 1)
        path.write_text('int main() { return 0; }\n')
        self.git('add', 'new.cpp')
        self.assert_pass(self.hook())
        self.git('-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'new file')
        self.git('mv', 'new.cpp', 'renamed café.cpp')
        self.assert_pass(self.hook())
        self.git('rm', '-f', 'renamed café.cpp')
        self.assert_pass(self.hook())

    def test_shell_warning_and_clean_quote_use_selected_bytes(self):
        path = self.write(
            'control.sh', '#!/usr/bin/env bash\nprintf "%s\\n" $(printf "hello world")\n'
        )
        self.git('add', 'control.sh')
        rejected = self.hook()
        self.assertEqual(rejected.returncode, 1, rejected.stdout + rejected.stderr)
        self.assertIn('SC2046', rejected.stdout + rejected.stderr)
        path.write_text('#!/usr/bin/env bash\nprintf "%s\\n" "$(printf \'hello world\')"\n')
        self.git('add', 'control.sh')
        self.assert_pass(self.hook())
        path = self.write(
            '.githooks/pre-commit', '#!/bin/sh\nitems=(one two)\nprintf "%s\\n" "${items[0]}"\n'
        )
        self.git('add', '.githooks/pre-commit')
        # Invoke the checker directly: the deliberately invalid hook cannot launch it.
        self.assertEqual(self.check('--staged').returncode, 1)
        path.write_text('#!/bin/sh\nprintf "%s\\n" ok\n')
        self.git('add', '.githooks/pre-commit')
        self.assert_pass(self.check('--staged'))
        self.write(
            'tools/parity/stock/container.sh', 'items=(one two)\nprintf "%s\\n" "${items[0]}"\n'
        )
        self.git('add', 'tools/parity/stock/container.sh')
        self.assert_pass(self.check('--staged'))

    def test_base_checks_head_bytes_instead_of_clean_working_content(self):
        base = self.git('rev-parse', 'HEAD').stdout.decode().strip()
        path = self.write('head control.py', 'value = missing_name\n')
        self.git('add', 'head control.py')
        self.git('-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'bad HEAD control')
        path.write_text('value = 1\n')
        rejected = self.check('--base', base)
        self.assertEqual(rejected.returncode, 1, rejected.stdout + rejected.stderr)
        self.assertIn('F821', rejected.stdout + rejected.stderr)
        self.assertEqual(path.read_text(), 'value = 1\n')

    def test_protected_deletion_is_rejected(self):
        self.write('ref/control.bin', 'not a game file\n')
        self.git('add', '-f', 'ref/control.bin')
        self.git('-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'protected deletion control')
        self.git('rm', 'ref/control.bin')
        rejected = self.hook()
        self.assertEqual(rejected.returncode, 1)
        self.assertIn('protected ref/', rejected.stderr)

    def test_missing_wrong_version_and_working_configuration_refuse(self):
        self.write('control.py', 'value = 1\n')
        self.git('add', 'control.py')
        config = self.root / 'ruff.toml'
        config.write_text(config.read_text() + '\n# unstaged configuration\n')
        self.assertIn('configuration differs', self.hook().stderr)
        self.git('restore', 'ruff.toml')
        config.unlink()
        self.assertIn('ruff.toml required', self.hook().stderr)
        self.git('restore', 'ruff.toml')
        version = self.root / 'tools/devtools.json'
        data = json.loads(version.read_text())
        data['ruff'] = '0.0.0'
        version.write_text(json.dumps(data))
        self.git('add', 'tools/devtools.json')
        self.assertIn('expected version 0.0.0', self.hook().stderr)
        self.git('restore', '--source=HEAD', '--staged', '--worktree', 'tools/devtools.json')
        empty_path = self.root / 'empty-path'
        empty_path.mkdir()
        # Python is absolute; supply only Git so selection can run but Ruff cannot.
        (empty_path / 'git').symlink_to(shutil.which('git'))
        environment = dict(os.environ, PATH=str(empty_path))
        self.assertIn('ruff required', self.check('--staged', env=environment).stderr)

    def test_explicit_format_never_stages_and_refuses_partial_selection(self):
        path = self.write('format.cpp', 'int main(){return 0;}\n')
        self.assert_pass(self.check('--paths', 'format.cpp', '--format'))
        self.assertEqual(path.read_text(), 'int main() { return 0; }\n')
        self.git('add', 'format.cpp')
        path.write_text('int main(){return 1;}\n')
        before = path.read_bytes()
        index = self.git('show', ':format.cpp').stdout
        self.assertEqual(self.check('--paths', 'format.cpp', '--format').returncode, 1)
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(self.git('show', ':format.cpp').stdout, index)

    def database(self, source):
        build = self.root / 'build'
        build.mkdir(exist_ok=True)
        compiler = shutil.which('clang++')
        self.assertIsNotNone(compiler)
        (build / 'compile_commands.json').write_text(
            json.dumps(
                [
                    {
                        'directory': str(self.root),
                        'file': str(source),
                        'arguments': [compiler, '-std=c++20', '-c', str(source)],
                    }
                ]
            )
        )

    def test_deep_lint_real_narrowing_and_explicit_cast(self):
        path = self.write('narrow.cpp', 'int narrow(double value) { return value; }\n')
        self.git('add', 'narrow.cpp')
        self.assert_pass(self.hook())
        self.assertIn('configure CMake first', self.check('--staged', '--tidy').stderr)
        self.database(path)
        rejected = self.check('--staged', '--tidy')
        self.assertEqual(rejected.returncode, 1, rejected.stdout + rejected.stderr)
        self.assertIn('bugprone-narrowing-conversions', rejected.stdout + rejected.stderr)
        path.write_text('int narrow(double value) { return static_cast<int>(value); }\n')
        self.git('add', 'narrow.cpp')
        self.assert_pass(self.check('--staged', '--tidy'))
        path.write_text('int narrow(double value) { return value; }\n')
        self.assertIn(
            'matching selected/working source bytes', self.check('--staged', '--tidy').stderr
        )

    def test_deep_lint_checks_each_distinct_macro_variant(self):
        path = self.write(
            'variants.cpp',
            '#ifdef NARROW\nint narrow(double value) { return value; }\n#else\nint narrow(double value) { return static_cast<int>(value); }\n#endif\n',
        )
        self.git('add', 'variants.cpp')
        self.database(path)
        database = self.root / 'build/compile_commands.json'
        good = json.loads(database.read_text())[0]
        bad = dict(good, arguments=[*good['arguments'], '-DNARROW'])
        database.write_text(json.dumps([good, good, bad]))
        rejected = self.check('--staged', '--tidy')
        self.assertEqual(rejected.returncode, 1, rejected.stdout + rejected.stderr)
        self.assertIn('bugprone-narrowing-conversions', rejected.stdout + rejected.stderr)
        self.assertIn('variant 1/2', rejected.stdout)
        self.assertIn('variant 2/2', rejected.stdout)
        self.assertNotIn('variant 3/', rejected.stdout)
        path.write_text(
            path.read_text().replace('return value;', 'return static_cast<int>(value);')
        )
        self.git('add', 'variants.cpp')
        self.assert_pass(self.check('--staged', '--tidy'))

    def test_parent_symlink_cannot_format_protected_or_external_data(self):
        protected = self.write('ref/precious.cpp', 'int main(){return 0;}\n')
        (self.root / 'alias').symlink_to(self.root / 'ref', target_is_directory=True)
        before = protected.read_bytes()
        rejected = self.check('--paths', 'alias/precious.cpp', '--format')
        self.assertEqual(rejected.returncode, 1)
        self.assertIn('canonical path alias', rejected.stderr)
        self.assertEqual(protected.read_bytes(), before)
        with tempfile.TemporaryDirectory() as outside:
            target = Path(outside) / 'precious.cpp'
            target.write_bytes(before)
            (self.root / 'external').symlink_to(outside, target_is_directory=True)
            self.assertEqual(
                self.check('--paths', 'external/precious.cpp', '--format').returncode, 1
            )
            self.assertEqual(target.read_bytes(), before)

    def test_deep_lint_refuses_untracked_repository_header_until_staged(self):
        source = self.write(
            'tracked.cpp', '#include "untracked.hpp"\nint use() { return number; }\n'
        )
        self.write('untracked.hpp', '#pragma once\nconstexpr int number = 1;\n')
        self.git('add', 'tracked.cpp')
        self.database(source)
        rejected = self.check('--staged', '--tidy')
        self.assertEqual(rejected.returncode, 1, rejected.stdout + rejected.stderr)
        self.assertIn('untracked repository dependency', rejected.stderr)
        self.git('add', 'untracked.hpp')
        self.assert_pass(self.check('--staged', '--tidy', '--translation-unit', '@all'))

    def test_headers_need_proven_includer_and_matching_dependency_bytes(self):
        source = self.write(
            'includer.cpp', '#include "control.hpp"\nint use() { return number; }\n'
        )
        self.write('control.hpp', '#pragma once\nconstexpr int number = 1;\n')
        self.git('add', 'includer.cpp', 'control.hpp')
        self.git('-c', 'core.hooksPath=/dev/null', 'commit', '-qm', 'header baseline')
        self.write('control.hpp', '#pragma once\nconstexpr int number = 2;\n')
        self.git('add', 'control.hpp')
        self.database(source)
        self.assertIn('--translation-unit', self.check('--staged', '--tidy').stderr)
        self.assert_pass(self.check('--staged', '--tidy', '--translation-unit', '@all'))
        source.write_text('#include "control.hpp"\nint use() { return 0; }\n')
        self.assertIn(
            'dependency bytes',
            self.check('--staged', '--tidy', '--translation-unit', '@all').stderr,
        )


if __name__ == '__main__':
    unittest.main()
