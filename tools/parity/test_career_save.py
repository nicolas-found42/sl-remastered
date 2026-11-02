"""Career prefix decoder controls; fabricated bytes are parser fixtures only."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('career_save', Path(__file__).with_name('career_save.py'))
career = importlib.util.module_from_spec(spec)
spec.loader.exec_module(career)


class CareerPrefixTest(unittest.TestCase):
    def fixture(self, name=b'ParserOnly\0', money=42):
        prefix = struct.pack('<IIIifiiI', 0x87654321, 21, 0x100db, money, .3, 0, 0, len(name)) + name
        return b'SDAT\0\1\2\0' + prefix + struct.pack('<III', 0, 0, 8)

    def test_prefix_preserves_fields_and_does_not_claim_whole_file_validation(self):
        row = career.career_prefix(self.fixture())
        self.assertEqual((row['name'], row['money'], row['portrait_word']), ('ParserOnly', 42, 0x100db))
        self.assertEqual(row['prestige_bits'], struct.pack('<f', .3).hex())
        self.assertEqual(row['packs'], [])

    def test_truncation_bad_trailer_and_unsupported_profile_are_rejected(self):
        raw = self.fixture()
        cases = [raw[:10], raw[:-1], b'FAIL' + raw[4:], raw[:12] + struct.pack('<I', 22) + raw[16:]]
        for data in cases:
            with self.subTest(data=data[:16]):
                with self.assertRaises((ValueError, career.sdat.Bad)):
                    career.career_prefix(data)

    def test_name_must_be_bounded_and_exactly_terminated(self):
        for name in (b'', b'not terminated', b'a\0hidden\0', b'a' * 129 + b'\0'):
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    career.career_prefix(self.fixture(name))


if __name__ == '__main__':
    unittest.main()
