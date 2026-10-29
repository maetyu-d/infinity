#!/usr/bin/env python3
"""Verify actual polar emission, opaque occlusion and render-unit continuity."""
import json
import math
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
log = (root / 'native.log').read_text()
assert 'PASS: polar optics' in log and log.count('POLAR_OPTICS scene=') == 15

def read(name):
    with (root / 'captures' / f'{name}.ppm').open('rb') as f:
        assert f.readline().strip() == b'P6'
        assert f.readline().strip() == b'1440 900'
        assert f.readline().strip() == b'255'
        data = f.read()
    assert len(data) == 1440*900*3
    return data

result = {}
for scene in ('night', 'day', 'ground', 'blocked', 'airless'):
    off, on, rebased = (read(f'{scene}-{i}') for i in range(3))
    changed = sum(max(abs(on[i+c]-off[i+c]) for c in range(3)) > 8
                  for i in range(0, len(on), 3))
    # Sparse whole-frame samples include the limb and curtains, rather than
    # only a planet-centre crop. No temporal noise/history is in this pass.
    errors = [abs(on[i]-rebased[i]) for i in range(0, len(on), 12)]
    rms = math.sqrt(sum(e*e for e in errors)/len(errors))
    large = sum(e > 48 for e in errors)/len(errors)
    assert rms < 6 and large < .02, f'{scene}: emission flickers when render units change'
    if scene in ('blocked', 'airless'):
        assert on == off, f'{scene}: aurora leaks through solid ground or into vacuum'
    result[scene] = dict(changed_pixels=changed, rebase_rms_255=rms, large_error_fraction=large)
assert result['night']['changed_pixels'] > 800, 'Night-side curtains are missing'
assert result['ground']['changed_pixels'] > 100, 'Polar sky lost the emission seen from the ground'
assert result['day']['changed_pixels'] < result['night']['changed_pixels']*.2, 'Daylight did not suppress aurora'
(root/'comparison.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
print('PASS: visible orbital and surface auroras; daylight suppression; exact opaque/vacuum occlusion; stable magnification')
