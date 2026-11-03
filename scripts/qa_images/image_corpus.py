#!/usr/bin/env python3
"""Deterministic, license-free PNG import samples for HP-QA-006."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

SIGNATURE = b'\x89PNG\r\n\x1a\n'


def chunk(name: bytes, payload: bytes) -> bytes:
    return struct.pack('>I', len(payload)) + name + payload + struct.pack('>I', zlib.crc32(name + payload))


def png(width: int, height: int, depth: int, mode: int, rows: list[bytes], extra: bytes = b'') -> bytes:
    if width <= 0 or height <= 0 or len(rows) != height:
        raise ValueError('invalid dimensions or row count')
    ihdr = struct.pack('>IIBBBBB', width, height, depth, mode, 0, 0, 0)
    return SIGNATURE + chunk(b'IHDR', ihdr) + extra + chunk(b'IDAT', zlib.compress(b''.join(b'\0'+r for r in rows), 9)) + chunk(b'IEND', b'')


def corpus():
    return {
        'rgb8': (png(2, 1, 8, 2, [bytes((255, 0, 0, 0, 255, 0))]), True),
        'rgba8': (png(2, 1, 8, 6, [bytes((255, 0, 0, 0, 0, 0, 255, 255))]), True),
        'gray16': (png(2, 1, 16, 0, [bytes((0, 0, 255, 255))]), True),
        'indexed8': (png(2, 1, 8, 3, [b'\0\1'], chunk(b'PLTE', b'\xff\0\0\0\xff\0')), True),
        'invalid-empty': (b'', False),
        'invalid-signature': (b'PNG-but-not-a-file', False),
        'invalid-truncated': (png(2, 1, 8, 2, [bytes((255, 0, 0, 0, 255, 0))])[:18], False),
    }


def smoke(cli: Path, *, runner=subprocess.run):
    failures = {}
    with tempfile.TemporaryDirectory(prefix='openstitch-images-') as temp:
        root = Path(temp)
        for name, (data, valid) in corpus().items():
            f = root / (name + '.png')
            f.write_bytes(data)
            try:
                p = runner([str(cli), 'info', str(f), '--json'], capture_output=True, text=True, timeout=6, check=False)
                if p.returncode < 0 or p.returncode > 255:
                    failures[name] = f'abnormal-exit-{p.returncode}'
                elif (p.returncode == 0) != valid:
                    failures[name] = f'expected-valid={valid}; exit={p.returncode}'
                elif valid:
                    parsed = json.loads(p.stdout)
                    if parsed.get('width_px') != 2 or parsed.get('height_px') != 1:
                        failures[name] = 'incorrect-image-dimensions'
            except (subprocess.TimeoutExpired, OSError, ValueError) as exc:
                failures[name] = type(exc).__name__
    return failures


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cli', type=Path, required=True)
    args = p.parse_args(argv)
    if not args.cli.is_file():
        p.error(f'CLI missing: {args.cli}')
    errors = smoke(args.cli)
    print(json.dumps({'schema': 1, 'cases': len(corpus()), 'failures': errors}, indent=2))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
