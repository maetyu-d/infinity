#!/usr/bin/env python3
"""Check the real market panel, trade images and portrait capture (stdlib only)."""
from pathlib import Path
import sys
root=Path(sys.argv[1]);images={}
for name in ('market-hyperion','market-cargo','market-original','market-hybrid','market-portrait','market-live-supply'):
    with (root/'captures'/(name+'.ppm')).open('rb') as f:
        assert f.readline().strip()==b'P6'
        w,h=map(int,f.readline().split());assert f.readline().strip()==b'255';pixels=f.read()
    assert len(pixels)==w*h*3
    if 'portrait' in name or 'live-supply' in name: assert h>w
    else: assert (w,h)==(1440,900)
    assert sum(pixels[i+1]>100 and pixels[i+2]>70 for i in range(0,len(pixels),3))>3000,name+': market lettering/scene missing'
    images[name]=pixels
assert images['market-hyperion']!=images['market-cargo']
assert images['market-original']!=images['market-hybrid']
assert 'PASS: native market purchase/resale' in (root/'native.log').read_text()
print('PASS: six native market, cargo, render-mode and portrait images')
