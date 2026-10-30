#!/usr/bin/env python3
"""Check headless handler decompilation failure and output preservation."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import shutil

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "tools/ghidra_run.sh"


class HeadlessEvidenceTest(unittest.TestCase):
    def test_incomplete_project_is_not_removed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "tools").mkdir()
            shutil.copy(RUNNER, root / "tools/ghidra_run.sh")
            (root / "ref").symlink_to(ROOT / "ref", target_is_directory=True)
            project = root / "local/ghidra-proj"
            project.mkdir(parents=True)
            sentinel = project / "keep.txt"
            sentinel.write_text("keep this project")
            result = subprocess.run([str(root / "tools/ghidra_run.sh"), "DecompileStockHandler.java"],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(sentinel.read_text(), "keep this project")

    def test_success_and_script_failure_preserves_existing_output(self):
        if not (ROOT / "local/ghidra-proj/defined.ok").is_file():
            self.skipTest("existing initialized Ghidra project required")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "handler.txt"
            command = [str(RUNNER), "DecompileStockHandler.java", str(output)]
            success = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(success.returncode, 0, success.stderr)
            evidence = output.read_text()
            self.assertIn("executable MD5: e686099e7233bb7d7c461b1b35c5064c", evidence)
            self.assertIn("entry: 0047c0b0", evidence)
            self.assertIn("return 0x25e;", evidence)
            failure = subprocess.run(command + ["unexpected"], cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(failure.returncode, 0)
            self.assertIn("ghidra_run: DecompileStockHandler.java failed", failure.stderr)
            self.assertIn("local/census/DecompileStockHandler.log", failure.stderr)
            self.assertEqual(output.read_text(), evidence)


if __name__ == "__main__":
    unittest.main()
