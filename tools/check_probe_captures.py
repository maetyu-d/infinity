"""Check actual launcher refit, first-person excavation, cavern entry and portrait feed frames."""
from pathlib import Path
import hashlib,json,re,sys,zlib
root=Path(sys.argv[1]);hashes=[]
log=(root/'native.log').read_text()
rows=re.findall(r'PROBE_CAPTURE (\d+) id=(\d+) depth=(\S+) meshes=(\d+) cut_tiles=(\d+)',log)
assert len(rows) in (9,11,16,20)
for stage in range(len(rows)):
    p=root/'captures'/f'probe-{stage}.ppm'
    raw=p.read_bytes() if p.exists() else zlib.decompress(p.with_suffix('.ppm.z').read_bytes())
    magic,size,maximum,pixels=raw.split(b'\n',3);w,h=map(int,size.split())
    assert magic==b'P6' and maximum==b'255' and len(pixels)==w*h*3
    assert (w,h)==((600,1000) if stage==8 else (1440,900))
    assert sum(v>80 for v in pixels)>5000,f'Empty mining-probe stage {stage}'
    hashes.append(hashlib.sha256(raw).hexdigest())
assert len(set(hashes))==len(rows)
assert 'PASS: native mining probe / turret refit / independent first-person camera / real excavated cavern' in log
assert len({r[1] for r in rows})==1
assert 0<float(rows[2][2])<float(rows[3][2])==float(rows[5][2])
assert int(rows[6][4])>0 and int(rows[7][3])>0
presentation=re.findall(r'PROBE_CAPTURE (\d+).*? rig=(\d+) lit=(\d+) dust=(\d+) flash=(\S+)',log)
if presentation:
    assert len(presentation)==len(rows)
    assert all(int(presentation[n][1])>0 for n in (1,2,3,4,5,8))
    assert all(int(presentation[n][2])>0 for n in range(2,8))
    assert all(int(presentation[n][3])>0 for n in (2,4,5))
    assert float(presentation[4][4])>0 and float(presentation[5][4])==0
(root/'capture-hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
if len(rows)>=11:
    controls=re.findall(r'PROBE_CAPTURE (\d+).*? hidden=(\d+) light_uploads=(\d+)',log)
    assert len(controls)==len(rows) and controls[9][1]==controls[10][1]=='0'
if len(rows)>=16:
    assert log.count('text_overlap=0')==len(rows)
    assert int(presentation[11][2])>0 and int(presentation[12][3])==0
    assert int(presentation[13][3])>0 and float(presentation[14][4])>0 and float(presentation[15][4])==0
if len(rows)==20:
    mining=re.findall(r'PROBE_CAPTURE (\d+).*? mining_wear=(\d+) tailings=(\d+) cutting_particles=(\d+)',log)
    assert len(mining)==20 and int(mining[16][2])==0
    assert all(int(mining[n][1])>0 and int(mining[n][2])>0 for n in (17,18,19))
    assert int(mining[17][3])==int(mining[18][3])==0 and int(mining[19][3])>0
print(f'PASS: {len(rows)} real refit, drilling, cavern, portrait and portal visibility views')
