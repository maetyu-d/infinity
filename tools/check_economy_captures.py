#!/usr/bin/env python3
"""Check native trading, industry, hauling, carrier history and portrait captures."""
from pathlib import Path
import sys
import io
import zlib

root = Path(sys.argv[1])
images = {}
for name in (
    'market-hyperion', 'market-cargo', 'market-portrait', 'market-live-supply',
    'market-routes', 'market-industry', 'market-refit-complete',
    'market-industry-portrait', 'market-routes-portrait',
    'jobs-board', 'jobs-sealed-cargo', 'jobs-paid', 'jobs-portrait',
    'services-before', 'services-repaired', 'services-upgraded', 'services-portrait',
    'economic-city-powered', 'economic-city-offline', 'economic-city-restored',
    'economic-freight-hud', 'economic-freight-portrait',
    'weapons-shop-portrait', 'weapons-upgraded-portrait', 'weapons-rearmed-portrait',
    'weapons-idle', 'weapons-fire', 'weapons-disabled', 'weapons-disabled-portrait',
    'recovery-jobs-portrait', 'recovery-working-portrait', 'recovery-outcome', 'recovery-outcome-portrait',
):
    path = root / 'captures' / (name + '.ppm')
    raw = path.read_bytes() if path.exists() else zlib.decompress(path.with_suffix('.ppm.z').read_bytes())
    with io.BytesIO(raw) as f:
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
    ('recovery-jobs-portrait',3), ('recovery-working-portrait',5), ('recovery-outcome',5), ('recovery-outcome-portrait',5),
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
assert 'PASS: native port HUD, real-ledger power outage/recovery, preserved city/camera and freight manifest' in log
# Compare the city inside the windshield, excluding the new report and fixed HUD.
a = images['economic-city-powered']; b = images['economic-city-offline']; c = images['economic-city-restored']
changed = recovered = 0
for y in range(180, 630):
    for x in range(360, 1080):
        i = (y * 1440 + x) * 3
        changed += sum(abs(a[i+j]-b[i+j]) for j in range(3)) > 15
        recovered += sum(abs(c[i+j]-b[i+j]) for j in range(3)) > 15
assert changed > 100 and recovered > 100, 'economic lights did not visibly dim and recover'
assert 'PASS: native funded recovery job, real supplier procurement, paid kit, crew work and persistent outcomes' in log
assert images['recovery-working-portrait'] != images['recovery-outcome-portrait']
assert 'PASS: native weapon purchase, upgrade, buyback, repurchase, visible beam, recoil, damage, disabled target and combat persistence' in log
assert images['weapons-shop-portrait'] != images['weapons-upgraded-portrait']
assert images['weapons-idle'] != images['weapons-fire']
assert images['weapons-fire'] != images['weapons-disabled']
print('PASS: thirty-three native trading, services, economic city lighting, port reports and freight images / changed', changed, 'recovered', recovered)
