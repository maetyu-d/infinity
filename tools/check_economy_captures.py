#!/usr/bin/env python3
"""Check real goods, route and industry pages, commissioning and portrait captures."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
images = {}
for name in (
    'market-hyperion', 'market-cargo', 'market-portrait', 'market-live-supply',
    'market-routes', 'market-industry', 'market-refit-complete',
    'market-industry-portrait', 'market-routes-portrait',
):
    with (root / 'captures' / (name + '.ppm')).open('rb') as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split())
        assert f.readline().strip() == b'255'
        pixels = f.read()
    assert len(pixels) == w * h * 3
    if 'portrait' in name or 'live-supply' in name:
        assert h > w, name + ': expected portrait capture'
    else:
        assert (w, h) == (1440, 900), name + ': unexpected wide capture size'
    assert sum(pixels[i + 1] > 100 and pixels[i + 2] > 70
               for i in range(0, len(pixels), 3)) > 3000, name + ': lettering/scene missing'
    images[name] = pixels

log = (root / 'native.log').read_text()
assert 'PASS: native market purchase/resale' in log
assert 'funded refits, delivered capital, commissioning, trade intelligence' in log
for name, page in (
    ('market-routes', 1), ('market-routes-portrait', 1), ('market-industry', 2),
    ('market-industry-portrait', 2), ('market-refit-complete', 2),
):
    assert f'ECONOMY_CAPTURE {name} page={page} ' in log, name + ': incorrect page'
for a, b in (
    ('market-hyperion', 'market-cargo'), ('market-industry', 'market-refit-complete'),
    ('market-routes', 'market-industry'), ('market-routes-portrait', 'market-industry-portrait'),
):
    assert images[a] != images[b], f'{a}/{b}: duplicate capture'
print('PASS: nine native goods, route, industry, construction, cargo and portrait images')
