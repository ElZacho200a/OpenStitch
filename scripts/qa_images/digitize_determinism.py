#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""E16: reproducibility gate for the real image-to-DST CLI pipeline.

Runs the exact same input twice in independent output folders, using the SAME
output basename: DST's LA header may include the output file name. This guards
against false mismatches when comparing two independently generated files.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def parse_cli_json(stdout: str) -> dict:
    """Accept a single JSON object after optional OpenCV startup log lines."""
    lines = stdout.splitlines()
    for i, line in enumerate(lines):
        if line.lstrip().startswith("{"):
            value = json.loads("\n".join(lines[i:]))
            if not isinstance(value, dict):
                raise ValueError("CLI JSON must be an object")
            return value
    raise ValueError("CLI JSON object missing")


def verify(cli: Path, image: Path, *, runner=subprocess.run, timeout: int = 120) -> dict:
    """Return a machine-readable report. Failures are never treated as success."""
    digests: list[str] = []
    stitches: list[int] = []
    failures: list[str] = []
    invalid_json_diagnostics: list[dict] = []
    with tempfile.TemporaryDirectory(prefix="openstitch-e16-golden-") as tmp:
        for index in (1, 2):
            output = Path(tmp) / f"pass-{index}" / "design.dst"
            output.parent.mkdir()
            command = [str(cli), "digitize", str(image), str(output), "--json"]
            try:
                completed = runner(
                    command, capture_output=True, text=True, check=False, timeout=timeout
                )
            except subprocess.TimeoutExpired:
                failures.append(f"pass-{index}:timeout")
                break
            except OSError as exc:
                failures.append(f"pass-{index}:{type(exc).__name__}")
                break

            if completed.returncode != 0:
                failures.append(f"pass-{index}:exit-{completed.returncode}")
                break
            if not output.is_file():
                failures.append(f"pass-{index}:missing-dst")
                break
            data = output.read_bytes()
            if len(data) <= 512 or (len(data) - 512) % 3 != 0 or not data.startswith(b"LA:"):
                failures.append(f"pass-{index}:invalid-dst-envelope")
                break
            try:
                report = parse_cli_json(completed.stdout)
                n = report["stitches"]
                if type(n) is not int or n < 0:
                    raise ValueError("stitches must be a nonnegative JSON integer")
            except (ValueError, TypeError, KeyError, json.JSONDecodeError):
                # Preserve bounded, non-sensitive CLI contract evidence in CI logs.
                observed = locals().get("report")
                if isinstance(observed, dict):
                    value = observed.get("stitches", "<missing>")
                    detail = {"pass": index, "type": type(value).__name__,
                              "value": repr(value)[:120],
                              "keys": sorted(str(key) for key in observed)[:20]}
                else:
                    detail = {"pass": index, "stdout_prefix": completed.stdout[:200]}
                invalid_json_diagnostics.append(detail)
                failures.append(f"pass-{index}:invalid-json-stitches")
                break

            digests.append(hashlib.sha256(data).hexdigest())
            stitches.append(n)

    if len(digests) == 2 and (digests[0] != digests[1] or stitches[0] != stitches[1]):
        failures.append("byte-or-stitch-drift")

    return {
        "schema": "openstitch.qa.e16-digitize-determinism/1",
        "passes": len(digests),
        "sha256": digests,
        "stitches": stitches,
        "failures": failures,
        "invalid_json_diagnostics": invalid_json_diagnostics,
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    args = parser.parse_args(argv)
    if not args.cli.is_file():
        parser.error(f"CLI missing: {args.cli}")
    if not args.image.is_file():
        parser.error(f"PNG fixture missing: {args.image}")
    result = verify(args.cli.resolve(), args.image.resolve())
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 1 if result["failures"] or result["passes"] != 2 else 0


if __name__ == "__main__":
    sys.exit(main())
