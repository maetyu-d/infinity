#!/usr/bin/env python3
"""Package the standalone C++ desert racer; never replaces the Infinity app."""
import pathlib
import plistlib
import re
import shutil
import subprocess
import sys
root = pathlib.Path(__file__).resolve().parents[1]
binary = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build/dune_breaker'
bundle = pathlib.Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else root / 'Dune Breaker.app'
if not binary.is_file() or bundle.suffix != '.app':
    raise SystemExit('Usage: package_desert_macos.py build/dune_breaker "Dune Breaker.app"')
info_path = bundle / 'Contents/Info.plist'
if info_path.exists() and plistlib.loads(info_path.read_bytes()).get('CFBundleIdentifier') != 'org.infinite.dunebreaker':
    raise SystemExit('Refusing to replace an unrelated app')
executable = bundle / 'Contents/MacOS/DuneBreaker'
executable.parent.mkdir(parents=True, exist_ok=True)
temporary = executable.with_suffix('.next')
# APFS clones avoid duplicating a native executable on a nearly full desktop.
# The resulting bundle has its own inode and can be signed independently.
cloned = subprocess.run(['cp', '-c', str(binary), str(temporary)], capture_output=True).returncode == 0
if not cloned:
    shutil.copy2(binary, temporary)
temporary.chmod(0o755)
temporary.replace(executable)
commands = subprocess.check_output(['otool', '-l', str(binary)], text=True)
minimum = re.search(r'\bminos\s+([0-9.]+)', commands)
info_path.write_bytes(plistlib.dumps({
    'CFBundleIdentifier': 'org.infinite.dunebreaker',
    'CFBundleName': 'Dune Breaker', 'CFBundleDisplayName': 'Dune Breaker',
    'CFBundleExecutable': 'DuneBreaker', 'CFBundlePackageType': 'APPL',
    'CFBundleShortVersionString': '4.21.3', 'CFBundleVersion': '32',
    'NSHighResolutionCapable': True,
    'LSMinimumSystemVersion': minimum.group(1) if minimum else '14.0',
}))
resources = bundle / 'Contents/Resources'
resources.mkdir(exist_ok=True)
shutil.copy2(root / 'docs/DUNE_BREAKER.md', resources / 'Read Me.md')
old_renderer_notes = resources / 'Ellipsoid Renderer.md'
if old_renderer_notes.exists():
    old_renderer_notes.replace(resources / 'ELLIPSOID_RENDERER.md')
shutil.copy2(root / 'docs/ELLIPSOID_RENDERER.md', resources / 'ELLIPSOID_RENDERER.md')
shutil.copy2(root / 'docs/DUNE_BREAKER_2.md', resources / 'DUNE_BREAKER_2.md')
shutil.copy2(root / 'docs/DUNE_BREAKER_3.md', resources / 'DUNE_BREAKER_3.md')
shutil.copy2(root / 'docs/DUNE_BREAKER_4.md', resources / 'DUNE_BREAKER_4.md')
shutil.copy2(root / 'THIRD_PARTY.md', resources / 'THIRD_PARTY.md')
shutil.copytree(root / 'third_party_licenses', resources / 'licenses', dirs_exist_ok=True)
for library in ('bgfx', 'bimg', 'bx'):
    for path in (root / 'external' / library / '3rdparty').rglob('*'):
        if path.is_file() and path.name.lower().startswith(('license', 'licence', 'copying', 'notice')):
            target = resources / 'licenses' / library / path.relative_to(root / 'external' / library)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(bundle)], check=True)
print(bundle)
