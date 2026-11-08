#!/usr/bin/env python3
"""Record the native probe camera directly to MP4 without retaining raw frames."""
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    encoder = shutil.which('ffmpeg')
    if not encoder:
        raise RuntimeError('ffmpeg is required for this optional recording tool')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    count = 0
    with tempfile.TemporaryDirectory(prefix='infinity-probe-video-') as tmp:
        root = Path(tmp)
        captures = root / 'captures'
        with args.output.with_suffix('.log').open('w') as log:
            process = subprocess.Popen([
                str(args.executable.resolve()), '--probe-video', '--width', '1280', '--height', '720',
                '--capture-dir', str(captures), '--data', str(root / 'data')
            ], stdout=log, stderr=subprocess.STDOUT)
            movie = subprocess.Popen([
                encoder, '-hide_banner', '-loglevel', 'error', '-y', '-f', 'image2pipe',
                '-vcodec', 'ppm', '-framerate', '30', '-i', 'pipe:0', '-an',
                '-c:v', 'libx264', '-preset', 'fast', '-crf', '19', '-pix_fmt', 'yuv420p',
                '-movflags', '+faststart', str(args.output.resolve())
            ], stdin=subprocess.PIPE)
            deadline = time.monotonic() + 420
            try:
                while True:
                    path = captures / f'frame-{count:06d}.ppm'
                    if path.exists():
                        raw = path.read_bytes()
                        parts = raw.split(b'\n', 3)
                        if len(parts) == 4 and parts[0] == b'P6' and parts[2] == b'255':
                            width, height = map(int, parts[1].split())
                            if len(parts[3]) == width * height * 3:
                                movie.stdin.write(raw)
                                path.unlink()
                                count += 1
                                continue
                    if process.poll() is not None:
                        if process.returncode:
                            raise RuntimeError(f'Native recording failed; see {args.output.with_suffix(".log")}')
                        if not path.exists():
                            break
                    if time.monotonic() > deadline:
                        raise TimeoutError('Probe recording exceeded its deadline')
                    time.sleep(.02)
                movie.stdin.close()
                if movie.wait(timeout=60):
                    raise RuntimeError('Video encoding failed')
                if count < 300:
                    raise RuntimeError(f'Incomplete probe video: {count} frames')
            finally:
                for child in (process, movie):
                    if child.poll() is None:
                        child.terminate()
                        try:
                            child.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            child.kill()
                            child.wait()
    args.output.with_suffix('.json').write_text(json.dumps({
        'source': 'Native Infinity mining-probe camera, deterministic gameplay deployment',
        'resolution': [1280, 720], 'fps': 30, 'frames': count, 'duration_seconds': count / 30,
        'stages': ['surface approach', 'boring', 'armed charge', 'detonation', 'cavern survey beacon'],
        'audio': False, 'synthetic_frames': False,
    }, indent=2) + '\n')
    print(f'Recorded {count} native frames / {count/30:.2f} seconds: {args.output}')


if __name__ == '__main__':
    main()
