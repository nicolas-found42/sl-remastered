"""Developer staging preserves selected bytes and refuses incomplete inputs."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class StageTests(unittest.TestCase):
    def setUp(self):
        self.stage = load('release_stage', ROOT / 'tools/release/stage.py')
        self.data = load('staging_data', ROOT / 'tools/parity/diagnostic-data.py')
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.app = self.root / 'Input.app'
        macos = self.app / 'Contents/MacOS'
        macos.mkdir(parents=True)
        (self.app / 'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable': 'Game'}))
        (macos / 'Game').write_bytes(b'not reached for refusal controls')
        self.content = self.root / 'selected'
        self.content.mkdir()
        (self.content / 'maps').mkdir()
        (self.content / 'maps/city.rpk').write_bytes(b'selected content fixture')
        (self.content / '.hidden').write_bytes(b'also required')
        self.inventory = self.root / 'content.json'
        self.write_inventory()
        self.cursor = self.root / 'stock.exe'
        self.cursor.write_bytes(b'MZ cursor resource carrier fixture')
        self.cursor_sha = hashlib.sha256(self.cursor.read_bytes()).hexdigest()
        self.output = self.root / 'Candidate.app'

    def write_inventory(self):
        self.inventory.write_text(json.dumps(self.data.collect(self.content, exclude_runtime=False)))

    def invoke(self):
        return self.stage.stage_application(self.app, self.content, self.inventory,
                                             self.cursor, self.cursor_sha, self.output)

    def test_staged_cli_runs_with_resources_after_inputs_are_moved(self):
        # Only the selected PE is an input; unrelated reference siblings can
        # contain links or exceed content-inventory limits.
        (self.root / 'unselected-reference-link').symlink_to(self.root / 'absent')
        source = '#include <stdio.h>\nint main(void) { puts("staged game fixture"); return 0; }'
        subprocess.run(['clang', '-arch', 'arm64', '-x', 'c', '-', '-o',
                        str(self.app / 'Contents/MacOS/Game'), '-Wl,-headerpad_max_install_names'],
                       input=source, text=True, capture_output=True, check=True, timeout=30)
        before_app = self.data.collect(self.app, exclude_runtime=False)['files']
        before_content = self.data.collect(self.content, exclude_runtime=False)['files']
        result = subprocess.run(['python3', '-I', str(ROOT / 'tools/release/stage.py'),
                                 '--application', str(self.app), '--content', str(self.content),
                                 '--inventory', str(self.inventory), '--cursor', str(self.cursor),
                                 '--cursor-sha256', self.cursor_sha, '--out', str(self.output)],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertFalse(report['release_accepted'])
        self.assertEqual(report['status'], 'developer_candidate')
        self.assertEqual(before_app, self.data.collect(self.app, exclude_runtime=False)['files'])
        self.assertEqual(before_content, self.data.collect(self.content, exclude_runtime=False)['files'])
        self.assertEqual(before_content, self.data.collect(self.output / 'Contents/Resources/Game', exclude_runtime=False)['files'])
        self.assertEqual((self.output / 'Contents/Resources/StockCursorResources.exe').read_bytes(), self.cursor.read_bytes())
        self.app.rename(self.root / 'unavailable-app')
        self.content.rename(self.root / 'unavailable-content')
        self.cursor.rename(self.root / 'unavailable-cursor')
        subprocess.run(['codesign', '--verify', '--strict', str(self.output)], check=True, capture_output=True, timeout=10)
        run = subprocess.run([str(self.output / 'Contents/MacOS/Game')], cwd='/',
                             env={k: v for k, v in os.environ.items() if not k.startswith('DYLD_')},
                             capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout, 'staged game fixture\n')

    def test_unlisted_file_refuses_before_creating_output(self):
        (self.content / 'unlisted').write_bytes(b'not in selected inventory')
        with self.assertRaises(ValueError):
            self.invoke()
        self.assertFalse(self.output.exists())

    def test_changed_file_refuses_before_creating_output(self):
        (self.content / 'maps/city.rpk').write_bytes(b'changed bytes')
        with self.assertRaises(ValueError):
            self.invoke()
        self.assertFalse(self.output.exists())

    def test_excluded_inventory_cannot_be_packaged_as_complete(self):
        document = json.loads(self.inventory.read_text())
        document['exclusions'] = [{'path': 'save/', 'reason': 'omitted'}]
        self.inventory.write_text(json.dumps(document))
        with self.assertRaises(ValueError):
            self.invoke()
        self.assertFalse(self.output.exists())

    def test_invalid_manifest_path_refuses(self):
        document = json.loads(self.inventory.read_text())
        document['files']['../outside'] = document['files'].pop('.hidden')
        self.inventory.write_text(json.dumps(document))
        with self.assertRaises(ValueError):
            self.invoke()
        self.assertFalse(self.output.exists())

    def test_symlink_content_refuses_without_following_target(self):
        (self.content / 'alias').symlink_to(self.cursor)
        self.write_inventory()
        with self.assertRaises(ValueError):
            self.invoke()
        self.assertEqual(hashlib.sha256(self.cursor.read_bytes()).hexdigest(), self.cursor_sha)
        self.assertFalse(self.output.exists())

    def test_cursor_hash_mismatch_refuses(self):
        self.cursor_sha = '0' * 64
        with self.assertRaises(ValueError):
            self.invoke()
        self.assertFalse(self.output.exists())

    def test_output_inside_source_refuses(self):
        self.output = self.content / 'Candidate.app'
        with self.assertRaises(ValueError):
            self.invoke()
        self.assertFalse(self.output.exists())

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        marker = self.output / 'retained'
        marker.write_bytes(b'preserve this')
        with self.assertRaises(ValueError):
            self.invoke()
        self.assertEqual(marker.read_bytes(), b'preserve this')

    def test_protected_reference_output_refuses(self):
        self.output = ROOT / 'ref/StageControlMustNotExist.app'
        with self.assertRaises(ValueError):
            self.invoke()
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
