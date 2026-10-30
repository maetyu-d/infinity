#!/usr/bin/env python3
import pathlib,sys,json,math
root=pathlib.Path(sys.argv[1]);log=(root/'native.log').read_text()
assert 'PASS: real planet families' in log and log.count('PLANET_VARIETY scene=')==18

def read(name):
 with (root/'captures'/(name+'.ppm')).open('rb') as f:
  assert f.readline().strip()==b'P6';assert f.readline().strip()==b'1440 900';assert f.readline().strip()==b'255';data=f.read()
 assert len(data)==1440*900*3
 return data

def changed(a,b):return sum(max(abs(a[i+c]-b[i+c]) for c in range(3))>5 for i in range(0,len(a),3))
result={}
for family in ('gas','ice-giant','sub-neptune'):
 original,hyperion,hybrid=(read(family+'-'+str(i)) for i in range(3))
 count=changed(original,hyperion);assert count>10000,family+': presentation absent'
 result[family]=dict(mode_changed_pixels=count)
on,off,rebase=read('gas-1'),read('rings-off'),read('rings-rebased')
count=changed(on,off);assert count>10000,'Generated rings are not visibly present'
rms=math.sqrt(sum((on[i]-rebase[i])**2 for i in range(0,len(on),12))/(len(on)/12))
assert rms<5,'Ring or planet changed on render-unit rebase'
for i in range(6):read('solid-'+str(i))
read('hot-giant')
result['rings']=dict(changed_pixels=count,rebase_rms_255=rms)
(root/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2));print('PASS: visible giant families, solid worlds, rings and stable physical views')
