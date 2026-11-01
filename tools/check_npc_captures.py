#!/usr/bin/env python3
from pathlib import Path
import io,sys,zlib
root=Path(sys.argv[1]);images={}
for name in ('npc-attack','npc-evade','npc-retreat','npc-patrol-friendly','npc-patrol-response','npc-patrol-portrait'):
    p=root/'captures'/(name+'.ppm')
    raw=p.read_bytes() if p.exists() else zlib.decompress(p.with_suffix('.ppm.z').read_bytes())
    with io.BytesIO(raw) as f:
        assert f.readline().strip()==b'P6'
        w,h=map(int,f.readline().split());assert f.readline().strip()==b'255';pixels=f.read()
    assert len(pixels)==w*h*3
    assert (h>w if 'portrait' in name else (w,h)==(1440,900)),name
    assert sum(pixels[i]>100 and pixels[i+1]>70 for i in range(0,len(pixels),3))>3000,name
    images[name]=pixels
assert images['npc-attack']!=images['npc-evade']!=images['npc-retreat']
assert images['npc-patrol-friendly']!=images['npc-patrol-response']
log=(root/'native.log').read_text()
assert 'PASS: native NPC acquisition, incoming damage, evasion, low-hull retreat, patrol retaliation, persistent IDs and portrait combat HUD' in log
assert 'NPC_PROOF attack_shots=' in log
print('PASS: six native attack, evasion, retreat, patrol response and fitted combat HUD images')
