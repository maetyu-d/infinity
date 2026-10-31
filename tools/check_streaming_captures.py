#!/usr/bin/env python3
"""Check the actual root handoff and terrain pixels from the native fixture."""
from pathlib import Path
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

# Shade colours are not coverage: blue ice can legitimately match blue sky.
# The native fixture rerasterizes the same tile digest with a flat mask shader,
# retaining the actual positions, indices, projection and depth test.
def uncovered(width, data):
    holes = count = 0
    for y in range(300, 880):
        for x in range(30, width - 30):
            i = (y * width + x) * 3
            holes += not (data[i] < 8 and data[i+1] > 180 and data[i+2] < 8)
            count += 1
    return holes / count

ground = {}
for name in ['before', 'HYPERION']:
    read('terrain-' + name + '.ppm')  # All normal presentation captures remain required.
    width, height, data = read('coverage-' + name + '.ppm')
    ratio = uncovered(width, data)
    assert ratio < .001, f'Terrain has uncovered raster pixels in {name}: {ratio}'
    ground[name] = ratio
# A deliberate 64x64 missing block must still fail the same 0.1% bound.
negative = bytearray(data)
for y in range(500, 564):
    start = (y * width + 700) * 3
    negative[start:start + 64*3] = bytes(64*3)
assert uncovered(width, negative) > .001, 'Coverage detector missed a deliberate terrain hole'
report = {'root_rebase_rms_rgb': rms, 'root_rebase_fraction_over_2': fraction,
          'terrain_uncovered_fraction': ground}
print(json.dumps(report, indent=2))
