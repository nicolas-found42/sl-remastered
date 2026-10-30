#!/usr/bin/env python3
"""Exercise the raw-disc conversion command with synthetic sectors."""

from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools/wine/prepare_disc.py"
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


def image(count=20):
    payloads = [bytearray([i]) * 2048 for i in range(count)]
    pvd = payloads[16]
    pvd[:7] = b"\x01CD001\x01"
    struct.pack_into("<I", pvd, 80, count)
    struct.pack_into(">I", pvd, 84, count)
    struct.pack_into("<H", pvd, 128, 2048)
    struct.pack_into(">H", pvd, 130, 2048)
    sectors = [SYNC + b"\x00\x00\x00\x01" + p + bytes(288) for p in payloads]
    return b"".join(sectors), b"".join(payloads)


class DiscTest(unittest.TestCase):
    def run_tool(self, source, dest):
        return subprocess.run([sys.executable, "-I", str(TOOL), str(source), str(dest)],
                              capture_output=True, text=True, check=False)

    def test_valid_image_and_existing_output(self):
        raw, expected = image()
        with tempfile.TemporaryDirectory() as directory:
            source, dest = Path(directory) / "source.bin", Path(directory) / "out.iso"
            source.write_bytes(raw)
            result = self.run_tool(source, dest)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(dest.read_bytes(), expected)
            self.assertEqual(source.read_bytes(), raw)
            self.assertNotEqual(self.run_tool(source, dest).returncode, 0)
            self.assertEqual(dest.read_bytes(), expected)

    def test_invalid_images_leave_no_output(self):
        original, _ = image()
        invalid = {
            "truncated": original[:-1],
            "nonmode": original[:15] + b"\x02" + original[16:],
            "bad_sync": b"\x01" + original[1:],
            "bad_pvd": original[:16 * 2352 + 16] + b"\x00" + original[16 * 2352 + 17:],
            "bad_count": original[:16 * 2352 + 16 + 80] + bytes(8) + original[16 * 2352 + 16 + 88:],
            "incomplete_volume": original[:-2352],
        }
        with tempfile.TemporaryDirectory() as directory:
            source, dest = Path(directory) / "source.bin", Path(directory) / "out.iso"
            for name, raw in invalid.items():
                with self.subTest(name=name):
                    source.write_bytes(raw)
                    result = self.run_tool(source, dest)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertTrue(result.stderr)
                    self.assertFalse(dest.exists())
                    self.assertEqual(source.read_bytes(), raw)

    def test_non_mode1_trailing_sectors_are_ignored(self):
        raw, expected = image()
        trailing = bytes(2352) + b"\xff" * 2352
        with tempfile.TemporaryDirectory() as directory:
            source, dest = Path(directory) / "source.bin", Path(directory) / "out.iso"
            source.write_bytes(raw + trailing)
            result = self.run_tool(source, dest)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("2 trailing sectors ignored", result.stdout)
            self.assertIn("audio tracks are not included", result.stdout)
            self.assertEqual(dest.read_bytes(), expected)
            self.assertEqual(source.read_bytes(), raw + trailing)

    def test_ref_destination_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.bin"
            source.write_bytes(image()[0])
            dest = ROOT / "ref" / "must-not-create.iso"
            result = self.run_tool(source, dest)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("outside ref/", result.stderr)
            self.assertFalse(dest.exists())


class SyntheticMetadataTest(unittest.TestCase):
    def tool(self, *args):
        return subprocess.run([sys.executable, "-I", str(ROOT / "tools/wine/synthetic_metadata.py"), *map(str, args)],
                              capture_output=True, text=True, check=False)

    def test_stock_texture_dat_gives_the_table_the_check_compares(self):
        exe, texture = ROOT / "ref/raw/StreetLegal_Redline.bak.exe", ROOT / "ref/raw/texture.dat"
        with tempfile.TemporaryDirectory() as directory:
            machine, report = Path(directory) / "m.txt", Path(directory) / "r.txt"
            result = self.tool(exe, texture, machine, report)
            self.assertEqual(result.returncode, 0, result.stderr)
            lines = machine.read_text().split("\n")
            self.assertEqual(lines[0], "11 4557237")
            self.assertEqual(lines[1].split()[1:], ["1270401"] + ["213401"] * 3 + ["32201"] * 6)
            self.assertIn("SYNTHETIC", report.read_text())

    def test_garbage_texture_is_rejected(self):
        exe = ROOT / "ref/raw/StreetLegal_Redline.bak.exe"
        with tempfile.TemporaryDirectory() as directory:
            bad = Path(directory) / "texture.dat"
            bad.write_bytes(bytes(76))
            result = self.tool(exe, bad, Path(directory) / "m", Path(directory) / "r")
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((Path(directory) / "m").exists())


if __name__ == "__main__":
    unittest.main()
