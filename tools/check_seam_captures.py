"""Validate the six native stages of a physical shrinking and selective mining expedition."""
from pathlib import Path
import hashlib
import json
import re
import sys
import zlib

root = Path(sys.argv[1])
hashes = []
for stage in range(6):
    path = root / 'captures' / f'seam-{stage}.ppm'
    raw = path.read_bytes() if path.exists() else zlib.decompress(path.with_suffix('.ppm.z').read_bytes())
    magic, size, maximum, pixels = raw.split(b'\n', 3)
    width, height = map(int, size.split())
    assert magic == b'P6' and maximum == b'255' and len(pixels) == width * height * 3
    assert (width, height) == ((600, 1000) if stage == 5 else (1440, 900))
    assert sum(v > 80 for v in pixels) > 5000, f'Empty expedition stage {stage}'
    hashes.append(hashlib.sha256(raw).hexdigest())
assert len(set(hashes)) == 6
log = (root / 'native.log').read_text()
assert 'PASS: native shrinking seam expedition / 20 KG selective cargo / saved marks' in log
rows = re.findall(r'SEAM_CAPTURE (\d) source=(\S+) hull=(\S+) cargo=(\d+)', log)
assert len(rows) == 6 and len({row[1] for row in rows}) == 1
assert float(rows[0][2]) == float(rows[4][2]) == 12
assert float(rows[2][2]) < float(rows[1][2]) < .12
assert int(rows[4][3]) == int(rows[5][3]) == 20
(root / 'capture-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
print('PASS: six actual surface, tiny rock, mineral, marked seam, full-size mining and portrait journal views')
