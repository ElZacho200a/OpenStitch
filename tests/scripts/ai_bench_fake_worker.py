#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Faux worker SAM 2 (protocole JSON Lines v1) pour les tests du benchmark : bibliotheque
standard seulement. Un masque par composante de couleur de l'image d'entree -- aucune
inference. `--fail-load CODE` fait echouer load_model avec ce code d'erreur."""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts" / "ai_bench"))
import pngio  # noqa: E402
from backends import color_components  # noqa: E402


def send(payload: dict) -> None:
    sys.stdout.write(json.dumps(payload) + "\n")
    sys.stdout.flush()


def segment(req: dict) -> None:
    job = Path(req["job_dir"])
    w, h, ch, px = pngio.decode((job / req.get("image_file", "input.png")).read_bytes())
    labels = color_components(w, h, px, ignore=(-1, -1, -1))
    (job / "masks").mkdir(exist_ok=True)
    entries = []
    for k in range(1, max(labels) + 1):
        flat = bytes(255 if v == k else 0 for v in labels)
        name = f"masks/mask_{k - 1:04d}.png"
        (job / name).write_bytes(pngio.encode(w, h, "L", flat))
        entries.append({"id": k - 1, "file": name, "area_pixels": flat.count(255), "bbox_xywh": [0, 0, w, h],
                        "predicted_iou": 0.9, "stability_score": 0.9})
    doc = {"schema_version": 1, "source": "input.png", "model": "fake", "width": w, "height": h, "masks": entries}
    (job / "masks.json").write_text(json.dumps(doc), encoding="utf-8")
    send({"id": req["id"], "type": "segment_result", "job_dir": str(job), "masks_file": "masks.json",
          "mask_count": len(entries)})


def main() -> None:
    fail = sys.argv[sys.argv.index("--fail-load") + 1] if "--fail-load" in sys.argv else None
    send({"type": "worker_starting", "pid": os.getpid(), "protocol_version": 1})
    send({"type": "worker_ready", "pid": os.getpid(), "protocol_version": 1})
    for line in sys.stdin:
        req = json.loads(line)
        kind = req.get("type")
        if kind == "load_model":
            if fail:
                send({"id": req["id"], "type": "error", "code": fail, "message": "modele indisponible", "details": ""})
            else:
                send({"id": req["id"], "type": "model_ready", "model": req["model"], "device": "cpu", "load_seconds": 0})
        elif kind == "segment_image":
            segment(req)
        elif kind == "shutdown":
            send({"id": req.get("id"), "type": "shutting_down"})
            return


if __name__ == "__main__":
    main()
