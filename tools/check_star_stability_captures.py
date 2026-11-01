#!/usr/bin/env python3
"""Measure native temporal brightness of the same addressed star apertures."""
from pathlib import Path
import json, math, statistics, sys, zlib
root = Path(sys.argv[1])
def read(name):
 p=root/'captures'/(name+'.ppm')
 data=p.read_bytes() if p.exists() else zlib.decompress(p.with_suffix('.ppm.z').read_bytes())
 magic,size,depth,pixels=data.split(b'\n',3);w,h=map(int,size.split())
 assert magic==b'P6' and depth==b'255' and (w,h)==(1440,900) and len(pixels)==w*h*3
 return pixels
probes=json.loads((root/'captures/probes.json').read_text())['probes']
log=(root/'native.log').read_text();assert 'PASS: native fixed-ship star stability' in log
frames={mode:[read(mode+'-idle-'+str(i)) for i in range(16)] for mode in ('legacy','filtered')}
static={}
for mode in frames:
 a,b=read(mode+'-still-0'),read(mode+'-still-1')
 delta=[max(abs(a[i+c]-b[i+c]) for c in range(3)) for i in range(0,len(a),3)]
 static[mode]=dict(max_delta_255=max(delta),changed_pixels=sum(d>0 for d in delta))
 # Permit one output code value from floating-point/8-bit rounding; require
 # every pixel to stay below visible contrast, including all 64 star probes.
 assert max(delta)<=1 and sum(d>0 for d in delta)<len(delta)*.00001, mode+': stationary stars flicker'

def luminance(data,x,y):
 i=(y*1440+x)*3
 return .299*data[i]+.587*data[i+1]+.114*data[i+2]
def aperture(data,x,y):
 x,y=round(x),round(y);inside=[];ring=[]
 for j in range(-13,14):
  for i in range(-13,14):
   r2=i*i+j*j
   if r2<=64:inside.append(luminance(data,x+i,y+j))
   elif 100<=r2<=169:ring.append(luminance(data,x+i,y+j))
 bg=statistics.median(ring)
 return sum(inside)-bg*len(inside),max(inside)-bg
records=[]
for x,y,identity in probes:
 values={mode:[aperture(f,x,y) for f in images] for mode,images in frames.items()}
 means={m:statistics.mean(v[0] for v in a) for m,a in values.items()}
 if max(v[1] for v in values['legacy'])<25 or min(means.values())<100:continue
 cv={m:statistics.pstdev(v[0] for v in a)/means[m] for m,a in values.items()}
 records.append(dict(id=identity,x=x,y=y,mean_flux=means,coefficient_of_variation=cv))
assert len(records)>=12, 'Too few visible real-star apertures: '+str(len(records))
med={m:statistics.median(r['coefficient_of_variation'][m] for r in records) for m in frames}
result=dict(stars_measured=len(records),median_cv=med,improvement=1-med['filtered']/max(med['legacy'],1e-12),static_frames=static,stars=records)
(root/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='stars'},indent=2))
assert med['filtered']<med['legacy']*.7, 'Native idle engine star modulation did not fall enough'
print('PASS: stable frozen frames within one display code value; same real-star apertures substantially steadier under idle engine vibration')
