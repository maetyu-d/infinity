#!/usr/bin/env python3
"""Real legacy/rugged worlds: approach heights, lighting and material budgets."""
import csv
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
width, height = map(int, sys.argv[2:4]) if len(sys.argv) > 2 else (1440,900)
log = (root / 'native.log').read_text()
assert 'PASS: native solid terrain worlds=2 modes=3' in log
records = re.findall(r'TERRAIN_RENDER (\S+) type=(.+?) mode=(\w+) tiles=(\d+) height_m=([\d.]+) shadow=(\d+) terrain=(\d+) geometry=(\d+)', log)
assert len(records) == 18, 'Missing a world / height / rendering mode'
scenes = {}
for address, kind, mode, tiles, altitude, shadow, terrain, geometry in records:
    scenes.setdefault((address, altitude), []).append((mode, terrain, geometry))
    assert int(tiles) > 50
    assert int(shadow) == (mode != 'ORIGINAL'), 'Terrain sun map missing or changed Original mode'
assert len(scenes) == 6
for scene in scenes.values():
    assert {r[0] for r in scene} == {'ORIGINAL', 'HYPERION', 'HYBRID'}
    assert len({r[1:] for r in scene}) == 1, 'Mode changed terrain or planet geometry'
images = sorted((root / 'captures').glob('*.ppm'))
assert len(images) == 18
for path in images:
    with path.open('rb') as image:
        assert image.readline().strip() == b'P6'
        w, h = map(int, image.readline().split())
        assert (w, h) == (width, height) and image.readline().strip() == b'255'
        data = image.read()
    assert len(data) == w * h * 3
    ground = b''.join(data[(y*w+int(w*400/1440))*3:(y*w+int(w*1040/1440))*3] for y in range(int(h*480/900), int(h*720/900)))
    if 'HYPERION' in path.name:
        assert max(ground) - min(ground) > 20, 'Surface lost all visible shading/material'
    assert max(ground) > 10, 'Missing surface'
frames = list(csv.DictReader((root / 'frames.csv').open()))
assert max(int(f['material_bytes']) for f in frames) <= 1398100 + 256*256*4
assert max(int(f['effect_bytes']) for f in frames) <= width*height*12 + (width//3)*(height//3)*16 + 8388608
assert any(int(f['shadow_updates']) for f in frames)
print('PASS: 18 native views at 250 m, 2 km and 11.8 km; identical planet/terrain meshes across modes; sun maps active; fixed texture budgets')
