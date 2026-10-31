#!/usr/bin/env python3
"""Regression for low-sun receiver precision in the actual saved camera."""
import pathlib
import re
import sys
root = pathlib.Path(sys.argv[1])
records = []
for mode in ('on', 'off'):
    log = (root/mode/'native.log').read_text()
    assert 'PASS: native solid terrain worlds=1 renderer=HYPERION' in log
    rows = re.findall(r'TERRAIN_RENDER (\S+) type=.+? mode=(\w+) tiles=(\d+) height_m=([\d.]+) shadow=(\d+) terrain=(\d+) geometry=(\d+)', log)
    assert len(rows) == 1 and all(int(r[2]) > 50 for r in rows)
    assert all(int(r[4]) == (mode == 'on') for r in rows)
    records.append([(r[0],r[1],r[5],r[6]) for r in rows])
assert records[0] == records[1], 'Shadow toggle changed geometry or identity'
def read(path):
    with path.open('rb') as f:
        assert f.readline().strip() == b'P6'
        w,h = map(int,f.readline().split())
        assert (w,h) == (2840,1800) and f.readline().strip() == b'255'
        data = f.read()
    assert len(data) == w*h*3
    # Terrain under grazing sunlight, avoiding cockpit, HUD and horizon.
    return b''.join(data[(y*w+600)*3:(y*w+1200)*3] for y in range(900,1400))
for mode in ('HYPERION',):
    a = read(root/'on'/'captures'/f'0-Plains-{mode}.ppm')
    b = read(root/'off'/'captures'/f'0-Plains-{mode}.ppm')
    error = sum(abs(x-y) for x,y in zip(a,b)) / len(a)
    assert error < .35, f'Grazing sunlight produced false surface shadows: {error:.3f}/255'
    print(f'{mode}: mean shadow-on/off ground difference {error:.4f}/255')
print('PASS: precise surface receivers, low-sun self-shadow suppression, fixed camera and immutable geometry')
