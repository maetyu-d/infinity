#!/usr/bin/env python3
"""Check native city, inhabitants, colony and weather captures and identity proofs."""
import pathlib
import re
import sys
import csv

root=pathlib.Path(sys.argv[1])
log=(root/'native.log').read_text()
assert 'PASS: native weather, city, residents and sealed colony' in log
rows=re.findall(r'SURFACE_RENDER (\S+) mode=(\S+) sites=(\d+) buildings=(\d+) residents=(\d+) triangles=(\d+) cloud_cells=(\d+) cpu_bytes=(\d+) gpu_bytes=(\d+)',log)
names=['weather-orbit','city-approach','inhabitants','sealed-colony','precipitation','organic-overview','planned-overview']
assert len(rows)==7, 'Missing seven Hyperion views'
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
shadows=re.findall(r'SURFACE_RENDER (\S+) mode=(\S+).*shadow_bytes=(\d+) shadow_active=(\d+) shadow_updates=(\d+)',log)
assert len(shadows)==7
assert all(int(row[2]) in {0,8*1024*1024} for row in shadows), 'Shadow map storage is unbounded'
assert all(int(row[3])==1 for row in shadows if row[0]=='city-approach'), 'City sunlight shadows were not rendered'
life=re.findall(r'life_worlds=(\d+) life_scanning=(\d+)',log)
assert len(life)==7 and all(int(row[0])>0 for row in life), 'The HUD failed to detect real inhabited surfaces'
profile=list(csv.DictReader((root/'frames.csv').open()))
# Hyperion now renders during initial streaming as well. Terrain morphing
# deliberately invalidates shadows then; check reuse in the captured, settled
# views and their following readback frames rather than counting loading draws.
captures=re.findall(r'SURFACE_RENDER (\S+) .*frame=(\d+) shadow_bytes=\d+ shadow_active=(\d+) shadow_updates=(\d+)',log)
assert len(captures)==7
cached=0
for name,frame,active,updated in captures:
    if int(active):
        window=[r for r in profile if int(frame)<=int(r['frame'])<int(frame)+6]
        assert len(window)==6 and all(int(r['shadow_active'])==1 and int(r['shadow_updates'])==0 for r in window), name+': settled city shadows were not cached'
        assert int(updated)==0
        cached+=len(window)
assert cached>=24, 'Too few settled shadow frames to verify cache reuse'
assert all(int(row['effect_bytes'])-int(row['shadow_bytes']) in {0,21600000} for row in profile), 'Unexpected targets beyond scene, bloom and half-resolution polar light and capped cloud volume'
print('PASS: seven real city/weather views, seven native captures, connected city morphologies, residents, bounded caches, cached sun shadows and Hyperion geometry/camera/city-data proofs')
