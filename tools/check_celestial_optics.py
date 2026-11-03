#!/usr/bin/env python3
"""Verify addressed stellar optics, opaque occlusion and render-unit continuity."""
import json
import math
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
log = (root / 'native.log').read_text()
assert 'PASS: celestial optics' in log and log.count('CELESTIAL_OPTICS scene=') == 18

def read(name):
    with (root / 'captures' / f'{name}.ppm').open('rb') as f:
        assert f.readline().strip() == b'P6'
        assert f.readline().strip() == b'1440 900'
        assert f.readline().strip() == b'255'
        data = f.read()
    assert len(data) == 1440*900*3
    return data

result = {}
for scene in ('sun', 'eclipse', 'away', 'day', 'ice', 'partial'):
    off, on, rebased = (read(f'{scene}-{i}') for i in range(3))
    changed = sum(max(abs(on[i+c]-off[i+c]) for c in range(3)) > 8
                  for i in range(0, len(on), 3))
    # Sparse whole-frame samples include the limb and curtains, rather than
    # only a planet-centre crop. No temporal noise/history is in this pass.
    errors = [abs(on[i]-rebased[i]) for i in range(0, len(on), 12)]
    rms = math.sqrt(sum(e*e for e in errors)/len(errors))
    large = sum(e > 48 for e in errors)/len(errors)
    assert rms < 6 and large < .02, f'{scene}: stellar/ice presentation flickers when render units change'
    if scene in ('eclipse', 'away'):
        assert on == off, f'{scene}: stellar optics leak through solid ground or into vacuum'
    result[scene] = dict(changed_pixels=changed, rebase_rms_255=rms, large_error_fraction=large)
for scene in ('sun','day','partial'):
    assert result[scene]['changed_pixels'] > 100, f'{scene}: visible celestial rendering is missing'

# The ice fixture once compared Original against Hyperion. With the single
# Hyperion presentation, this switch controls only the addressed stellar pass;
# the off-screen star must not change the planet's ice material.
ice_off, ice_on = read('ice-0'), read('ice-1')
assert ice_on == ice_off, 'ice: off-screen stellar optics changed the retained ice material'
patch = [tuple(ice_on[(y*1440+x)*3:(y*1440+x)*3+3])
         for y in range(180,680) for x in range(320,1120)]
cold = sum(b > r+10 and g > r+5 for r,g,b in patch)
luminance = sorted((r+g+b)/3 for r,g,b in patch)
contrast = luminance[19*len(patch)//20]-luminance[len(patch)//20]
assert cold > len(patch)*.25 and contrast > 20, 'ice: resolved glacial shading is missing'
result['ice'].update(cold_material_pixels=cold, material_luminance_range_255=contrast)
(root/'comparison.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
print('PASS: addressed sun and ice; exact planetary eclipse/behind-camera controls; stable magnification')
