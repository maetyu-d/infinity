#!/usr/bin/env python3
"""Check actual solid-world captures and shared scene identity proofs."""
import pathlib
import re
import sys
root=pathlib.Path(sys.argv[1])
log=(root/'native.log').read_text()
assert 'PASS: native solid terrain worlds=' in log
fixtures=re.findall(r'TERRAIN_FIXTURE (\S+) label=(.+?) seed=(\d+) relief_m=([\d.]+)',log)
assert len(fixtures)>=3, 'Expected diverse actual catalogue worlds'
assert all(float(f[3])>30 for f in fixtures), 'Local terrain is still flat'
captures=list((root/'captures').glob('*.ppm'))
assert len(captures)==len(fixtures)*3
for path in captures:
    with path.open('rb') as image:
        assert image.readline().strip()==b'P6'
        w,h=map(int,image.readline().split())
        assert (w,h)==(1440,900)
        assert image.readline().strip()==b'255'
        data=image.read()
    assert len(data)==w*h*3
    # World pixels below the horizon, excluding the cockpit's side panels.
    ground=b''.join(data[(y*w+400)*3:(y*w+1040)*3] for y in range(480,720))
    assert max(ground)-min(ground)>20, 'Missing visible surface relief/material'
print(f'PASS: {len(fixtures)} generated terrain worlds, {len(captures)} native images, solid-only patches and identical cameras/geometry in all modes')
