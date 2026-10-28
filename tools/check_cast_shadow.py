#!/usr/bin/env python3
"""A real height-field ridge must cast a shadow, with the same scene on/off."""
import pathlib
import re
import sys
root = pathlib.Path(sys.argv[1])
proof=[]
for mode in ('on','off'):
    log=(root/mode/'native.log').read_text()
    rows=re.findall(r'TERRAIN_RENDER (\S+) type=.+? mode=(\w+) tiles=(\d+) height_m=([\d.]+) shadow=(\d+) terrain=(\d+) geometry=(\d+)',log)
    assert len(rows)==3 and all(int(r[2])>50 for r in rows)
    assert all(int(r[4])==(mode=='on' and r[1]!='ORIGINAL') for r in rows)
    proof.append([(r[0],r[1],r[5],r[6]) for r in rows])
assert proof[0]==proof[1], 'Sunlight changed the terrain geometry'
def read(path):
    with path.open('rb') as f:
        assert f.readline().strip()==b'P6'
        w,h=map(int,f.readline().split())
        assert (w,h)==(1440,900) and f.readline().strip()==b'255'
        data=f.read()
    assert len(data)==w*h*3
    return b''.join(data[(y*w+500)*3:(y*w+940)*3] for y in range(350,650))
for mode in ('ORIGINAL','HYPERION','HYBRID'):
    a=read(root/'on'/'captures'/f'0-Highlands-{mode}.ppm')
    b=read(root/'off'/'captures'/f'0-Highlands-{mode}.ppm')
    delta=[abs(x-y) for x,y in zip(a,b)]
    if mode=='ORIGINAL': assert max(delta)==0
    elif mode=='HYPERION': assert sum(v>3 for v in delta)>1000 and max(delta)>15, 'Known terrain occluder cast no visible shadow'
    else: assert sum(v>0 for v in delta)>200 and max(delta)>1, 'Hybrid lost the terrain shadow'
    print(f'{mode}: {sum(v>3 for v in delta)} components differ >3/255; maximum {max(delta)}/255')
print('PASS: a real generated ridge casts sunlight shadows without changing geometry, cameras or Original mode')
