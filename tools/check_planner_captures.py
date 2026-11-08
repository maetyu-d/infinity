"""Check staged native planner frames; no image fixtures or synthetic screenshots."""
from pathlib import Path
import hashlib, json, re, sys, zlib
root = Path(sys.argv[1])
log = (root / 'native.log').read_text()
rows = re.findall(r'PLANNER_CAPTURE (\d+) stage=(\d+) contract=(\d+) identity=(\d+) text_overlap=0', log)
assert len(rows) == 18
assert [int(r[0]) for r in rows] == list(range(18))
assert len({r[2] for r in rows}) == len({r[3] for r in rows}) == 1
assert 'PASS: native expedition planner / 18 layouts / real assay handover / unchanged ship and world' in log
hashes = []
for stage in range(18):
    path = root / 'captures' / f'planner-{stage}.ppm'
    raw = path.read_bytes() if path.exists() else zlib.decompress(path.with_suffix('.ppm.z').read_bytes())
    magic, size, maximum, pixels = raw.split(b'\n', 3)
    w, h = map(int, size.split())
    assert magic == b'P6' and maximum == b'255' and len(pixels) == w*h*3
    assert (w, h) == ((600, 1000) if stage in (14,17) else (1440, 900))
    assert sum(v > 80 for v in pixels) > 5000
    hashes.append(hashlib.sha256(raw).hexdigest())
assert len(set(hashes)) >= 14
(root / 'capture-hashes.json').write_text(json.dumps(hashes, indent=2)+'\n')
print('PASS: 18 native minimal/detail planner, delivery, portrait and objective-tracker views')
