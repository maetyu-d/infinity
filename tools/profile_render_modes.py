#!/usr/bin/env python3
"""Sequential identical native tours; captures/proof work are excluded from timing."""
import argparse
import json
import pathlib
import subprocess


def main():
    p = argparse.ArgumentParser(__doc__)
    p.add_argument("--binary", type=pathlib.Path, required=True)
    p.add_argument("--baseline", type=pathlib.Path)
    p.add_argument("--output", type=pathlib.Path, required=True)
    p.add_argument("--runs", type=int, default=3)
    p.add_argument("--modes", nargs="+", choices=["original","hyperion","hybrid"],
                   default=["original","hyperion","hybrid"])
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    cases = [(mode, args.binary.resolve()) for mode in args.modes]
    if args.baseline:
        cases.insert(0, ("baseline", args.baseline.resolve()))
    for mode, binary in cases:
        for run in range(1, args.runs+1):
            prefix = args.output / (mode+"-"+str(run))
            command = [str(binary), "--benchmark-tour", "--width", "1920", "--height", "1080",
                       "--profile", str(prefix.with_suffix(".csv")), "--data", str(prefix)+"-data"]
            if mode != "baseline":
                command += ["--render-mode", mode, "--visual-time", "12"]
            with prefix.with_suffix(".log").open("w") as out:
                subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, check=True)
            print(mode, run, "complete", flush=True)
    (args.output/"method.json").write_text(json.dumps({"runs":args.runs,"width":1920,"height":1080,
        "cases":[x[0] for x in cases],"tour":"eight scales, return; no capture/proof workers",
        "visual_time_seconds":12},indent=2)+"\n")


if __name__ == "__main__":
    main()
