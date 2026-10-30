import importlib.util
import tempfile
import unittest
from pathlib import Path

from PIL import Image
spec = importlib.util.spec_from_file_location('compare_frames', Path(__file__).with_name('compare_frames.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
compare = module.compare


class CompareFramesTest(unittest.TestCase):
    def test_identical_and_one_changed_pixel(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            a, b = root / 'a.png', root / 'b.png'
            image = Image.new('RGB', (2, 2), (0, 0, 0))
            image.save(a)
            self.assertEqual(compare(a, a, root / 'same')['different_pixels'], 0)
            image.putpixel((0, 0), (30, 0, 0))
            image.save(b)
            result = compare(a, b, root / 'different')
            self.assertEqual(result['different_pixels'], 1)
            self.assertEqual(result['mean_absolute_channel_difference'], 2.5)
            self.assertEqual(result['max_channel_difference'], 30)
            self.assertEqual(Image.open(root / 'different/difference.png').getpixel((0, 0)), (30, 0, 0))
            with self.assertRaises(FileExistsError):
                compare(a, b, root / 'different')

    def test_different_dimensions_require_explicit_scaling(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            a, b = root / 'a.png', root / 'b.png'
            Image.new('RGB', (4, 4), 'red').save(a)
            Image.new('RGB', (2, 2), 'red').save(b)
            with self.assertRaises(ValueError):
                compare(a, b, root / 'refused')
            result = compare(a, b, root / 'scaled', True)
            self.assertEqual(result['native_original_size'], [2, 2])
            self.assertEqual(result['native_resampling'], 'bilinear')
            self.assertEqual(result['different_pixels'], 0)


if __name__ == '__main__':
    unittest.main()
