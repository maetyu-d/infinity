#!/usr/bin/env python3
from pathlib import Path
import io
import sys
import zlib

root = Path(sys.argv[1])
frames = []
for name in ('crew-flight-stations', 'crew-flight-orders', 'crew-flight-emergency',
             'crew-flight-response', 'crew-flight-portrait'):
    p = root / 'captures' / (name + '.ppm')
    data = p.read_bytes() if p.exists() else zlib.decompress(p.with_suffix('.ppm.z').read_bytes())
    with io.BytesIO(data) as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split())
        assert f.readline().strip() == b'255'
        pixels = f.read()
    assert len(pixels) == w * h * 3
    assert (h > w if name.endswith('portrait') else (w, h) == (1440, 900)), name
    assert sum(pixels[i] > 70 and pixels[i+1] > 90 for i in range(0, len(pixels), 3)) > 5000, name
    frames.append(pixels)
assert len(set(frames)) == 5
assert 'PASS: native live helm and roll, crew orders' in (root / 'native.log').read_text()
print('PASS: five live-flight crew console, orders, emergency, response and portrait frames')
