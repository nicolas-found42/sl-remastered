#!/usr/bin/env python3
"""Check the native inventory's narrow exception for the stale class."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("natives_called", ROOT / "tools/natives_called.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
reader = module.load_reader()


class StaleClassTest(unittest.TestCase):
    def test_exact_stale_file_is_the_only_skipped_failure(self):
        data = (ROOT / "ref/raw" / module.STALE_PATH).read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stale = root / module.STALE_PATH
            stale.parent.mkdir(parents=True)
            stale.write_bytes(data)
            self.assertIsNone(module.read_class(reader, root, stale))
            other = root / "other.class"
            other.write_bytes(data)
            with self.assertRaises(reader.Bad):
                module.read_class(reader, root, other)
            stale.write_bytes(data + b"changed")
            with self.assertRaises(reader.Bad):
                module.read_class(reader, root, stale)


if __name__ == "__main__":
    unittest.main()
