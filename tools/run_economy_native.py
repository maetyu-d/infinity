#!/usr/bin/env python3
"""Run the native fixture and compress completed test frames without changing pixels."""
from pathlib import Path
import argparse
import signal
import subprocess
import sys
import time
import zlib


def compress(captures):
    for path in captures.glob('*.ppm'):
        raw = path.read_bytes()
        parts = raw.split(b'\n', 3)
        if len(parts) != 4 or parts[0] != b'P6' or parts[2] != b'255':
            continue
        width, height = map(int, parts[1].split())
        if len(parts[3]) != width * height * 3:
            continue  # The GPU callback is still writing this frame.
        packed = zlib.compress(raw, 3)
        assert zlib.decompress(packed) == raw
        target = path.with_suffix('.ppm.z')
        pending = target.with_suffix('.pending')
        pending.write_bytes(packed)
        assert zlib.decompress(pending.read_bytes()) == raw
        pending.replace(target)
        path.unlink()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('executable')
    parser.add_argument('output', type=Path)
    parser.add_argument('--scenario', choices=('economy-test', 'npc-combat-test'), default='economy-test')
    args = parser.parse_args()
    root = args.output
    command = [args.executable, '--' + args.scenario, '--render-mode', 'hyperion',
               '--width', '1440', '--height', '900', '--capture-dir', str(root / 'captures'),
               '--data', str(root / 'data'), '--profile', str(root / 'frames.csv')]
    process = subprocess.Popen(command)
    signal.signal(signal.SIGTERM, lambda *_: process.terminate())
    deadline = time.monotonic() + 350
    try:
        while process.poll() is None:
            compress(root / 'captures')
            if time.monotonic() > deadline:
                raise TimeoutError('Native gameplay exceeded its capture deadline')
            time.sleep(.5)
        compress(root / 'captures')
        return process.returncode
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == '__main__':
    sys.exit(main())
