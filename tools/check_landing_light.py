#!/usr/bin/env python3
"""Actual landable terrain: contact shade, negative controls and physical rebase."""
import json
import math
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
log = (root / 'native.log').read_text()
assert 'PASS: landing contact' in log and log.count('LANDING_LIGHT scene=') == 6

def read(name):
    with (root / 'captures' / (name + '.ppm')).open('rb') as stream:
        assert stream.readline().strip() == b'P6'
        assert stream.readline().strip() == b'1440 900'
        assert stream.readline().strip() == b'255'
        data = stream.read()
    assert len(data) == 1440 * 900 * 3
    return data

result = {}
for scene in ('near', 'distant'):
    off, on, rebased = (read(scene + '-' + str(i)) for i in range(3))
    threshold = 3
    changed = sum(max(abs(on[i+c]-off[i+c]) for c in range(3)) > threshold
                  for i in range(0, len(on), 3))
    errors = [abs(on[i] - rebased[i]) for i in range(0, len(on), 12)]
    rms = math.sqrt(sum(e*e for e in errors) / len(errors))
    assert rms < 5, scene + ': landing material flickers with render units'
    if scene == 'distant':
        assert on == off, scene + ': contact shade leaked outside its scope'
    else:
        assert 100 < changed < 1440 * 900 * .85, scene + ': contact shade missing or screen-wide'
        brighter = sum(sum(on[i:i+3]) > sum(off[i:i+3]) + 9 for i in range(0, len(on), 3))
        assert brighter < 1440 * 900 * .001, scene + ': contact shade added light'
    peak = max(abs(a-b) for a, b in zip(off, on))
    result[scene] = dict(changed_pixels=changed, change_threshold_255=threshold,
                         peak_delta_255=peak, rebase_rms_255=rms)
(root / 'comparison.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
print('PASS: localized landing shade, distant negative control and stable physical scale')
