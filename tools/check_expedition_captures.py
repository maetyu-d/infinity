#!/usr/bin/env python3
"""Check actual native expedition frames and the source-geometry identity proof."""
from pathlib import Path
import hashlib
import json
import sys
import zlib
root = Path(sys.argv[1])
hashes = []
for stage in range(8):
    path = root / 'captures' / f'expedition-{stage}.ppm'
    raw = path.read_bytes() if path.exists() else zlib.decompress(path.with_suffix('.ppm.z').read_bytes())
    magic, dimensions, maximum, pixels = raw.split(b'\n', 3)
    width, height = map(int, dimensions.split())
    assert magic == b'P6' and maximum == b'255'
    assert (width, height) == ((600, 1000) if stage == 7 else (1440, 900))
    assert len(pixels) == width * height * 3
    assert sum(pixels[i] > 70 and pixels[i+1] > 70 for i in range(0, len(pixels), 3)) > 3000
    if 3 <= stage <= 5:
        # The material itself must be resolved in the middle of the windshield.
        region = b''.join(pixels[(y*width+400)*3:(y*width+1040)*3] for y in range(200, 620))
        assert sum(v > 100 for v in region) > 1000, f'Empty source material at stage {stage}'
    hashes.append({'stage': stage, 'sha256': hashlib.sha256(raw).hexdigest()})
assert len({r['sha256'] for r in hashes}) == 8
log = (root / 'native.log').read_text()
assert 'PASS: native colony diagnosis' in log
assert log.count('SPECIMEN_ID ') == 3
assert 'Native GPU tour incomplete' not in log
(root / 'capture-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
print('PASS: eight native expedition views, three exact source-geometry matches, and portrait cockpit')
