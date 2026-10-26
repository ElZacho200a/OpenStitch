#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Worker long-vivant `openstitch.ai.backend/1` : un backend par processus, JSON Lines sur
stdin/stdout, logs sur stderr. Aucune dependance de modele liee au coeur C++.

    python ai-worker/openstitch_ai_worker.py --backend sam2 --models-dir <dossier> [--model small]
    python ai-worker/openstitch_ai_worker.py --backend fake-grid          # tests, stdlib seule
"""
from __future__ import annotations

import argparse
import logging
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "backends"))

import protocol as proto  # noqa: E402
from server import Server  # noqa: E402


def build_backend(args: argparse.Namespace):
    if args.backend == "fake-grid":
        from fake import FakeGrid
        return FakeGrid()
    if args.backend == "fake-flaky":
        from fake import FakeFlaky
        return FakeFlaky()
    if args.backend == "sam2":
        if not args.models_dir:
            raise SystemExit("--models-dir est requis pour le backend sam2")
        from sam2_adapter import Sam2Backend
        return Sam2Backend(args.models_dir, args.model, args.device)
    raise SystemExit(f"backend inconnu : {args.backend}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--backend", required=True, choices=["sam2", "fake-grid", "fake-flaky"])
    ap.add_argument("--models-dir")
    ap.add_argument("--model", default="small", choices=["tiny", "small", "base_plus", "large"])
    ap.add_argument("--device", default="auto", choices=["auto", "cpu", "cuda"])
    ap.add_argument("--advertise-protocol", default=proto.PROTOCOL, help=argparse.SUPPRESS)  # tests de version
    args = ap.parse_args(argv)
    logging.basicConfig(level=os.environ.get("OPENSTITCH_AI_LOG_LEVEL", "INFO"), stream=sys.stderr,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    Server(build_backend(args), advertise_protocol=args.advertise_protocol).serve(proto.read_requests())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
