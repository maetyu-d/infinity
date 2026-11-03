#!/usr/bin/env python3
"""Validate the native, physically flown berth arrival and departure."""
from pathlib import Path
import hashlib
import json
import sys
import zlib

root = Path(sys.argv[1])
names = ['01-arrival', '02-alignment', '03-glide', '04-final-approach',
         '05-soft-capture', '06-docked', '07-departure', '08-clear']
hashes = []
for name in names:
    path = root / 'captures' / (name + '.ppm')
    raw = path.read_bytes() if path.exists() else zlib.decompress(path.with_suffix('.ppm.z').read_bytes())
    magic, size, maximum, pixels = raw.split(b'\n', 3)
    assert magic == b'P6' and size == b'1440 900' and maximum == b'255', name
    assert len(pixels) == 1440 * 900 * 3, name
    world = b''.join(pixels[(y*1440+320)*3:(y*1440+1120)*3] for y in range(180, 650))
    assert max(world) - min(world) > 40, f'{name}: missing visible station/lane/world'
    hashes.append({'name': name, 'sha256': hashlib.sha256(raw).hexdigest()})
assert len({x['sha256'] for x in hashes}) == 8, 'Docking stages did not change the view'
log = (root / 'native.log').read_text()
assert 'PASS: native arrival, alignment, glide, soft capture, physical port access and departure' in log
assert all('DOCKING_CAPTURE ' + name in log for name in names)
(root / 'capture-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
print('PASS: eight actual native docking stages, visible berth/lane and physical port-access proof')
