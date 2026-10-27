#!/usr/bin/env python3
"""Summarize matching settled scene samples; report run variation explicitly."""
import csv
import itertools
import json
import pathlib
import re
import statistics
import sys


def percentile(values, p):
    values = sorted(values)
    return values[int((len(values)-1)*p)]


root = pathlib.Path(sys.argv[1])
result = {}
reference_tour = None
for mode in ["baseline", "original", "hyperion", "hybrid"]:
    runs = []
    for path in sorted(root.glob(mode+"-*.csv")):
        log = path.with_suffix(".log").read_text()
        assert "PASS:" in log, path
        tour = re.findall(r"^TOUR \d+ .*? (\S+) scale=([^ ]+)", log, re.M)
        if reference_tour is None:
            reference_tour = tour
        assert tour == reference_tour, f"Different address/camera scale in {path}"
        groups = []
        for kind, rows in itertools.groupby(csv.DictReader(path.open()), lambda r:r["kind"]):
            settled = [r for r in rows if r["pending"]=="0" and r.get("queued","0")=="0"
                       and r["transitioning"]=="0" and r["lod_transitions"]=="0"
                       and r.get("upload_deferred","0")=="0"]
            if len(settled)>=25:
                groups.append((kind, settled[-25:]))
        assert len(groups)==15, (path,len(groups))
        rows = [r for _, group in groups for r in group]
        def metrics(rows):
            cpu = [float(r["cpu_ms"]) for r in rows]
            gpu = [float(r["gpu_ms"]) for r in rows]
            wall = [float(r["wall_ms"]) for r in rows]
            return dict(cpu_median=statistics.median(cpu),cpu_p95=percentile(cpu,.95),
                        gpu_median=statistics.median(gpu),gpu_p95=percentile(gpu,.95),
                        wall_median=statistics.median(wall),wall_p95=percentile(wall,.95))
        kinds = {}
        for kind in dict(groups):
            part = [r for k, group in groups if k==kind for r in group]
            kinds[kind] = metrics(part)
            kinds[kind].update({k:statistics.median(float(r[k]) for r in part)
                               for k in ["vertices","triangles","particles","atoms","atom_triangles"]})
        runs.append(dict(file=path.name,samples=len(rows),**metrics(rows),kinds=kinds,
                         peak_rss=int(re.search(r"peak_rss_bytes=(\d+)",log)[1]),
                         effect_bytes=max(int(r.get("effect_bytes","0")) for r in rows)))
    assert len(runs)==3, (mode,len(runs))
    summary = {key:statistics.median(r[key] for r in runs)
               for key in ["cpu_median","cpu_p95","gpu_median","gpu_p95","wall_median","wall_p95","peak_rss","effect_bytes"]}
    summary["gpu_median_range"]=[min(r["gpu_median"] for r in runs),max(r["gpu_median"] for r in runs)]
    summary["kinds"]={kind:{key:statistics.median(r["kinds"][kind][key] for r in runs)
                             for key in runs[0]["kinds"][kind]} for kind in runs[0]["kinds"]}
    result[mode]=dict(summary=summary,runs=runs)

# The presentation must not buy speed by dropping any original geometry.
for mode in ["original","hyperion","hybrid"]:
    for kind, baseline in result["baseline"]["summary"]["kinds"].items():
        for metric in ["vertices","triangles","particles","atoms","atom_triangles"]:
            assert result[mode]["summary"]["kinds"][kind][metric]==baseline[metric], (mode,kind,metric)
(root/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
lines = ["| Mode | CPU median / P95 ms | GPU median / P95 ms | Peak RSS MiB | Effect targets MiB |",
         "|---|---:|---:|---:|---:|"]
for mode, data in result.items():
    s=data["summary"]
    lines.append(f'| {mode} | {s["cpu_median"]:.3f} / {s["cpu_p95"]:.3f} | '
                 f'{s["gpu_median"]:.3f} / {s["gpu_p95"]:.3f} | {s["peak_rss"]/1048576:.1f} | '
                 f'{s["effect_bytes"]/1048576:.2f} |')
(root/"summary.md").write_text("\n".join(lines)+"\n")
print("\n".join(lines))
