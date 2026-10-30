#!/usr/bin/env python3
"""Check the startup trace against installed files and altered copies."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/startup_trace.py"
RAW = ROOT / "ref/raw"
FILES = ("system.rpk", "sl.rpk", "system/Scripts/util/Config.class", "sl/Scripts/game/Init.class")


def run(root):
    return subprocess.run([sys.executable, "-I", str(TOOL), str(root)], capture_output=True, text=True)


class StartupTraceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in FILES:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(RAW / name, target)

    def test_actual_chain(self):
        result = run(self.root)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("system.rpk 0x47 -> java.util.Config", result.stdout)
        self.assertIn("Config.init -> sl.rpk 0x40 -> java.game.Init", result.stdout)
        self.assertIn("first script execution and menu transition are not established", result.stdout)

    def test_unexpected_resource_payload_rejected(self):
        path = self.root / "sl.rpk"
        original = path.read_bytes()
        self.assertIn(b"java.game.Init", original)
        path.write_bytes(original.replace(b"java.game.Init", b"java.game.Wrong"))
        self.assertNotEqual(run(self.root).returncode, 0)

    def test_corrupt_or_missing_data_rejected(self):
        for name in FILES:
            with self.subTest(name=name):
                path = self.root / name
                old = path.read_bytes()
                path.write_bytes(old[:12])
                self.assertNotEqual(run(self.root).returncode, 0)
                path.write_bytes(old)
        (self.root / FILES[-1]).unlink()
        self.assertNotEqual(run(self.root).returncode, 0)


if __name__ == "__main__":
    unittest.main()
