#!/usr/bin/env python3
"""Validate native three-mode captures and the scene/camera/generation proof."""
import hashlib
import gzip
import csv
import pathlib
import re
import sys


def read_ppm(path):
    # Optional lossless archival keeps the 51 full-resolution frames practical
    # on disk-constrained development machines; validate identical P6 bytes.
    compressed = path.with_suffix(path.suffix + ".gz")
    try:
        source = path.open("rb")
    except FileNotFoundError:
        source = gzip.open(compressed, "rb")
    with source as stream:
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
    assert costs <= {0, 28569600, 34560000}, "Effect target storage grew while switching modes"
originals = sorted({p.with_suffix("") if p.suffix == ".gz" else p
                    for pattern in ["*-ORIGINAL.ppm", "*-ORIGINAL.ppm.gz"]
                    for p in captures.glob(pattern)})
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
# A huge inherited physical material tile becomes locally constant at nano
# magnification. Quantised relief must not turn the enclosing mineral into
# alternating black/bright pixels. This crop is outside the lattice and UI.
for mode in ["HYPERION", "HYBRID"]:
    w, h, pixels = read_ppm(captures / f"8-CRYSTAL LATTICE-{mode}.ppm")
    count = chatter = 0
    for y in range(320, 760):
        for x in range(80, 400):
            i = (y*w+x)*3
            chatter += max(abs(pixels[i+c]-pixels[i+3+c]) for c in range(3)) > 25
            count += 1
    assert chatter/count < .03, f"{mode}: unresolved nano material produced stipple ({chatter/count:.3f})"
print("PASS: 51 native 1080p captures, 9 scales, 17 scene/camera/generation proofs; return identities match")
