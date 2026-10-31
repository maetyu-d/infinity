#!/usr/bin/env python3
"""Validate the single Hyperion renderer through a complete multiscale tour."""
import csv
import pathlib
import re
import sys

captures, log = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
text = log.read_text()
proofs = re.findall(r"PRESENTATION_PROOF (\S+) geometry=(\d+) camera=identical generation=unchanged renderer=HYPERION", text)
assert len(proofs) == 17, "Expected nine inward scales and eight return stops"
identities = {}
for address, geometry in proofs:
    assert address not in identities or identities[address] == geometry, "Revisit geometry changed"
    identities[address] = geometry
assert len(identities) == 9 and "PASS: native GPU journey visited 9 scales" in text
rows = list(csv.DictReader((captures.parent / "frames.csv").open()))
assert {r['render_mode'] for r in rows} == {'HYPERION'}, "A removed graphics mode was rendered"
images = sorted(captures.glob("*-HYPERION.ppm"))
assert len(images) == 17
for path in images:
    with path.open('rb') as f:
        assert f.readline().strip() == b'P6'
        width, height = map(int, f.readline().split())
        assert (width, height) == (1920, 1080) and f.readline().strip() == b'255'
        pixels = f.read()
    assert len(pixels) == width * height * 3
    world = pixels[240*width*3:900*width*3]
    assert max(world) - min(world) > 15, "Blank world image"
    if path.name == "8-CRYSTAL LATTICE-HYPERION.ppm":
        chatter = count = 0
        for y in range(320, 760):
            for x in range(80, 400):
                i = (y*width+x)*3
                chatter += max(abs(pixels[i+c]-pixels[i+3+c]) for c in range(3)) > 25
                count += 1
        assert chatter/count < .03, "Unresolved nano material produced stipple"
print("PASS: 17 Hyperion 1080p captures, nine scales, unchanged geometry/camera/generation and matching return identities")
