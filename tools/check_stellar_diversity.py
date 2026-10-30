#!/usr/bin/env python3
"""Check native stellar material/precision evidence without extra dependencies."""
import pathlib,sys,json,math,statistics
root=pathlib.Path(sys.argv[1]);log=(root/'native.log').read_text()
assert 'PASS: addressed stellar families' in log and log.count('STELLAR_VARIETY scene=')==28

def read(name):
 with (root/'captures'/(name+'.ppm')).open('rb') as f:
  assert f.readline().strip()==b'P6';assert f.readline().strip()==b'1440 900';assert f.readline().strip()==b'255';data=f.read()
 assert len(data)==1440*900*3
 return data
# Above the reticle and below the left catalogue; compare photospheres rather
# than the different galaxies, cockpit labels, or procedural neighbouring bodies.
points=[(y*1440+x)*3 for y in range(335,420,2) for x in range(670,785,2)]

def rms(a,b):return math.sqrt(sum((a[i+c]-b[i+c])**2 for i in points for c in range(3))/(len(points)*3))
def variation(a):return statistics.pstdev(a[i] for i in points)
result={'surface_red_channel_stddev_255':{}}
for i in range(10):
 a=read('star-'+str(i));result['surface_red_channel_stddev_255'][str(i)]=variation(a)
 assert max(a[i] for i in points)>25,'Invisible generated star family '+str(i)
for t in 'OBAFGKM':read('spectral-'+t)
for i in (5,6,7,8):
 a=read('star-'+str(i));original=read(f'star-{i}-original');hybrid=read(f'star-{i}-hybrid')
 assert rms(a,original)>2,'Mode presentation absent'
 assert rms(a,hybrid)>1,'Hybrid presentation absent'
result['rebase_rms_255']={}
for i in (6,7):
 error=rms(read('star-'+str(i)),read(f'star-{i}-rebased'))
 assert error<3,'Compact-star material moved with render units'
 result['rebase_rms_255'][str(i)]=error
# Cool convective stars must have resolved cells; compact remnants must avoid
# generic solar mottling. Control ensures the mesh and analytic disc do not add.
assert variation(read('star-2'))>5,'Giant convection washed out'
assert variation(read('star-8'))>3,'Brown-dwarf weather absent'
error=rms(read('star-3'),read('star-3-no-optics'))
assert error<8,'Analytic disc doubles resolved stellar emission'
result['resolved_photosphere_optics_control_rms_255']=error
(root/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2));print('PASS: visible stellar families, three modes, convection, compact precision and exposure control')
