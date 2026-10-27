#!/usr/bin/env python3
"""Validate native three-mode captures and the scene/camera/generation proof."""
import hashlib
import csv
import pathlib
import re
import sys


def read_ppm(path):
    with path.open("rb") as stream:
        assert stream.readline().strip() == b"P6", path
        width, height = map(int, stream.readline().split())
        assert stream.readline().strip() == b"255", path
        pixels = stream.read()
    assert len(pixels) == width*height*3, path
    return width, height, pixels


captures, log = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
text = log.read_text()
proofs = re.findall(r"MODE_PROOF (\S+) geometry=(\d+) camera=identical generation=unchanged modes=3", text)
assert len(proofs) == 17, "Expected nine inward scales and eight return stops"
identities = {}
for address, geometry in proofs:
    assert address not in identities or identities[address] == geometry, "Revisit geometry changed"
    identities[address] = geometry
assert len(identities) == 9
assert "PASS: native GPU journey visited 9 scales" in text
profile = captures.parent / "frames.csv"
if profile.exists():
    costs = {int(r["effect_bytes"]) for r in csv.DictReader(profile.open())}
    assert costs <= {0, 28569600}, "Effect target storage grew while switching modes"
originals = list(captures.glob("*-ORIGINAL.ppm"))
assert len(originals) == 17, "Expected all 17 original views"
for original in originals:
    digests = []
    for mode in ["ORIGINAL", "HYPERION", "HYBRID"]:
        path = original.with_name(original.name.replace("ORIGINAL", mode))
        w, h, pixels = read_ppm(path)
        assert (w, h) == (1920, 1080), "Native size mismatch"
        # Exclude UI and inspect the actual world, rather than mode labels.
        world = pixels[240*w*3:900*w*3]
        assert max(world)-min(world) > 15, "Blank/flat world image"
        digests.append(hashlib.sha256(world).hexdigest())
    assert len(set(digests)) == 3, "Rendering modes did not change the world image"
print("PASS: 51 native 1080p captures, 9 scales, 17 scene/camera/generation proofs; return identities match")
