"""Exact comparison controls; these tiny fabricated SDATs are parser fixtures only."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('stock_career', Path(__file__).with_name('stock_career_save.py'))
career = importlib.util.module_from_spec(spec)
spec.loader.exec_module(career)


class StockCareerCompareTest(unittest.TestCase):
    def fixture(self, time=43240.0, money=20000, portrait=0x100f0):
        name = b'SaveProof\0'
        prefix = struct.pack('<IIIifiiI', 0x87654321, 21, portrait, money, .3, 0, 0, len(name)) + name
        tail = struct.pack('<ifi7i', 0, time, 1, *([0] * 7))
        pack = b'frontend.rpk\0'
        trailer = struct.pack('<IIII', 0, 1, 1, len(pack)) + pack
        return b'SDAT\0\1\2\0' + prefix + tail + trailer + struct.pack('<I', len(trailer))

    def test_actual_time_words_remain_an_exact_reported_divergence(self):
        raw = self.fixture()
        same = career.compare_careers(raw, raw)
        self.assertTrue(same['exact_resave'])
        report = career.compare_careers(raw, self.fixture(time=43320.0))
        self.assertFalse(report['exact_resave'])
        self.assertTrue(report['changed_byte_offsets'])
        self.assertTrue(report['all_other_file_bytes_exact'])
        self.assertEqual((report['serialized_current_time']['before'], report['serialized_current_time']['after']), (43240.0, 43320.0))

    def test_changed_profile_asset_binding_or_reserved_tail_is_rejected(self):
        raw = self.fixture()
        for bad in (self.fixture(money=19999), self.fixture(portrait=0x100db),
                    raw.replace(b'frontend.rpk', b'frontenv.rpk')):
            with self.assertRaises(ValueError):
                career.compare_careers(raw, bad)
        bad = bytearray(raw)
        end = 8 + career.native.sdat.parse(raw)[2]
        bad[end - 4] = 1
        with self.assertRaisesRegex(ValueError, 'unexplained exact'):
            career.compare_careers(raw, bytes(bad))

    def test_nonfinite_or_out_of_range_time_is_not_a_valid_observation(self):
        for time in (float('nan'), float('inf'), -1, 86400):
            with self.assertRaises(ValueError):
                career.compare_careers(self.fixture(), self.fixture(time=time))


if __name__ == '__main__':
    unittest.main()
