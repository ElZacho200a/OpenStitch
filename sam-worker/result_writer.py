# SPDX-License-Identifier: Apache-2.0
"""Ecriture des resultats d'une tache de segmentation dans son dossier :
un PNG binaire par masque, un apercu colorise, et `masks.json` -- jamais de
Base64 dans le protocole JSON, uniquement des chemins relatifs au dossier de
tache (cf. schema partage avec libs/ai_segmentation/src/mask_result.cpp)."""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image

SCHEMA_VERSION = 1


def write_job_results(
    job_dir: Path,
    width: int,
    height: int,
    source_file: str,
    model_worker_id: str,
    masks: list[dict[str, Any]],
) -> Path:
    job_dir = Path(job_dir)
    masks_dir = job_dir / "masks"
    masks_dir.mkdir(parents=True, exist_ok=True)

    entries = []
    preview = np.zeros((height, width, 3), dtype=np.uint8)
    rng = np.random.default_rng(12345)  # couleurs deterministes (reproductibilite des tests)

    for i, mask in enumerate(masks):
        segmentation = mask["segmentation"].astype(bool)
        file_name = f"masks/mask_{i:04d}.png"
        Image.fromarray((segmentation.astype(np.uint8) * 255), mode="L").save(job_dir / file_name)

        color = rng.integers(60, 255, size=3, dtype=np.uint8)
        preview[segmentation] = color

        x, y, w, h = mask["bbox_xywh"]
        entries.append(
            {
                "id": i,
                "file": file_name,
                "area_pixels": int(mask["area_pixels"]),
                "bbox_xywh": [int(x), int(y), int(w), int(h)],
                "predicted_iou": float(mask.get("predicted_iou", 0.0)),
                "stability_score": float(mask.get("stability_score", 0.0)),
                "component_count_before": int(mask.get("component_count_before", 1)),
                "component_count_after_cleanup": int(mask.get("component_count_after_cleanup", 1)),
                "removed_island_count": int(mask.get("removed_island_count", 0)),
                "filled_hole_count": int(mask.get("filled_hole_count", 0)),
            }
        )

    Image.fromarray(preview, mode="RGB").save(job_dir / "preview.png")

    payload = {
        "schema_version": SCHEMA_VERSION,
        "source": source_file,
        "model": model_worker_id,
        "width": width,
        "height": height,
        "masks": entries,
    }
    masks_json_path = job_dir / "masks.json"
    tmp_path = job_dir / "masks.json.tmp"
    tmp_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    tmp_path.replace(masks_json_path)  # ecriture atomique : jamais de masks.json partiel visible
    return masks_json_path
