#!/usr/bin/env python3
"""Inspect finalized PCM without playing it or opening an audio device."""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import sys
import wave


def summarize(path, encoding, channels, rate, container='raw'):
    if encoding not in ('f32le', 's16le'):
        raise ValueError('unsupported PCM encoding')
    if type(channels) is not int or not 1 <= channels <= 32:
        raise ValueError('invalid PCM channel count')
    if type(rate) is not int or not 1 <= rate <= 768000:
        raise ValueError('invalid PCM sample rate')
    path = Path(path)
    before = path.stat()
    width = 4 if encoding == 'f32le' else 2
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    if container == 'wav':
        if encoding != 's16le':
            raise ValueError('WAV capture currently requires signed 16-bit PCM')
        source = wave.open(str(path), 'rb')
        if (source.getnchannels() != channels or source.getframerate() != rate or
                source.getsampwidth() != width or source.getcomptype() != 'NONE'):
            source.close()
            raise ValueError('WAV format differs from declared capture')
        expected = source.getnframes() * channels * width
        read = lambda: source.readframes(16384)
    elif container == 'raw':
        expected = before.st_size
        source = path.open('rb')
        read = lambda: source.read(65536)
    else:
        raise ValueError('unsupported PCM container')
    count = nonzero = size = 0
    square = peak = 0.0
    try:
        for block in iter(read, b''):
            if len(block) % width:
                raise ValueError('truncated PCM sample')
            size += len(block)
            values = array.array('f' if encoding == 'f32le' else 'h')
            values.frombytes(block)
            if sys.byteorder != 'little':
                values.byteswap()
            for raw in values:
                value = raw if encoding == 'f32le' else raw / 32768.0
                if not math.isfinite(value):
                    raise ValueError('nonfinite PCM sample')
                count += 1
                nonzero += value != 0
                square += value * value
                peak = max(peak, abs(value))
    finally:
        source.close()
    after = path.stat()
    stable = lambda st: (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns, st.st_ctime_ns)
    if stable(before) != stable(after):
        raise ValueError('capture changed during inspection')
    if size != expected or size % (width * channels):
        raise ValueError('truncated PCM frame')
    return {'schema': 1, 'encoding': encoding, 'channels': channels, 'rate': rate,
            'container': container, 'bytes': before.st_size, 'pcm_bytes': size,
            'frames': count // channels, 'nonzero_samples': nonzero,
            'peak': peak, 'rms': math.sqrt(square / count) if count else 0.0,
            'sha256': digest.hexdigest()}


def native_capture(run_root):
    """Use the actual opened device format, not an inherited SDL preference."""
    root = Path(run_root)
    candidates = [p for p in root.iterdir() if p.is_dir() and not p.is_symlink()]
    if len(candidates) != 1:
        raise ValueError('expected exactly one native Run')
    run = candidates[0]
    path = run / 'audio-mix.raw'
    if path.is_symlink():
        raise ValueError('symlink audio capture')
    spec_path = path.with_name(path.name + '.spec.jsonl')
    if spec_path.is_symlink():
        raise ValueError('symlink audio format')
    formats = [json.loads(line) for line in spec_path.read_text().splitlines()]
    if (not formats or not all(isinstance(f, dict) for f in formats) or
            any(f != formats[0] for f in formats) or
            type(formats[0].get('schema')) is not int or formats[0]['schema'] != 1 or
            formats[0].get('driver') != 'disk' or
            not {'encoding', 'channels', 'rate', 'sample_frames'} <= formats[0].keys() or
            type(formats[0]['sample_frames']) is not int or formats[0]['sample_frames'] <= 0):
        raise ValueError('missing, changed or unsupported disk capture format')
    spec = formats[0]
    result = summarize(path, spec['encoding'], spec['channels'], spec['rate'])
    result.update(route='sdl-disk', stage='physical_final_mix', complete=True,
                  physical_output=False, sample_frames=spec['sample_frames'],
                  path=str(path.relative_to(root.parent)))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--encoding', choices=('f32le', 's16le'), required=True)
    parser.add_argument('--channels', type=int, required=True)
    parser.add_argument('--rate', type=int, required=True)
    parser.add_argument('--container', choices=('raw', 'wav'), default='raw')
    args = parser.parse_args()
    try:
        result = summarize(args.capture, args.encoding, args.channels, args.rate, args.container)
    except (OSError, ValueError, EOFError, wave.Error) as error:
        print(json.dumps({'status': 'inconclusive', 'reason': str(error)}))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
