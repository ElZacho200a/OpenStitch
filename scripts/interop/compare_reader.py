#!/usr/bin/env python3
"""Cross-check machine files with OpenStitch CLI and independent pyembroidery.

pyembroidery is MIT-licensed and installed separately; do not vendor it.
Counts can legitimately diverge for trim/stop normalization: report them rather
than silently treating the file as interoperable. This is a *screening* tool,
not proof of machine compatibility.
"""
from __future__ import annotations

import argparse
import importlib
import json
from pathlib import Path
import subprocess
import sys

SUPPORTED = frozenset({".dst", ".pes", ".jef", ".exp"})
COUNTERS = ("stitches", "jumps", "trims", "color_changes")


def count_external(pattern, decoder):
    """Count command classes, ignoring pyembroidery's command flags/options."""
    mapping = {
        getattr(decoder, "STITCH"): "stitches",
        getattr(decoder, "JUMP"): "jumps",
        getattr(decoder, "TRIM"): "trims",
        getattr(decoder, "COLOR_CHANGE"): "color_changes",
    }
    stop = getattr(decoder, "STOP", None)
    counts = {key: 0 for key in COUNTERS}
    for sample in pattern.stitches:
        if len(sample) != 3:
            raise ValueError("reader produced an invalid stitch tuple")
        command = int(sample[2]) & int(getattr(decoder, "COMMAND_MASK", 0xFF))
        if command == stop:
            # A STOP is kept separate: OpenStitch can encode it as color change.
            continue
        name = mapping.get(command)
        if name:
            counts[name] += 1
    return counts


def read_openstitch_stats(cli: Path, path: Path, runner=subprocess.run):
    result = runner([str(cli), "stats", str(path), "--json"], capture_output=True, text=True, check=False)
    if result.returncode:
        raise ValueError(f"OpenStitch stats returned {result.returncode}: {result.stderr.strip()[:300]}")
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise ValueError("OpenStitch stats did not produce valid JSON") from exc
    if not isinstance(payload, dict) or any(not isinstance(payload.get(k), int) or isinstance(payload.get(k), bool) or payload[k] < 0 for k in COUNTERS):
        raise ValueError("OpenStitch stats JSON has invalid or missing counters")
    return {k: payload[k] for k in COUNTERS}


def compare(path: Path, cli: Path, decoder, runner=subprocess.run):
    if path.suffix.lower() not in SUPPORTED:
        raise ValueError(f"unsupported extension: {path.suffix}")
    if not path.is_file():
        raise FileNotFoundError(str(path))
    native = read_openstitch_stats(cli, path, runner)
    reader = decoder.read(str(path))
    if reader is None or not hasattr(reader, "stitches"):
        raise ValueError("third-party decoder returned no stitch sequence")
    external = count_external(reader, decoder)
    differences = {k: {"openstitch": native[k], "third_party": external[k]}
                   for k in COUNTERS if native[k] != external[k]}
    return {"file": str(path), "openstitch": native, "third_party": external,
            "differences": differences, "status": "match" if not differences else "different"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True, help="path to openstitch-cli executable")
    parser.add_argument("files", type=Path, nargs="+", help="DST/PES/JEF/EXP machine files")
    args = parser.parse_args(argv)
    try:
        decoder = importlib.import_module("pyembroidery")
    except ModuleNotFoundError:
        parser.error("pyembroidery is not installed (pip install pyembroidery)")
    results = []
    for path in args.files:
        try:
            results.append(compare(path, args.cli, decoder))
        except (OSError, ValueError, RuntimeError) as exc:
            results.append({"file": str(path), "status": "error", "error": str(exc)})
    print(json.dumps({"schema": 1, "files": results}, ensure_ascii=False, indent=2))
    return 0 if all(x["status"] == "match" for x in results) else 1


if __name__ == "__main__":
    sys.exit(main())
