"""Exercise dependency relocation with real Mach-O loaders, not mocked otool output."""
import importlib.util
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BundleTests(unittest.TestCase):
    def test_transitive_libraries_run_without_build_machine_paths(self):
        self.check_relocation(False)

    def test_inherited_runpath_is_resolved_and_removed(self):
        self.check_relocation(True)

    def test_bare_loader_path_resolves_sibling_library(self):
        self.check_relocation('loader')

    def test_system_prefix_cannot_hide_external_dependency(self):
        self.check_relocation(False, traversal=True)

    def test_universal_dependency_is_thinned_to_arm64(self):
        self.check_relocation(False, universal=True)

    def test_diamond_dependencies_allow_equivalent_runpath_contexts(self):
        self.check_relocation(True, diamond=True)

    def check_relocation(self, runpath, traversal=False, universal=False, diamond=False):
        spec = importlib.util.spec_from_file_location('release_bundle', ROOT / 'tools/release/bundle.py')
        bundle = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(bundle)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            external = root / 'external'
            external.mkdir()
            app = root / 'Game.app'
            macos = app / 'Contents/MacOS'
            macos.mkdir(parents=True)
            (app / 'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable': 'Game'}))
            leaf = external / 'libleaf.dylib'
            middle = external / 'libmiddle.dylib'
            def compile(source, output, flags):
                subprocess.run(['clang', '-arch', 'arm64', '-x', 'c', '-', '-x', 'none', '-o', str(output),
                                *map(str, flags)], input=source, text=True, check=True,
                               capture_output=True, timeout=30)
            leaf_id = '@rpath/libleaf.dylib' if runpath else ('/usr/lib/../..' if traversal else '') + str(leaf)
            compile('int leaf(void) { return 23; }', leaf,
                    ['-dynamiclib', '-Wl,-install_name,' + leaf_id, *(['-arch', 'x86_64'] if universal else [])])
            compile('extern int leaf(void); int middle(void) { return leaf(); }', middle,
                    ['-dynamiclib', '-Wl,-install_name,' + ('@rpath/libmiddle.dylib' if runpath else str(middle)), leaf,
                     *(['-Wl,-rpath,@loader_path'] if runpath == 'loader' else [])])
            others = []
            main = 'extern int middle(void); int main(void) { return middle(); }'
            if diamond:
                other = external / 'libother.dylib'
                unused = root / 'unused'
                unused.mkdir()
                compile('extern int leaf(void); int other(void) { return leaf(); }', other,
                        ['-dynamiclib', '-Wl,-install_name,' + str(other), leaf, '-Wl,-rpath,' + str(unused)])
                others = [other]
                main = 'extern int middle(void); extern int other(void); int main(void) { return middle()+other()-23; }'
            compile(main, macos / 'Game',
                    [middle, *others, '-Wl,-headerpad_max_install_names', *(['-Wl,-rpath,' + str(external)] if runpath else [])])
            originals = {p.name: p.read_bytes() for p in external.iterdir()}
            report = bundle.bundle_libraries(app)
            self.assertEqual(len(report['libraries']), 3 if diamond else 2)
            for row in report['libraries']:
                architectures = subprocess.check_output(['lipo', '-archs', str(app / row['bundled_path'])], text=True).split()
                self.assertEqual(architectures, ['arm64'])
            self.assertEqual(originals, {p.name: p.read_bytes() for p in external.iterdir()})
            external.rename(root / 'unavailable')
            environment = {k: v for k, v in os.environ.items() if not k.startswith('DYLD_')}
            result = subprocess.run([str(macos / 'Game')], cwd='/', env=environment,
                                    capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 23, result.stderr.decode())


if __name__ == '__main__':
    unittest.main()
