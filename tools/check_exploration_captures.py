#!/usr/bin/env python3
"""Validate the native survey, equipment, extraction and journal sequence."""
from pathlib import Path
import hashlib, sys, zlib
root=Path(sys.argv[1])
names=['survey-equipment','survey-orbit-scan','survey-world-journal','survey-contract-paid',
       'survey-surface-scan','survey-mining-beam','survey-extracted-cargo','survey-journal-portrait']
hashes=set()
for name in names:
    p=root/'captures'/(name+'.ppm')
    raw=p.read_bytes() if p.exists() else zlib.decompress(p.with_suffix('.ppm.z').read_bytes())
    magic, dimensions, maximum, pixels=raw.split(b'\n',3)
    assert magic==b'P6' and maximum==b'255',name
    w,h=map(int,dimensions.split())
    assert (w,h)==((600,1000) if name.endswith('portrait') else (1440,900)),name
    assert len(pixels)==w*h*3,name
    assert sum(pixels[i]>70 and pixels[i+1]>70 for i in range(0,len(pixels),3))>3000,name
    hashes.add(hashlib.sha256(raw).hexdigest())
assert len(hashes)==len(names)
assert 'PASS: native survey' in (root/'native.log').read_text()
print('PASS: eight native scanner, journal, funded reward, physical mining and portrait captures')
