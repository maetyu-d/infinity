#!/usr/bin/env python3
"""Check the native combat, damage-control and recovery sequence."""
from pathlib import Path
import hashlib,io,sys,zlib
root=Path(sys.argv[1])
names=('ops-hold-fire','ops-autonomous-gunners','ops-targeted-engines','ops-component-damage',
       'ops-crew-repaired','ops-wreck-approach','ops-recovery-progress','ops-salvage-rescued',
       'ops-rescue-port','ops-rescue-crew-office')
hashes=set()
for name in names:
    p=root/'captures'/(name+'.ppm')
    raw=p.read_bytes() if p.exists() else zlib.decompress(p.with_suffix('.ppm.z').read_bytes())
    with io.BytesIO(raw) as f:
        assert f.readline().strip()==b'P6',name
        w,h=map(int,f.readline().split());assert(w,h)==(1440,900),name
        assert f.readline().strip()==b'255';pixels=f.read()
    assert len(pixels)==w*h*3
    assert sum(pixels[i]>70 and pixels[i+1]>80 for i in range(0,len(pixels),3))>5000,name
    hashes.add(hashlib.sha256(raw).hexdigest())
assert len(hashes)==len(names)
assert 'PASS: native autonomous guns' in (root/'native.log').read_text()
print('PASS: ten distinct native autonomous-gunner, subsystem, repair, wreck, recovery and survivor handover frames')
