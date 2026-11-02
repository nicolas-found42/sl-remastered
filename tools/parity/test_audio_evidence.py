#!/usr/bin/env python3
"""Signal/corruption tests; no sound devices, windows or focus operations."""
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import tempfile
import unittest
import wave

spec = importlib.util.spec_from_file_location('audio_evidence', Path(__file__).with_name('audio_evidence.py'))
audio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audio)


class AudioEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='sl-audio-evidence-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.path = self.root / 'mix.raw'

    def test_float_signal_counts_hash_peak_and_rms(self):
        data = struct.pack('<4f', 0, .25, -.5, 1)
        self.path.write_bytes(data)
        result = audio.summarize(self.path, 'f32le', 2, 48000)
        self.assertEqual(result['frames'], 2)
        self.assertEqual(result['nonzero_samples'], 3)
        self.assertEqual(result['peak'], 1)
        self.assertAlmostEqual(result['rms'], math.sqrt(1.3125 / 4))
        self.assertEqual(result['sha256'], hashlib.sha256(data).hexdigest())

    def test_wave_pcm_and_declared_format_must_agree(self):
        self.path = self.root / 'mix.wav'
        with wave.open(str(self.path), 'wb') as out:
            out.setnchannels(2)
            out.setsampwidth(2)
            out.setframerate(48000)
            out.writeframes(struct.pack('<4h', 0, 8192, -32768, 32767))
        result = audio.summarize(self.path, 's16le', 2, 48000, 'wav')
        self.assertEqual(result['frames'], 2)
        self.assertEqual(result['peak'], 1)
        self.assertEqual(result['nonzero_samples'], 3)
        with self.assertRaises(ValueError):
            audio.summarize(self.path, 's16le', 1, 48000, 'wav')

    def test_partial_frames_and_nonfinite_samples_are_rejected(self):
        for data in (b'123', struct.pack('<3f', 0, 1, 2),
                     struct.pack('<2f', float('nan'), 0), struct.pack('<2f', float('inf'), 0)):
            with self.subTest(data=data):
                self.path.write_bytes(data)
                with self.assertRaises(ValueError):
                    audio.summarize(self.path, 'f32le', 2, 48000)

    def test_silence_is_measured_without_inventing_signal(self):
        self.path.write_bytes(bytes(32))
        result = audio.summarize(self.path, 'f32le', 2, 48000)
        self.assertEqual(result['frames'], 4)
        self.assertEqual(result['nonzero_samples'], 0)
        self.assertEqual(result['peak'], 0)
        self.assertEqual(result['rms'], 0)

    def test_native_format_is_retained_and_changes_are_refused(self):
        run_root = self.root / 'run'
        run = run_root / 'one'
        run.mkdir(parents=True)
        (run_root / 'latest').symlink_to(run, target_is_directory=True)
        (run / 'audio-mix.raw').write_bytes(struct.pack('<4f', .25, .25, .5, .5))
        fmt = {'schema': 1, 'driver': 'disk', 'encoding': 'f32le', 'channels': 2,
               'rate': 48000, 'sample_frames': 960}
        descriptor = run / 'audio-mix.raw.spec.jsonl'
        descriptor.write_text(json.dumps(fmt) + '\n' + json.dumps(fmt) + '\n')
        result = audio.native_capture(run_root)
        self.assertEqual(result['frames'], 2)
        self.assertEqual(result['path'], 'run/one/audio-mix.raw')
        self.assertEqual(result['stage'], 'physical_final_mix')
        descriptor.write_text(json.dumps(fmt) + '\n' + json.dumps(dict(fmt, rate=44100)) + '\n')
        with self.assertRaises(ValueError):
            audio.native_capture(run_root)

    def test_bad_native_descriptors_and_symlinks_are_rejected(self):
        run_root = self.root / 'run'
        run = run_root / 'one'
        run.mkdir(parents=True)
        capture = run / 'audio-mix.raw'
        capture.write_bytes(bytes(16))
        descriptor = run / 'audio-mix.raw.spec.jsonl'
        for data in ('', 'null\n', '{}\n', '{"schema":true}\n'):
            descriptor.write_text(data)
            with self.assertRaises(ValueError):
                audio.native_capture(run_root)
        capture.unlink()
        capture.symlink_to(descriptor)
        with self.assertRaises(ValueError):
            audio.native_capture(run_root)


if __name__ == '__main__':
    unittest.main()
