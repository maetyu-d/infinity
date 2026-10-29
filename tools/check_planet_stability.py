#!/usr/bin/env python3
"""Compare the same physical planetary view at different render-unit sizes."""
import json
import math
import pathlib
import sys


def read_ppm(path):
    with path.open("rb") as stream:
        assert stream.readline().strip() == b"P6", path
        width, height = map(int, stream.readline().split())
        assert stream.readline().strip() == b"255", path
        data = stream.read()
    assert len(data) == width * height * 3, path
    return width, height, data


def compare(reference, candidate):
    w, h, a = read_ppm(reference)
    cw, ch, b = read_ppm(candidate)
    assert (w, h) == (cw, ch)
    # Interior of a sphere at four radii, clear of the silhouette, sky, HUD,
    # traffic and subpixel edge filtering. The physical eye and FOV are fixed.
    radius = h * .5 / math.tan(math.radians(35)) / math.sqrt(15) * .70
    errors = []
    for y in range(int(h / 2 - radius), int(h / 2 + radius)):
        for x in range(int(w / 2 - radius), int(w / 2 + radius)):
            if (x - w / 2)**2 + (y - h / 2)**2 > radius**2:
                continue
            offset = (y * w + x) * 3
            errors.extend(abs(a[offset + c] - b[offset + c]) for c in range(3))
    rms = math.sqrt(sum(e * e for e in errors) / len(errors))
    large = sum(e > 48 for e in errors) / len(errors)
    return {"rms_255": rms, "large_error_fraction": large,
            "passed": rms < 6 and large < .02}


directory = pathlib.Path(sys.argv[1])
log = (directory.parent / "native.log").read_text()
assert log.count("PLANET_STABILITY ") == 18, "Native run skipped a world, rendering mode or magnification"
assert "PASS: planet stability captured primary/moon" in log, "Native fixture did not complete"
results = {}
for world in ["solid", "moon"]:
    for mode in ["ORIGINAL", "HYPERION", "HYBRID"]:
        for sample in [1, 2]:
            name = f"{world}-{mode}-{sample}"
            results[name] = compare(directory / f"{world}-{mode}-0.ppm", directory / f"{name}.ppm")
report = json.dumps(results, indent=2)
(directory.parent / "comparison.json").write_text(report + "\n")
print(report)
if not all(r["passed"] for r in results.values()):
    raise SystemExit("FAIL: changing render units made a planet disappear or flicker")
print("PASS: solid primaries and moons retain coverage and shading across depth magnification and jitter")
