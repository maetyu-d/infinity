#!/usr/bin/env python3
"""Check native trading, industry, hauling, carrier history and portrait captures."""
from pathlib import Path
import sys

root = Path(sys.argv[1])
images = {}
for name in (
    'market-hyperion', 'market-cargo', 'market-portrait', 'market-live-supply',
    'market-routes', 'market-industry', 'market-refit-complete',
    'market-industry-portrait', 'market-routes-portrait',
    'jobs-board', 'jobs-sealed-cargo', 'jobs-paid', 'jobs-portrait',
    'services-before', 'services-repaired', 'services-upgraded', 'services-portrait',
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
    ('jobs-board',3), ('jobs-sealed-cargo',3), ('jobs-paid',3), ('jobs-portrait',3),
    ('services-before',4), ('services-repaired',4), ('services-upgraded',4), ('services-portrait',4),
):
    assert f'ECONOMY_CAPTURE {name} page={page} ' in log, name + ': incorrect page'
for a, b in (
    ('market-hyperion', 'market-cargo'), ('market-industry', 'market-refit-complete'),
    ('market-routes', 'market-industry'), ('market-routes-portrait', 'market-industry-portrait'),
):
    assert images[a] != images[b], f'{a}/{b}: duplicate capture'
assert 'PASS: native job board, reserved acceptance, physical pickup, sealed cargo, landed delivery and paid carrier history' in log
assert images['jobs-sealed-cargo'] != images['jobs-paid']
assert 'PASS: native funded repair, physical hull receipt, cargo upgrade and service portrait fit' in log
assert images['services-before'] != images['services-repaired']
assert images['services-repaired'] != images['services-upgraded']
print('PASS: seventeen native trading, hauling, services, upgrades, planning and portrait images')
