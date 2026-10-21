#!/usr/bin/env python3
"""Bounded, deterministic malformed-OSP smoke test against the real CLI.

Invalid projects must be rejected normally, without crash, hang or sidecars.
Not a replacement for libFuzzer or real user-file testing.
"""
from __future__ import annotations

import argparse
import io
import json
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import zipfile

SEED = 0x05F0
TIMEOUT_SECONDS = 4


def archive(payload: bytes, *, entry: str = "project.json") -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as out:
        out.writestr(entry, payload)
    return buffer.getvalue()


def invalid_corpus():
    rng = random.Random(SEED)
    samples = {
        "empty": b"",
        "not-zip": b"not a zip",
        "zip-without-json": archive(b"{}", entry="other.json"),
        "json-null": archive(b"null"),
        "json-array": archive(b"[]"),
        "json-bad-type": archive(b'{"schemaVersion":"wrong","document":{}}'),
        "json-negative-schema": archive(b'{"schemaVersion":-1,"document":{}}'),
        "json-future-schema": archive(b'{"schemaVersion":2147483647,"document":{}}'),
        "json-missing-document": archive(b'{"schemaVersion":6}'),
        "json-wrong-document": archive(b'{"schemaVersion":6,"document":42}'),
        "json-truncated": archive(b'{"schemaVersion":6,"document":'),
        "zip-nested-manifest": archive(b"{}", entry="../project.json"),
        "zip-empty-manifest": archive(b""),
    }
    base = archive(b'{"schemaVersion":6,"document":42}')
    for n in (1, 2, 4, 8, 16, 32, len(base) // 2, len(base) - 1):
        samples[f"zip-truncate-{n}"] = base[:n]
    for index in range(24):
        noisy = bytearray(base)
        for _ in range(1 + index % 5):
            at = rng.randrange(len(noisy))
            noisy[at] = rng.randrange(256)
        samples[f"zip-mutated-{index:02d}"] = bytes(noisy)
    for index in range(16):
        samples[f"random-{index:02d}"] = rng.randbytes(index * 13 + 1)
    return samples


def check_one(cli: Path, filename: Path, *, runner=subprocess.run):
    try:
        result = runner(
            [str(cli), "project", "inspect", str(filename), "--no-stitches"],
            timeout=TIMEOUT_SECONDS, capture_output=True, text=True, check=False,
        )
    except subprocess.TimeoutExpired:
        return "timeout"
    except OSError as exc:
        return f"cli-execution-error: {exc}"
    if result.returncode == 0:
        return "unexpectedly-accepted"
    if result.returncode < 0:
        return f"signal: {-result.returncode}"
    if result.returncode > 255:
        return f"abnormal-exit: {result.returncode}"
    return None


def run_corpus(cli: Path, *, runner=subprocess.run):
    failures = {}
    with tempfile.TemporaryDirectory(prefix="openstitch-osp-fuzz-") as directory:
        base = Path(directory)
        for name, contents in invalid_corpus().items():
            path = base / f"{name}.osp"
            path.write_bytes(contents)
            issue = check_one(cli, path, runner=runner)
            if issue:
                failures[name] = issue
            actual = {p.name for p in base.iterdir()}
            expected = {p.name for p in base.iterdir() if p.suffix == ".osp"}
            if actual != expected:
                failures[name] = "cli-created-sidecar"
    return failures


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    args = parser.parse_args(argv)
    if not args.cli.is_file():
        parser.error(f"CLI not found: {args.cli}")
    failures = run_corpus(args.cli)
    print(json.dumps({"schema": 1, "cases": len(invalid_corpus()), "failures": failures}, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
