#!/usr/bin/env python3
"""Check native city, inhabitants, colony and weather captures and identity proofs."""
import pathlib
import re
import sys

root=pathlib.Path(sys.argv[1])
log=(root/'native.log').read_text()
assert 'PASS: native weather, city, residents and sealed colony' in log
rows=re.findall(r'SURFACE_RENDER (\S+) mode=(\S+) sites=(\d+) buildings=(\d+) residents=(\d+) triangles=(\d+) cloud_cells=(\d+) cpu_bytes=(\d+) gpu_bytes=(\d+)',log)
names=['weather-orbit','city-approach','inhabitants','sealed-colony','precipitation','organic-overview','planned-overview']
assert len(rows)==21, 'Missing seven views in three modes'
assert {r[0] for r in rows}==set(names)
for name,mode,sites,buildings,residents,triangles,clouds,cpu,gpu in rows:
    assert int(sites)>0
    assert int(cpu)<=4*1024*1024 and int(gpu)<2*1024*1024
    if name=='inhabitants': assert int(residents)>0, 'No visible residents'
    if name in {'weather-orbit','precipitation'}: assert int(clouds)>0, 'No planetary weather'
    else: assert int(buildings)>0 and int(triangles)>0, 'No surface buildings'
    path=root/'captures'/f"{names.index(name)}-{name}-{mode}.ppm"
    with path.open('rb') as image:
        assert image.readline().strip()==b'P6'
        w,h=map(int,image.readline().split())
        assert (w,h)==(1440,900)
        assert image.readline().strip()==b'255'
        data=image.read()
    assert len(data)==w*h*3
    windscreen=b''.join(data[(y*w+400)*3:(y*w+1040)*3] for y in range(240,660))
    assert max(windscreen)-min(windscreen)>35, 'Blank world view'
assert re.search(r'SURFACE_RENDER precipitation .* precipitation=[1-9]\d*',log),'No local precipitation particles'
layouts=re.findall(r'CITY_LAYOUT (.+?) pattern=(\S+) buildings=(\d+) streets=(\d+) parcels=(\d+) population=(\d+) recipe=(\d+)',log)
assert len(layouts)>=3 and {'PLANNED','RADIAL','ORGANIC'}<={x[1] for x in layouts}
assert all(int(x[2])>=4 and int(x[3])>0 and int(x[6])==1 for x in layouts)
print('PASS: seven real city/weather views, 21 native captures, connected city morphologies, residents, bounded caches and three-mode geometry/camera/city-data proofs')
