#!/usr/bin/env python3
"""Check completed native crew frames, preserving lossless capture compression."""
from pathlib import Path
import io
import sys
import zlib

root = Path(sys.argv[1])
images = []
for name in ('crew-stations', 'crew-emergency', 'crew-responders', 'crew-recovered', 'crew-portrait'):
    path = root / 'captures' / (name + '.ppm')
    raw = path.read_bytes() if path.exists() else zlib.decompress(path.with_suffix('.ppm.z').read_bytes())
    with io.BytesIO(raw) as stream:
        assert stream.readline().strip() == b'P6'
        width, height = map(int, stream.readline().split())
        assert stream.readline().strip() == b'255'
        pixels = stream.read()
    assert len(pixels) == width * height * 3
    assert (height > width if name == 'crew-portrait' else (width, height) == (1440, 900)), name
    assert sum(pixels[i] > 80 and pixels[i+1] > 80 for i in range(0, len(pixels), 3)) > 5000, name
    images.append(pixels)
assert len(set(images)) == len(images)
assert 'PASS: native crew stations, hull-hit fire and injuries' in (root / 'native.log').read_text()
print('PASS: five native crew roster, emergency, moving responders, recovery and portrait images')
