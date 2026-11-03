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
    if args.backend == "florence2":
        if not args.model_dir:
            raise SystemExit("--model-dir est requis pour le backend florence2")
        from florence2 import Florence2Backend
        return Florence2Backend(args.model_dir, args.device)
    if args.backend == "mobilesam":
        if not args.checkpoint:
            raise SystemExit("--checkpoint est requis pour le backend mobilesam")
        from mobilesam import MobileSamBackend
        return MobileSamBackend(args.checkpoint, args.device)
    if args.backend in ("sam2-box", "hqsam2-box"):
        if not (args.checkpoint and args.config):
            raise SystemExit("--checkpoint et --config sont requis pour sam2-box / hqsam2-box")
        from sam2_box import Sam2BoxBackend
        hq = args.backend == "hqsam2-box"
        return Sam2BoxBackend("hqsam2" if hq else "sam2-box", "HQ-SAM 2 (boites)" if hq else "SAM 2.1 (boites)",
                              args.checkpoint, args.config, "sam2.1-hq" if hq else "sam2.1", args.device)
    raise SystemExit(f"backend inconnu : {args.backend}")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--backend", required=True, choices=["sam2", "sam2-box", "hqsam2-box", "mobilesam", "florence2", "fake-grid", "fake-flaky"])
    ap.add_argument("--models-dir")
    ap.add_argument("--model-dir", help="dossier du modele (florence2, format natif transformers)")
    ap.add_argument("--checkpoint", help="fichier de poids (sam2-box, hqsam2-box, mobilesam)")
    ap.add_argument("--config", help="configuration Hydra du modele (sam2-box, hqsam2-box)")
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
