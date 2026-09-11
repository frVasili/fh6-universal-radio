"""Independently decode artwork_codec_test's BC7 fixture using Pillow."""
from PIL import Image
import struct
import io
from pathlib import Path
import sys
payload = Path(sys.argv[1]).read_bytes()
header = [124, 0x81007, 392, 392, len(payload), 0, 1] + [0] * 11
header += [32, 4, 0x30315844, 0, 0, 0, 0, 0] + [0x1000, 0, 0, 0, 0]
raw = b'DDS ' + struct.pack('<31I', *header) + struct.pack('<5I', 98, 3, 0, 1, 0) + payload
im = Image.open(io.BytesIO(raw))
im.load()
assert im.size == (392, 392)
# BC7 is lossy: one endpoint quantization step is acceptable.
for position, expected in [((10, 10), (255, 0, 0, 255)), ((300, 10), (0, 0, 0, 0))]:
    actual = im.getpixel(position)
    assert all(abs(a - b) <= 2 for a, b in zip(actual, expected)), actual
print('Independent DDS decode: PASS (cover colors and transparent padding)')
