#!/usr/bin/env python3
"""Deterministic, non-destructive physical embroidery trial log (HP-PHYS-001).

This tool records *observations* only. It does not claim that any stitch
settings are validated until they have been embroidered and measured.
"""
from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

FABRICS = ("cotton", "jersey", "terry")
SATIN_WIDTHS_MM = (1, 2, 4, 6, 8, 10)
TATAMI_SPACINGS_MM = (0.3, 0.4, 0.5)
LETTER_HEIGHTS_MM = (3, 5, 8, 12, 20)
FIELDS = (
    "case_id", "fabric", "family", "nominal_mm", "underlay", "machine",
    "thread", "needle", "stabilizer", "osp_commit", "dst_sha256",
    "actual_mm", "defects", "photo", "notes",
)


def cases():
    for fabric in FABRICS:
        for width in SATIN_WIDTHS_MM:
            yield (f"{fabric}-satin-{width}", fabric, "satin", width, "center")
        for spacing in TATAMI_SPACINGS_MM:
            for underlay in ("none", "edge"):
                suffix = str(spacing).replace(".", "p")
                yield (f"{fabric}-tatami-{suffix}-{underlay}", fabric, "tatami", spacing, underlay)
        for height in LETTER_HEIGHTS_MM:
            yield (f"{fabric}-letter-{height}", fabric, "letter", height, "auto")


def template_rows():
    for case_id, fabric, family, nominal, underlay in cases():
        row = dict.fromkeys(FIELDS, "")
        row.update(case_id=case_id, fabric=fabric, family=family,
                   nominal_mm=str(nominal), underlay=underlay)
        yield row


def write_template(path: Path):
    if path.exists():
        raise FileExistsError(f"Refusing to overwrite existing measurements: {path}")
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(template_rows())


def validate(path: Path):
    errors = []
    expected = {row["case_id"]: row for row in template_rows()}
    seen = set()
    done = 0
    with path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != list(FIELDS):
            return ["Unexpected CSV columns/order"], 0, len(expected)
        for lineno, row in enumerate(reader, start=2):
            case_id = row["case_id"]
            if case_id in seen:
                errors.append(f"line {lineno}: duplicate case {case_id}")
                continue
            seen.add(case_id)
            baseline = expected.get(case_id)
            if baseline is None:
                errors.append(f"line {lineno}: unknown case {case_id}")
                continue
            for column in ("fabric", "family", "nominal_mm", "underlay"):
                if row[column] != baseline[column]:
                    errors.append(f"line {lineno}: changed test definition {column}")
            if row["actual_mm"]:
                try:
                    value = float(row["actual_mm"])
                    if not math.isfinite(value) or value <= 0:
                        raise ValueError()
                except ValueError:
                    errors.append(f"line {lineno}: actual_mm must be finite and positive")
            populated = [row[key] for key in ("machine", "thread", "needle", "stabilizer", "osp_commit", "dst_sha256", "actual_mm", "photo")]
            if any(populated) and not all(populated):
                errors.append(f"line {lineno}: partial observation: fill all provenance and measurement fields")
            if all(populated):
                digest = row["dst_sha256"]
                if len(digest) != 64 or any(c not in "0123456789abcdefABCDEF" for c in digest):
                    errors.append(f"line {lineno}: dst_sha256 must be 64 hex characters")
                done += 1
    missing = expected.keys() - seen
    if missing:
        errors.append(f"missing {len(missing)} test cases")
    return errors, done, len(expected)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("new", help="generate a blank, deterministic CSV").add_argument("csv", type=Path)
    sub.add_parser("check", help="check schema, provenance and test coverage").add_argument("csv", type=Path)
    args = parser.parse_args(argv)
    if args.command == "new":
        try:
            write_template(args.csv)
        except FileExistsError as exc:
            parser.error(str(exc))
        return 0
    try:
        errors, done, total = validate(args.csv)
    except (OSError, csv.Error) as exc:
        parser.error(str(exc))
    print(f"Recorded: {done}/{total}; errors: {len(errors)}")
    for error in errors:
        print(f"ERROR: {error}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
