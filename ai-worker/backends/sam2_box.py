# SPDX-License-Identifier: Apache-2.0
"""Backend SAM 2 guide par boites (API `SAM2ImagePredictor`) : sert SAM 2.1 ET HQ-SAM 2, dont
l'API est identique (le paquet `sam2` installe dans l'environnement decide du modele).

Le checkpoint et sa configuration sont donnes explicitement (jamais deduits) ; `model_hash` est le
SHA-256 du fichier. Invite supportee : `box`. Les scores sont ceux rapportes par le modele.
"""
from __future__ import annotations

import threading
from pathlib import Path

from backend import Backend, BackendError, Context, PredictRequest
from common import box_xyxy, load_rgb, pick_device, sha256_file, write_masks
import hashlib


class Sam2BoxBackend(Backend):
    def __init__(self, backend_id: str, display: str, checkpoint: str, config: str, revision: str,
                 device: str = "auto", license_weights: str = "Apache-2.0") -> None:
        self.backend_id, self.display, self.revision = backend_id, display, revision
        self.checkpoint, self.config, self.device_pref = Path(checkpoint), config, device
        self.license_weights = license_weights
        self._predictor = None
        self._hash: str | None = None
        self._lock = threading.Lock()

    def capabilities(self) -> dict:
        with self._lock:
            if self._hash is None and self.checkpoint.is_file():
                self._hash = sha256_file(self.checkpoint)
        return {"backend_id": self.backend_id, "backend_revision": self.revision, "display_name": self.display,
                "model_id": self.checkpoint.stem, "model_hash": self._hash, "prompts": ["box"],
                "devices": ["cpu", "cuda"], "license_code": "Apache-2.0", "license_weights": self.license_weights,
                "experimental": True}

    def preload(self) -> None:
        import torch  # noqa: F401
        import sam2.build_sam  # noqa: F401
        import sam2.sam2_image_predictor  # noqa: F401

    def health(self) -> dict:
        if not self.checkpoint.is_file():
            return {"state": "unavailable", "detail": f"checkpoint absent : {self.checkpoint.name}"}
        try:
            import sam2  # noqa: F401
        except ImportError:
            return {"state": "unavailable", "detail": "paquet sam2 non installe"}
        return {"state": "ready", "detail": ""}

    def _load(self, ctx: Context):
        if self._predictor is None:
            ctx.progress("loading_model")
            try:
                from sam2.build_sam import build_sam2
                from sam2.sam2_image_predictor import SAM2ImagePredictor
            except ImportError as exc:
                raise BackendError("SAM2_NOT_INSTALLED", "paquet sam2 absent", str(exc)) from exc
            if not self.checkpoint.is_file():
                raise BackendError("CHECKPOINT_NOT_FOUND", "checkpoint introuvable", str(self.checkpoint))
            try:
                model = build_sam2(self.config, str(self.checkpoint), device=pick_device(self.device_pref))
            except Exception as exc:  # noqa: BLE001
                raise BackendError("CONFIG_NOT_FOUND", "construction du modele impossible", str(exc)) from exc
            self._predictor = SAM2ImagePredictor(model)
        return self._predictor

    def predict(self, request: PredictRequest, ctx: Context) -> dict:
        import numpy as np
        import torch

        job = Path(request.job_dir)
        predictor = self._load(ctx)
        ctx.progress("preparing_image")
        array, source_size, data = load_rgb(job / request.image_file, request.options.get("max_resolution"))
        ph, pw = array.shape[:2]
        scale = pw / source_size[0]
        ctx.check_cancelled()
        ctx.progress("running_inference")
        items = []
        with torch.inference_mode():
            predictor.set_image(array)
            for idx, prompt in enumerate(request.prompts):
                ctx.check_cancelled()
                box = np.array(box_xyxy(prompt, source_size, scale), dtype=np.float32)
                masks, scores, _ = predictor.predict(box=box, multimask_output=False)
                items.append({"mask": masks[0] > 0, "confidence": float(scores[0]), "prompt_index": idx})
        ctx.progress("writing_results")
        return {"image_sha256": hashlib.sha256(data).hexdigest(),
                "transform": {"source_width": source_size[0], "source_height": source_size[1], "width": pw,
                              "height": ph, "scale": scale, "crop": None},
                "masks": write_masks(job, items, (pw, ph), source_size), "warnings": []}
