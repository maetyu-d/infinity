#!/usr/bin/env python3
"""Native cockpit visibility and capture-completeness checks (stdlib only)."""
import csv
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
images = {}
for name in ("orbit-original", "orbit-hyperion", "orbit-hybrid", "landing-approach",
             "landed", "departure", "atomic-shrink", "stellar-background", "micro-shrink", "system-contacts", "orbital-activity", "busy-orbit", "moon-family", "controls", "roll-right", "roll-left", "roll-stopped"):
    with (root / (name + ".ppm")).open("rb") as file:
        assert file.readline().strip() == b"P6"
        width, height = map(int, file.readline().split())
        assert file.readline().strip() == b"255"
        pixels = file.read()
    assert (width, height) == (1440, 900)
    assert len(pixels) == width * height * 3
    def count(x0, y0, x1, y1, predicate):
        return sum(predicate(pixels[(y*width+x)*3:(y*width+x)*3+3])
                   for y in range(y0, y1) for x in range(x0, x1))
    assert count(0, 100, 35, 740, lambda p: max(p) > 50) > 3000, name + ": missing cockpit geometry"
    assert count(500, 795, 720, 870, lambda p: p[1] > 120 and p[2] > 110) > 120, name + ": missing velocity instruments"
    assert count(350, 150, 1050, 700, lambda p: max(p) > 40) > 200, name + ": missing scene"
    images[name] = pixels
assert images["orbit-original"] != images["orbit-hyperion"]
assert images["orbit-hyperion"] != images["orbit-hybrid"]
assert images["system-contacts"] != images["orbit-hyperion"]
assert images["orbital-activity"] != images["orbit-hyperion"]
assert images["controls"] != images["moon-family"]
assert images["roll-right"] != images["roll-left"] != images["roll-stopped"]
print("PASS: all seventeen cockpit, roll/release, controls guide, real moon family, orbital activity, celestial contacts, addressed stellar background, landing/departure and micro/nano ship captures contain scene geometry and flight instruments")

profile = root.parent / "frames.csv"
if profile.exists():
    rows = list(csv.DictReader(profile.open()))
    assert {int(r["material_bytes"]) for r in rows} <= {0, 256*256*4, 1398100 + 256*256*4}, "Material texture escaped its fixed budget"
    assert any(r["render_mode"] == "HYPERION" and int(r["material_bytes"]) == 1398100 + 256*256*4 for r in rows), "Material bake was never published to the GPU"
    print("PASS: asynchronous material texture uploaded; complete mip chain plus shared strata lookup uses 1,660,244 bytes")
