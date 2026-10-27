#!/usr/bin/env python3
"""Rebuild checked-in presentation shaders with the pinned bgfx shaderc.

No shader compiler or new runtime library is needed for ordinary builds.
"""
import argparse
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROFILES = {"metal": ("osx", "metal"), "spirv": ("linux", "spirv"),
            "glsl": ("linux", "440"), "dxbc": ("windows", "s_5_0")}


def write_manifest(shaderc):
    sources = sorted((ROOT/"shaders").glob("*.sc")) + sorted((ROOT/"shaders").glob("*.sh"))
    binaries = sorted((ROOT/"shaders/bin").glob("*/*.bin"))
    def hashes(files):
        return {str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}
    revision = json.loads((ROOT/"cmake/dependencies.lock.json").read_text())["bgfx"]["revision"]
    manifest = dict(bgfx_revision=revision,
                    shaderc_version=subprocess.check_output([shaderc,"--version"],text=True).strip(),
                    sources=hashes(sources),binaries=hashes(binaries))
    (ROOT/"shaders/manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")


def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument("--shaderc", required=True)
    p.add_argument("--backends", nargs="+", choices=PROFILES, default=["metal", "spirv", "glsl"])
    args = p.parse_args()
    for backend in args.backends:
        platform, profile = PROFILES[backend]
        output = ROOT / "shaders/bin" / backend
        output.mkdir(parents=True, exist_ok=True)
        for source in sorted((ROOT / "shaders").glob("*.sc")):
            if source.name == "varying.def.sc":
                continue
            subprocess.run([args.shaderc, "-f", str(source), "-o", str(output / (source.stem + ".bin")),
                            "--platform", platform, "-p", profile, "--type", source.name[:2],
                            "--varyingdef", str(ROOT / "shaders/varying.def.sc"),
                            "-i", str(ROOT / "external/bgfx/src"), "-i", str(ROOT / "shaders"),
                            "-O", "3"], check=True)
        print("Compiled", backend)
    write_manifest(args.shaderc)


if __name__ == "__main__":
    main()
