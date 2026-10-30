import hashlib
from pathlib import Path
import struct
import sys
root, output = map(Path, sys.argv[1:])
if not root.is_dir():
    raise SystemExit(f"DDS corpus directory is unavailable: {root}")
fixtures = []
for format_code, block_size in [(0x31545844, 8), (0x33545844, 16), (0x35545844, 16)]:
    data = bytearray(128)
    for offset, value in [(0, 0x20534444), (4, 124), (12, 5), (16, 7), (28, 3), (76, 32), (80, 4), (84, format_code)]:
        struct.pack_into('<I', data, offset, value)
    width, height = 7, 5
    for _ in range(3):
        data.extend(bytes(((width + 3) // 4) * ((height + 3) // 4) * block_size))
        width, height = max(width // 2, 1), max(height // 2, 1)
    fixtures.append((f'synthetic-{format_code:08x}', bytes(data)))
for path in sorted(root.rglob('*')):
    if path.is_file() and path.suffix.lower() == '.dds':
        fixtures.append((str(path.relative_to(root)), path.read_bytes()))
if len(fixtures) == 3:
    raise SystemExit(f"DDS corpus contains no DDS files: {root}")
output.mkdir(parents=True, exist_ok=True)
with (output / 'fixtures.bin').open('wb') as file, (output / 'manifest.tsv').open('w') as manifest:
    file.write(struct.pack('<I', len(fixtures)))
    manifest.write('case\tpath\tsize\tsha256\n')
    for i, (name, data) in enumerate(fixtures):
        file.write(struct.pack('<I', len(data)))
        file.write(data)
        manifest.write(f'{i}\t{name}\t{len(data)}\t{hashlib.sha256(data).hexdigest()}\n')
print(f'{len(fixtures)} DDS layout fixtures')
