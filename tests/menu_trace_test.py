#!/usr/bin/env python3
"""Exercise the menu trace command against the installed class files."""

from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/menu_trace.py"
MENU = ROOT / "ref/raw/sl/Scripts/game/MainMenu.class"
STALE = ROOT / "ref/raw/system/Scripts/render/osd/Dialog.class"


def run_trace(*args):
    return subprocess.run([sys.executable, "-I", str(TOOL), *map(str, args)],
                          capture_output=True, text=True, check=False)


class MenuTraceTest(unittest.TestCase):
    def test_show_tree_resolves_reference(self):
        result = run_trace(MENU, "--class", "java.game.MainMenuDialog", "--method", "show")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("class java.game.MainMenuDialog method show()V", result.stdout)
        self.assertIn("reference='java.lang.Math.random ()F'", result.stdout)
        self.assertIn("reference='java.game.GameLogic.klampiPatch I'", result.stdout)

    def test_missing_method_is_an_error(self):
        result = run_trace(MENU, "--class", "java.game.MainMenuDialog", "--method", "not_a_method")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("method not found: not_a_method", result.stderr)

    def test_stale_dialog_is_not_skipped(self):
        result = run_trace(STALE, "--method", "show")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CONS unknown kind 10 at entry 5", result.stderr)


if __name__ == "__main__":
    unittest.main()
