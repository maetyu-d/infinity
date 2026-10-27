#!/usr/bin/env python3
"""Fetch immutable source archives, verify SHA-256, and unpack locally."""
import concurrent.futures
import hashlib
import json
import pathlib
import shutil
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCK = ROOT / "cmake/dependencies.lock.json"


def wanted(name, path):
    """Exclude examples, test assets and unused shader compilers from source trees."""
    if name == "bgfx":
        if path.startswith("examples/common/debugdraw/"):
            return path.endswith(("vs_debugdraw_lines.bin.h", "fs_debugdraw_lines.bin.h"))
        if path.startswith("examples/runtime/shaders/"):
            return path.endswith(("/vs_instancing.bin", "/fs_instancing.bin"))
        if path.startswith("3rdparty/"):
            return not any(path.startswith("3rdparty/" + p + "/") for p in
                           ("dawn", "glslang", "spirv-tools", "spirv-cross", "l-smash"))
        return path.startswith(("include/", "src/", "scripts/")) or "/" not in path
    if name == "SDL":
        return path.startswith(("src/", "include/", "cmake/", "build-scripts/")) or "/" not in path
    if name == "glm":
        return path.startswith(("glm/", "cmake/")) or "/" not in path
    if name == "zstd":
        return path.startswith(("lib/", "build/cmake/")) or "/" not in path
    return not path.startswith(("doc/", "docs/", "test/", "tests/", "examples/"))


def fetch(dep):
    name, spec = dep
    destination = ROOT / "external" / name
    stamp = destination / ".infinite-pin"
    if stamp.exists() and stamp.read_text() == spec["revision"]:
        return name + " already present"
    url = "https://codeload.github.com/" + spec["repository"] + "/tar.gz/" + spec["revision"]
    archive = ROOT / "external" / "downloads" / (name + ".tar.gz")
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        with urllib.request.urlopen(url, timeout=180) as response, archive.open("wb") as out:
            shutil.copyfileobj(response, out)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != spec["sha256"]:
        raise RuntimeError(f"{name}: checksum mismatch: {digest}")
    temporary = ROOT / "external" / (".unpack-" + name)
    temporary.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as source:
        # Archives are pinned, but still reject paths escaping the extraction directory.
        members = []
        for member in source.getmembers():
            if not (temporary / member.name).resolve().is_relative_to(temporary.resolve()):
                raise RuntimeError("Unsafe archive path")
            if wanted(name, "/".join(member.name.split("/")[1:])):
                members.append(member)
        source.extractall(temporary, members=members)
    extracted = next(temporary.iterdir())
    if destination.exists():
        shutil.rmtree(destination)
    extracted.rename(destination)
    temporary.rmdir()
    stamp.write_text(spec["revision"])
    archive.unlink()  # Source trees are sufficient for subsequent offline builds.
    return name + " verified"


if __name__ == "__main__":
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for result in pool.map(fetch, json.loads(LOCK.read_text()).items()):
            print(result, flush=True)
