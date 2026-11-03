#!/usr/bin/env python3
"""Convert benchmark logs to deterministic JSON and Markdown snapshots.

Numbers from different CI runners are observations, not pass/fail thresholds.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import re
import sys

LINE = re.compile(r'^\s*(?P<name>.*?)\s+froid\s+(?P<cold>\d+(?:\.\d+)?)\s+ms\s*\|\s*chaud min\s+(?P<minimum>\d+(?:\.\d+)?)\s+med\s+(?P<median>\d+(?:\.\d+)?)\s+ms\s*\|\s*pic\s+(?P<memory>\d+(?:\.\d+)?)\s+Mo\s*$')


def parse(log: str):
    rows = []
    names = set()
    for line in log.splitlines():
        match = LINE.fullmatch(line)
        if not match:
            continue
        name = match.group('name').strip()
        if not name or name in names:
            raise ValueError('blank or duplicate benchmark label')
        names.add(name)
        fields = {key: float(match.group(key)) for key in ('cold', 'minimum', 'median', 'memory')}
        if not all(math.isfinite(x) and x >= 0 for x in fields.values()):
            raise ValueError('non-finite or negative measurement')
        rows.append({'name': name, 'cold_ms': fields['cold'], 'min_ms': fields['minimum'],
                     'median_ms': fields['median'], 'peak_mb': fields['memory']})
    if not rows:
        raise ValueError('no benchmark measurements found')
    return rows


def markdown(rows):
    lines = ['# OpenStitch benchmark snapshot', '',
             'Measurements from one CI run; no fixed performance threshold.', '',
             '| Phase | Cold (ms) | Min warm (ms) | Median warm (ms) | Peak (MB) |',
             '|---|---:|---:|---:|---:|']
    for row in rows:
        name = row['name'].replace('|', '\\|')
        lines.append(f"| {name} | {row['cold_ms']:.2f} | {row['min_ms']:.2f} | {row['median_ms']:.2f} | {row['peak_mb']:.1f} |")
    return '\n'.join(lines) + '\n'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--json', type=Path, required=True)
    parser.add_argument('--markdown', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        rows = parse(args.log.read_text(encoding='utf-8', errors='replace'))
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    args.json.write_text(json.dumps({'schema': 1, 'measurements': rows}, indent=2, sort_keys=True)+'\n', encoding='utf-8')
    args.markdown.write_text(markdown(rows), encoding='utf-8')
    return 0


if __name__ == '__main__':
    sys.exit(main())
