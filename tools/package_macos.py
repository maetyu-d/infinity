#!/usr/bin/env python3
"""Package an already-built standalone executable as a local macOS application."""
import pathlib
import plistlib
import re
import shutil
import subprocess
import sys

binary = pathlib.Path(sys.argv[1]).resolve()
bundle = pathlib.Path(sys.argv[2]).resolve()
if not binary.is_file() or bundle.suffix != ".app":
    raise SystemExit("Usage: package_macos.py path/to/infinite Infinite.app")
info_path = bundle / "Contents/Info.plist"
if info_path.exists():
    info = plistlib.loads(info_path.read_bytes())
    if info.get("CFBundleIdentifier") != "org.infinite.universe":
        raise SystemExit("Refusing to replace an unrelated app")
destination = bundle / "Contents/MacOS/Infinite"
destination.parent.mkdir(parents=True, exist_ok=True)
temporary = destination.with_suffix(".next")
shutil.copy2(binary, temporary)
temporary.chmod(0o755)
temporary.replace(destination)
load_commands = subprocess.check_output(["otool", "-l", str(binary)], text=True)
minimum = re.search(r"\bminos\s+([0-9.]+)", load_commands)
info = {
    "CFBundleIdentifier": "org.infinite.universe",
    "CFBundleName": "Infinite",
    "CFBundleDisplayName": "Infinite",
    "CFBundleExecutable": "Infinite",
    "CFBundlePackageType": "APPL",
    "CFBundleShortVersionString": "0.51.0",
    "CFBundleVersion": "57",
    "NSHighResolutionCapable": True,
    "LSMinimumSystemVersion": minimum.group(1) if minimum else "14.0",
}
info_path.write_bytes(plistlib.dumps(info))
root = pathlib.Path(__file__).resolve().parents[1]
resources = bundle / "Contents/Resources"
resources.mkdir(exist_ok=True)
shutil.copy2(root / "THIRD_PARTY.md", resources / "THIRD_PARTY.md")
shutil.copytree(root / "third_party_licenses", resources / "licenses", dirs_exist_ok=True)
for library in ("bgfx", "bimg", "bx"):
    for path in (root / "external" / library / "3rdparty").rglob("*"):
        if path.is_file() and path.name.lower().startswith(("license", "licence", "copying", "notice")):
            target = resources / "licenses" / library / path.relative_to(root / "external" / library)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
print(bundle)
