#!/usr/bin/env python3
"""Compare the same live probe trajectory with/without render optimisations.

No video capture, screenshots, frame interpolation, or universe changes are used.
The reference switch is a validation control, not an in-game rendering mode.
"""
from pathlib import Path
import argparse
import json
import re
import statistics
import subprocess


def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--repeats',type=int,default=2)
    args=parser.parse_args()
    if not 1<=args.repeats<=5:
        parser.error('--repeats must be between 1 and 5')
    args.output.mkdir(parents=True,exist_ok=True)
    runs=[]
    runner=Path(__file__).with_name('run_economy_native.py')
    for repeat in range(args.repeats):
        order=('reference','optimised') if repeat%2==0 else ('optimised','reference')
        for mode in order:
            root=args.output/f'{repeat}-{mode}'
            root.mkdir(exist_ok=False)
            command=['python3',str(runner),str(args.executable.resolve()),str(root),
                     '--scenario','probe-test','--probe-benchmark']
            if mode=='reference':command.append('--probe-reference-render')
            with (root/'native.log').open('w') as log:
                subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=350)
            log=(root/'native.log').read_text()
            row=re.search(r'^PROBE_PERF (.+)$',log,re.MULTILINE)
            if not row or 'PASS: native probe benchmark' not in log:
                raise RuntimeError(f'Incomplete benchmark: {root}')
            integers={'frames','upload_bytes','light_updates','hidden_frames','bore','descriptor'}
            values={key:(int(value) if key in integers else float(value)) for key,value in re.findall(r'(\w+)=(\S+)',row[1])}
            assert values['frames']==639
            values.update(mode=mode,repeat=repeat)
            runs.append(values)
            print(f"{mode} / median {values['median_ms']:.3f} ms / probe CPU {values['probe_cpu_median_ms']:.3f} ms",flush=True)
    assert len({(r['bore'],r['descriptor'],r['frames']) for r in runs})==1
    metrics=('median_ms','p95_ms','cpu_median_ms','probe_cpu_median_ms','upload_bytes','light_updates','hidden_frames')
    summary={mode:{key:statistics.median(r[key] for r in runs if r['mode']==mode) for key in metrics}
             for mode in ('reference','optimised')}
    result={'workload':'639 fixed probe-camera steps at 1440x900, no screenshot or encoder overhead',
            'order':'alternating reference/optimised, then optimised/reference',
            'repeats_per_mode':args.repeats,'runs':runs,'median_of_runs':summary,
            'frame_time_reduction_percent':100*(1-summary['optimised']['median_ms']/summary['reference']['median_ms'])}
    (args.output/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result['median_of_runs'],indent=2),flush=True)


if __name__=='__main__':main()
