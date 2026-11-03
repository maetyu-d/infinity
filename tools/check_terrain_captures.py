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
records=re.findall(r'TERRAIN_RENDER (\S+) type=(.+?) mode=(\w+) tiles=(\d+) height_m=([\d.]+) shadow=(\d+) terrain=(\d+) geometry=(\d+)',log)
assert len(captures)==len(fixtures)==len(records), 'Missing a generated terrain world'
assert {r[0] for r in records}=={f[0] for f in fixtures}, 'Capture lost an addressed world'
assert all(r[2]=='HYPERION' and int(r[3])>50 and r[5]=='1' and int(r[6]) and int(r[7])
           for r in records), 'Resolved Hyperion terrain, shadows or geometry are missing'
assert {p.name for p in captures}=={
    f'{i}-{r[1]}-HYPERION.ppm' for i,r in enumerate(records)
}, 'Unexpected or missing single-renderer capture'
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
print(f'PASS: {len(fixtures)} generated terrain worlds, {len(captures)} Hyperion images, solid-only patches, resolved relief, sun shadows and retained addresses/geometry')
