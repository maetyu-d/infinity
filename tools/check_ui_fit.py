#!/usr/bin/env python3
"""Check fitted native HUD captures and real row-selection proofs."""
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
log = (root / 'native.log').read_text()
assert 'UI_STARTUP window_and_decorations=inside_work_area' in log
assert 'PASS: native UI fit' in log
records = re.findall(r'UI_FIT case=(\w+) pixels=(\d+),(\d+) viewport=(\d+),(\d+),(\d+),(\d+) projection=([\d.]+) hit_testing=PASS camera=unchanged', log)
assert len(records) == 6
assert {r[0] for r in records} == {'small', 'square', 'portrait', 'retina', 'wide', 'controls'}
for name, w, h, x, y, vw, vh, projection in records:
    w, h, x, y, vw, vh = map(int, (w, h, x, y, vw, vh))
    assert 0 <= x and 0 <= y and x + vw <= w and y + vh <= h and vw >= vh
    assert abs((x + vw / 2) - w / 2) <= .5 and abs((y + vh / 2) - h / 2) <= .5
    with (root / 'captures' / f'{name}.ppm').open('rb') as image:
        assert image.readline().strip() == b'P6'
        assert tuple(map(int, image.readline().split())) == (w, h)
        assert image.readline().strip() == b'255'
        pixels = image.read()
    assert len(pixels) == w * h * 3
    def lit(left, top, right, bottom):
        return sum(max(pixels[(py*w+px)*3:(py*w+px)*3+3]) > 110
                   for py in range(max(0, int(y + top*vh/900)), min(h, int(y + bottom*vh/900)+1))
                   for px in range(max(0, int(x + left*vh/900)), min(w, int(x + right*vh/900)+1)))
    canvas_w = vw * 900 / vh
    assert lit(24, 14, 170, 35) > 12, f'{name}: clipped title'
    assert lit(24, 886, 172, 900) > 5, f'{name}: clipped bottom controls hint'
    assert lit(canvas_w - 200, 886, canvas_w - 24, 900) > 5, f'{name}: clipped right footer'
    if name == 'controls':
        assert lit(canvas_w / 2 - 260, 225, canvas_w / 2 + 260, 615) > 250, 'Missing controls guide'
print('PASS: six fitted native HUD/guide captures; full title/footer; real world and traffic clicks; unchanged camera')
