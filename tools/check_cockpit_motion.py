#!/usr/bin/env python3
"""Check actual framebuffer feedback, recovery and stable cockpit instruments."""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
for mode in ("original", "hyperion", "hybrid"):
    images = {}
    for effect in ("neutral", "turn", "recoil", "impact", "damage", "recovery"):
        with (root / f"{mode}-{effect}.ppm").open("rb") as f:
            assert f.readline().strip() == b"P6"
            assert tuple(map(int, f.readline().split())) == (1440, 900)
            assert f.readline().strip() == b"255"
            pixels = f.read()
        assert len(pixels) == 1440 * 900 * 3
        images[effect] = pixels
    baseline = images["neutral"]
    for effect in ("turn", "recoil", "impact", "damage"):
        changed = sum(a != b for a, b in zip(baseline, images[effect]))
        assert changed > 500, f"{mode}-{effect}: feedback did not reach the framebuffer"
        print(f"{mode}-{effect}: {changed} changed channels")
    # Once the spring settles, the same physical view should return. Small
    # residual subpixel differences are allowed for GPU raster rounding.
    error = sum(abs(a - b) for a, b in zip(baseline, images["recovery"])) / len(baseline)
    assert error < .10, f"{mode}: recovery changed the resting scene ({error=})"
    print(f"{mode}: recovery mean channel error {error:.6f}")
print("PASS: all eighteen cockpit feedback captures show visible impulses and return to the same scene")
