#!/usr/bin/env python3
"""Check native station-control proof images without external image packages."""
from pathlib import Path
import io
import sys
import zlib

root = Path(sys.argv[1])
frames = []
names = ('swap-helm', 'swap-relief-walking', 'swap-gun-ready', 'swap-gun-fire',
         'swap-gun-aim', 'swap-portrait', 'swap-pilot-unavailable', 'swap-returned-helm',
         'swap-crew-circle', 'swap-crew-evade', 'swap-crew-retreat', 'swap-security-jobs')
for name in names:
    p = root / 'captures' / (name + '.ppm')
    data = p.read_bytes() if p.exists() else zlib.decompress(p.with_suffix('.ppm.z').read_bytes())
    with io.BytesIO(data) as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split())
        assert f.readline().strip() == b'255'
        pixels = f.read()
    assert len(pixels) == w * h * 3, name
    assert (h > w if name in ('swap-portrait', 'swap-pilot-unavailable') else (w, h) == (1440, 900)), name
    assert sum(pixels[i] > 70 and pixels[i + 1] > 90 for i in range(0, len(pixels), 3)) > 5000, name
    frames.append(pixels)
assert len(set(frames)) == len(names)
assert 'PASS: native captain/gunner handover' in (root / 'native.log').read_text()
print('PASS: twelve native helm, stations, crew manoeuvres, portrait and security board frames')
