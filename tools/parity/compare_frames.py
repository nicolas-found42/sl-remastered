#!/usr/bin/env python3
"""Compare two captured game frames; this does not establish gameplay parity."""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageStat


def compare(stock_path, native_path, output, resize_native=False):
    stock_path, native_path, output = map(Path, (stock_path, native_path, output))
    with Image.open(stock_path) as image:
        stock = image.convert('RGB')
    with Image.open(native_path) as image:
        native = image.convert('RGB')
    original_native_size = native.size
    if stock.size != native.size:
        if not resize_native:
            raise ValueError('frame sizes differ; pass --resize-native to scale native to stock size')
        native = native.resize(stock.size, Image.Resampling.BILINEAR)
    output.mkdir(parents=True, exist_ok=False)
    diff = ImageChops.difference(stock, native)
    histogram = diff.histogram()
    pixels = stock.width * stock.height
    # A pixel is different if any one of its three channels differs.
    red, green, blue = diff.split()
    mask = ImageChops.lighter(ImageChops.lighter(red, green), blue)
    changed = pixels - mask.histogram()[0]
    report = {
        'stock': str(stock_path.resolve()), 'native': str(native_path.resolve()),
        'stock_sha256': hashlib.sha256(stock_path.read_bytes()).hexdigest(),
        'native_sha256': hashlib.sha256(native_path.read_bytes()).hexdigest(),
        'stock_size': list(stock.size), 'native_original_size': list(original_native_size),
        'native_resampling': 'bilinear' if original_native_size != stock.size else None,
        'different_pixels': changed, 'total_pixels': pixels,
        'different_pixel_fraction': changed / pixels,
        'mean_absolute_channel_difference': sum(ImageStat.Stat(diff).mean) / 3,
        'max_channel_difference': max((i % 256 for i, n in enumerate(histogram) if n), default=0),
    }
    stock.save(output / 'stock.png')
    native.save(output / 'native.png')
    diff.save(output / 'difference.png')
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stock', help='stock back-buffer image')
    parser.add_argument('native', help='native frame image')
    parser.add_argument('output', help='new output directory')
    parser.add_argument('--resize-native', action='store_true', help='bilinear-scale native to stock dimensions')
    args = parser.parse_args()
    try:
        print(json.dumps(compare(args.stock, args.native, args.output, args.resize_native), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(2, f'compare_frames: {error}\n')


if __name__ == '__main__':
    main()
