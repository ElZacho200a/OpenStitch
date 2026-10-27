#!/usr/bin/env python3
"""Experimental embroidery pull calibration: measured CSV to deterministic JSON."""
from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path
from statistics import mean, pstdev

FIELDS = ("fabric", "machine", "stabilizer", "thread", "needle",
          "family", "nominal_mm", "applied_pull_mm", "actual_mm")
FAMILIES = ("satin", "tatami", "directional")


def template(path: Path):
    with path.open("x", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, FIELDS)
        writer.writeheader()
        for fabric in ("cotton", "jersey", "terry"):
            for family in FAMILIES:
                for width in (1, 2, 4, 6, 8, 10):
                    writer.writerow(dict(fabric=fabric, family=family,
                                         nominal_mm=width))


def observations(path: Path):
    with path.open(encoding="utf-8", newline="") as stream:
        rows = csv.DictReader(stream)
        if rows.fieldnames != list(FIELDS):
            raise ValueError("invalid calibration columns")
        for line, row in enumerate(rows, start=2):
            if not row["actual_mm"]:
                continue  # template row, not yet measured
            if any(not row[k] for k in FIELDS[:7]) or row["family"] not in FAMILIES:
                raise ValueError(f"line {line}: missing setup or invalid family")
            try:
                nominal = float(row["nominal_mm"])
                applied = float(row["applied_pull_mm"])
                actual = float(row["actual_mm"])
            except ValueError as exc:
                raise ValueError(f"line {line}: invalid measurement") from exc
            if not all(map(math.isfinite, (nominal, applied, actual))) or (
                nominal <= 0 or applied < 0 or actual <= 0
            ):
                raise ValueError(f"line {line}: invalid range")
            key = tuple(row[k] for k in FIELDS[:6]) + (nominal,)
            # First-order estimate only; assumes pull affects both ends equally.
            yield key, applied + (nominal - actual) / 2.0


def fit(path: Path):
    grouped = defaultdict(list)
    for key, estimate in observations(path):
        grouped[key].append(estimate)
    if not grouped:
        raise ValueError("no measured observations")
    profiles = defaultdict(list)
    for key in sorted(grouped):
        values = grouped[key]
        raw = mean(values)
        maximum = 0.35 if key[5] == "satin" else 3.0
        profiles[key[:6]].append({
            "nominal_mm": key[6],
            "pull_per_side_mm": round(max(0.0, min(maximum, raw)), 4),
            "raw_estimate_mm": round(raw, 4),
            "spread_mm": round(pstdev(values), 4),
            "samples": len(values),
            "clamped": not 0 <= raw <= maximum,
        })
    return {
        "schema": "openstitch.pull-calibration/1",
        "validated": False,
        "estimator": "applied_pull_mm + (nominal_mm - actual_mm) / 2",
        "profiles": [
            dict(zip(FIELDS[:6], key), points=profiles[key])
            for key in sorted(profiles)
        ],
    }


def interpolate(points, width_mm: float):
    """Linear interpolation inside measurements, constant outside."""
    if not points or not math.isfinite(width_mm) or width_mm <= 0:
        raise ValueError("invalid interpolation request")
    points = sorted(points, key=lambda p: p["nominal_mm"])
    if width_mm <= points[0]["nominal_mm"]:
        return points[0]["pull_per_side_mm"]
    for left, right in zip(points, points[1:]):
        if width_mm <= right["nominal_mm"]:
            fraction = (width_mm - left["nominal_mm"]) / (
                right["nominal_mm"] - left["nominal_mm"]
            )
            return left["pull_per_side_mm"] + fraction * (
                right["pull_per_side_mm"] - left["pull_per_side_mm"]
            )
    return points[-1]["pull_per_side_mm"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    cmds = parser.add_subparsers(dest="command", required=True)
    cmds.add_parser("new").add_argument("csv", type=Path)
    fit_cmd = cmds.add_parser("fit")
    fit_cmd.add_argument("csv", type=Path)
    fit_cmd.add_argument("json", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "new":
            template(args.csv)
        else:
            result = fit(args.csv)
            with args.json.open("x", encoding="utf-8") as stream:
                json.dump(result, stream, indent=2, sort_keys=True)
                stream.write("\n")
    except (OSError, ValueError, csv.Error) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
