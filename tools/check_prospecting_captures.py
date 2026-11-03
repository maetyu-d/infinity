#!/usr/bin/env python3
"""Check real geological-sampling scenes and the extracted cargo sequence."""
from pathlib import Path
import hashlib,json,sys,zlib
root=Path(sys.argv[1]); hashes=[]
for stage in range(6):
 p=root/'captures'/f'prospect-{stage}.ppm'
 raw=p.read_bytes() if p.exists() else zlib.decompress(p.with_suffix('.ppm.z').read_bytes())
 magic,size,maximum,pixels=raw.split(b'\n',3);w,h=map(int,size.split())
 assert magic==b'P6' and maximum==b'255' and len(pixels)==w*h*3
 assert (w,h)==((600,1000) if stage==5 else (1440,900))
 assert sum(v>80 for v in pixels)>5000,f'Empty geological scene {stage}'
 hashes.append({'stage':stage,'sha256':hashlib.sha256(raw).hexdigest()})
assert len({x['sha256'] for x in hashes})==6
log=(root/'native.log').read_text()
assert 'PASS: native geological reconnaissance' in log
assert log.count('PROSPECT_CAPTURE ')==6
assert log.count('OUTCROP_ID ')==3
assert (root/'captures/prospect-view.txt').exists()
(root/'capture-hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
print('PASS: six actual geological reconnaissance, outcrop, survey, extraction, cold-world and portrait views')
