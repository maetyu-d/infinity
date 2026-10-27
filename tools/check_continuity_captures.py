#!/usr/bin/env python3
"""Check actual native render captures, including visibility of the atomic specimen."""
import pathlib
import re
import sys

directory=pathlib.Path(sys.argv[1])
log=pathlib.Path(sys.argv[2]).read_text()
if "PROOF recipe=3 objects=8 revisits=7" not in log:
    raise SystemExit("FAIL: incomplete full-object regeneration proof")
size=re.search(r"BACKBUFFER (\d+)x(\d+)",log)
expected=tuple(map(int,size.groups()))
stops=re.findall(r"^TOUR (\d+) .*? ([-\d]+,[-\d]+,[-\d]+/[^ ]+) scale=",log,re.M)
if len(stops)!=15:
    raise SystemExit("FAIL: native journey did not settle at all 15 stops")
addresses=[address for stage,address in stops]
if addresses[:7]!=list(reversed(addresses[8:])):
    raise SystemExit("FAIL: native tour returned through different addresses")
for stage,address in stops:
    kind=address.rsplit("/",1)[1].split(":",1)[0]
    source=directory/(stage+"-"+kind+".ppm")
    with source.open("rb") as image:
        if image.readline()!=b"P6\n":raise SystemExit("FAIL: unsupported capture format")
        width,height=map(int,image.readline().split())
        if (width,height)!=expected or image.readline()!=b"255\n":raise SystemExit("FAIL: wrong capture dimensions")
        pixels=image.read()
    if len(pixels)!=width*height*3:raise SystemExit("FAIL: truncated capture")
    if stage=="7":
        # Ignore all UI text. Oxygen red is absent from the quartz/olivine/ice
        # parent section; its presence proves atoms reached the final framebuffer.
        red=cyan=gold=carbon=0
        for y in range(height//5,height*4//5):
            for x in range(width//5,width*4//5):
                r,g,b=pixels[(y*width+x)*3:(y*width+x)*3+3]
                red+=r>50 and r>g*1.8 and r>b*1.8
                cyan+=b>45 and b>r*1.25 and g>r*1.1
                gold+=r>55 and r>g*1.2 and r>b*1.6
                # Carbon spheres exceed 100 in all channels. The enclosing
                # graphite section's unlit material maximum is 92/255, so it
                # cannot satisfy this test even before its darkening tint.
                carbon+=min(r,g,b)>100 and max(r,g,b)<min(r,g,b)*1.2
        mineral=re.search(r"^TOUR 7 .* mineral=(\d+)",log,re.M)
        iron=bool(mineral and mineral.group(1)=="1")
        graphite=bool(mineral and mineral.group(1)=="4")
        if (carbon if graphite else gold if iron else red)<500:
            raise SystemExit(f"FAIL: atomic specimen is occluded (oxygen={red}, iron={gold}, carbon={carbon}, cyan={cyan})")
        print(f"Atomic pixels: oxygen={red}, iron={gold}, carbon={carbon}, cyan={cyan}")
print(f"PASS: 15 native captures, {expected[0]}x{expected[1]}, matching return addresses and visible atoms")
