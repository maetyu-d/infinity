#!/usr/bin/env python3
"""Compare unchanged world pixels with the frozen 0.4 native executable.

PPM inputs need no decoder. PNG evidence optionally uses ffmpeg as a test tool.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess


def pixels(path, decoder):
    if path.suffix == ".ppm":
        with path.open("rb") as f:
            assert f.readline().strip() == b"P6"
            assert f.readline().strip() == b"1920 1080"
            assert f.readline().strip() == b"255"
            return f.read()
    return subprocess.check_output([decoder,"-v","error","-i",str(path),"-f","rawvideo",
                                    "-pix_fmt","rgb24","-threads","1","-"])


p = argparse.ArgumentParser(__doc__)
p.add_argument("--baseline",type=pathlib.Path,required=True)
p.add_argument("--current",type=pathlib.Path,required=True)
p.add_argument("--decoder",default="ffmpeg")
p.add_argument("--output",type=pathlib.Path,required=True)
args = p.parse_args()
names = ["GALAXY","STAR SYSTEM","PLANET","SURFACE","MOUNTAIN","ROCK","MINERAL GRAIN","CRYSTAL LATTICE"]
results = []
for i,kind in enumerate(names):
    baseline=args.baseline/f"{i}-{i+1}.ppm"
    current=args.current/f"{i+1}-{kind}-ORIGINAL.ppm"
    if not baseline.exists():baseline=baseline.with_suffix(".png")
    if not current.exists():current=current.with_suffix(".png")
    before=pixels(baseline,args.decoder)
    after=pixels(current,args.decoder)
    assert len(before)==len(after)==1920*1080*3
    # Top/bottom UI contains changing clocks, profiling counters and notices.
    # Preserve and compare the complete central world region at native size.
    a=before[220*1920*3:900*1920*3]
    b=after[220*1920*3:900*1920*3]
    difference=0 if a==b else sum(x!=y for x,y in zip(a,b))
    results.append(dict(kind=kind,compared_pixels=1920*680,changed_channels=difference,
                        baseline_sha256=hashlib.sha256(a).hexdigest(),current_sha256=hashlib.sha256(b).hexdigest()))
args.output.write_text(json.dumps(results,indent=2)+"\n")
for result in results:print(result["kind"],result["changed_channels"],"changed channels")
assert all(r["changed_channels"]==0 for r in results), "ORIGINAL pixels differ from baseline"
print("PASS: ORIGINAL matches frozen 0.4 exactly across 10,444,800 world pixels")
