#!/usr/bin/env python3
"""Check the actual root handoff and terrain pixels from the native fixture."""
from pathlib import Path
from collections import Counter
import json
import math
import sys

folder = Path(sys.argv[1])

def read(name):
    with (folder / name).open('rb') as stream:
        assert stream.readline().strip() == b'P6'
        width, height = map(int, stream.readline().split())
        assert (width, height) == (1920, 1080)
        assert stream.readline().strip() == b'255'
        data = stream.read()
        assert len(data) == width * height * 3
    return width, height, data

a, b = read('root-a.ppm'), read('root-b.ppm')
width, height, first = a
second = b[2]
squared = 0
changed = 0
samples = 0
for y in range(230, 900):
    row = y * width * 3
    for offset in range(row, row + width * 3):
        delta = first[offset] - second[offset]
        squared += delta * delta
        changed += abs(delta) > 2
        samples += 1
rms = math.sqrt(squared / samples)
fraction = changed / samples
assert rms < 1 and fraction < .005, f'Root rebase changed the image: RMS={rms}, fraction={fraction}'

ground = {}
for name in ['terrain-before.ppm', 'terrain-HYPERION.ppm', 'terrain-HYBRID.ppm', 'terrain-ORIGINAL.ppm']:
    width, height, data = read(name)
    sky = Counter(tuple(data[(y * width + x) * 3:(y * width + x) * 3 + 3])
                  for y in range(0, 20, 2) for x in range(0, width, 4)).most_common(1)[0][0]
    holes = 0
    count = 0
    for y in range(300, 880):
        for x in range(30, width - 30):
            offset = (y * width + x) * 3
            holes += all(abs(data[offset + c] - sky[c]) <= 3 for c in range(3))
            count += 1
    ratio = holes / count
    assert ratio < .001, f'Terrain has uncovered pixels in {name}: {ratio}'
    ground[name] = ratio
report = {'root_rebase_rms_rgb': rms, 'root_rebase_fraction_over_2': fraction,
          'terrain_uncovered_fraction': ground}
print(json.dumps(report, indent=2))
