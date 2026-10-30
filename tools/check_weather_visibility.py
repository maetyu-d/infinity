#!/usr/bin/env python3
"""Real rain, snow, dust and storm visibility; same physical camera and terrain."""
import json
import math
import pathlib
import sys
root=pathlib.Path(sys.argv[1])
log=(root/'native.log').read_text()
assert 'PASS: real planetary rain' in log and log.count('WEATHER_VISIBILITY scene=')==15
def read(name):
    with (root/'captures'/(name+'.ppm')).open('rb') as f:
        assert f.readline().strip()==b'P6'
        assert f.readline().strip()==b'1440 900'
        assert f.readline().strip()==b'255'
        data=f.read()
    assert len(data)==1440*900*3
    return data
result={}
for scene in ('rain','snow','dust','storm','vacuum'):
    off,on,rebased=(read(scene+'-'+str(i)) for i in range(3))
    changed=sum(max(abs(on[i+c]-off[i+c]) for c in range(3))>5 for i in range(0,len(on),3))
    errors=[abs(on[i]-rebased[i]) for i in range(0,len(on),12)]
    rms=math.sqrt(sum(e*e for e in errors)/len(errors))
    assert rms<6,scene+': weather shifted or flickered on render-unit rebase'
    if scene=='vacuum':assert off==on,'Airless planet acquired weather effects'
    elif scene=='storm':assert changed>500,'Cloud discharge is not visibly present'
    else:assert changed>100000,scene+': local weather visibility loss is missing'
    result[scene]=dict(changed_pixels=changed,rebase_rms_255=rms)
(root/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
print('PASS: visible real forecasts; vacuum negative control; stable render-unit changes')
