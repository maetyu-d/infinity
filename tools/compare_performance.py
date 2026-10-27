#!/usr/bin/env python3
"""Compare frozen native binaries using matching 1080p, fixed-step tours."""
import csv
import json
import math
import pathlib
import re
import statistics
import sys

before, after, output = map(pathlib.Path, sys.argv[1:4])


def percentile(values, fraction):
    values = sorted(values)
    return values[min(len(values) - 1, math.ceil(len(values) * fraction) - 1)]


def read(directory):
    rows = []
    runs = []
    generation = {}
    for run in range(1, 4):
        data = list(csv.DictReader((directory / f"frames-{run}.csv").open()))
        rows += data[10:]
        log = (directory / f"tour-{run}.log").read_text()
        if "BACKBUFFER 1920x1080" not in log or "PASS: native GPU journey" not in log:
            raise ValueError(f"Incomplete native measurement: {directory}, run {run}")
        runs.append({"rss": int(re.search(r"peak_rss_bytes=(\d+)", log)[1]),
                     "frames": len(data),
                     "wall_p95": percentile([float(r["wall_ms"]) for r in data[10:]], .95)})
        for row in csv.DictReader((directory / f"generation-{run}.csv").open()):
            generation.setdefault((row["kind"], int(row["detail"])), []).append(row)
    kinds = {}
    for kind in dict.fromkeys(row["kind"] for row in rows):
        settled = [row for row in rows if row["kind"] == kind and row["transitioning"] == "0"
                   and row["pending"] == "0" and row["lod_transitions"] == "0"
                   and row.get("queued", "0") == "0" and row.get("upload_deferred", "0") == "0"]
        times = [float(row["cpu_ms"]) for row in settled]
        kinds[kind] = {"samples": len(settled), "cpu_median_ms": statistics.median(times),
                       "cpu_p95_ms": percentile(times, .95),
                       "gpu_median_ms": statistics.median(float(row["gpu_ms"]) for row in settled),
                       **{key: max(int(row[key]) for row in settled)
                          for key in ("vertices", "triangles", "particles", "atoms", "gpu_bytes")}}
    values = [float(row["cpu_ms"]) for row in rows]
    return {"runs": runs, "rss_median": statistics.median(run["rss"] for run in runs),
            "rss_max": max(run["rss"] for run in runs),
            "cpu_all_p95_ms": percentile(values, .95),
            "cpu_cache_max": max(int(row["cpu_cache_bytes"]) for row in rows),
            "gpu_cache_max": max(int(row["gpu_bytes"]) for row in rows),
            "kinds": kinds,
            "generation": {f"{kind}:{level}": {
                "median_of_run_medians_ms": statistics.median(float(r["median_ms"]) for r in data),
                "max_run_p95_ms": max(float(r["p95_ms"]) for r in data),
                "bytes": int(data[0]["geometry_bytes"]), "vertices": int(data[0]["vertices"]),
                "atoms": int(data[0]["atoms"])} for (kind, level), data in generation.items()}}


a, b = read(before), read(after)
output.parent.mkdir(parents=True, exist_ok=True)
output.with_suffix(".json").write_text(json.dumps({"before": a, "after": b}, indent=2) + "\n")
lines = ["| Settled view | CPU median before → after | CPU P95 before → after | Submitted detail before → after |",
         "|---|---:|---:|---|"]
for kind, old in a["kinds"].items():
    new = b["kinds"][kind]
    key = "particles" if kind in ("GALAXY", "STAR SYSTEM") else "atoms" if kind == "CRYSTAL LATTICE" else "triangles"
    detail = f"{old[key]:,} → {new[key]:,} {key}"
    lines.append(f"| {kind} | {old['cpu_median_ms']:.3f} → {new['cpu_median_ms']:.3f} ms | "
                 f"{old['cpu_p95_ms']:.3f} → {new['cpu_p95_ms']:.3f} ms | {detail} |")
lines += ["", f"Median peak RSS: {a['rss_median']/1048576:.1f} → {b['rss_median']/1048576:.1f} MiB "
          f"({(b['rss_median']/a['rss_median']-1)*100:+.1f}%).",
          f"Whole-tour CPU submission P95: {a['cpu_all_p95_ms']:.3f} → {b['cpu_all_p95_ms']:.3f} ms.",
          "", "| Generation workload | Median before → after | Speedup |",
          "|---|---:|---:|"]
for kind, level in [("GALAXY", 4), ("STAR SYSTEM", 0), ("PLANET", 2), ("SURFACE", 4),
                    ("MOUNTAIN", 4), ("ROCK", 2), ("MINERAL GRAIN", 2), ("CRYSTAL LATTICE", 0)]:
    old = a["generation"][f"{kind}:{level}"]["median_of_run_medians_ms"]
    new = b["generation"][f"{kind}:{level}"]["median_of_run_medians_ms"]
    lines.append(f"| {kind}, requested detail {level} | {old:.3f} → {new:.3f} ms | {old/new:.2f}× |")
output.with_suffix(".md").write_text("\n".join(lines) + "\n")
print("\n".join(lines))
